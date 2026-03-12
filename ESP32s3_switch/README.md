# ESP32s3_switch

开关控制节点，基于 ESP32-S3 和 PainlessMesh。

## 硬件

- ESP32-S3 开发板
- 继电器模块（或MOSFET）

## 接线

| ESP32-S3 | 继电器模块  |
|----------|------------|
| 3V3      | VCC        |
| GPIO5    | IN (控制信号) |
| GND      | GND        |

## 功能

- 接收网关发来的开关控制命令
- 控制继电器开/关
- 可控制灯、风扇等设备

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
cd ESP32s3_switch
pio run --target upload
```
