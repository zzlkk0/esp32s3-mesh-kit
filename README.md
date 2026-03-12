# esp32s3-mesh-kit

# ESP32-S3 Mesh Kit - 基于 ESP32-S3 的 WiFi Mesh 组网方案

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESPressif32-blue)](https://platformio.org/)
[![Arduino](https://img.shields.io/badge/Framework-Arduino-green)](https://www.arduino.cc/)

An open-source IoT and WiFi Mesh project based on ESP32-S3. Includes sensors, buttons, switches, and a web-based control interface.

---

## Features

- WiFi Mesh network based on ESP32-S3 (Pover)
- DHT temperature and humidity sensor
- Button input
- Switch control
- Web based control interface
- Asynchronous operation

## Modules

| Module                  | Description                                                    |
|-------------------------|----------------------------------------------------------------|
| ESP32S3_sensor         | DHT sensor node (Temperature/Humidity)                        |
| ESP32S3_server         | Mesh Gateway Node                                             |
| ESP32s3_button         | Button input node                                             |
| ESP32s3_switch         | Switch control node                                           |
| ESP32s3_web_node       | Web based control node                                        |

## Connection

An ESP32-S3 board communicates with an ESP32-S3 FLASH Simple-Protocol Mesh network. The SERVER node acts as the mesh gateway.

FOR example, if you want to connect a 3D board to the mesh network:

1. Build the SERVER node first
2. Build remote nodes (sensor/button/switch)

The SERVER node should start first, and remote nodes will auto-connect to it.

---

## Build and Flash

Use platformio to build and upload the firmware.

```bash
# Install platformio
pip install platformio

# Clone this repository and open in PlatformIO
git clone https://github.com/zzlkk0/esp32s3-mesh-kit.git
cd esp32s3-mesh-kit/ESP32S3_sensor
pio run --target upload
```

---

## Documentation

Within each module directory, there is a README.md file that covers specific details.

> * [ESP32S3_sensor](./ESP32S3_sensor/README.md) - DHT sensor
> * [ESP32S3_server](./ESP32S3_server/README.md) - Mesh Gateway
> * [ESP32s3_button](./ESP32s3_button/README.md) - Button input
> * [ESP32s3_switch](./ESP32s3_switch/README.md) - Switch control
> * [ESP32s3_web_node](./ESP32s3_web_node/README.md) - Web control

---

## License

Project is released under MIT License. See LICENSE.md for details.

---

## Contact

For questions or contributions, please open an issue or submit a pull request.
