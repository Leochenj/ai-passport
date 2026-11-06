<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Heartbeat

An AI Passport application that shows only the live heart rate as a numeral over
a looping cat-and-heart video background. There is no menu, status text,
statistics page, battery indicator, or button action.

## Data path

```text
Apple Watch / iPhone Health
        │  (HealthKit, inside a third-party broadcaster)
        ▼
iPhone BLE broadcaster  ── standard Heart Rate Service 0x180D ──▶  AI Passport
                         Heart Rate Measurement 0x2A37            (BLE central)
```

Apple Watch does not expose the standard Heart Rate Service to third-party
devices. A small iPhone app must read the watch/Health data and rebroadcast it.
[HeartCast](https://www.heartcast.app/) is one such app; any app or chest strap
that advertises the standard 0x180D service works.

## Display

- The supplied video is converted to 40 frames at 5 fps, 240 × 320 RGB565,
  center-cropped to 3:4 and drawn at the panel's native resolution. Full-panel
  frames avoid the ESP32-C3 software scaler, which cannot sustain continuous
  playback in this application.
- The only overlay is the BPM numeral, bottom-center, with a white outline for
  contrast on the light and dark parts of the video.
- Before the first notification, and whenever the last notification is older
  than 5 s, the numeral shows `--`.
- The three physical buttons are not used. The device scans and reconnects
  automatically, preferring the last sensor address stored in NVS.

Set up the iPhone broadcaster, keep the Apple Watch on the wrist, and power on
the AI Passport. The first valid notification replaces `--` with the current
heart rate.

## Assets and generation

| Path | Role |
| --- | --- |
| `assets/video/heart_cat.mp4` | Source cat video supplied by the project owner. |
| `main/assets/heart_bg.bin` | 40 full-panel RGB565 little-endian frames, 6,144,000 bytes. |
| `main/fonts/heart_font_80.c` | 80 px numerals `0123456789-` generated from Noto Sans SC. |
| `tools/gen_heart_app_assets.py` | Regenerates the frames, font, and manifest. |
| `tests/test_heart_app_assets.py` | Checks frame size/digest, font wiring, and firmware constants. |

Generate from the repository root:

```text
LV_FONT_CONV=/path/to/lv_font_conv.js NODE_EXE=/path/to/node \
python3 tools/gen_heart_app_assets.py
```

The embedded frame array is read directly from Flash; no JPEG decoder or
full-frame RAM buffer is required.

## Build and validation

```text
./tools/validate.sh --static
./tools/validate.sh --firmware
```

`--firmware` produces `build/FoloToy-AI-Passport-full.bin` for flashing at
offset `0x0`. The merged image may reset NVS.

## Limits and unverified items

- Apple Watch cannot connect directly; the iPhone broadcaster is mandatory.
- The numeral shows the last value for up to 5 s, then `--`; there is no other
  connection indicator.
- The 5 fps loop must be checked for smoothness and CPU load on the device; the
  first attempt at LVGL scaling triggered the task watchdog and was replaced by
  full-panel frames.
- Some iPhone broadcasters send a non-standard one- or two-byte BPM instead of a
  complete Heart Rate Measurement frame. The firmware accepts both the standard
  frame and these compatibility variants; the phone app must still provide a
  live heart rate (HealthKit permission and, for watch data, an active workout).
- BPM accuracy, reconnect behavior with a specific iPhone app, and battery
  runtime are not established by host tests.
