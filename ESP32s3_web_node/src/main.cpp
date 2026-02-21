// === web_node/src/main.cpp ===
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

Preferences preferences;

static const int UART1_TX_PIN = 17;
static const int UART1_RX_PIN = 18;

const char* AP_SSID = "MeshWeb_AP";
const char* AP_PASS = "12345678";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

String pcBuf = "";
String serverBuf = "";
bool serverConnected = false;
uint32_t lastPingTime = 0;
uint32_t lastServerRxTime = 0;
uint32_t lastPcInputTime = 0; 
bool isAPMode = true;

// ========================================================
// Core Web Page (Embedded HTML)
// ========================================================

static const char EMBEDDED_CONSOLE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Mesh Smart Hub</title>
  <style>
    body { font-family: sans-serif; background: #f0f2f5; margin: 0; padding: 20px; text-align: center; color: #333; }
    h2 { margin-top: 0; }
    #status { font-weight: bold; margin-bottom: 15px; padding: 10px; border-radius: 8px; background: #fff; box-shadow: 0 2px 4px rgba(0,0,0,0.05); }
    .broadcast-zone { background: #e3f2fd; padding: 15px; border-radius: 10px; margin-bottom: 20px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }
    .card { background: #fff; border-radius: 10px; padding: 20px; margin: 15px auto; max-width: 400px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); text-align: left; position: relative; }
    .badge { position: absolute; top: 20px; right: 20px; background: #e9ecef; padding: 4px 10px; border-radius: 12px; font-size: 12px; font-weight: bold; }
    .online { color: #28a745; font-weight: bold; }
    .offline { color: #dc3545; font-weight: bold; }
    button { padding: 10px 20px; border: none; border-radius: 6px; font-size: 15px; cursor: pointer; color: #fff; transition: 0.2s; margin-right: 10px; margin-top: 10px;}
    button:active { transform: scale(0.95); }
    .btn-on { background: #28a745; }
    .btn-off { background: #dc3545; }
    .btn-action { background: #007bff; }
    .input-box { width: 70px; padding: 6px; border-radius: 4px; border: 1px solid #ccc; text-align: center; font-size: 14px; }
    .sensor-panel { display: flex; justify-content: space-around; background: #f8f9fa; padding: 15px; border-radius: 8px; margin-top: 15px; text-align: center; border: 1px solid #eee; }
    .val-text { font-size: 24px; font-weight: bold; margin-top: 5px; }
    #sensor-log { margin-top: 20px; font-family: monospace; font-size: 12px; background: #333; color: #0f0; padding: 10px; border-radius: 8px; text-align: left; height: 120px; overflow-y: auto; }
  </style>
</head>
<body>
  <h2>Mesh Smart Hub</h2>
  <div id="status">Connecting to gateway...</div>
  
  <div class="broadcast-zone">
    <h3 style="margin-top:0; font-size:16px;">Global Broadcast Control</h3>
    <button class="btn-on" onclick="sendCmd({cmd:'switch', id:0, state:1})">All Lights ON</button>
    <button class="btn-off" onclick="sendCmd({cmd:'switch', id:0, state:0})">All Lights OFF</button>
    
    <div style="margin-top: 15px; padding-top: 10px; border-top: 1px dashed #b6d4fe;">
      <span style="font-size: 14px; color: #555;">Trigger Duration (ms): </span>
      <input type="number" id="ms-0" class="input-box" value="500">
      <button class="btn-action" onclick="triggerButton(0)">Trigger All Access Control</button>
    </div>
  </div>

  <div id="devices"></div>
  <div id="sensor-log">System Logs...<br></div>

  <script>
    let ws;
    let currentList = [];
    let sensorCache = {}; // Core: Used to cache the latest temp/humidity data for each sensor

    const statusEl = document.getElementById('status');
    const devEl = document.getElementById('devices');
    const logEl = document.getElementById('sensor-log');

    function log(msg) {
      logEl.innerHTML += msg + '<br>';
      logEl.scrollTop = logEl.scrollHeight;
    }

    function connect() {
      ws = new WebSocket(`ws://${location.hostname}/ws`);
      ws.onopen = () => {
        statusEl.innerHTML = "<span style='color:#28a745'>[Connected] WebSocket communication normal</span>";
        ws.send(JSON.stringify({cmd: 'list'})); 
      };
      ws.onclose = () => {
        statusEl.innerHTML = "<span style='color:#dc3545'>[Disconnected] WebSocket reconnecting...</span>";
        setTimeout(connect, 3000);
      };
      ws.onmessage = (e) => {
        try {
          const data = JSON.parse(e.data);
          
          if (data.evt === 'devices' && data.list) {
            currentList = data.list;
            renderDevices(currentList);
          } 
          else if (data.type === 'report' || data.type === 'ack' || data.type === 'sensor') {
            log(`[Data Report] ${JSON.stringify(data)}`);
            
            // === Magic: Intercept sensor data and update cache ===
            let isSensorUpdate = false;
            if (data.type === 'sensor' && data.data) {
              sensorCache[data.node || data.id] = data.data;
              isSensorUpdate = true;
            } else if (data.type === 'report' && data.deviceType === 'sensor' && data.content) {
              sensorCache[data.id] = data.content;
              isSensorUpdate = true;
            }

            // If it's a sensor update, refresh UI seamlessly; otherwise request full list again
            if (isSensorUpdate && currentList.length > 0) {
              renderDevices(currentList); 
            } else {
              ws.send(JSON.stringify({cmd: 'list'}));
            }
          }
        } catch(err) {
          log(`[Unknown Data] ${e.data}`);
        }
      };
    }

    function sendCmd(cmdObj) {
      if (ws && ws.readyState === 1) {
        ws.send(JSON.stringify(cmdObj));
        log(`[Send Command] ${JSON.stringify(cmdObj)}`);
      } else {
        alert("Network not connected, please try again later");
      }
    }

    // Exclusive button trigger function to read the ms time input from the webpage
    function triggerButton(id) {
      let inputEl = document.getElementById('ms-' + id);
      let msVal = inputEl ? parseInt(inputEl.value) : 500;
      if (isNaN(msVal) || msVal < 0) msVal = 500; // Error handling/fallback
      
      sendCmd({cmd: 'button', id: id, ms: msVal});
    }

    function renderDevices(list) {
      if (list.length === 0) {
        devEl.innerHTML = "<div class='card'>No devices currently online in the Mesh network</div>";
        return;
      }
      let html = "";
      list.forEach(dev => {
        html += `<div class="card">`;
        html += `<span class="badge">${dev.type.toUpperCase()}</span>`;
        html += `<h3 style="margin-top:0;">Device ID: ${dev.id}</h3>`;
        html += `<p>Network Status: ${dev.online ? '<span class="online">Online</span>' : '<span class="offline">Offline</span>'}</p>`;
        
        if (dev.type === 'switch') {
          let isOn = (dev.state === 1);
          html += `<p>Switch Status: <strong>${isOn ? 'ON' : 'OFF'}</strong></p>`;
          html += `<button class="btn-on" onclick="sendCmd({cmd:'switch', id:${dev.id}, state:1})">ON</button>`;
          html += `<button class="btn-off" onclick="sendCmd({cmd:'switch', id:${dev.id}, state:0})">OFF</button>`;
        } 
        else if (dev.type === 'button') {
          html += `<div style="margin-top:15px; background:#f8f9fa; padding:15px; border-radius:8px; border:1px solid #eee;">`;
          html += `<span style="font-size: 14px; color: #555;">Trigger Duration (ms): </span>`;
          html += `<input type="number" id="ms-${dev.id}" class="input-box" value="500">`;
          html += `<br><button class="btn-action" onclick="triggerButton(${dev.id})">Send Trigger Command</button>`;
          html += `</div>`;
        } 
        else if (dev.type === 'sensor') {
          // Read the latest data for this sensor from cache
          let sData = sensorCache[dev.id];
          if (sData) {
            let tStr = (sData.t !== null && sData.t !== undefined) ? sData.t + ' °C' : '--';
            let hStr = (sData.h !== null && sData.h !== undefined) ? sData.h + ' %' : '--';
            html += `<div class="sensor-panel">`;
            html += `  <div><div style="font-size:14px; color:#666;">Temperature (T)</div><div class="val-text" style="color:#ff5722;">${tStr}</div></div>`;
            html += `  <div><div style="font-size:14px; color:#666;">Humidity (H)</div><div class="val-text" style="color:#00bcd4;">${hStr}</div></div>`;
            html += `</div>`;
          } else {
            html += `<p style="color:#888; font-style:italic; font-size:14px;">Waiting for sensor to report real-time data...</p>`;
          }
        }
        
        html += `</div>`;
      });
      devEl.innerHTML = html;
    }

    window.onload = connect;
  </script>
</body>
</html>
)HTML";
// ================== Network Mode Logic ==================

void setupWiFiAP() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  isAPMode = true;
  Serial.println("\n[WiFi] AP Mode Active. IP: " + WiFi.softAPIP().toString());
}

void setupWiFiSTA(String ssid, String pass, bool save = true) {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  preferences.begin("wifi-config", true);
  bool useStatic = preferences.getBool("useStatic", false);
  if (useStatic) {
    IPAddress ip, gw, sn;
    ip.fromString(preferences.getString("staticIP", "192.168.0.101"));
    gw.fromString(preferences.getString("gateway", "192.168.0.1"));
    sn.fromString(preferences.getString("subnet", "255.255.255.0"));
    if (!WiFi.config(ip, gw, sn)) {
      Serial.println("[WiFi] Static IP Config Failed!");
    } else {
      Serial.println("[WiFi] Static IP Applied: " + ip.toString());
    }
  }
  preferences.end();

  WiFi.begin(ssid.c_str(), pass.c_str());
  isAPMode = false;
  
  Serial.printf("\n[WiFi] Connecting to %s ", ssid.c_str());
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500); Serial.print("."); timeout++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
    if (save) {
      preferences.begin("wifi-config", false);
      preferences.putString("ssid", ssid);
      preferences.putString("pass", pass);
      preferences.end();
    }
  } else {
    Serial.println("\n[WiFi] Connection Failed. Reverting to AP...");
    setupWiFiAP(); 
  }
}

// ================== Command Processor ==================

void processPcCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("{") && cmd.endsWith("}")) {
    Serial1.println(cmd);
    return;
  }

  String cmdLower = cmd; cmdLower.toLowerCase();

  if (cmdLower == "wifi ap") {
    setupWiFiAP();
  } 
  else if (cmdLower == "wifi ip") {
    Serial.println("\n--- IP Status ---");
    Serial.println(isAPMode ? "Mode: AP | IP: " + WiFi.softAPIP().toString() : "Mode: STA | IP: " + WiFi.localIP().toString());
  }
  else if (cmdLower.startsWith("wifi sta ")) {
    int spaceIdx = cmd.indexOf(' ', 9); 
    if (spaceIdx != -1) {
      setupWiFiSTA(cmd.substring(9, spaceIdx), cmd.substring(spaceIdx + 1), true);
    }
  }
  else if (cmdLower.startsWith("wifi setip ")) {
    char buffer[64];
    cmd.substring(11).toCharArray(buffer, 64);
    char *ip = strtok(buffer, " ");
    char *gw = strtok(NULL, " ");
    char *sn = strtok(NULL, " ");

    if (ip && gw && sn) {
      preferences.begin("wifi-config", false);
      preferences.putBool("useStatic", true); // FIXED: Changed from setBool to putBool
      preferences.putString("staticIP", String(ip));
      preferences.putString("gateway", String(gw));
      preferences.putString("subnet", String(sn));
      preferences.end();
      Serial.println("[NVS] Static IP saved. Restarting connection...");
      
      preferences.begin("wifi-config", true);
      setupWiFiSTA(preferences.getString("ssid", ""), preferences.getString("pass", ""), false);
      preferences.end();
    }
  }
  else if (cmdLower == "wifi dhcp") {
    preferences.begin("wifi-config", false);
    preferences.putBool("useStatic", false); // FIXED: Changed from setBool to putBool
    preferences.end();
    Serial.println("[NVS] Switched to DHCP. Restarting...");
    preferences.begin("wifi-config", true);
    setupWiFiSTA(preferences.getString("ssid", ""), preferences.getString("pass", ""), false);
    preferences.end();
  }
  else if (cmdLower == "wifi forget") {
    preferences.begin("wifi-config", false); preferences.clear(); preferences.end();
    Serial.println("[NVS] Config Cleared.");
  }
}

// ================== WebSocket & Setup ==================

void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT) {
      String msg((char*)data, len);
      Serial1.println(msg);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, UART1_RX_PIN, UART1_TX_PIN);
  
  pcBuf.reserve(512); serverBuf.reserve(2048);

  preferences.begin("wifi-config", true);
  String savedSSID = preferences.getString("ssid", "");
  String savedPASS = preferences.getString("pass", "");
  preferences.end();

  if (savedSSID.length() > 0) {
    setupWiFiSTA(savedSSID, savedPASS, false);
  } else {
    setupWiFiAP();
  }

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  
  // FIXED: Replaced send_P with standard send method
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){ req->send(200, "text/html", EMBEDDED_CONSOLE_HTML); });
  server.onNotFound([](AsyncWebServerRequest* req){ req->redirect("/"); });
  server.begin();
}

void loop() {
  uint32_t now = millis();
  ws.cleanupClients();

  // Serial from PC
  while (Serial.available()) {
    char c = Serial.read();
    lastPcInputTime = now;
    if (c >= 32 && c <= 126) pcBuf += c;
    if (c == '}' && pcBuf.indexOf('{') != -1) {
      Serial1.println(pcBuf.substring(pcBuf.indexOf('{')));
      pcBuf = "";
    }
    if (c == '\n' || c == '\r') { if(pcBuf.length()>0) { processPcCommand(pcBuf); pcBuf=""; } }
  }
  if (pcBuf.length() > 0 && (now - lastPcInputTime > 500)) { processPcCommand(pcBuf); pcBuf = ""; }

  // Serial from Mesh (UART1)
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n' || c == '\r') {
      if (serverBuf.length() > 0) {
        lastServerRxTime = now;
        StaticJsonDocument<512> doc;
        if (!deserializeJson(doc, serverBuf) && doc["ack"] == "ping") {
          if (!serverConnected) { serverConnected = true; Serial1.println("{\"cmd\":\"list\"}"); }
        } else { ws.textAll(serverBuf); }
        serverBuf = "";
      }
    } else if (c >= 32 && c <= 126) { serverBuf += c; }
  }

  // Heartbeat
  if (now - lastPingTime > (serverConnected ? 10000 : 2000)) {
    Serial1.println("{\"cmd\":\"ping\"}");
    lastPingTime = now;
  }
  if (serverConnected && (now - lastServerRxTime > 15000)) {
    serverConnected = false;
    Serial.println("[System] Mesh Connection Lost.");
  }
}