ESP32 Wireless Pan-Tilt Laser Turret

A wireless ESP32-based pan-tilt turret system using two SG90 servos and a low-power 3.3V laser module.
The ESP32 acts as a wireless turret server. A PC, browser console, or future Python/OpenCV/GPT application can send JSON commands over WebSocket to control:
•	Pan angle
•	Tilt angle
•	Movement speed
•	Laser arm/disarm
•	Laser pulse
•	Centering
•	Emergency stop
This project is designed so that the ESP32 firmware stays mostly fixed, while future features like computer vision, click-to-aim, red-color tracking, and GPT-based commands can be built on the PC side.________________________________________
Project Goal
The goal is to create a stable communication base between a PC and ESP32.
Current stage:
PC / Browser Console
        ↓
WebSocket JSON Commands
        ↓
ESP32
        ↓
Pan Servo + Tilt Servo + 3.3V Laser
Future stage:
Camera + OpenCV + GPT / Voice Commands
        ↓
Python PC App
        ↓
WebSocket JSON Commands
        ↓
ESP32 Turret Server
        ↓
Pan/Tilt/Laser Hardware

Features
•	ESP32 connects to saved WiFi
•	If WiFi is not saved, ESP32 creates setup hotspot
•	WebSocket server for real-time command control
•	JSON-based command protocol
•	Pan and tilt servo control
•	Speed-controlled smooth movement
•	Laser arm/disarm safety system
•	Laser pulse mode
•	Auto laser-off safety
•	HTTP status endpoint
•	mDNS hostname support: turret.local
•	Future-ready for Python/OpenCV/GPT integration
________________________________________
Hardware Used
Component	Quantity
ESP32 Dev Board	1
SG90 Servo	2
3.3V Laser Module	1
External 5V Power Supply	1
Jumper Wires	As required
Optional capacitor	470µF / 1000µF
________________________________________
Wiring
Servo Wiring
Use the same wiring as the earlier pan-tilt build.
Part	ESP32 Pin / Supply
Pan Servo Signal	GPIO 18
Tilt Servo Signal	GPIO 19
Servo VCC	External 5V
Servo GND	Common GND
ESP32 GND	Common GND
Important:
Servo GND and ESP32 GND must be connected together.
Do not power SG90 servos from ESP32 3.3V.
________________________________________
Laser Wiring
For a tiny 3.3V laser module:
Laser Pin	Connection
Laser +	GPIO 4
Laser -	GND
The code uses:
#define LASER_PIN 4
Recommended safer future wiring:
ESP32 GPIO 4 → resistor → transistor base
Laser +      → 3.3V
Laser -      → transistor collector
Emitter      → GND
Use a small transistor like:
BC547 / 2N2222 / S8050
________________________________________
Safety Notes
This project is for a low-power laser pointer / LED demo only.
Do not aim the laser at:
•	Eyes
•	People
•	Animals
•	Mirrors
•	Vehicles
•	Roads
•	Aircraft
•	Outside areas
The firmware keeps the laser OFF by default and requires an arm command before laser activation.
________________________________________
Required Arduino Libraries
Install these from Arduino Library Manager:
ESP32Servo
WebSockets by Markus Sattler
ArduinoJson by Benoit Blanchon
Board package:
esp32 by Espressif Systems
Recommended board:
ESP32 Dev Module
________________________________________
ESP32 Firmware Code
Save this as:
esp32_turret_server.ino
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

// ===================== PINS =====================
#define PAN_PIN    18
#define TILT_PIN   19
#define LASER_PIN  4

// ===================== DEVICE =====================
#define DEVICE_NAME "turret"
#define SETUP_SSID  "TURRET-SETUP"
#define SETUP_PASS  "12345678"

// ===================== OBJECTS =====================
Servo panServo;
Servo tiltServo;

WebServer server(80);
WebSocketsServer ws(81);
Preferences prefs;

