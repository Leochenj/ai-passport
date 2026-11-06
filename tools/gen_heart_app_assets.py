#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate the heartbeat background video frames and the BPM numeral font.

The video is converted to 5 fps, 240x320, little-endian RGB565 raw frames. Full
panel frames avoid LVGL software scaling, which is too slow on the ESP32-C3 when
combined with continuous playback.

The source font is Noto Sans SC (SIL Open Font License 1.1). Generated files are
committed so a normal firmware build does not need Python, Node.js, or FFmpeg.

Run from the repository root:

    LV_FONT_CONV=/path/to/lv_font_conv.js NODE_EXE=/path/to/node \
    python3 tools/gen_heart_app_assets.py
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VIDEO = ROOT / "assets" / "video" / "heart_cat.mp4"
FONT_SOURCE = ROOT / "assets" / "fonts" / "NotoSansSC-Regular.otf"
FONT_OUTPUT = ROOT / "main" / "fonts" / "heart_font_80.c"
BG_OUTPUT = ROOT / "main" / "assets" / "heart_bg.bin"
MANIFEST = ROOT / "tests" / "heart_app_assets.json"
TMP_DIR = ROOT / ".tmp" / "heart-assets"
PREVIEW = ROOT / ".tmp" / "heart_preview.png"

FRAME_W = 240
FRAME_H = 320
FRAME_BYTES = FRAME_W * FRAME_H * 2
FPS = 5
FRAMES = 40
FONT_NAME = "heart_font_80"
FONT_SIZE = 80
FONT_SYMBOLS = "0123456789-"
CONVERTER_VERSION = "1.5.3"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def tool(name: str, env_name: str) -> str:
    configured = os.environ.get(env_name, "").strip()
    if configured:
        return configured
    found = shutil.which(name)
    if found:
        return found
    raise SystemExit(f"{name} was not found; set {env_name} or add it to PATH")


def run(command: list[str]) -> None:
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"command failed ({result.returncode}): {' '.join(command)}")


def converter_command() -> list[str]:
    configured = os.environ.get("LV_FONT_CONV", "").strip()
    node = os.environ.get("NODE_EXE", "node").strip() or "node"
    if configured:
        return [node, configured] if configured.endswith(".js") else [configured]
    found = shutil.which("lv_font_conv")
    if found:
        return [found]
    candidate = ROOT.parent / "tools" / "lvfont" / "node_modules" / "lv_font_conv" / "lv_font_conv.js"
    if candidate.is_file():
        return [node, str(candidate)]
    raise SystemExit("lv_font_conv not found; set LV_FONT_CONV")


def generate_video(ffmpeg: str) -> None:
    if not VIDEO.is_file():
        raise SystemExit(f"missing video source: {VIDEO}")
    BG_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    run([
        ffmpeg, "-y", "-v", "error", "-i", str(VIDEO),
        "-vf", f"fps={FPS},scale={FRAME_W}:{FRAME_H}:force_original_aspect_ratio=increase,"
               f"crop={FRAME_W}:{FRAME_H}",
        "-frames:v", str(FRAMES), "-pix_fmt", "rgb565le", "-f", "rawvideo",
        str(BG_OUTPUT),
    ])
    expected = FRAME_BYTES * FRAMES
    actual = BG_OUTPUT.stat().st_size
    if actual != expected:
        raise SystemExit(f"video output {actual} bytes, expected {expected}")


