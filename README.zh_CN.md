<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# AI Passport — 心跳

<p align="center">
  <img src="docs/applications/heartbeat/preview.png" width="240" alt="猫咪视频背景与 72 BPM 大数字">
</p>

一个 FoloToy AI Passport 固件，只在循环播放的猫咪爱心视频上显示**实时心率数字**。
没有菜单、状态文字、统计页、电量显示，也没有按键功能。

本项目基于 [FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport)，保留上游 BSP、
构建系统和 MIT 许可证。

## 行为

- 40 帧、5 fps、240 × 320 的视频循环，直接从 Flash 读取。
- 底部只有一个 80 px 心率数字，带白色描边以保证对比度。
- 收到第一个有效通知前，以及超过 5 秒没有新数据时，显示 `--`。
- 三个实体按键不初始化。
- 上次心率设备地址保存在 NVS 中，重连时优先使用。

5 fps 和整屏帧是刻意取舍：LVGL 对 120 × 160 帧做 12 fps 软件缩放会触发 ESP32-C3
任务看门狗，因此改为直接绘制 240 × 320 RGB565 整屏帧。

## 数据通路

```text
Apple Watch / iPhone 健康
        │  HealthKit
        ▼
iPhone BLE 广播 App  ── 标准心率服务 0x180D ──▶  AI Passport
                       心率测量特征 0x2A37       (BLE 中央设备)
```

Apple Watch 不会把标准心率服务直接暴露给第三方设备，必须由 iPhone App 读取手表/
健康数据后广播。[HeartCast](https://www.heartcast.app/) 是其中一种；标准 BLE 心率
胸带也可以直接使用。

要让手表数据实时更新，iPhone App 通常需要 HealthKit 权限和进行中的体能训练。设备
同时接受标准心率测量帧，以及部分 iPhone App 发出的 1 或 2 字节原始 BPM 帧。

## 构建

要求：ESP-IDF 5.5.3、目标 `esp32c3`、8 MB Flash、无 PSRAM。

```bash
./tools/validate.sh --static
./tools/validate.sh --firmware
```

固件门禁会生成用于 `0x0` 地址的合并镜像 `build/FoloToy-AI-Passport-full.bin`。
镜像包含 NVS/PHY 间隙，从 `0x0` 刷写可能重置已保存的设置。

使用 ESP-IDF 的 Python 环境刷写：

```bash
python -m esptool --chip esp32c3 --port COMx --baud 460800 \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB \
  0x0 build/FoloToy-AI-Passport-full.bin
```

预编译镜像及其 SHA-256 清单发布在 GitHub Releases 中。不要提交 `build/`，它已被
`.gitignore` 忽略。

## 目录

| 路径 | 作用 |
| --- | --- |
| `main/heart_ble.c` | NimBLE 中央设备、心率服务发现/订阅、自动重连 |
| `main/heart_model.c` | 标准与兼容 BPM 解析 |
| `main/heart_ui.c` | 整屏视频循环与唯一的 BPM 数字 |
| `main/heart_app.c` | BLE 状态轮询与界面更新任务 |
| `main/assets/heart_bg.bin` | 40 帧 240 × 320 RGB565 小端数据 |
| `main/fonts/heart_font_80.c` | 80 px 数字 `0123456789-` |
| `assets/video/heart_cat.mp4` | 源视频 |
| `tools/gen_heart_app_assets.py` | 重新生成帧、字库和清单 |
| `docs/applications/heartbeat/README.zh_CN.md` | 详细行为与设置说明 |

## 测试

| 检查 | 状态 |
| --- | --- |
| 主机测试 / 静态门禁 | PASS |
| 固件构建、合并、布局校验 | PASS |
| 设备启动、BLE 扫描、iPhone 广播显示 | 在开发机上 PASS |

主机测试不覆盖：长时间稳定性、其它 iPhone 广播 App、电池续航、视频流畅度的主观感受。

## 资源与许可

- 代码与文档：MIT，见 [`LICENSE`](LICENSE)。
- 上游项目：[FoloToy AI Passport](https://gitee.com/FoloToy/ai-passport)，MIT。
- 字体：Noto Sans SC，SIL Open Font License 1.1，见 [`assets/fonts/OFL.txt`](assets/fonts/OFL.txt)。
- 猫咪视频：由项目所有者提供，未单独声明再分发许可。

完整归属说明见 [`NOTICE`](NOTICE)。

## 医疗声明

本设备不是医疗设备。心率数值及其派生信息仅供参考，不能用于诊断或治疗。