// ===================== SERVO STATE =====================
int panMin = 0;
int panMax = 180;
int tiltMin = 0;
int tiltMax = 180;

int currentPan = 90;
int currentTilt = 90;
int targetPan = 90;
int targetTilt = 90;

int speedLevel = 5;  // 1 slow, 10 fast

// ===================== LASER STATE =====================
bool armed = false;
bool laserState = false;
unsigned long laserOffAt = 0;
const unsigned long MAX_LASER_MS = 3000;

// ===================== TIMING =====================
unsigned long lastCmdAt = 0;
unsigned long lastMoveAt = 0;

// ===================== WIFI SETUP PAGE =====================
const char WIFI_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Turret Setup</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
</head>
<body style="font-family:Arial;text-align:center;padding:30px;">
  <h2>ESP32 Turret WiFi Setup</h2>
  <form action="/save" method="POST">
    <input name="ssid" placeholder="WiFi SSID" style="padding:10px;width:80%;"><br><br>
    <input name="pass" placeholder="WiFi Password" style="padding:10px;width:80%;"><br><br>
    <button type="submit" style="padding:12px 25px;">Save & Restart</button>
  </form>
</body>
</html>
)rawliteral";

// ===================== LASER =====================
void laserOff() {
  laserState = false;
  laserOffAt = 0;
  digitalWrite(LASER_PIN, LOW);
}

bool laserPulse(unsigned long ms) {
  if (!armed) {
    laserOff();
    return false;
  }

  ms = constrain(ms, 50UL, MAX_LASER_MS);

  laserState = true;
  laserOffAt = millis() + ms;
  digitalWrite(LASER_PIN, HIGH);
  return true;
}

// ===================== STATUS JSON =====================
String makeStatus(int id, bool ok, const char* msg) {
  StaticJsonDocument<384> doc;

  doc["v"] = 1;
  doc["id"] = id;
  doc["ok"] = ok;
  doc["msg"] = msg;

  doc["pan"] = currentPan;
  doc["tilt"] = currentTilt;
  doc["target_pan"] = targetPan;
  doc["target_tilt"] = targetTilt;
  doc["speed"] = speedLevel;
  doc["armed"] = armed;
  doc["laser"] = laserState;

  if (WiFi.getMode() == WIFI_AP) {
    doc["ip"] = WiFi.softAPIP().toString();
  } else {
    doc["ip"] = WiFi.localIP().toString();
  }

  String out;
  serializeJson(doc, out);
  return out;
}

void sendStatus(uint8_t client, int id, bool ok, const char* msg) {
  String response = makeStatus(id, ok, msg);
  ws.sendTXT(client, response);
}

