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
  doc["ip"] = WiFi.localIP().toString();

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

    if (!armed) laserOff();

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

    targetPan = constrain((int)doc["pan"], panMin, panMax);
    targetTilt = constrain((int)doc["tilt"], tiltMin, tiltMax);
    speedLevel = constrain((int)(doc["speed"] | speedLevel), 1, 10);

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
    for (size_t i = 0; i < length; i++) msg += (char)payload[i];

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
  if (millis() - lastMoveAt < 20) return;
  lastMoveAt = millis();

  int stepSize = speedLevel;

  if (currentPan < targetPan) currentPan += min(stepSize, targetPan - currentPan);
  if (currentPan > targetPan) currentPan -= min(stepSize, currentPan - targetPan);

  if (currentTilt < targetTilt) currentTilt += min(stepSize, targetTilt - currentTilt);
  if (currentTilt > targetTilt) currentTilt -= min(stepSize, currentTilt - targetTilt);

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
    server.send(200, "text/plain",
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

  if (ssid.length() == 0) return false;

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