def generate_font(ffmpeg: str) -> None:
    if not FONT_SOURCE.is_file():
        raise SystemExit(f"missing source font: {FONT_SOURCE}")
    FONT_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    command = converter_command() + ["--font", FONT_SOURCE.relative_to(ROOT).as_posix()]
    TMP_DIR.mkdir(parents=True, exist_ok=True)
    dump = TMP_DIR / "font-dump"
    if dump.exists():
        shutil.rmtree(dump)
    dump.mkdir(parents=True)
    run(command + [
        "--symbols", FONT_SYMBOLS, "--size", str(FONT_SIZE), "--bpp", "4",
        "--format", "dump", "-o", str(dump),
    ])
    info = json.loads((dump / "font_info.json").read_text(encoding="utf-8"))
    available = {int(glyph["code"]) for glyph in info["glyphs"]}
    missing = [ord(char) for char in FONT_SYMBOLS if ord(char) not in available]
    if missing:
        raise SystemExit("font lacks " + ", ".join(f"U+{code:04X}" for code in missing))

    run(command + [
        "--symbols", FONT_SYMBOLS, "--size", str(FONT_SIZE), "--bpp", "4",
        "--format", "lvgl", "--no-compress",
        "--lv-font-name", FONT_NAME, "--lv-include", "lvgl.h",
        "-o", FONT_OUTPUT.relative_to(ROOT).as_posix(),
    ])
    if f"lv_font_t {FONT_NAME}" not in FONT_OUTPUT.read_text(encoding="utf-8"):
        raise SystemExit(f"{FONT_OUTPUT} does not declare lv_font_t {FONT_NAME}")


def render_preview(ffmpeg: str) -> None:
    try:
        from PIL import Image, ImageDraw, ImageFont
    except Exception as exc:  # pragma: no cover - optional developer aid
        print(f"preview skipped: {exc}")
        return

    frame_png = TMP_DIR / "preview-frame.png"
    run([
        ffmpeg, "-y", "-v", "error", "-ss", "2", "-i", str(VIDEO),
        "-vf", "scale=240:320:force_original_aspect_ratio=increase,crop=240:320",
        "-frames:v", "1", str(frame_png),
    ])
    image = Image.open(frame_png).convert("RGB")
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(str(FONT_SOURCE), FONT_SIZE)
    draw.text((120, 272), "72", font=font, fill=(11, 18, 32),
              stroke_width=5, stroke_fill=(255, 255, 255), anchor="mm")
    PREVIEW.parent.mkdir(parents=True, exist_ok=True)
    image.save(PREVIEW)
    print(f"preview: {PREVIEW.relative_to(ROOT)}")


def main() -> int:
    ffmpeg = tool("ffmpeg", "FFMPEG")
    generate_video(ffmpeg)
    generate_font(ffmpeg)

    manifest = {
        "schema": "heart-app-assets-v2",
        "generated_by": "tools/gen_heart_app_assets.py",
        "video": {
            "source": VIDEO.relative_to(ROOT).as_posix(),
            "source_sha256": sha256(VIDEO),
            "output": BG_OUTPUT.relative_to(ROOT).as_posix(),
            "output_sha256": sha256(BG_OUTPUT),
            "bytes": BG_OUTPUT.stat().st_size,
            "width": FRAME_W,
            "height": FRAME_H,
            "fps": FPS,
            "frames": FRAMES,
            "pixel_format": "rgb565le",
            "scale_to_panel": 1,
        },
        "font": {
            "source": FONT_SOURCE.relative_to(ROOT).as_posix(),
            "source_sha256": sha256(FONT_SOURCE),
            "license": "SIL Open Font License 1.1",
            "output": FONT_OUTPUT.relative_to(ROOT).as_posix(),
            "output_sha256": sha256(FONT_OUTPUT),
            "bytes": FONT_OUTPUT.stat().st_size,
            "name": FONT_NAME,
            "size": FONT_SIZE,
            "bpp": 4,
            "symbols": FONT_SYMBOLS,
            "converter": f"lv_font_conv {CONVERTER_VERSION}",
        },
    }
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"video: {BG_OUTPUT.relative_to(ROOT)} ({manifest['video']['bytes']} bytes, {FRAMES} frames)")
    print(f"font: {FONT_OUTPUT.relative_to(ROOT)} ({manifest['font']['bytes']} bytes)")
    print(f"manifest: {MANIFEST.relative_to(ROOT)}")
    render_preview(ffmpeg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