// ===================== COMMAND HANDLER =====================
void handleJson(uint8_t client, String payload) {
  lastCmdAt = millis();

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, payload);

  if (err) {
    sendStatus(client, -1, false, "bad json");
    return;
  }

  int id = doc["id"] | -1;
  String cmd = doc["cmd"] | "";

  if (cmd == "ping") {
    sendStatus(client, id, true, "pong");
    return;
  }

  if (cmd == "status") {
    sendStatus(client, id, true, "status");
    return;
  }

  if (cmd == "arm") {
    armed = doc["value"] | false;

    if (!armed) {
      laserOff();
    }

    sendStatus(client, id, true, armed ? "armed" : "disarmed");
    return;
  }

  if (cmd == "center") {
    targetPan = 90;
    targetTilt = 90;
    laserOff();

    sendStatus(client, id, true, "centered");
    return;
  }

  if (cmd == "stop") {
    targetPan = currentPan;
    targetTilt = currentTilt;
    laserOff();

    sendStatus(client, id, true, "stopped");
    return;
  }

  if (cmd == "aim") {
    if (!doc.containsKey("pan") || !doc.containsKey("tilt")) {
      sendStatus(client, id, false, "aim needs pan and tilt");
      return;
    }

    int panInput = doc["pan"] | targetPan;
    int tiltInput = doc["tilt"] | targetTilt;
    int speedInput = doc["speed"] | speedLevel;

    targetPan = constrain(panInput, panMin, panMax);
    targetTilt = constrain(tiltInput, tiltMin, tiltMax);
    speedLevel = constrain(speedInput, 1, 10);

    int laser = doc["laser"] | 0;

    if (laser == 1) {
      unsigned long pulseMs = doc["pulse_ms"] | 300;

      if (!laserPulse(pulseMs)) {
        sendStatus(client, id, false, "aim ok, laser blocked: not armed");
        return;
      }
    } else {
      laserOff();
    }

    sendStatus(client, id, true, "aim accepted");
    return;
  }

  if (cmd == "laser") {
    int value = doc["value"] | 0;

    if (value == 1) {
      unsigned long pulseMs = doc["pulse_ms"] | 300;

      if (!laserPulse(pulseMs)) {
        sendStatus(client, id, false, "laser blocked: not armed");
        return;
      }

      sendStatus(client, id, true, "laser pulse");
      return;
    }

    laserOff();
    sendStatus(client, id, true, "laser off");
    return;
  }

  if (cmd == "limits") {
    panMin = constrain((int)(doc["pan_min"] | panMin), 0, 180);
    panMax = constrain((int)(doc["pan_max"] | panMax), 0, 180);
    tiltMin = constrain((int)(doc["tilt_min"] | tiltMin), 0, 180);
    tiltMax = constrain((int)(doc["tilt_max"] | tiltMax), 0, 180);

    prefs.putInt("panMin", panMin);
    prefs.putInt("panMax", panMax);
    prefs.putInt("tiltMin", tiltMin);
    prefs.putInt("tiltMax", tiltMax);

    sendStatus(client, id, true, "limits saved");
    return;
  }

  sendStatus(client, id, false, "unknown command");
}

// ===================== WEBSOCKET =====================
void wsEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    sendStatus(client, -1, true, "connected");
  }

  else if (type == WStype_TEXT) {
    String msg;

    for (size_t i = 0; i < length; i++) {
      msg += (char)payload[i];
    }

    Serial.print("RX: ");
    Serial.println(msg);

    handleJson(client, msg);
  }

  else if (type == WStype_DISCONNECTED) {
    laserOff();
  }
}

// ===================== SERVO SMOOTHING =====================
void updateServos() {
  if (millis() - lastMoveAt < 20) {
    return;
  }

  lastMoveAt = millis();

  int stepSize = speedLevel;

  if (currentPan < targetPan) {
    currentPan += min(stepSize, targetPan - currentPan);
  }

  if (currentPan > targetPan) {
    currentPan -= min(stepSize, currentPan - targetPan);
  }

  if (currentTilt < targetTilt) {
    currentTilt += min(stepSize, targetTilt - currentTilt);
  }

  if (currentTilt > targetTilt) {
    currentTilt -= min(stepSize, currentTilt - targetTilt);
  }

  currentPan = constrain(currentPan, panMin, panMax);
  currentTilt = constrain(currentTilt, tiltMin, tiltMax);

  panServo.write(currentPan);
  tiltServo.write(currentTilt);
}

// ===================== HTTP =====================
void handleRoot() {
  if (WiFi.getMode() == WIFI_AP) {
    server.send_P(200, "text/html", WIFI_PAGE);
  } else {
    server.send(
      200,
      "text/plain",
      "ESP32 Turret Online\n"
      "WebSocket: ws://turret.local:81/\n"
      "Status: /status\n"
    );
  }
}

void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");

  ssid.trim();

  if (ssid.length() == 0) {
    server.send(400, "text/plain", "SSID empty");
    return;
  }

  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);

  server.send(200, "text/plain", "Saved. Restarting...");
  delay(800);
  ESP.restart();
}

