<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, video, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Heartbeat application

| File | Size and format | Use and source |
| --- | --- | --- |
| [`fonts/NotoSansSC-Regular.otf`](fonts/NotoSansSC-Regular.otf) | 8.3 MB, OpenType (CFF) | Source font for the heartbeat numerals, Noto Sans SC Regular. Licensed under the SIL Open Font License 1.1; the bundled [`fonts/OFL.txt`](fonts/OFL.txt) is the license copy. |
| `main/fonts/heart_font_80.c` | Generated LVGL 9 C array, 4 bpp, uncompressed | 80 px heart-rate numerals: `0123456789-` only. The dash renders the `--` no-data state. |

Generate the font and the video frames with `lv_font_conv` 1.5.3 and FFmpeg from
the repository root:

```text
LV_FONT_CONV=/path/to/lv_font_conv.js NODE_EXE=/path/to/node \
python3 tools/gen_heart_app_assets.py
```

The script verifies all eleven font symbols against the converter's `dump`
output, writes `tests/heart_app_assets.json`, converts the cat video to
`main/assets/heart_bg.bin`, and renders a developer preview to
`.tmp/heart_preview.png`. `tests/test_heart_app_assets.py` fails when a source
or generated asset changes without regeneration. Generated files are committed
so firmware builds do not require Node.js or FFmpeg.

## Video

Store reusable video sources in `video/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`video/heart_cat.mp4`](video/heart_cat.mp4) | 720 × 1280, 8.04 s, 24 fps, H.264 video only | Cat-and-heart background supplied by the project owner for this application; no separate redistribution license is asserted. The source audio track and AIGC metadata were removed for the public repository. The AI-generation watermark at the source edge is outside the center crop used for the display frames. |
| `main/assets/heart_bg.bin` | 6,144,000 bytes, 40 frames of 240 × 320 RGB565 little-endian, 5 fps | Generated from the source video by `tools/gen_heart_app_assets.py`. Frames are center-cropped to 3:4 and used at the panel's native resolution; no runtime scaling is performed. |

- The firmware reads the frame binary directly from Flash and draws each
  full-panel frame without a transform, avoiding both a 150 KB frame buffer and
  the slow ESP32-C3 software scaler.
- Re-run the generator when changing the video, frame size, frame rate, or
  frame count; update the constants in `main/heart_ui.c` together with it.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
