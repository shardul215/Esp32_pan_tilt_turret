# ESP32 Wireless Pan-Tilt Control System

A wireless ESP32-based pan-tilt control system using two SG90 servos and a low-power 3.3V laser module.
The ESP32 works as a hardware controller that receives JSON commands over WebSocket from a PC/browser.

This project is designed as a base for future upgrades like Python control, OpenCV camera tracking, click-to-aim, red object tracking, and GPT/voice-based command control.

---

## Overview

The main goal of this project is not just to move servos, but to build a stable **PC-to-ESP32 communication layer**.

The ESP32 handles:

* WiFi connection
* WebSocket server
* JSON command parsing
* Pan servo control
* Tilt servo control
* Speed-controlled movement
* Laser arm/disarm safety
* Laser pulse control
* Real-time status feedback

The PC side can later handle:

* Camera feed
* Python logic
* OpenCV tracking
* Screen click detection
* GPT/voice command interpretation
* Target-to-servo angle mapping

---

## System Architecture

```text
PC / Browser / Future Python App
        ↓
WebSocket JSON Command
        ↓
ESP32 Turret Server
        ↓
Pan Servo + Tilt Servo + Low-Power Laser
```

Future architecture:

```text
Camera Feed
        ↓
Python + OpenCV / GPT Logic
        ↓
Pan-Tilt Target Calculation
        ↓
WebSocket JSON Command
        ↓
ESP32 Hardware Controller
```

---

## Why WebSocket?

WebSocket is used because it keeps a live two-way connection open between the PC and ESP32.

Unlike normal HTTP, where every command needs a new request-response cycle, WebSocket allows:

* Continuous real-time communication
* Low-delay command transfer
* Instant status feedback from ESP32
* Easy integration with browser, Python, and future AI logic

This makes it suitable for robotic control systems where the PC sends live movement commands and the ESP32 responds with current status.

---

## Why JSON?

Commands are sent in JSON format because JSON is:

* Easy to read
* Easy to debug
* Easy to send from browser or Python
* Future-proof for adding more fields later

Example command:

```json
{
  "cmd": "aim",
  "pan": 120,
  "tilt": 70,
  "speed": 5,
  "laser": 0
}
```

This is much cleaner than sending random single-character commands.

---

## Hardware Used

| Component                |       Quantity |
| ------------------------ | -------------: |
| ESP32 Dev Board          |              1 |
| SG90 Servo Motor         |              2 |
| 3.3V Laser Module        |              1 |
| External 5V Power Supply |              1 |
| Jumper Wires             |    As required |
| Capacitor, optional      | 470µF / 1000µF |

---

## Wiring

### Servo Connections

| Component         | ESP32 / Supply |
| ----------------- | -------------- |
| Pan Servo Signal  | GPIO 18        |
| Tilt Servo Signal | GPIO 19        |
| Servo VCC         | External 5V    |
| Servo GND         | Common GND     |
| ESP32 GND         | Common GND     |

Important:

```text
Servo power supply GND and ESP32 GND must be connected together.
```

Do not power SG90 servos from the ESP32 3.3V pin.

---

### Laser Connection

For a small 3.3V laser module:

| Laser Pin | Connection |
| --------- | ---------- |
| Laser +   | GPIO 4     |
| Laser -   | GND        |

The firmware uses:

```cpp
#define LASER_PIN 4
```

Recommended safer method for future improvement:

```text
GPIO 4 → resistor → transistor base
Laser + → 3.3V
Laser - → transistor collector
Transistor emitter → GND
```

This is better if the laser module requires more current than the ESP32 GPIO can safely provide.

---

## Safety Notice

This project is intended only for a low-power laser pointer or LED-based demo.

Do not aim the laser at:

* Eyes
* People
* Animals
* Mirrors
* Vehicles
* Roads
* Aircraft
* Outdoor public areas

The firmware includes a basic safety layer:

