var gateway = `ws://${window.location.hostname}/ws`;
var websocket;
var devicesData = {};
var meshConnected = false;

function initWebSocket() {
  websocket = new WebSocket(gateway);
  websocket.onopen = onOpen;
  websocket.onclose = onClose;
  websocket.onmessage = onMessage;
}

function onOpen(event) {
  document.getElementById('ws-dot').classList.add('connected');
  document.getElementById('ws-text').innerText = 'WebSocket 已连接';
  websocket.send(JSON.stringify({cmd: "list"}));
}

function onClose(event) {
  document.getElementById('ws-dot').classList.remove('connected');
  document.getElementById('ws-text').innerText = 'WebSocket 已断开，重连中...';
  setMeshStatus(false);
  setTimeout(initWebSocket, 2000);
}

function setMeshStatus(isConnected) {
  meshConnected = isConnected;
  const dot = document.getElementById('mesh-dot');
  if(isConnected) { 
    dot.classList.add('connected'); 
    document.getElementById('mesh-text').innerText = 'Mesh 网关通讯正常'; 
  } else { 
    dot.classList.remove('connected'); 
    document.getElementById('mesh-text').innerText = 'Mesh 网关离线'; 
  }
}

function onMessage(event) {
  try {
    var msg = JSON.parse(event.data);
    console.log("收到:", msg);

    if (msg.evt === "hb" && msg.type === "server") { setMeshStatus(true); }
    
    if (msg.evt === "devices" && msg.list) {
      devicesData = {};
      msg.list.forEach(d => devicesData[d.id] = d);
      renderAll();
    }

    if (msg.evt === "discover" || msg.evt === "online" || msg.evt === "type" || msg.evt === "new") {
      if(!devicesData[msg.id]) devicesData[msg.id] = { id: msg.id, type: msg.type || "unknown", online: true };
      devicesData[msg.id].online = true;
      if(msg.type) devicesData[msg.id].type = msg.type;
      renderAll();
    }

    if (msg.evt === "offline") {
      if(devicesData[msg.id]) { devicesData[msg.id].online = false; renderAll(); }
    }

    if (msg.evt === "report") {
      let d = devicesData[msg.id];
      if(!d) return;
      d.online = true; 
      
      if (msg.type === "switch" && msg.content && msg.content.state !== undefined) {
        d.state = msg.content.state;
        updateSwitchUI(d.id, d.state);
      }
      if (msg.type === "sensor" && msg.content && msg.content.data) {
        d.data = msg.content.data;
        updateSensorUI(d.id, d.data);
      }
      if (msg.type === "button") {
        flashCard(d.id);
      }
    }
  } catch(e) { console.error("解析错误:", e); }
}

function renderAll() {
  const grid = document.getElementById('devices-grid');
  grid.innerHTML = '';
  Object.values(devicesData).forEach(d => {
    if(d.type === "server") return; 

    let card = document.createElement('div');
    card.className = `card ${d.online ? '' : 'offline'}`;
    card.id = `card-${d.id}`;
    
    let header = `<div class="card-header"><span class="dev-type">${d.type}</span><span class="dev-id">ID: ${d.id}</span></div>`;
    let body = '';

    if (d.type === "switch") {
      let st = d.state === 1;
      body = `<button id="btn-${d.id}" class="btn ${st ? '' : 'off'}" onclick="toggleSwitch(${d.id}, ${st ? 0 : 1})" ${d.online?'':'disabled'}>${st ? '开启 (ON)' : '关闭 (OFF)'}</button>`;
    } else if (d.type === "button") {
      body = `<button class="btn" onclick="pressButton(${d.id})" ${d.online?'':'disabled'}>模拟按下触发</button>`;
    } else if (d.type === "sensor") {
      let t = (d.data && d.data.t !== undefined) ? d.data.t + '<span>°C</span>' : '--';
      let h = (d.data && d.data.h !== undefined) ? d.data.h + '<span>%</span>' : '--';
      body = `<div class="sensor-data" id="sens-${d.id}"> ${t} &nbsp;&nbsp;  ${h}</div>`;
    } else {
      body = `<div style="text-align:center; color:#999;">未知设备类型</div>`;
    }

    card.innerHTML = `<div class="event-flash" id="flash-${d.id}"></div>` + header + body;
    grid.appendChild(card);
  });
}

function updateSwitchUI(id, state) {
  let btn = document.getElementById(`btn-${id}`);
  if(btn) {
    let st = state === 1;
    btn.className = `btn ${st ? '' : 'off'}`;
    btn.innerText = st ? '开启 (ON)' : '关闭 (OFF)';
    btn.onclick = () => toggleSwitch(id, st ? 0 : 1);
    document.getElementById(`card-${id}`).classList.remove('offline');
  } else { renderAll(); }
}

function updateSensorUI(id, data) {
  let div = document.getElementById(`sens-${id}`);
  if(div) {
    let t = (data.t !== undefined) ? data.t + '<span>°C</span>' : '--';
    let h = (data.h !== undefined) ? data.h + '<span>%</span>' : '--';
    div.innerHTML = `🌡️ ${t} &nbsp;&nbsp; 💧 ${h}`;
    document.getElementById(`card-${id}`).classList.remove('offline');
  } else { renderAll(); }
}

function flashCard(id) {
  let flash = document.getElementById(`flash-${id}`);
  if(flash) {
    flash.style.opacity = '1';
    setTimeout(() => flash.style.opacity = '0', 200);
  }
}

function toggleSwitch(id, targetState) {
  websocket.send(JSON.stringify({ cmd: "switch", id: id, state: targetState }));
}

function pressButton(id) {
  websocket.send(JSON.stringify({ cmd: "button", id: id, ms: 200 }));
}

window.addEventListener('load', initWebSocket);