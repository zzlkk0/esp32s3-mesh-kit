# ESP32s3_web_node

Web 控制界面节点，基于 ESP32-S3，提供网页控制界面。

## 功能

- 内置 Web 服务器
- 通过网页控制 Mesh 网络中的节点
- 实时显示传感器数据
- 控制开关状态

## 访问

1. 启动设备后，ESP32-S3 会创建 WiFi 热点
2. 连接热点，访问 `192.168.4.1`
3. 或连接到同一 WiFi 网络后，通过 mDNS 访问

## 配置

在 `platformio.ini` 中配置：

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
```

## 文件系统

`data/` 目录包含网页文件，会被烧录到 SPIFFS/LittleFS。

## 烧录

```bash
# 烧录固件
cd ESP32s3_web_node
pio run --target upload

# 烧录文件系统
pio run --target uploadfs
```

## 备选固件

`src/` 目录中有多个备选版本的固件：

- `main.cpp` - 当前使用版本
- `main完美运行button.txt` - 兼容按钮的版本
- `main最接近完美的.txt` - 功能较全的版本

根据需要选择合适的版本替换 `main.cpp`。