void handleStatus() {
  server.send(200, "application/json", makeStatus(-1, true, "http status"));
}

// ===================== WIFI =====================
bool connectWiFi() {
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");

  if (ssid.length() == 0) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.print("Connecting to ");
  Serial.println(ssid);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  return WiFi.status() == WL_CONNECTED;
}

void startSetupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(SETUP_SSID, SETUP_PASS);

  Serial.println("Setup AP started");
  Serial.println("WiFi: TURRET-SETUP");
  Serial.println("Pass: 12345678");
  Serial.println("Open: http://192.168.4.1");
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  prefs.begin("turret", false);

  panMin = prefs.getInt("panMin", 0);
  panMax = prefs.getInt("panMax", 180);
  tiltMin = prefs.getInt("tiltMin", 0);
  tiltMax = prefs.getInt("tiltMax", 180);

  pinMode(LASER_PIN, OUTPUT);
  laserOff();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  panServo.setPeriodHertz(50);
  tiltServo.setPeriodHertz(50);

  panServo.attach(PAN_PIN, 500, 2400);
  tiltServo.attach(TILT_PIN, 500, 2400);

  panServo.write(90);
  tiltServo.write(90);

  Serial.println();
  Serial.println("ESP32 Turret Server");

  if (connectWiFi()) {
    Serial.print("Connected. IP: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin(DEVICE_NAME)) {
      Serial.println("mDNS: turret.local");
    }
  } else {
    startSetupAP();
  }

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();

  ws.begin();
  ws.onEvent(wsEvent);

  Serial.println("HTTP port: 80");
  Serial.println("WebSocket port: 81");
}

// ===================== LOOP =====================
void loop() {
  server.handleClient();
  ws.loop();

  updateServos();

  if (laserState && laserOffAt > 0 && millis() > laserOffAt) {
    laserOff();
  }

  if (laserState && millis() - lastCmdAt > 5000) {
    laserOff();
  }
}
________________________________________
First-Time WiFi Setup
After uploading, if no WiFi is saved, ESP32 creates a setup hotspot:
WiFi Name: TURRET-SETUP
Password: 12345678
Connect to it and open:
http://192.168.4.1
Enter your WiFi or mobile hotspot SSID and password.
After restart, ESP32 connects to that WiFi.
Serial Monitor will show:
Connected. IP: 192.168.x.x
mDNS: turret.local
HTTP port: 80
WebSocket port: 81
Example:
Connected. IP: 192.168.0.107
________________________________________
Testing ESP32 Status
Open in browser:
http://192.168.0.107/status
or:
http://turret.local/status
Expected JSON:
{
  "v": 1,
  "id": -1,
  "ok": true,
  "msg": "http status",
  "pan": 90,
  "tilt": 90,
  "target_pan": 90,
  "target_tilt": 90,
  "speed": 5,
  "armed": false,
  "laser": false,
  "ip": "192.168.0.107"
}
________________________________________
Browser Console Test Controller
Open any page in Chrome.
Press:
F12 → Console
If Chrome blocks pasting, type:
allow pasting
Then paste this code.
Replace the IP if your ESP32 IP is different.
window.ws = new WebSocket("ws://192.168.0.107:81/");

ws.onopen = () => console.log("✅ Connected to ESP32 Turret");
ws.onmessage = e => console.log("📩 ESP32:", e.data);
ws.onerror = e => console.log("❌ Error:", e);
ws.onclose = () => console.log("🔌 Disconnected");

let id = 1;

function send(obj) {
  obj.id = id++;
  ws.send(JSON.stringify(obj));
  console.log("➡️ Sent:", obj);
}

function ping() {
  send({cmd: "ping"});
}

function aim(pan, tilt, speed = 5) {
  send({
    cmd: "aim",
    pan: pan,
    tilt: tilt,
    speed: speed,
    laser: 0
  });
}

function center() {
  send({cmd: "center"});
}

