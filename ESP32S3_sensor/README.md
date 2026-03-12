# ESP32S3_sensor

DHT 温度湿度传感器节点，基于 ESP32-S3 和 PainlessMesh 组网。

## 硬件

- ESP32-S3 开发板
- DHT11/DHT22 温湿度传感器

## 接线

| ESP32-S3 | DHT 传感器 |
|----------|-----------|
| 3V3      | VCC       |
| GPIO4    | DATA      |
| GND      | GND       |

## 配置

在 `platformio.ini` 中修改设置：

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino

lib_deps =
    beekeeto/DHT sensor library for ESPx@^1.19
    adafruit/DHT sensor library@^1.4.6
    adafruit/Adafruit Unified Sensor@^1.1.14
```

## 烧录

```bash
cd ESP32S3_sensor
pio run --target upload
```

## 串口监视器

```bash
pio device monitor
```
