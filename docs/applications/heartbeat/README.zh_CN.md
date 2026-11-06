<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 心跳

一个只显示实时心率数字的 AI Passport 应用，背景循环播放猫咪爱心视频。没有菜单、
状态文字、统计页、电量显示，也没有按键功能。

## 数据通路

```text
Apple Watch / iPhone 健康
        │  (HealthKit,由第三方广播 App 读取)
        ▼
iPhone BLE 广播 App  ── 标准心率服务 0x180D ──▶  AI Passport
                       心率测量特征 0x2A37          (BLE 中央设备)
```

Apple Watch 不会把标准心率服务直接暴露给第三方设备,必须由 iPhone 上的小 App
读取手表/健康数据后再广播。[HeartCast](https://www.heartcast.app/) 是其中一种;
任何广播标准 0x180D 服务的 App 或心率胸带都可以配合本固件使用。

## 显示

- 附件视频被转换为 40 帧、5 fps、240 × 320 RGB565,中心裁切为 3:4,直接按面板原生
  分辨率绘制。整屏帧避开 ESP32-C3 的软件缩放器,因为该路径无法稳定支持连续播放。
- 唯一的叠加内容是底部居中的心率数字,带白色描边,以便在画面明暗区域都清晰。
- 收到第一帧通知前,以及最后一帧超过 5 秒时,数字显示 `--`。
- 三个实体按键不参与功能。设备自动扫描并重连,优先使用 NVS 中保存的上次设备地址。

在 iPhone 上打开心率广播 App、保持手表佩戴,然后开机。收到有效心率后,`--` 会被
当前心率替换。

## 资源与生成

| 路径 | 作用 |
| --- | --- |
| `assets/video/heart_cat.mp4` | 项目所有者提供的猫咪视频源。 |
| `main/assets/heart_bg.bin` | 40 帧整屏 RGB565 小端数据,共 6,144,000 字节。 |
| `main/fonts/heart_font_80.c` | 由 Noto Sans SC 生成的 80 px 数字 `0123456789-`。 |
| `tools/gen_heart_app_assets.py` | 重新生成帧、字库与清单。 |
| `tests/test_heart_app_assets.py` | 检查帧大小/摘要、字库接线与固件常量。 |

在仓库根目录生成：

```text
LV_FONT_CONV=/path/to/lv_font_conv.js NODE_EXE=/path/to/node \
python3 tools/gen_heart_app_assets.py
```

帧数据嵌入 Flash 后直接读取,不需要 JPEG 解码器或整帧 RAM 缓冲。

## 构建与验证

```text
./tools/validate.sh --static
./tools/validate.sh --firmware
```

`--firmware` 会生成用于 `0x0` 地址刷写的 `build/FoloToy-AI-Passport-full.bin`。
合并镜像可能重置 NVS。

## 限制与未验证项

- Apple Watch 不能直连,必须有 iPhone 广播 App。
- 数字最多显示上一帧 5 秒,之后变为 `--`;没有其它连接状态提示。
- 5 fps 循环的流畅度和 CPU 占用必须在真机上检查;第一版使用 LVGL 缩放时触发了
  任务看门狗,已改为整屏帧。
- 部分 iPhone 广播 App 发送的不是完整的心率测量帧,而是 1 或 2 字节的原始 BPM。
  固件同时接受标准帧和这些兼容格式;但手机 App 本身必须提供实时心率(HealthKit
  权限,手表数据还需要进行中的体能训练)。
- 心率准确性、与具体 iPhone App 的重连行为、电池续航不能由主机测试证明。
