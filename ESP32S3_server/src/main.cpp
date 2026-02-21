// === server/main.cpp ===
#include <Arduino.h>
#include "painlessMesh.h"
#include <ArduinoJson.h>
#include <vector>

// ================== Mesh 配置 ==================
#define MESH_PREFIX    "mesh"
#define MESH_PASSWORD  "ehtbn@@08"
#define MESH_PORT      6666

// 心跳与离线判定
static const uint32_t HEARTBEAT_INTERVAL_MS = 30000;
static const uint32_t OFFLINE_TIMEOUT_MS    = 45000; 

// 串口解析参数
static const uint32_t SERIAL_IDLE_PARSE_MS  = 200;   
static const size_t   SERIAL_BUF_MAX        = 1024;  
static const uint32_t SERVER_HB_INTERVAL_MS = 60000; 

// UART1（与 Web ESP32 通信的接口）
static const int UART1_TX_PIN = 17; // 接 Web 板的 RX
static const int UART1_RX_PIN = 18; // 接 Web 板的 TX

// ================== 全局对象 ==================
painlessMesh mesh;
Scheduler userScheduler;

// ================== 设备模型 ==================
enum class DevType : uint8_t { UNKNOWN=0, SWITCH, BUTTON, SENSOR };

struct Device {
  uint32_t id;
  DevType  type;
  uint32_t lastSeenMs;
  bool     online;
  int      switchState; // 仅 SWITCH 用
};

std::vector<Device> devices;

// ================== 任务声明 ==================
void taskPresenceSweepCb();
void taskSerialPollCb();
void taskServerHbCb();
Task taskPresenceSweep(TASK_SECOND * 5, TASK_FOREVER, &taskPresenceSweepCb);
Task taskSerialPoll (TASK_MILLISECOND * 50, TASK_FOREVER, &taskSerialPollCb);
Task taskServerHb   (TASK_MILLISECOND * SERVER_HB_INTERVAL_MS, TASK_FOREVER, &taskServerHbCb);

// ================== 工具函数 ==================
const char* devTypeToStr(DevType t) {
  switch (t) {
    case DevType::SWITCH: return "switch";
    case DevType::BUTTON: return "button";
    case DevType::SENSOR: return "sensor";
    default:              return "unknown";
  }
}
DevType strToDevType(const String& s) {
  if (s == "switch") return DevType::SWITCH;
  if (s == "button") return DevType::BUTTON;
  if (s == "sensor") return DevType::SENSOR;
  return DevType::UNKNOWN;
}

int findDeviceIdx(uint32_t id) {
  for (size_t i=0;i<devices.size();++i) if (devices[i].id==id) return (int)i;
  return -1;
}

// 双通道 JSON 输出：同时发给 PC(Serial) 和 Web板(Serial1)
template <typename TDoc>
static inline void sendJson(const TDoc& doc) { 
  serializeJson(doc, Serial); Serial.println(); 
  serializeJson(doc, Serial1); Serial1.println(); 
}

// 单向回复：谁发出的命令，就单独回复给谁
template <typename TDoc>
static inline void replyJson(Stream& port, const TDoc& doc) { 
  serializeJson(doc, port); port.println(); 
}

Device& upsertDevice(uint32_t id) {
  int idx = findDeviceIdx(id);
  if (idx >= 0) return devices[idx];
  Device d; d.id=id; d.type=DevType::UNKNOWN; d.lastSeenMs=millis(); d.online=true; d.switchState=-1;
  devices.push_back(d);
  
  StaticJsonDocument<256> doc;
  doc["evt"] = "discover";
  doc["id"]  = id;
  doc["type"]= devTypeToStr(d.type);
  sendJson(doc);
  return devices.back();
}

void markSeen(Device& d) {
  d.lastSeenMs = millis();
  if (!d.online) {
    d.online = true;
    StaticJsonDocument<256> doc;
    doc["evt"] = "online";
    doc["id"]  = d.id;
    doc["type"]= devTypeToStr(d.type);
    sendJson(doc);
  }
}

void setDevType(Device& d, DevType t) {
  if (d.type == t) return;
  d.type = t;
  StaticJsonDocument<256> doc;
  doc["evt"]  = "type";
  doc["id"]   = d.id;
  doc["type"] = devTypeToStr(t);
  sendJson(doc);
}

// ================== 串口命令（支持定向回复） ==================
bool parseNodeIdFlexible(const JsonVariantConst& v, uint32_t& out) {
  if (v.is<uint32_t>()) { out = v.as<uint32_t>(); return true; }
  if (v.is<int64_t>())  { int64_t x = v.as<int64_t>(); if (x<0 || x>0xFFFFFFFF) return false; out=(uint32_t)x; return true; }
  if (v.is<double>())   { double d = v.as<double>(); if (d<0.0 || d>4294967295.0) return false; out = (uint32_t)(d + 0.5); return true; }
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (!s || !*s) return false;
    char* endp = nullptr;
    uint32_t base = (s[0]=='0' && (s[1]=='x' || s[1]=='X')) ? 16 : 10;
    unsigned long val = strtoul(s, &endp, base);
    if (endp==s || *endp!='\0') return false;
    out = (uint32_t)val; return true;
  }
  return false;
}

