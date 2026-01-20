#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESP32Servo.h>

// ====== Wi-Fi AP ======
const char* ssid = "ESP32-Car";
const char* pass = "12345678";

// ====== Web server & WebSocket ======
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ====== Servo setup ======
Servo steering;
const int SERVO_PIN = 13;

int angleMin = 60;
int angleMax = 120;
int angleCenter = 90;

int targetServo = angleCenter;   // цель серво для плавности
int currentServo = angleCenter;

// Ограничение частоты движения серво
unsigned long lastServoUpdate = 0;
const int servoInterval = 10; // ms между шагами серво (~66Hz)

// ====== HTML страница ======
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Control</title>
<style>
body { 
  margin:0; 
  background:#111; 
  color:#fff; 
  font-family:Arial; 
  display:flex; 
  flex-direction:column; 
  height:100vh; 
  overflow:hidden;
}
#top-panel {
  display:flex;
  justify-content:center;
  align-items:center;
  flex-direction:column;
  margin-top:10px;
}
#angle { font-size:18px; color:#aaa; }
#status { font-size:14px; color:#888; margin-top:5px; }

#main-area {
  flex:1;
  position:relative;
}

/* Руль слева */
#wheel { 
  width:180px; 
  height:180px; 
  border-radius:50%; 
  border:10px solid #555; 
  position:absolute; 
  left:10px; 
  top:50%; 
  transform:translateY(-50%);
  touch-action:none;
  background: radial-gradient(circle at center, #222 0%, #111 100%);
  box-shadow: 0 0 20px #000 inset, 0 0 15px #555;
  transition: transform 0.1s ease-out;
}
/* Центральная ось */
#wheel-center {
  position:absolute; top:50%; left:50%;
  width:16px; height:16px; background:#aaa;
  border-radius:50%; transform:translate(-50%,-50%);
  z-index:2;
}
/* Спицы */
.spoke {
  position:absolute; top:50%; left:50%;
  width:4px; height:100px; background:#888;
  transform-origin:50% 50%;
  border-radius:2px;
}
.spoke1 { transform:translate(-50%,-50%) rotate(0deg); }
.spoke2 { transform:translate(-50%,-50%) rotate(120deg); }
.spoke3 { transform:translate(-50%,-50%) rotate(240deg); }

/* Стрелки вперед/назад справа */
#side-buttons {
  display:flex;
  flex-direction:column;
  position:absolute;
  right:10px;
  top:50%;
  transform:translateY(-50%);
  gap:10px;
}
#side-buttons button {
  font-size:24px;
  width:60px;
  height:60px;
  display:flex;
  justify-content:center;
  align-items:center;
  padding:0;
  border:none; 
  border-radius:10px; 
  background:#333; 
  color:#fff; 
  cursor:pointer; 
  transition:0.2s; 
}
#side-buttons button:hover { background:#555; }

/* Кнопки центр/лево/право */
#bottom-buttons {
  display:flex;
  justify-content:center;
  gap:10px;
  margin-bottom:10px;
}
#bottom-buttons button {
  padding:12px 24px; 
  font-size:16px; 
  border:none; 
  border-radius:6px; 
  background:#333; 
  color:#fff; 
  cursor:pointer; 
  transition:0.2s; 
}
#bottom-buttons button:hover { background:#555; }
</style>
</head>
<body>

<div id="top-panel">
  <div id="angle">Steering: 0°</div>
  <div id="status">Connecting...</div>
</div>

<div id="main-area">
  <!-- Руль слева -->
  <div id="wheel">
    <div id="wheel-center"></div>
    <div class="spoke spoke1"></div>
    <div class="spoke spoke2"></div>
    <div class="spoke spoke3"></div>
  </div>

  <!-- Стрелки вперед/назад справа -->
  <div id="side-buttons">
    <button onclick="moveForward()">&#9650;</button> <!-- стрелка вверх -->
    <button onclick="moveBackward()">&#9660;</button> <!-- стрелка вниз -->
  </div>
</div>

<div id="bottom-buttons">
  <button onclick="setAngle(-45)">Left</button>
  <button onclick="setAngle(0)">Center</button>
  <button onclick="setAngle(45)">Right</button>
