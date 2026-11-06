#!/usr/bin/env python3
"""Check the heartbeat video frames, BPM font, and firmware wiring.

tools/gen_heart_app_assets.py converts the supplied cat video to 96 RGB565
frames and generates the numeral font. This test binds the committed artifacts
to that manifest so a stale or truncated asset fails before flashing.
"""

from __future__ import annotations

import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests" / "heart_app_assets.json"
CMAKELISTS = ROOT / "main" / "CMakeLists.txt"
UI = ROOT / "main" / "heart_ui.c"
SYMBOLS = set("0123456789-")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    errors: list[str] = []

    if not MANIFEST.is_file():
        print("ERROR: tests/heart_app_assets.json is missing", file=sys.stderr)
        return 1

    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("schema") != "heart-app-assets-v2":
        errors.append("unexpected manifest schema")

    video = manifest.get("video", {})
    video_source = ROOT / video.get("source", "")
    video_output = ROOT / video.get("output", "")
    if not video_source.is_file():
        errors.append(f"missing video source {video_source.relative_to(ROOT)}")
    elif sha256(video_source) != video.get("source_sha256"):
        errors.append("source video changed; regenerate the background frames")
    if not video_output.is_file():
        errors.append(f"missing frame binary {video_output.relative_to(ROOT)}")
    else:
        expected = int(video.get("frames", 0)) * int(video.get("width", 0)) * \
            int(video.get("height", 0)) * 2
        if video_output.stat().st_size != expected:
            errors.append(
                f"frame binary is {video_output.stat().st_size} bytes, expected {expected}"
            )
        if video.get("bytes") != video_output.stat().st_size:
            errors.append("frame binary size does not match the manifest")
        if sha256(video_output) != video.get("output_sha256"):
            errors.append("frame binary digest does not match the manifest")
        if (video.get("width"), video.get("height"), video.get("fps"),
                video.get("frames")) != (240, 320, 5, 40):
            errors.append("video geometry must stay 240x320, 5 fps, 40 frames")
        if video.get("scale_to_panel") != 1:
            errors.append("full-panel frames must not be scaled at runtime")
        if video.get("pixel_format") != "rgb565le":
            errors.append("video pixel format must be rgb565le")

    font = manifest.get("font", {})
    font_source = ROOT / font.get("source", "")
    font_output = ROOT / font.get("output", "")
    if not font_source.is_file():
        errors.append(f"missing font source {font_source.relative_to(ROOT)}")
    elif sha256(font_source) != font.get("source_sha256"):
        errors.append("source font changed; regenerate heart_font_80.c")
    if not font_output.is_file():
        errors.append(f"missing generated font {font_output.relative_to(ROOT)}")
    else:
        if (font_output.stat().st_size != font.get("bytes") or
                sha256(font_output) != font.get("output_sha256")):
            errors.append(f"{font_output.relative_to(ROOT)} does not match the manifest")
        text = font_output.read_text(encoding="utf-8")
        if "lv_font_t heart_font_80" not in text:
            errors.append("generated font does not declare lv_font_t heart_font_80")
        if re.search(r"[A-Za-z]:[\\/]Users", text) or ("/" + "home/") in text:
            errors.append("generated font contains a private absolute path")
    if set(font.get("symbols", "")) != SYMBOLS:
        errors.append("font symbol set must be 0123456789-")
    if font.get("size") != 80 or font.get("bpp") != 4:
        errors.append("font must stay 80 px, 4 bpp")

    cmake = CMAKELISTS.read_text(encoding="utf-8")
    if "heart_font_80.c" not in cmake:
        errors.append("main/CMakeLists.txt does not compile heart_font_80.c")
    if "EMBED_FILES" not in cmake or "assets/heart_bg.bin" not in cmake:
        errors.append("main/CMakeLists.txt does not embed assets/heart_bg.bin")
    if "heart_font_16" in cmake or "heart_font_20" in cmake:
        errors.append("main/CMakeLists.txt still references the removed Chinese fonts")

    ui = UI.read_text(encoding="utf-8")
    for pattern, label in (
        (r"#define FRAME_COUNT 40", "40 frames"),
        (r"#define FRAME_W 240", "240 px frame width"),
        (r"#define FRAME_H 320", "320 px frame height"),
    ):
        if not re.search(pattern, ui):
            errors.append(f"heart_ui.c is missing {label}")
    if "lv_image_set_scale" in ui:
        errors.append("heart_ui.c must not scale the background at runtime")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("Heartbeat video/font assets: PASS (40 full-panel frames, 80 px numerals)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
