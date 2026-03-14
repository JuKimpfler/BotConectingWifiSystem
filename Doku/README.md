# BotConnectingWifiSystem – Documentation

## Overview

A 3-node ESP-NOW mesh consisting of:

| Node | Role | Description |
|------|------|-------------|
| ESP #1 | SAT1 | Satellite – UART bridge to Teensy #1, P2P bridge to SAT2 |
| ESP #2 | SAT2 | Satellite – UART bridge to Teensy #2, P2P bridge to SAT1 |
| ESP #3 | HUB  | Hub – hosts WebSocket UI, routes commands to SAT1/SAT2 |

All devices are **Seeed Studio XIAO ESP32-C3**.

---

## Architecture

```
 [Browser]
     |  WebSocket
 [ESP #3 – HUB]  ─ ESP-NOW ch6 ─►  [ESP #1 – SAT1]  ─ UART ─►  [Teensy #1]
                  ◄─────────────    [ESP #1 – SAT1]  ◄─ UART ─   [Teensy #1]
                                         │ ▲
                                P2P fast │ │ (7 ms)
                                         ▼ │
                  ─ ESP-NOW ch6 ─►  [ESP #2 – SAT2]  ─ UART ─►  [Teensy #2]
                  ◄─────────────    [ESP #2 – SAT2]  ◄─ UART ─   [Teensy #2]
```

### Key Design Points

- **P2P Bridge**: SAT1 ↔ SAT2 always active at ~7 ms cycle, even when HUB is offline.
- **Hub Command Links**: Low-rate (~100 ms) command paths from HUB to SAT1/SAT2.
- **Telemetry**: Teensy sends `DBG1:name=value` (or `DBG2:`) lines; satellites forward them to HUB via ESP-NOW; HUB throttles to UI at max 20 Hz.
- **ACK**: Non-cyclic commands (MODE, CAL, PAIR, SETTINGS) use `FLAG_ACK_REQ` with up to 3 retries at 500 ms timeout.
- **Security**: ESP-NOW PMK (global) + per-peer LTK, both configurable via UI and stored in NVS.
- **Heartbeat**: 1 Hz bidirectional; 4 s timeout marks peer offline.

---

## Folder Structure

```
BotConnectingWifiSystem/
├── ESP_base/            ← Original example projects (reference only)
├── P2P_projekt/         ← Original P2P example (reference only)
│
├── shared/              ← Protocol definitions shared across all targets
│   ├── messages.h       ← Frame structs, message types, roles, flags
│   ├── crc16.h          ← CRC-16/MODBUS implementation (inline)
│   ├── config_schema.json  ← JSON schema for hub config
│   └── config_default.json ← Factory default config
│
├── ESP_Hub/             ← PlatformIO project for ESP #3 (HUB)
│   ├── platformio.ini
│   ├── include/         ← Hub-specific headers
│   │   ├── hub_config.h
│   │   ├── EspNowManager.h
│   │   ├── PeerRegistry.h
│   │   ├── ConfigStore.h
│   │   ├── TelemetryBuffer.h
│   │   ├── HeartbeatService.h
│   │   └── CommandRouter.h
│   ├── src/             ← Hub firmware source
│   │   ├── main.cpp
│   │   ├── EspNowManager.cpp
│   │   ├── PeerRegistry.cpp
│   │   ├── ConfigStore.cpp
│   │   ├── TelemetryBuffer.cpp
│   │   ├── HeartbeatService.cpp
│   │   └── CommandRouter.cpp
│   ├── data/            ← LittleFS image (built by Vite)
│   └── ui/              ← Vite Web UI source
│       ├── index.html
│       ├── package.json
│       ├── vite.config.js
│       └── src/
│           ├── main.js
│           ├── ws.js
│           └── style.css
│
├── ESP_Satellite/       ← PlatformIO project for ESP #1 and #2 (SAT)
│   ├── platformio.ini
│   ├── include/
│   │   ├── sat_config.h
│   │   ├── EspNowBridge.h
│   │   ├── AckManager.h
│   │   └── CommandParser.h
│   └── src/
│       ├── main.cpp
│       ├── EspNowBridge.cpp
│       ├── AckManager.cpp
│       └── CommandParser.cpp
│
├── Teensy_lib/          ← Arduino library for Teensy 4.0
│   ├── src/
│   │   ├── BotConnect.h
│   │   └── BotConnect.cpp
│   └── examples/
│       └── BasicUsage/BasicUsage.ino
│
├── test/
│   └── unit/            ← Host-side unit tests (CMake)
│       ├── CMakeLists.txt
│       ├── test_crc16.cpp
│       ├── test_messages.cpp
│       └── test_command_parser.cpp
│
└── Doku/
    └── README.md        ← This file
```