</div>

<script>
const wheel = document.getElementById('wheel');
const angleText = document.getElementById('angle');
const statusText = document.getElementById('status');

let ws;
let dragging = false;
let lastSend = 0;
let lastSentAngle = 0;
const throttleMs = 30;

function connectWS(){
  ws = new WebSocket('ws://' + location.hostname + ':81/');
  ws.onopen = ()=>{ statusText.textContent = "Connected"; statusText.style.color = "#0f0"; };
  ws.onclose = ()=>{ statusText.textContent = "Reconnecting..."; statusText.style.color = "#f00"; setTimeout(connectWS,1000); };
}
connectWS();

function getAngle(x,y){
  const r = wheel.getBoundingClientRect();
  const cx = r.left + r.width/2;
  const cy = r.top + r.height/2;
  return Math.atan2(y-cy,x-cx)*180/Math.PI + 90;
}

function sendAngle(a){
  const now = Date.now();
  if(now - lastSend > throttleMs){
    if(Math.abs(a - lastSentAngle) >= 1){
      if(ws.readyState===1) ws.send(a.toFixed(1));
      lastSend = now;
      lastSentAngle = a;
    }
  }
}

function setAngle(a){
  wheel.style.transform = `rotate(${a}deg)`;
  angleText.textContent = `Steering: ${Math.round(a)}°`;
  sendAngle(a);
}

let returnInterval = null;
function returnToCenter(){
  dragging = false;
  if(returnInterval) clearInterval(returnInterval);
  
  returnInterval = setInterval(()=>{
    const style = wheel.style.transform;
    const match = style.match(/rotate\(([-\d.]+)deg\)/);
    if(!match) return;
    let current = parseFloat(match[1]);
    if(Math.abs(current) < 0.5){
      setAngle(0);
      clearInterval(returnInterval);
      returnInterval = null;
    } else {
      const step = current > 0 ? -3 : 3;
      setAngle(current + step);
    }
  }, 15);
}

wheel.addEventListener('pointerdown', e=>{
  dragging = true;
  wheel.setPointerCapture(e.pointerId);
});

wheel.addEventListener('pointermove', e=>{
  if(!dragging) return;
  let a = getAngle(e.clientX,e.clientY);
  a = Math.max(-45,Math.min(45, a));
  setAngle(a);
});

wheel.addEventListener('pointerup', returnToCenter);
wheel.addEventListener('pointercancel', returnToCenter);

function moveForward(){ if(ws.readyState===1) ws.send("F"); }
function moveBackward(){ if(ws.readyState===1) ws.send("B"); }
</script>
</body>
</html>

)rawliteral";

// ====== WebSocket callback ======
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){
  if(type == WStype_TEXT){
    float wheelAngle = atof((char*)payload); // -45..+45
    targetServo = angleCenter + (wheelAngle/45.0)*(angleMax - angleCenter);
    targetServo = constrain(targetServo, angleMin, angleMax);
  }
}

// ====== Web server handler ======
void handleRoot(){
  server.send_P(200, "text/html", index_html);
}

// ====== Setup ======
void setup(){
  Serial.begin(115200);
  steering.attach(SERVO_PIN);
  steering.write(angleCenter);

  // Wi-Fi AP
  WiFi.softAP(ssid, pass);
  Serial.println("Wi-Fi AP started: ESP32-Car");

  // Web server
  server.on("/", handleRoot);
  server.begin();

  // WebSocket
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
}

// ====== Loop ======
void loop(){
  server.handleClient();
  webSocket.loop();

  // плавное движение серво к цели с ограничением частоты
  unsigned long now = millis();
  if(now - lastServoUpdate > servoInterval){
    if(currentServo != targetServo){
      int step =2;
      // если почти в центре, двигаем чуть быстрее
      if(abs(targetServo - angleCenter) < 5) step = 3;
      if(abs(currentServo - targetServo) < step) currentServo = targetServo;
      else currentServo += (targetServo > currentServo ? step : -step);
      steering.write(currentServo);
    }
    lastServoUpdate = now;
  }
}
