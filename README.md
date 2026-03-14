# BotConnectingWifiSystem – Setup Guide

A wireless control system for two competing robots, built on a 3-node ESP-NOW mesh. Two ESP32-C3 satellites bridge UART to Teensy 4.0 motor controllers; a third ESP32-C3 hub hosts a browser-based control UI.

```
 [Browser]
     │  WebSocket
 [ESP #3 – HUB]  ─ ESP-NOW ──►  [ESP #1 – SAT1]  ─ UART ──►  [Teensy #1]
                  ◄──────────    [ESP #1 – SAT1]  ◄─ UART ─   [Teensy #1]
                                       │ ▲
                              P2P ~7ms │ │
                                       ▼ │
                  ─ ESP-NOW ──►  [ESP #2 – SAT2]  ─ UART ──►  [Teensy #2]
                  ◄──────────    [ESP #2 – SAT2]  ◄─ UART ─   [Teensy #2]
```

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Repository Structure](#2-repository-structure)
3. [Build the Web UI](#3-build-the-web-ui)
4. [Flash ESP\_Hub (ESP #3)](#4-flash-esp_hub-esp-3)
5. [Flash ESP\_Satellite (ESP #1 and #2)](#5-flash-esp_satellite-esp-1-and-2)
6. [Integrate the Teensy Library](#6-integrate-the-teensy-library)
7. [Wiring](#7-wiring)
8. [First Boot & Pairing](#8-first-boot--pairing)
9. [Run Unit Tests](#9-run-unit-tests)
10. [Troubleshooting](#10-troubleshooting)

---

## 1. Prerequisites

### Hardware

| Qty | Component |
|-----|-----------|
| 3 | [Seeed Studio XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) |
| 2 | Teensy 4.0 |
| – | USB-C cables for flashing |

### Software

| Tool | Version | Notes |
|------|---------|-------|
| [PlatformIO Core](https://platformio.org/install/cli) | latest | CLI **or** the VS Code extension |
| [Node.js](https://nodejs.org) | ≥ 18 | Required only for the Web UI build |
| npm | bundled with Node.js | – |
| [CMake](https://cmake.org/download/) | ≥ 3.14 | Required only for unit tests |
| [Arduino IDE](https://www.arduino.cc/en/software) / [Teensyduino](https://www.pjrc.com/teensy/teensyduino.html) | ≥ 2.x | Required only for the Teensy library |
| A C++17-capable compiler (`g++` / `clang++`) | – | Required only for unit tests |

> **Tip:** PlatformIO handles all ESP32 toolchains automatically on first build.

---

## 2. Repository Structure

```
BotConnectingWifiSystem/
├── shared/              ← Protocol headers & config schemas (used by all targets)
├── ESP_Hub/             ← PlatformIO project – ESP #3 (HUB firmware + Vite UI)
│   └── ui/              ← Vite Web UI source
├── ESP_Satellite/       ← PlatformIO project – ESP #1 & #2 (SAT firmware)
├── Teensy_lib/          ← Arduino library for Teensy 4.0
│   └── examples/BasicUsage/
├── test/unit/           ← Host-side CMake unit tests
└── Doku/README.md       ← Detailed architecture & protocol documentation
```

---

## 3. Build the Web UI

The hub firmware serves its UI from a LittleFS partition. Build the UI assets first so they are ready to upload.

```bash
cd ESP_Hub/ui
npm install
npm run build      # outputs compiled assets to ESP_Hub/data/
```

---

## 4. Flash ESP\_Hub (ESP #3)

Connect ESP #3 via USB-C, then run from the repository root:

```bash
cd ESP_Hub
pio run -e esp_hub -t upload       # compile & flash firmware
pio run -e esp_hub -t uploadfs     # upload LittleFS image (Web UI)
```

> If PlatformIO is not on your PATH, use `python -m platformio` instead of `pio`.

---

## 5. Flash ESP\_Satellite (ESP #1 and #2)

Both satellites share the same source; the role is selected via a build flag.

**Flash SAT1 to ESP #1:**

```bash
cd ESP_Satellite
pio run -e esp_sat1 -t upload
```

**Flash SAT2 to ESP #2:**

```bash
pio run -e esp_sat2 -t upload
```

---

## 6. Integrate the Teensy Library

### Install the library

Copy (or symlink) `Teensy_lib/` into your Arduino libraries folder:

```bash
# macOS / Linux
cp -r Teensy_lib ~/Arduino/libraries/BotConnect

# Windows (PowerShell)
Copy-Item -Recurse Teensy_lib "$env:USERPROFILE\Documents\Arduino\libraries\BotConnect"
```

### Use in your sketch

```cpp
#include "BotConnect.h"

void setup() {
    Serial1.begin(115200);
    // SAT_ID must match the environment used when flashing the satellite (1 or 2)
    BC.begin(Serial1, 1);

    BC.onMode([](uint8_t id) { /* select robot mode */ });
    BC.onControl([](int16_t spd, int16_t ang, uint8_t sw, uint8_t btn, uint8_t start) {
        /* drive motors */
    });
    BC.onCalibrate([](const char *cmd) { /* run calibration */ });
}

void loop() {
    BC.process();                             // must be called every loop iteration
    BC.sendTelemetryFloat("BallAngle", 12.5f);
    BC.sendTelemetryInt("Mode", 1);
}
```

A full example is in [`Teensy_lib/examples/BasicUsage/BasicUsage.ino`](Teensy_lib/examples/BasicUsage/BasicUsage.ino).

---

## 7. Wiring

### ESP Satellite ↔ Teensy 4.0

| Signal  | XIAO ESP32-C3 Pin | GPIO | Teensy 4.0 Pin |
|---------|-------------------|------|----------------|
| UART TX | D6                | 21   | Serial1 RX (pin 0) |
| UART RX | D7                | 20   | Serial1 TX (pin 1) |
| GND     | GND               | –    | GND            |

> **Note:** TX of the ESP must connect to RX of the Teensy, and vice-versa.

---

## 8. First Boot & Pairing

1. Power on all three ESP32-C3 boards.
2. On your computer or phone, connect to the Wi-Fi network **`ESP-Hub`** (password: `hub12345`).
3. Open a browser and navigate to **`http://192.168.4.1`**.
4. Go to the **Settings** tab and click **Scan for peers**.
5. Assign names and roles (`SAT1` / `SAT2`) to the discovered devices.
6. Click **Save Config**.

The hub will store the configuration in NVS and reconnect automatically on subsequent boots.

---

## 9. Run Unit Tests

The host-side unit tests verify the CRC-16, message framing, and command-parser logic without any hardware.

```bash
cd test/unit
mkdir build && cd build
cmake .. -DSAT_ID=1
make
ctest --output-on-failure
```

Expected output: all 3 tests pass (`test_crc16`, `test_messages`, `test_command_parser`).

---

## 10. Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| SAT badges always offline | Wrong ESP-NOW channel or MACs not paired | Re-scan in **Settings**; verify `channel = 6` everywhere |
| No telemetry data | Teensy not sending `DBG1:`/`DBG2:` lines | Check `BC.begin(Serial1, 1)` and that `BC.process()` is called every loop |
| ACK timeout | Satellite offline or out of range | Check power and distance; verify LTK in Settings |
| UI shows 404 / blank page | LittleFS image not flashed | Run `pio run -e esp_hub -t uploadfs` |
| Settings not saved after reboot | LittleFS mount failed | Run `pio run -e esp_hub -t erase`, then reflash firmware **and** FS |
| Mode button reports no ACK | Teensy UART not connected | Check TX/RX wiring (they must be crossed) |

---

## Further Reading

- **[Doku/README.md](Doku/README.md)** – full architecture description, message protocol reference, configuration schema, and Web UI tab overview.