void handleSerialLine(const String& line, Stream& port) {
  String s = line; s.trim();
  if (!s.length()) return;

  StaticJsonDocument<512> in;
  DeserializationError err = deserializeJson(in, s);
  if (err) {
    StaticJsonDocument<160> out; out["err"] = "bad_json"; out["msg"] = err.c_str();
    replyJson(port, out); return;
  }

  const char* cmd = in["cmd"] | "";
  if (cmd[0] == '\0') {
    StaticJsonDocument<160> out; out["err"]="bad_args";
    JsonArray need = out.createNestedArray("need"); need.add("cmd");
    replyJson(port, out); return;
  }

  // ping: 握手心跳
  if (strcmp(cmd, "ping") == 0) {
    StaticJsonDocument<96> ack;
    ack["ack"] = "ping";
    ack["ok"] = true;
    replyJson(port, ack);
    
    if (&port == &Serial1) {
      static uint32_t lastWebPingMsg = 0;
      if (millis() - lastWebPingMsg > 5000) { 
        Serial.println("\n[系统]  成功与 Web 通信板建立 UART1 握手！");
        lastWebPingMsg = millis();
      }
    }
    return;
  }

  // list: 请求花名册
  if (strcmp(cmd, "list")==0) {
    StaticJsonDocument<1536> out;
    out["evt"] = "devices";
    JsonArray arr = out.createNestedArray("list");
    for (auto& d: devices) {
      JsonObject o = arr.createNestedObject();
      o["id"] = d.id;
      o["type"] = devTypeToStr(d.type);
      o["online"] = d.online;
      if (d.type==DevType::SWITCH) {
        if (d.switchState < 0) o["state"] = nullptr;
        else                   o["state"] = d.switchState;
      }
      o["lastSeenMs"] = d.lastSeenMs;
    }
    replyJson(port, out);
    return;
  }

  // switch
  if (strcmp(cmd,"switch")==0) {
    if (!in.containsKey("id") || !in.containsKey("state")) {
      StaticJsonDocument<192> out; out["err"]="bad_args";
      JsonArray need = out.createNestedArray("need");
      if (!in.containsKey("id"))    need.add("id");
      if (!in.containsKey("state")) need.add("state");
      replyJson(port, out); return;
    }
    uint32_t id = 0;
    if (!parseNodeIdFlexible(in["id"], id)) {
      StaticJsonDocument<160> out; out["err"]="bad_args";
      replyJson(port, out); return;
    }
    int state = in["state"] | -1;
    if (state!=0 && state!=1) { StaticJsonDocument<96> out; out["err"]="bad_state"; replyJson(port, out); return; }

    StaticJsonDocument<128> ctl; ctl["type"]="ctl"; ctl["dev"]="switch"; ctl["value"]=state; ctl["node"] = id; 
    String payload; serializeJson(ctl, payload);
    bool ok = (id == 0) ? (mesh.sendBroadcast(payload), true) : mesh.sendSingle(id, payload);
    
    StaticJsonDocument<128> ack; ack["ack"]="switch"; ack["id"]=id; ack["ok"]=ok; 
    replyJson(port, ack); return;
  }

  // button
  if (strcmp(cmd,"button")==0) {
    if (!in.containsKey("id")) {
      StaticJsonDocument<160> out; out["err"]="bad_args";
      replyJson(port, out); return;
    }
    uint32_t id = 0;
    if (!parseNodeIdFlexible(in["id"], id)) {
      StaticJsonDocument<160> out; out["err"]="bad_args";
      replyJson(port, out); return;
    }
    uint32_t ms = in["ms"] | 100;
    if (ms==0) ms = 100;

    StaticJsonDocument<128> ctl; ctl["type"]="ctl"; ctl["dev"]="button"; ctl["action"]="press"; ctl["ms"]=ms; ctl["node"] = id;
    String payload; serializeJson(ctl, payload);
    bool ok = (id == 0) ? (mesh.sendBroadcast(payload), true) : mesh.sendSingle(id, payload);
    
    StaticJsonDocument<128> ack; ack["ack"]="button"; ack["id"]=id; ack["ok"]=ok; ack["ms"]=ms; 
    replyJson(port, ack); return;
  }

  { StaticJsonDocument<160> out; out["err"]="unknown_cmd"; out["cmd"]=cmd; replyJson(port, out); }
}

// ================== 双串口独立轮询 ==================
String buf0, buf1;
uint32_t lastByteMs0 = 0, lastByteMs1 = 0;

