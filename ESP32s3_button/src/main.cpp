// === button_node/main.cpp ===
#include <Arduino.h>
#include "painlessMesh.h"
#include <ArduinoJson.h>

// ===== Mesh 配置 =====
#define MESH_PREFIX    "mesh"
#define MESH_PASSWORD  "ehtbn@@08"
#define MESH_PORT      6666

// ===== 硬件（可选）=====
// 将 BUTTON_PIN 连接到你的“被按键”电路；设为 -1 表示不接硬件，仅逻辑动作
#define BUTTON_PIN       3    // 例：改成 2
#define BUTTON_IDLE_HIGH 0     // 空闲电平是否为高：1=高空闲(按下为低)，0=低空闲(按下为高)

// ===== 周期 =====
static const uint32_t HEARTBEAT_INTERVAL_MS   = 30000; // Mesh 心跳
static const uint32_t SERIAL_HEARTBEAT_MS     = 30000; // 串口心跳
static const uint32_t SERIAL_POLL_INTERVAL_MS = 50;    // 串口轮询
static const uint32_t PRESS_DEFAULT_MS        = 100;   // 默认按下时长

painlessMesh mesh;
Scheduler userScheduler;

// ---- 按键脉冲状态（非阻塞）----
bool pressing = false;
uint32_t pressEndMs = 0;

int idleLevel()  { return BUTTON_IDLE_HIGH ? HIGH : LOW; }
int pressLevel() { return BUTTON_IDLE_HIGH ? LOW  : HIGH; }

void hwInit() {
  if (BUTTON_PIN >= 0) {
    pinMode(BUTTON_PIN, OUTPUT);
    digitalWrite(BUTTON_PIN, idleLevel());
  }
}
void hwStartPress(uint32_t ms) {
  if (BUTTON_PIN >= 0) {
    digitalWrite(BUTTON_PIN, pressLevel());
  }
  pressing = true;
  pressEndMs = millis() + ms;
}
void hwEndPress() {
  if (BUTTON_PIN >= 0) {
    digitalWrite(BUTTON_PIN, idleLevel());
  }
  pressing = false;
}

// ---- Mesh 上报工具 ----
static inline void sendJsonMesh(const JsonDocument& doc) {
  String s; serializeJson(doc, s); mesh.sendBroadcast(s);
}

