# ESP32s3_button

按钮输入节点，基于 ESP32-S3 和 PainlessMesh。

## 硬件

- ESP32-S3 开发板
- 按钮/轻触开关

## 接线

| ESP32-S3 | 按钮      |
|----------|----------|
| GPIO0    | 按钮一端  |
| 3V3      | 按钮另一端（通过上拉）|

## 功能

- 检测按钮按下事件
- 通过 Mesh 网络发送按钮状态到网关
- 支持长按和短按检测

## 配置

在 `platformio.ini` 中配置 GPIO：

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
```

## 烧录

```bash
cd ESP32s3_button
pio run --target upload
```