function stopTurret() {
  send({cmd: "stop"});
}

function arm() {
  send({cmd: "arm", value: true});
}

function disarm() {
  send({cmd: "arm", value: false});
}

function pulse(ms = 300) {
  send({
    cmd: "laser",
    value: 1,
    pulse_ms: ms
  });
}

function aimPulse(pan, tilt, speed = 5, ms = 300) {
  send({
    cmd: "aim",
    pan: pan,
    tilt: tilt,
    speed: speed,
    laser: 1,
    pulse_ms: ms
  });
}
________________________________________
Available Console Commands
After pasting the browser console test controller, the following commands can be used.
________________________________________
1. Check Connection
ping();
Purpose:
Checks if ESP32 WebSocket server is alive.
Expected reply:
{
  "ok": true,
  "msg": "pong"
}
________________________________________
2. Center Turret
center();
Purpose:
Moves pan and tilt to 90 degrees.
Turns laser off.
________________________________________
3. Aim Turret
aim(120, 70, 5);
Format:
aim(pan, tilt, speed);
Meaning:
pan   = 120 degrees
tilt  = 70 degrees
speed = 5
Speed range:
1  = slow
5  = normal
10 = fast
Examples:
aim(90, 90, 5);
aim(130, 90, 5);
aim(50, 90, 5);
aim(90, 50, 5);
aim(90, 130, 5);
________________________________________
4. Slow Movement Test
aim(140, 140, 1);
Purpose:
Moves slowly to pan 140, tilt 140.
________________________________________
5. Fast Movement Test
aim(40, 40, 10);
Purpose:
Moves quickly to pan 40, tilt 40.
________________________________________
6. Stop Turret
stopTurret();
Purpose:
Stops current movement at current position.
Turns laser off.
________________________________________
7. Arm Laser
arm();
Purpose:
Allows laser pulse commands.
The laser will not turn on unless the system is armed.
________________________________________
8. Disarm Laser
disarm();
Purpose:
Blocks laser from turning on.
Also turns laser off immediately.
________________________________________
9. Pulse Laser
pulse(500);
Format:
pulse(milliseconds);
Example:
pulse(300);
pulse(500);
pulse(1000);
Purpose:
Turns laser on for the selected time and then automatically turns it off.
The firmware limits laser pulse time to a maximum of 3000 ms.
________________________________________
10. Aim and Pulse Together
aimPulse(120, 70, 5, 300);
Format:
aimPulse(pan, tilt, speed, pulse_ms);
Meaning:
Move to pan 120
Move to tilt 70
Use speed 5
Pulse laser for 300 ms
Laser must be armed first:
arm();
aimPulse(120, 70, 5, 300);
disarm();
________________________________________
Cool Test Patterns
Square Test
function squareTest() {
  arm();

  setTimeout(() => aim(60, 60, 5), 0);
  setTimeout(() => pulse(200), 700);

  setTimeout(() => aim(130, 60, 5), 1200);
  setTimeout(() => pulse(200), 1900);

  setTimeout(() => aim(130, 130, 5), 2400);
  setTimeout(() => pulse(200), 3100);

  setTimeout(() => aim(60, 130, 5), 3600);
  setTimeout(() => pulse(200), 4300);

  setTimeout(() => center(), 5000);
  setTimeout(() => disarm(), 5600);
}
Run:
squareTest();
________________________________________
Scan Test
function scanTest() {
  aim(30, 90, 4);
  setTimeout(() => aim(150, 90, 4), 1500);
  setTimeout(() => aim(30, 90, 4), 3000);
  setTimeout(() => center(), 4500);
}
Run:
scanTest();
________________________________________
Simple Left-Right Pulse Test
function pulseScan() {
  arm();

  setTimeout(() => aimPulse(50, 90, 5, 200), 0);
  setTimeout(() => aimPulse(90, 90, 5, 200), 1000);
  setTimeout(() => aimPulse(130, 90, 5, 200), 2000);

  setTimeout(() => center(), 3000);
  setTimeout(() => disarm(), 3500);
}
Run:
pulseScan();
________________________________________
WebSocket Command Format
The ESP32 receives JSON text over WebSocket.
WebSocket address:
ws://192.168.0.107:81/
or:
ws://turret.local:81/
Basic JSON command format:
{
  "id": 1,
  "cmd": "command_name"
}
________________________________________
Command Reference
Ping
{
  "id": 1,
  "cmd": "ping"
}
________________________________________
Aim
{
  "id": 2,
  "cmd": "aim",
  "pan": 120,
  "tilt": 70,
  "speed": 5,
  "laser": 0
}
________________________________________
Center
{
  "id": 3,
  "cmd": "center"
}
________________________________________
Stop
{
  "id": 4,
  "cmd": "stop"
}
________________________________________
Arm
{
  "id": 5,
  "cmd": "arm",
  "value": true
}
________________________________________
Disarm
{
  "id": 6,
  "cmd": "arm",
  "value": false
}
________________________________________
Laser Pulse
{
  "id": 7,
  "cmd": "laser",
  "value": 1,
  "pulse_ms": 500
}
________________________________________
Laser Off
{
  "id": 8,
  "cmd": "laser",
  "value": 0
}
________________________________________
Aim + Laser Pulse
{
  "id": 9,
  "cmd": "aim",
  "pan": 120,
  "tilt": 70,
  "speed": 5,
  "laser": 1,
  "pulse_ms": 300
}
________________________________________
Set Servo Limits
{
  "id": 10,
  "cmd": "limits",
  "pan_min": 0,
  "pan_max": 180,
  "tilt_min": 0,
  "tilt_max": 180
}
________________________________________
ESP32 Reply Format
The ESP32 replies with JSON.
Example:
{
  "v": 1,
  "id": 2,
  "ok": true,
  "msg": "aim accepted",
  "pan": 90,
  "tilt": 90,
  "target_pan": 120,
  "target_tilt": 70,
  "speed": 5,
  "armed": false,
  "laser": false,
  "ip": "192.168.0.107"
}
Important fields:
Field	Meaning
ok	Whether command was accepted
msg	Status message
pan	Current pan angle
tilt	Current tilt angle
target_pan	Target pan angle
target_tilt	Target tilt angle
speed	Current movement speed
armed	Laser arm state
laser	Laser output state
________________________________________
Future Python Integration
Later, a Python app can connect to the same WebSocket:
ws://192.168.0.107:81/
The Python app will be responsible for:
•	Camera feed
•	Click detection
•	Red color tracking
•	Object tracking
•	GPT or voice command interpretation
•	Mapping clicked screen position to pan/tilt angles
ESP32 will remain the actuator controller.
Final future system:
Camera
  ↓
Python OpenCV
  ↓
Click / Red Tracking / GPT Command
  ↓
Calculate pan and tilt
  ↓
Send JSON over WebSocket
  ↓
ESP32 moves turret
________________________________________
Current Project Status
Completed:
ESP32 WiFi connection
WebSocket server
JSON command system
Pan control
Tilt control
Speed control
Laser arm/disarm
Laser pulse
Status endpoint
Browser console testing
Next possible steps:
Python WebSocket client
OpenCV camera window
Click-to-aim
Red color tracking
GPT command parser
Voice command interface
________________________________________
Short Description
Wireless ESP32 Pan-Tilt Laser Turret built using two SG90 servos and a 3.3V laser module.
The ESP32 runs as a WebSocket-based turret server and receives JSON commands from a PC/browser.
It supports pan, tilt, speed control, laser arm/disarm, laser pulse, status feedback, and WiFi setup mode.
This creates a future-ready base for Python, OpenCV, click-to-aim, red object tracking, and GPT-based command control.