---

## Setup Guide

### Prerequisites

- [PlatformIO Core](https://platformio.org/install/cli) or PlatformIO IDE (VS Code extension)
- [Node.js ≥ 18](https://nodejs.org) (for Web UI build)
- 3× Seeed Studio XIAO ESP32-C3

### 1. Flash ESP_Hub firmware (ESP #3)

Connect the Hub ESP32-C3 via USB, then run:

```bash
cd ESP_Hub
pio run -e esp_hub -t upload
```

This compiles and flashes the firmware.  
The serial monitor can be started with `pio device monitor` (115200 baud).

### 2. Upload the Website (LittleFS)

The Web UI is a Vite application located in `ESP_Hub/ui/`.  
It must be compiled and then uploaded as a LittleFS filesystem image to host the site on the Hub.

> **The `uploadfs` command handles both steps automatically:**  
> it builds the UI first (runs `npm install` + `npm run build` into `ESP_Hub/data/`)  
> and then packs and flashes the LittleFS image.

```bash
cd ESP_Hub
pio run -e esp_hub -t uploadfs
```

If you prefer to build the UI manually beforehand:

```bash
# Step A – build the UI (only needed once, or after UI source changes)
cd ESP_Hub/ui
npm install
npm run build   # output goes to ESP_Hub/data/

# Step B – flash the LittleFS image
cd ..
pio run -e esp_hub -t uploadfs
```

> **Important:** Flash the **firmware first** (`-t upload`) and then the  
> **filesystem** (`-t uploadfs`). Flashing the firmware after the filesystem  
> will erase the LittleFS partition.

### 3. Flash ESP_Satellite

Flash **SAT1** to ESP #1:
```bash
cd ESP_Satellite
pio run -e esp_sat1 -t upload
```

Flash **SAT2** to ESP #2:
```bash
pio run -e esp_sat2 -t upload
```

### 4. Wiring (Seeed XIAO ESP32-C3)

| Signal     | XIAO Pin | GPIO | Teensy Pin |
|------------|----------|------|------------|
| UART TX    | D6       | 21   | Serial1 RX |
| UART RX    | D7       | 20   | Serial1 TX |
| GND        | GND      | –    | GND        |

### 5. Pairing / First Boot

1. Connect to the `ESP-Hub` WiFi AP (password: `hub12345`).
2. Open browser at `http://192.168.4.1`.
3. Navigate to **Settings** → click **Scan for peers**.
4. Assign names and roles to discovered devices.
5. Click **Save Config**.

---

## Message Protocol

### Frame Format

```
Offset  Size  Field
  0      1    magic     = 0xBE (start-of-frame)
  1      1    msg_type  (see table below)
  2      1    seq       (0–255, rolling)
  3      1    src_role  (0=HUB, 1=SAT1, 2=SAT2, 0xFF=broadcast)
  4      1    dst_role
  5      1    flags     (bit0=ACK_REQ, bit1=IS_RESPONSE, bit2=PRIORITY)
  6      1    len       (payload bytes, 0–180)
  7      1    reserved  = 0
  8..N   N    payload
N+1..2  CRC-16/MODBUS (init=0xFFFF, poly=0xA001) over bytes 0..N
```

Max frame size: **190 bytes** (within ESP-NOW 250 B limit).

### Message Types

| Type | Value | Description | ACK? |
|------|-------|-------------|------|
| DBG  | 0x01  | Telemetry stream from Teensy | No |
| CTRL | 0x02  | Control: speed, angle, buttons | No |
| MODE | 0x03  | Mode select (1–5) | **Yes** |
| CAL  | 0x04  | Calibration command | **Yes** |
| PAIR | 0x05  | Pairing request/response | **Yes** |
| HB   | 0x06  | Heartbeat (keepalive) | No |
| ACK  | 0x07  | Acknowledgement | No |
| ERR  | 0x08  | Error response | No |
| SET  | 0x09  | Settings update | **Yes** |
| DISC | 0x0A  | Discovery broadcast/announce | No |

### Command → Teensy UART Strings

| Command | UART string produced |
|---------|---------------------|
| Control | `V<speed>A<angle>SW<sw>BTN<btn>START<start>\n` |
| Mode    | `M<n>\n` (n = 1..5) |
| Cal IR_Max | `CAL_IR_MAX\n` |
| Cal IR_Min | `CAL_IR_MIN\n` |
| Cal Line_Max | `CAL_LINE_MAX\n` |
| Cal Line_Min | `CAL_LINE_MIN\n` |
| Cal BNO | `CAL_BNO\n` |

### Teensy → ESP Telemetry (UART)

```
DBG1:StreamName=value\n   ← from SAT1's Teensy
DBG2:StreamName=value\n   ← from SAT2's Teensy
```

`value` may be an integer, float, `0`/`1` for bool, or a short string.

---

## Web UI Tabs

| Tab | Description |
|-----|-------------|
| **Debug** | Telemetry table (name / current / min / max) + raw text monitor |
| **Manual** | D-pad, Speed/Angle inputs, SW1–3 switches, B1–B4 buttons, Start toggle, target selector |
| **Modes** | Buttons for modes 1–5 (PID / Ball Approach / Goal Rotate / Homing / Defender) |
| **Calibrate** | IR_Max, IR_Min, Line_Max, Line_Min, BNO calibration buttons |
| **Settings** | Scan/pair, channel, PMK, telemetry rate, save/load, factory reset |

---

## BotConnect Teensy Library

```cpp
#include "BotConnect.h"

void setup() {
    Serial1.begin(115200);
    BC.begin(Serial1, 1);  // SAT_ID = 1 or 2
    BC.onMode([](uint8_t id) { /* set mode */ });
    BC.onControl([](int16_t spd, int16_t ang, uint8_t sw, uint8_t btn, uint8_t start) {
        /* drive motors */
    });
    BC.onCalibrate([](const char *cmd) { /* run calibration */ });
}

void loop() {
    BC.process();  // must be called every loop
    BC.sendTelemetryFloat("BallAngle", angle);
    BC.sendTelemetryInt("Mode", currentMode);
}
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| SAT badges always offline | Wrong ESP-NOW channel or MACs not paired | Re-scan in Settings; check channel=6 everywhere |
| No telemetry data | Teensy not sending `DBG1:` / `DBG2:` lines | Check `BC.begin(Serial1, 1)` and that `process()` is called |
| ACK timeout | Satellite offline or out of range | Check power; reduce distance; check LTK configuration |
| UI 404 / blank page | LittleFS not flashed | Run `cd ESP_Hub && pio run -e esp_hub -t uploadfs` |
| `uploadfs` fails – `npm` not found | Node.js not installed | Install Node.js ≥ 18 from https://nodejs.org |
| `uploadfs` fails – `npm run build` error | UI dependencies missing or broken | Run `cd ESP_Hub/ui && npm install` then retry `uploadfs` |
| Browser shows old/cached UI | Browser cache stale | Hard-reload (`Ctrl+Shift+R` / `Cmd+Shift+R`) |
| Settings not saved | LittleFS mount failed | Run `pio run -e esp_hub -t erase`, then re-flash firmware, then `uploadfs` |
| Mode button no ACK | Teensy not connected to satellite | Check UART wiring (TX/RX swap!) |

---

## Running Unit Tests

```bash
cd test/unit
mkdir build && cd build
cmake .. -DSAT_ID=1
make
ctest --output-on-failure
```

All 3 tests (test_crc16, test_messages, test_command_parser) should pass.

---

## Configuration Schema

See `shared/config_schema.json` for the full JSON schema.  
Default config: `shared/config_default.json`.

Key fields:

| Field | Default | Description |
|-------|---------|-------------|
| `channel` | 6 | ESP-NOW / WiFi channel |
| `pmk` | "" | 32-hex-char Primary Master Key (stored in NVS) |
| `peers[].role` | – | `"SAT1"` or `"SAT2"` |
| `telemetry.max_rate_hz` | 20 | UI telemetry update rate |
| `heartbeat.interval_ms` | 1000 | Heartbeat send interval |
| `heartbeat.timeout_ms` | 4000 | Peer offline timeout |
