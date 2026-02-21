// === switch_node/main.cpp ===
#include <Arduino.h>
#include "painlessMesh.h"
#include <ArduinoJson.h>

// ===== Mesh 配置 =====
#define MESH_PREFIX    "mesh"
#define MESH_PASSWORD  "ehtbn@@08"
#define MESH_PORT      6666

// ===== 可选：外接负载引脚（继电器/LED），不需要可保持 -1 =====
#define SWITCH_PIN          4     // 改成你的负载引脚，如 2；-1 表示不用
#define SWITCH_ACTIVE_HIGH   1     // 1: 高电平=开；0: 低电平=开

// ===== 固定动作：当开关状态为 1 时，GPIO3 输出高电平 =====
#define CONTROL_GPIO         3

// ===== 周期 =====
static const uint32_t REPORT_INTERVAL_MS     = 1000;   // 每 1s 上报状态（发到 mesh）
static const uint32_t HEARTBEAT_INTERVAL_MS  = 30000;  // 每 30s mesh 心跳
static const uint32_t SERIAL_HEARTBEAT_MS    = 30000;  // 每 30s 串口心跳
static const uint32_t SERIAL_POLL_INTERVALMS = 50;     // 串口轮询

painlessMesh mesh;
Scheduler userScheduler;

int  switchState = 0; // 0:off, 1:on

// ---- 任务 ----
void taskReportCb();
void taskHeartbeatCb();
void taskSerialHeartbeatCb();
void taskSerialPollCb();

Task taskReport        (TASK_MILLISECOND * REPORT_INTERVAL_MS,    TASK_FOREVER, &taskReportCb);
Task taskHeartbeat     (TASK_MILLISECOND * HEARTBEAT_INTERVAL_MS, TASK_FOREVER, &taskHeartbeatCb);
Task taskSerialHb      (TASK_MILLISECOND * SERIAL_HEARTBEAT_MS,   TASK_FOREVER, &taskSerialHeartbeatCb);
Task taskSerialPoll    (TASK_MILLISECOND * SERIAL_POLL_INTERVALMS,TASK_FOREVER, &taskSerialPollCb);

// ========== 工具 ==========

// peers 数量（用于串口心跳）
size_t getPeerCount() {
  auto nodes = mesh.getNodeList();
  return nodes.size();
}

// 应用到硬件：SWITCH_PIN 与 GPIO3
void applyHardware() {
  if (SWITCH_PIN >= 0) {
    int level = SWITCH_ACTIVE_HIGH ? (switchState ? HIGH : LOW)
                                   : (switchState ? LOW  : HIGH);
    digitalWrite(SWITCH_PIN, level);
  }
  digitalWrite(CONTROL_GPIO, switchState ? HIGH : LOW); // 题意固定 GPIO3
}

// 发送到 mesh 的 JSON
static inline void sendJsonMesh(const JsonDocument& doc) {
  String s; serializeJson(doc, s);
  mesh.sendBroadcast(s);
}

// 发送到串口的 JSON（NDJSON，每行一个 JSON）
static inline void sendJsonSerial(const JsonDocument& doc) {
  serializeJson(doc, Serial);
  Serial.println();
}

// ====== 各类上报 ======
void sendAnnounceMesh() {
  StaticJsonDocument<128> d;
  d["type"] = "announce";
  d["dev"]  = "switch";
  d["node"] = (uint32_t)mesh.getNodeId(); // 带上 ID
  sendJsonMesh(d);
}
void sendHeartbeatMesh() {
  StaticJsonDocument<128> d; // 稍微调大容量
  d["type"] = "hb";
  d["dev"]  = "switch";
  d["node"] = (uint32_t)mesh.getNodeId(); // 带上 ID
  sendJsonMesh(d);
}
void sendSwitchStateMesh() {
  StaticJsonDocument<128> d; // 稍微调大容量
  d["type"]  = "switch";
  d["node"]  = (uint32_t)mesh.getNodeId(); // 带上 ID
  d["state"] = switchState;
  sendJsonMesh(d);
}

// 串口心跳（包含节点信息、peers、当前状态）
void sendHeartbeatSerial() {
  StaticJsonDocument<256> d;
  d["evt"]   = "hb";
  d["type"]  = "switch";
  d["node"]  = (uint32_t)mesh.getNodeId();
  d["peers"] = (uint32_t)getPeerCount();
  d["state"] = switchState;
  sendJsonSerial(d);
}

// 串口上报一次当前状态（状态变化后立即调用）
void sendReportSerial() {
  StaticJsonDocument<192> d;
  d["evt"]   = "report";
  d["type"]  = "switch";
  d["node"]  = (uint32_t)mesh.getNodeId();
  d["state"] = switchState;
  sendJsonSerial(d);
}

// ========== Mesh 回调（仍支持服务器控制） ==========
void receivedCallback(uint32_t from, String &msg) {
  if (msg.length()==0 || msg[0] != '{') return;
  StaticJsonDocument<256> in;
  if (deserializeJson(in, msg)) return;

  const char* type = in["type"] | "";
  const char* dev  = in["dev"]  | "";
  
  if (strcmp(type,"ctl")==0 && strcmp(dev,"switch")==0) {
    
    // --- 精准寻址逻辑 ---
    uint32_t targetNode = in["node"] | 0U; 
    // 如果 targetNode 不是 0，且不等于这块板子自己的 Node ID，直接忽略
    if (targetNode != 0 && targetNode != mesh.getNodeId()) {
      return; 
    }
    // --------------------------

    int val = in["value"] | -1;
    if (val==0 || val==1) {
      switchState = val;
      applyHardware();
      sendSwitchStateMesh(); // mesh 同步
      sendReportSerial();    // 串口上报
    }
  }
}
void newConnectionCallback(uint32_t) {}
void changedConnectionCallback() {}
void nodeTimeAdjustedCallback(int32_t) {}