// ---- 串口 JSON 工具（NDJSON：每行一个 JSON）----
static inline void sendJsonSerial(const JsonDocument& doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

// ---- 上报封装 ----
void sendAnnounce() {
  StaticJsonDocument<128> d;
  d["type"] = "announce";
  d["dev"]  = "button";
  d["node"] = (uint32_t)mesh.getNodeId(); // 上线广播带上 ID
  sendJsonMesh(d);
}
void sendHeartbeatMesh() {
  StaticJsonDocument<128> d;
  d["type"] = "hb";
  d["dev"]  = "button";
  d["node"] = (uint32_t)mesh.getNodeId(); // Mesh心跳带上 ID
  sendJsonMesh(d);
}
void sendHeartbeatSerial() {
  StaticJsonDocument<192> d;
  d["evt"]   = "hb";
  d["type"]  = "button";
  d["node"]  = (uint32_t)mesh.getNodeId();
  d["peers"] = (uint32_t)mesh.getNodeList().size();
  sendJsonSerial(d);
}
void sendPressedReport(uint32_t ms) {
  // 给 Mesh
  {
    StaticJsonDocument<160> d;
    d["type"]  = "button";
    d["event"] = "pressed";
    d["node"]  = (uint32_t)mesh.getNodeId(); // 告诉 Mesh 网络是谁执行了动作
    d["ms"]    = ms;
    sendJsonMesh(d);
  }
  // 给串口
  {
    StaticJsonDocument<160> d;
    d["evt"]   = "report";
    d["type"]  = "button";
    d["node"]  = (uint32_t)mesh.getNodeId();
    d["event"] = "pressed";
    d["ms"]    = ms;
    sendJsonSerial(d);
  }
}

// ====== Mesh 回调：接收服务器下发的控制 ======
void receivedCallback(uint32_t from, String &msg) {
  if (msg.length()==0 || msg[0] != '{') return;
  StaticJsonDocument<256> in; if (deserializeJson(in, msg)) return;
  
  const char* type = in["type"] | "";
  const char* dev  = in["dev"]  | "";
  
  if (strcmp(type,"ctl")==0 && strcmp(dev,"button")==0) {
    
    // --- 精准寻址逻辑 ---
    // 获取指令中的目标 node ID。如果没有这个字段，默认值为 0 (代表广播给所有节点)
    uint32_t targetNode = in["node"] | 0U; 
    
    // 如果 targetNode 不是 0，且不等于这块板子自己的 Node ID，直接丢弃指令
    if (targetNode != 0 && targetNode != mesh.getNodeId()) {
      return; 
    }
    // --------------------------

    const char* action = in["action"] | "press";
    uint32_t ms = in["ms"] | PRESS_DEFAULT_MS;
    if (ms == 0) ms = PRESS_DEFAULT_MS;
    
    if (strcmp(action,"press")==0) {
      hwStartPress(ms);
      sendPressedReport(ms);
    }
  }
}
void newConnectionCallback(uint32_t) {}
void changedConnectionCallback() {}
void nodeTimeAdjustedCallback(int32_t) {}

// ====== 串口命令（本地控制，即使不在 mesh 里也生效） ======
String serialBuf;
uint32_t lastByteMs = 0;

// 支持：
// 1) JSON：{"cmd":"press","ms":100}    // ms 可省，默认 100
// 2) 文本：press 或 press 150
void handleSerialLine(const String& line) {
  String s = line; s.trim();
  if (!s.length()) return;

  // 尝试 JSON
  StaticJsonDocument<256> in;
  DeserializationError err = deserializeJson(in, s);
  if (!err) {
    const char* cmd = in["cmd"] | "";
    if (cmd[0] == '\0') { StaticJsonDocument<96> e; e["err"]="bad_args"; sendJsonSerial(e); return; }

    if (strcmp(cmd,"press")==0) {
      uint32_t ms = in["ms"] | PRESS_DEFAULT_MS;
      if (ms == 0) ms = PRESS_DEFAULT_MS;
      hwStartPress(ms);
      sendPressedReport(ms);
      StaticJsonDocument<128> ack; ack["ack"]="press"; ack["ok"]=true; ack["ms"]=ms; sendJsonSerial(ack);
      return;
    }

    // 未知命令
    { StaticJsonDocument<96> e; e["err"]="unknown_cmd"; sendJsonSerial(e); }
    return;
  }

  // 非 JSON 简写：press / press N
  String low = s; low.toLowerCase();
  if (low.startsWith("press")) {
    uint32_t ms = PRESS_DEFAULT_MS;
    // 解析后续数字（如 "press 150"）
    int space = low.indexOf(' ');
    if (space > 0) {
      long v = low.substring(space+1).toInt();
      if (v > 0) ms = (uint32_t)v;
    }
    hwStartPress(ms);
    sendPressedReport(ms);
    StaticJsonDocument<128> ack; ack["ack"]="press"; ack["ok"]=true; ack["ms"]=ms; sendJsonSerial(ack);
    return;
  }

  // 其他输入
  { StaticJsonDocument<96> e; e["err"]="bad_input"; sendJsonSerial(e); }
}

// 更鲁棒的串口轮询：支持 \n / \r / 无换行（见到 '}' 或空闲 200ms 也会解析）
void taskSerialPollCb() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    lastByteMs = millis();

    if (c == '\n' || c == '\r') {
      String line = serialBuf; serialBuf = "";
      line.trim();
      if (line.length()) handleSerialLine(line);
      continue;
    }

    serialBuf += c;

    // 无换行：JSON 以 '}' 结束
    if (c == '}' && serialBuf.startsWith("{")) {
      String line = serialBuf; serialBuf = "";
      line.trim();
      if (line.length()) handleSerialLine(line);
      continue;
    }

    if (serialBuf.length() > 512) serialBuf = ""; // 防溢
  }

  // 输入空闲超时也尝试解析一次（适配某些终端）
  if (!serialBuf.isEmpty() && (millis() - lastByteMs) > 200) {
    String line = serialBuf; serialBuf = "";
    line.trim();
    if (line.length()) handleSerialLine(line);
  }
}

// ====== 任务回调 ======
void taskPulseSvcCb() {
  if (pressing && (int32_t)(millis()-pressEndMs) >= 0) {
    hwEndPress();
  }
}
void taskHeartbeatCb()    { sendHeartbeatMesh();   }
void taskSerialHbCb()     { sendHeartbeatSerial(); }

// ====== 任务对象 ======
Task taskPulseSvc (TASK_MILLISECOND * 10,                   TASK_FOREVER, &taskPulseSvcCb);
Task taskHeartbeat(TASK_MILLISECOND * HEARTBEAT_INTERVAL_MS, TASK_FOREVER, &taskHeartbeatCb);
Task taskSerialHb (TASK_MILLISECOND * SERIAL_HEARTBEAT_MS,   TASK_FOREVER, &taskSerialHbCb);
Task taskSerialPoll(TASK_MILLISECOND * SERIAL_POLL_INTERVAL_MS, TASK_FOREVER, &taskSerialPollCb);

// ====== setup / loop ======
void setup() {
  Serial.begin(115200);
  uint32_t t0=millis(); while(!Serial && millis()-t0<2000) delay(10);

  hwInit();

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskPulseSvc);
  userScheduler.addTask(taskHeartbeat);
  userScheduler.addTask(taskSerialHb);
  userScheduler.addTask(taskSerialPoll);
  taskPulseSvc.enable();
  taskHeartbeat.enable();
  taskSerialHb.enable();
  taskSerialPoll.enable();

  // 开机串口提示 + 立即一条串口心跳
  {
    StaticJsonDocument<160> boot;
    boot["evt"]  = "boot";
    boot["type"] = "button";
    boot["node"] = (uint32_t)mesh.getNodeId();
    sendJsonSerial(boot);
    sendHeartbeatSerial();
  }

  // 进入 mesh：announce + hb
  sendAnnounce();
  sendHeartbeatMesh();
}

void loop() { mesh.update(); }