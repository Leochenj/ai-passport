<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# AI Passport — Heartbeat

<p align="center">
  <img src="docs/applications/heartbeat/preview.png" width="240" alt="Cat video background with a large 72 BPM numeral">
</p>

A FoloToy AI Passport firmware that shows **only the live heart rate** over a
looping cat-and-heart video. There is no menu, status text, statistics page,
battery indicator, or button action.

The application is derived from [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport)
and keeps the upstream BSP, build system, and MIT license.

## Behavior

- 40-frame, 5 fps, 240 × 320 video loop read directly from Flash.
- A single 80 px BPM numeral at the bottom, with a white outline for contrast.
- `--` before the first valid notification and after 5 seconds without one.
- The three physical buttons are not initialized.
- The last sensor address is stored in NVS and preferred on reconnect.

The 5 fps loop and full-panel frames are deliberate: LVGL software scaling of
120 × 160 frames at 12 fps triggered the ESP32-C3 task watchdog, so the display
draws native 240 × 320 RGB565 frames instead.

## Data path

```text
Apple Watch / iPhone Health
        │  HealthKit
        ▼
iPhone BLE broadcaster  ── standard Heart Rate Service 0x180D ──▶  AI Passport
                         Heart Rate Measurement 0x2A37            (BLE central)
```

Apple Watch does not expose the standard Heart Rate Service directly to third-party
devices. An iPhone app is required to read the Watch/Health data and rebroadcast
it. [HeartCast](https://www.heartcast.app/) is one option; a standard BLE heart
rate strap also works.

For live Watch data, the iPhone app normally needs HealthKit permission and an
active workout. The device accepts both the standard Heart Rate Measurement frame
and the 1- or 2-byte raw-BPM variants some iPhone apps emit.

## Build

Requirements: ESP-IDF 5.5.3, target `esp32c3`, 8 MB Flash, no PSRAM.

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

The firmware gate writes `build/FoloToy-AI-Passport-full.bin`, a merged image for
offset `0x0`. The image includes NVS/PHY gaps; flashing it from `0x0` can reset
saved settings.

Flash with the ESP-IDF Python environment:

```bash
python -m esptool --chip esp32c3 --port COMx --baud 460800 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 build/FoloToy-AI-Passport-full.bin
```

Prebuilt images and their SHA-256 manifests are published under GitHub Releases.
Do not commit `build/`; it is ignored.

## Repository map

| Path | Purpose |
| --- | --- |
| `main/heart_ble.c` | NimBLE central, HRS discovery/subscription, reconnect |
| `main/heart_model.c` | Standard and compatibility BPM parsers |
| `main/heart_ui.c` | Full-panel video loop and sole BPM numeral |
| `main/heart_app.c` | BLE status polling and UI update task |
| `main/assets/heart_bg.bin` | 40 frames of 240 × 320 RGB565 little-endian |
| `main/fonts/heart_font_80.c` | 80 px numerals `0123456789-` |
| `assets/video/heart_cat.mp4` | Source video |
| `tools/gen_heart_app_assets.py` | Regenerates frames, font, and manifest |
| `docs/applications/heartbeat/README.md` | Detailed behavior and setup |

## Tests

| Check | Status |
| --- | --- |
| Host tests / static gate | PASS |
| Firmware build, merge, layout verification | PASS |
| Device boot, BLE scan, iPhone broadcast display | PASS on the development unit |

Not covered by host tests: long-run stability, other iPhone broadcaster apps,
battery runtime, and subjective video smoothness.

## Assets and licenses

- Code and documentation: MIT. See [`LICENSE`](LICENSE).
- Upstream project: [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport), MIT.
- Font: Noto Sans SC, SIL Open Font License 1.1; see [`assets/fonts/OFL.txt`](assets/fonts/OFL.txt).
- Cat video: supplied by the project owner; no separate redistribution license is asserted.

See [`NOTICE`](NOTICE) for attribution details.

## Medical disclaimer

This is not a medical device. Heart rate values and any derived information are
for reference only and must not be used for diagnosis or treatment.
