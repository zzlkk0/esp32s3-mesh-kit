// === sensor_node/main.cpp ===
#include <Arduino.h>
#include "painlessMesh.h"
#include <ArduinoJson.h>
#include "DHT.h"

// ===== Mesh 配置 =====
#define MESH_PREFIX    "mesh"
#define MESH_PASSWORD  "ehtbn@@08"
#define MESH_PORT      6666

// ===== DHT 传感器 =====
#define DHTPIN   4
#define DHTTYPE  DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== 周期 =====
static const uint32_t SENSOR_INTERVAL_MS    = 10000;  // 每10s上报
static const uint32_t HEARTBEAT_INTERVAL_MS = 30000;  // 每30s Mesh 心跳
static const uint32_t SERIAL_HEARTBEAT_MS   = 30000;  // 每30s 串口心跳

painlessMesh mesh;
Scheduler userScheduler;

// ---- 工具 ----
// 发送到 Mesh 的 JSON
static inline void sendJsonMesh(const JsonDocument& doc) {
  String s; serializeJson(doc, s); mesh.sendBroadcast(s);
}

// 发送到串口的 JSON（NDJSON，每行一个 JSON）
static inline void sendJsonSerial(const JsonDocument& doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

// peers 数量（用于串口心跳）
size_t getPeerCount() {
  auto nodes = mesh.getNodeList();
  return nodes.size();
}

// ---- 上报（v6 写法）----
void sendAnnounce() {
  StaticJsonDocument<128> d;
  d["type"] = "announce";
  d["dev"]  = "sensor";
  d["node"] = (uint32_t)mesh.getNodeId(); // 带上 ID
  sendJsonMesh(d);
}

void sendHeartbeatMesh() {
  StaticJsonDocument<128> d;
  d["type"] = "hb";
  d["dev"]  = "sensor";
  d["node"] = (uint32_t)mesh.getNodeId(); // 带上 ID
  sendJsonMesh(d);
}

void sendHeartbeatSerial() {
  StaticJsonDocument<256> d;
  d["evt"]   = "hb";
  d["type"]  = "sensor";
  d["node"]  = (uint32_t)mesh.getNodeId();
  d["peers"] = (uint32_t)getPeerCount();
  sendJsonSerial(d);
}

void sendSensor() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  // 1. 发送给 Mesh 网络
  {
    StaticJsonDocument<256> doc;
    doc["type"] = "sensor";
    doc["node"] = (uint32_t)mesh.getNodeId(); // 告诉网关是哪个传感器的数据
    JsonObject data = doc.createNestedObject("data");

    if (isnan(t)) data["t"] = nullptr; else data["t"] = t;
    if (isnan(h)) data["h"] = nullptr; else data["h"] = h;
    sendJsonMesh(doc);
  }

  // 2. 发送给本地串口
  {
    StaticJsonDocument<256> doc;
    doc["evt"]  = "report";
    doc["type"] = "sensor";
    doc["node"] = (uint32_t)mesh.getNodeId();
    JsonObject data = doc.createNestedObject("data");

    if (isnan(t)) data["t"] = nullptr; else data["t"] = t;
    if (isnan(h)) data["h"] = nullptr; else data["h"] = h;
    sendJsonSerial(doc);
  }
}

// ---- 任务 ----
void taskSensorCb()   { sendSensor(); }
void taskHeartbeatCb(){ sendHeartbeatMesh(); }
void taskSerialHbCb() { sendHeartbeatSerial(); }

Task taskSensor   (TASK_MILLISECOND * SENSOR_INTERVAL_MS,    TASK_FOREVER, &taskSensorCb);
Task taskHeartbeat(TASK_MILLISECOND * HEARTBEAT_INTERVAL_MS, TASK_FOREVER, &taskHeartbeatCb);
Task taskSerialHb (TASK_MILLISECOND * SERIAL_HEARTBEAT_MS,   TASK_FOREVER, &taskSerialHbCb);

// ---- Mesh 回调 ----
void receivedCallback(uint32_t, String&) { 
  // 传感器属于只读设备，通常不需要接收网关的控制指令，所以这里保持为空即可。
}
void newConnectionCallback(uint32_t) {}
void changedConnectionCallback() {}
void nodeTimeAdjustedCallback(int32_t) {}

void setup() {
  Serial.begin(115200);
  uint32_t t0=millis(); while(!Serial && millis()-t0<2000) delay(10);

  dht.begin();

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskSensor);
  userScheduler.addTask(taskHeartbeat);
  userScheduler.addTask(taskSerialHb);
  taskSensor.enable();
  taskHeartbeat.enable();
  taskSerialHb.enable();

  // 开机向串口打一条启动信息 + 立即一条串口心跳
  {
    StaticJsonDocument<192> boot;
    boot["evt"]  = "boot";
    boot["type"] = "sensor";
    boot["node"] = (uint32_t)mesh.getNodeId();
    sendJsonSerial(boot);
    sendHeartbeatSerial();
  }

  sendAnnounce();
  sendHeartbeatMesh();
}

void loop(){ mesh.update(); }