void pollStream(Stream& port, String& buf, uint32_t& lastMs) {
  while (port.available()) {
    char c = (char)port.read();
    lastMs = millis();

    if (c == '\n' || c == '\r') {
      String line = buf; buf = ""; line.trim();
      if (line.length()) handleSerialLine(line, port);
      continue;
    }
    
    // 过滤掉不可见字符，防止电磁噪声干扰
    if ((c >= 32 && c <= 126) || c == '{' || c == '}') {
      buf += c;
    }
    
    if (c == '}' && buf.startsWith("{")) {
      String line = buf; buf = ""; line.trim();
      if (line.length()) handleSerialLine(line, port);
      continue;
    }
    if (buf.length() > SERIAL_BUF_MAX) buf = ""; 
  }

  if (!buf.isEmpty() && (millis() - lastMs) > SERIAL_IDLE_PARSE_MS) {
    String line = buf; buf = ""; line.trim();
    if (line.length()) handleSerialLine(line, port);
  }
}

void taskSerialPollCb() {
  pollStream(Serial, buf0, lastByteMs0);   // 监听电脑 USB
  pollStream(Serial1, buf1, lastByteMs1);  // 监听 Web 板
}

// ================== 判活与心跳 ==================
void taskPresenceSweepCb() {
  uint32_t now = millis();
  for (auto& d: devices) {
    if (d.online && (now - d.lastSeenMs > OFFLINE_TIMEOUT_MS)) {
      d.online = false;
      StaticJsonDocument<256> doc;
      doc["evt"] = "offline"; doc["id"] = d.id; doc["type"]= devTypeToStr(d.type);
      sendJson(doc);
    }
  }
}

void taskServerHbCb() {
  StaticJsonDocument<192> out;
  out["evt"] = "hb"; 
  out["id"] = (uint32_t)mesh.getNodeId(); // 统一格式使用 id
  out["type"] = "server";
  out["ms"] = millis();
  sendJson(out); 
}

// ================== Mesh 回调 ==================
void receivedCallback(uint32_t from, String &msg) {
  Device& d = upsertDevice(from);
  markSeen(d);

  if (msg.length()==0 || msg[0] != '{') return;

  StaticJsonDocument<512> in;
  DeserializationError err = deserializeJson(in, msg);
  if (err) return;

  String type = in["type"] | "";
  String dev  = in["dev"]  | "";

  if (type == "announce") {
    setDevType(d, strToDevType(dev));
    StaticJsonDocument<256> out;
    out["evt"]="announce"; out["id"]=from; out["type"]=devTypeToStr(d.type);
    sendJson(out);
    return;
  }

  if (type == "hb") {
    if (dev.length()) setDevType(d, strToDevType(dev));
    return; // 心跳只用于更新 lastSeenMs
  }

  if (type == "switch") {
    if (in.containsKey("state")) d.switchState = (int)in["state"];
    StaticJsonDocument<256> out;
    out["evt"]="report"; out["id"]=from; out["type"]="switch";
    out["content"]["state"] = in["state"] | d.switchState;
    sendJson(out);
    return;
  }

  if (type == "sensor") {
    StaticJsonDocument<512> out;
    out["evt"]="report"; out["id"]=from; out["type"]="sensor";
    if (!in["data"].isNull()) {
      JsonObject content = out.createNestedObject("content");
      content["data"] = in["data"];
    }
    sendJson(out);
    return;
  }

  if (type == "button") {
    StaticJsonDocument<256> out;
    out["evt"]="report"; out["id"]=from; out["type"]="button";
    if (in.containsKey("event")) out["content"]["event"]= in["event"].as<const char*>();
    if (in.containsKey("ms"))    out["content"]["ms"] = in["ms"]; 
    sendJson(out);
    return;
  }
}

void newConnectionCallback(uint32_t nodeId) {
  Device& d = upsertDevice(nodeId);
  markSeen(d);
  StaticJsonDocument<256> out; out["evt"]="new"; out["id"]=nodeId; out["type"]=devTypeToStr(d.type);
  sendJson(out);
}
void changedConnectionCallback() {}
void nodeTimeAdjustedCallback(int32_t) {}

// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);
  
  // 【抗干扰优化】上拉 RX 引脚并发送空白对齐符
  pinMode(UART1_RX_PIN, INPUT_PULLUP);
  Serial1.begin(115200, SERIAL_8N1, UART1_RX_PIN, UART1_TX_PIN);
  delay(50);
  Serial1.flush();
  Serial1.print("\n\n\n");
  
  uint32_t t0 = millis();
  while (!Serial && millis()-t0 < 2000) { delay(10); }

  mesh.setDebugMsgTypes(ERROR | STARTUP); 
  // 【恢复默认】去掉信道参数，让 mesh 自动协商
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskPresenceSweep);
  userScheduler.addTask(taskSerialPoll);
  userScheduler.addTask(taskServerHb);
  taskPresenceSweep.enable();
  taskSerialPoll.enable();
  taskServerHb.enable();

  StaticJsonDocument<160> boot;
  boot["evt"]  = "boot";
  boot["type"] = "server";
  boot["id"]   = (uint32_t)mesh.getNodeId();
  sendJson(boot);
}

void loop() {
  mesh.update();
}