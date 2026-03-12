# ESP32S3_server

Mesh 网络网关节点，基于 ESP32-S3 和 PainlessMesh。

## 功能

- 作为 Mesh 网络的根节点/网关
- 接收来自传感器、按钮、开关的数据
- 汇总并处理网络中的所有节点

## 配置

在 `platformio.ini` 中配置网络参数：

```ini
[env:esp32-c3-devkitm-1]
platform = espressif32
board = esp32-c3-devkitm-1
framework = arduino
```

## 启动顺序

**重要**：网关节点需要首先启动，然后再启动其他节点。

1. 先烧录并启动 ESP32S3_server
2. 等待串口输出 "Mesh connected"
3. 再烧录其他节点（sensor/button/switch）

## 串口输出

连接后可在串口监视器查看：
- 连接的节点数量
- 接收到的传感器数据
- 网络状态