* Laser is OFF by default
* Laser must be armed before use
* Laser pulse automatically turns off
* Laser turns off on stop/disconnect

---

## Required Libraries

Install these libraries in Arduino IDE:

```text
ESP32Servo
WebSockets by Markus Sattler
ArduinoJson by Benoit Blanchon
```

Board package:

```text
esp32 by Espressif Systems
```

Recommended board selection:

```text
ESP32 Dev Module
```

---

## Repository Structure

```text
ESP32-Wireless-Pan-Tilt-Control/
│
├── esp32_turret_server/
│   └── esp32_turret_server.ino
│
├── README.md
│
└── media/
    ├── setup_photo.jpg
    └── demo_video.mp4
```

---

## Firmware Features

The ESP32 firmware includes:

* WiFi setup mode
* Saved WiFi credentials using Preferences
* WebSocket server on port `81`
* HTTP status endpoint on port `80`
* mDNS hostname support: `turret.local`
* JSON command parser
* Servo smoothing
* Speed control
* Laser arm/disarm
* Laser pulse
* Status response after every command

---

## First-Time WiFi Setup

After uploading the code, if no WiFi is saved, the ESP32 creates a setup hotspot.

Connect to:

```text
WiFi Name: TURRET-SETUP
Password: 12345678
```

Then open:

```text
http://192.168.4.1
```

Enter your WiFi or mobile hotspot SSID and password.

After saving, ESP32 restarts and connects to the saved WiFi.

Serial Monitor will show something like:

```text
Connected. IP: 192.168.0.107
mDNS: turret.local
HTTP port: 80
WebSocket port: 81
```

---

## Checking ESP32 Status

Open this in a browser:

```text
http://192.168.0.107/status
```

or:

```text
http://turret.local/status
```

Example response:

```json
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
```

---

## WebSocket Address

Use this WebSocket address:

```text
ws://192.168.0.107:81/
```

or, if mDNS works:

```text
ws://turret.local:81/
```

Replace `192.168.0.107` with the IP printed in your Serial Monitor.

---

## Browser Console Test Controller

Open Chrome, press:

```text
F12 → Console
```

If Chrome blocks pasting, type:

```text
allow pasting
```

Then paste this:

```js
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
```

---

## Available Console Commands

### Check Connection

```js
ping();
```

Expected reply:

```json
{
  "ok": true,
  "msg": "pong"
}
```

---

### Center Turret

```js
center();
```

This moves the pan and tilt servos to 90 degrees and turns the laser off.

---

### Aim Turret

```js
aim(120, 70, 5);
```

Format:

```js
aim(pan, tilt, speed);
```

Example:

```js
aim(90, 90, 5);
aim(130, 90, 5);
aim(50, 90, 5);
aim(90, 50, 5);
aim(90, 130, 5);
```

Speed range:

```text
1  = slow
5  = normal
10 = fast
```

---

### Stop Turret

```js
stopTurret();
```

This stops the turret at the current position and turns the laser off.

---

### Arm Laser

```js
arm();
```

The laser will not turn on unless the system is armed.

---

### Disarm Laser

```js
disarm();
```

This blocks laser activation and turns the laser off immediately.

---

### Pulse Laser

```js
pulse(500);
```

This turns the laser on for 500 ms, then automatically turns it off.

Other examples:

```js
pulse(300);
pulse(1000);
```

The firmware limits the maximum pulse duration.

---

### Aim and Pulse Together

```js
arm();
aimPulse(120, 70, 5, 300);
disarm();
```

Format:

```js
aimPulse(pan, tilt, speed, pulse_ms);
```

---

## JSON Command Reference

### Ping

```json
{
  "id": 1,
  "cmd": "ping"
}
```

---

### Aim

```json
{
  "id": 2,
  "cmd": "aim",
  "pan": 120,
  "tilt": 70,
  "speed": 5,
  "laser": 0
}
```

---

### Center