// ========== 串口命令解析（本地控制，mesh 不在也能用） ==========
String serialBuf;

// 支持以下命令（任取其一）：
// 1) JSON：{"cmd":"set","state":0/1}
// 2) JSON：{"cmd":"toggle"}
// 3) 简写文本：on / off / toggle
void handleSerialLine(const String& line) {
  String s = line; s.trim();
  if (!s.length()) return;

  // 优先尝试 JSON
  StaticJsonDocument<256> in;
  DeserializationError err = deserializeJson(in, s);
  if (!err) {
    const char* cmd = in["cmd"] | "";
    if (strcmp(cmd,"set")==0) {
      int st = in["state"] | -1;
      if (st==0 || st==1) {
        switchState = st;
        applyHardware();
        sendReportSerial();      // 立即回报
        sendSwitchStateMesh();   // 若在 mesh，也同步
        // ACK
        StaticJsonDocument<128> ack; ack["ack"]="set"; ack["state"]=switchState; sendJsonSerial(ack);
      } else {
        StaticJsonDocument<128> e; e["err"]="bad_state"; sendJsonSerial(e);
      }
      return;
    } else if (strcmp(cmd,"toggle")==0) {
      switchState = !switchState;
      applyHardware();
      sendReportSerial();
      sendSwitchStateMesh();
      StaticJsonDocument<128> ack; ack["ack"]="toggle"; ack["state"]=switchState; sendJsonSerial(ack);
      return;
    } else {
      StaticJsonDocument<128> e; e["err"]="unknown_cmd"; sendJsonSerial(e);
      return;
    }
  }

  // 非 JSON：简写命令
  String low = s; low.toLowerCase();
  if (low == "on") {
    switchState = 1; applyHardware(); sendReportSerial(); sendSwitchStateMesh();
    StaticJsonDocument<96> ack; ack["ack"]="set"; ack["state"]=1; sendJsonSerial(ack);
    return;
  }
  if (low == "off") {
    switchState = 0; applyHardware(); sendReportSerial(); sendSwitchStateMesh();
    StaticJsonDocument<96> ack; ack["ack"]="set"; ack["state"]=0; sendJsonSerial(ack);
    return;
  }
  if (low == "toggle") {
    switchState = !switchState; applyHardware(); sendReportSerial(); sendSwitchStateMesh();
    StaticJsonDocument<96> ack; ack["ack"]="toggle"; ack["state"]=switchState; sendJsonSerial(ack);
    return;
  }

  // 其他内容忽略或回错
  StaticJsonDocument<128> e; e["err"]="bad_input"; sendJsonSerial(e);
}
uint32_t lastByteMs = 0;
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

    // 1) 无换行场景：当看到 '}' 认为一条 JSON 结束（简单且够用）
    if (c == '}' && serialBuf.startsWith("{")) {
      String line = serialBuf; serialBuf = "";
      line.trim();
      if (line.length()) handleSerialLine(line);
      continue;
    }

    // 防止异常输入过长
    if (serialBuf.length() > 512) serialBuf = "";
  }

  // 2) 输入停顿超时也尝试解析一次（适合“粘贴发送但没换行”）
  if (!serialBuf.isEmpty() && (millis() - lastByteMs) > 200) {
    String line = serialBuf; serialBuf = "";
    line.trim();
    if (line.length()) handleSerialLine(line);
  }
}

// ========== 任务回调 ==========
void taskReportCb()      { sendSwitchStateMesh(); }
void taskHeartbeatCb()   { sendHeartbeatMesh();   }
void taskSerialHeartbeatCb() { sendHeartbeatSerial(); }

// ========== setup / loop ==========
void setup() {
  Serial.begin(115200);
  uint32_t t0=millis(); while(!Serial && millis()-t0<2000) delay(10);

  if (SWITCH_PIN >= 0) pinMode(SWITCH_PIN, OUTPUT);
  pinMode(CONTROL_GPIO, OUTPUT);
  applyHardware(); // 以初始 state=0 拉低 GPIO3

  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT);
  mesh.onReceive(&receivedCallback);
  mesh.onNewConnection(&newConnectionCallback);
  mesh.onChangedConnections(&changedConnectionCallback);
  mesh.onNodeTimeAdjusted(&nodeTimeAdjustedCallback);

  userScheduler.addTask(taskReport);
  userScheduler.addTask(taskHeartbeat);
  userScheduler.addTask(taskSerialHb);
  userScheduler.addTask(taskSerialPoll);
  taskReport.enable();
  taskHeartbeat.enable();
  taskSerialHb.enable();
  taskSerialPoll.enable();

  // 开机向串口打一条启动信息 + 立即一条串口心跳
  {
    StaticJsonDocument<192> boot;
    boot["evt"]  = "boot";
    boot["type"] = "switch";
    boot["node"] = (uint32_t)mesh.getNodeId();
    boot["state"]= switchState;
    sendJsonSerial(boot);
    sendHeartbeatSerial();
  }

  // 进入 mesh：announce + hb
  sendAnnounceMesh();
  sendHeartbeatMesh();
}

void loop() {
  mesh.update();
}