```json
{
  "id": 3,
  "cmd": "center"
}
```

---

### Stop

```json
{
  "id": 4,
  "cmd": "stop"
}
```

---

### Arm

```json
{
  "id": 5,
  "cmd": "arm",
  "value": true
}
```

---

### Disarm

```json
{
  "id": 6,
  "cmd": "arm",
  "value": false
}
```

---

### Laser Pulse

```json
{
  "id": 7,
  "cmd": "laser",
  "value": 1,
  "pulse_ms": 500
}
```

---

### Laser Off

```json
{
  "id": 8,
  "cmd": "laser",
  "value": 0
}
```

---

### Aim + Laser Pulse

```json
{
  "id": 9,
  "cmd": "aim",
  "pan": 120,
  "tilt": 70,
  "speed": 5,
  "laser": 1,
  "pulse_ms": 300
}
```

---

### Set Servo Limits

```json
{
  "id": 10,
  "cmd": "limits",
  "pan_min": 0,
  "pan_max": 180,
  "tilt_min": 0,
  "tilt_max": 180
}
```

---

## ESP32 Reply Format

The ESP32 replies with JSON after receiving commands.

Example:

```json
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
```

Field meaning:

| Field         | Meaning                      |
| ------------- | ---------------------------- |
| `ok`          | Whether command was accepted |
| `msg`         | Status message               |
| `pan`         | Current pan angle            |
| `tilt`        | Current tilt angle           |
| `target_pan`  | Target pan angle             |
| `target_tilt` | Target tilt angle            |
| `speed`       | Servo movement speed         |
| `armed`       | Laser safety state           |
| `laser`       | Current laser output state   |
| `ip`          | ESP32 IP address             |

---

## Test Patterns

### Square Test

Paste this in the browser console after the test controller:

```js
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
```

Run:

```js
squareTest();
```

---

### Scan Test

```js
function scanTest() {
  aim(30, 90, 4);
  setTimeout(() => aim(150, 90, 4), 1500);
  setTimeout(() => aim(30, 90, 4), 3000);
  setTimeout(() => center(), 4500);
}
```

Run:

```js
scanTest();
```

---

### Pulse Scan Test

```js
function pulseScan() {
  arm();

  setTimeout(() => aimPulse(50, 90, 5, 200), 0);
  setTimeout(() => aimPulse(90, 90, 5, 200), 1000);
  setTimeout(() => aimPulse(130, 90, 5, 200), 2000);

  setTimeout(() => center(), 3000);
  setTimeout(() => disarm(), 3500);
}
```

Run:

```js
pulseScan();
```

---

## Future Python Integration

Later, Python can connect to the same WebSocket server and send the same JSON commands.

Planned Python-side features:

* WebSocket client
* OpenCV camera feed
* Mouse click-to-aim
* Red color tracking
* Object tracking
* GPT/voice command parser
* Coordinate-to-servo calibration system

Future flow:

```text
Camera Frame
    ↓
Python/OpenCV Processing
    ↓
Calculate Pan/Tilt
    ↓
Send JSON Command
    ↓
ESP32 Moves Turret
```

---

## Current Project Status

Completed:

* ESP32 WiFi setup
* ESP32 WebSocket server
* JSON-based command system
* Pan servo control
* Tilt servo control
* Speed-controlled movement
* Laser arm/disarm logic
* Laser pulse system
* HTTP status endpoint
* Browser console testing

Next steps:

* Python WebSocket client
* Camera feed integration
* Click-to-aim
* Red color tracking
* GPT/voice command layer

---

## Short Project Description

Wireless ESP32 pan-tilt control system using WebSocket and JSON commands.
The ESP32 acts as a hardware controller for two SG90 servos and a low-power laser module, while the PC can send real-time commands wirelessly.

This project creates a future-ready base for Python, OpenCV, click-to-aim, red object tracking, and GPT/voice-based control.

---

## License

This project is for educational and prototyping purposes.
