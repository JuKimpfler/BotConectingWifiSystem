# Setup Guide – BotConnectingWifiSystem

This guide walks you through the complete setup process for the BotConnectingWifiSystem, from prerequisites to first boot.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Build the Web UI](#build-the-web-ui)
3. [Flash ESP_Hub (ESP #3)](#flash-esp_hub-esp-3)
4. [Flash ESP_Satellite (ESP #1 and #2)](#flash-esp_satellite-esp-1-and-2)
5. [Install Teensy Library](#install-teensy-library)
6. [First Boot & Pairing](#first-boot--pairing)
7. [Verify Installation](#verify-installation)

---

## Prerequisites

### Hardware Requirements

| Qty | Component | Notes |
|-----|-----------|-------|
| 3 | [Seeed Studio XIAO ESP32-C3](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/) | Compact ESP32-C3 development boards |
| 2 | Teensy 4.0 | Motor controller boards |
| – | USB-C cables | For flashing ESP32-C3 boards |
| – | Micro-USB cables | For flashing Teensy boards |
| – | Jumper wires | For UART connections |

> **Note:** See [Hardware.md](Hardware.md) for detailed wiring diagrams and specifications.

### Software Requirements

| Tool | Version | Purpose | Installation |
|------|---------|---------|--------------|
| [PlatformIO Core](https://platformio.org/install/cli) | latest | ESP32 firmware compilation & flashing | CLI or VS Code extension |
| [Node.js](https://nodejs.org) | ≥ 18 | Web UI build system | Download from nodejs.org |
| npm | bundled | Node package manager | Comes with Node.js |
| [CMake](https://cmake.org/download/) | ≥ 3.14 | Unit test compilation | Optional – for running tests |
| [Arduino IDE](https://www.arduino.cc/en/software) | ≥ 2.x | Teensy development | With Teensyduino add-on |
| [Teensyduino](https://www.pjrc.com/teensy/teensyduino.html) | latest | Teensy support for Arduino | Add-on for Arduino IDE |

> **Tip:** PlatformIO handles all ESP32 toolchains automatically on first build.

### Verify Installation

Before proceeding, verify that the required tools are installed:

```bash
# Check PlatformIO
pio --version

# Check Node.js and npm
node --version
npm --version

# Check CMake (optional)
cmake --version
```

---

## Build the Web UI

The hub firmware serves a web interface from its LittleFS partition. Build the UI assets first so they are ready for upload.

### Step 1: Install Dependencies

```bash
cd ESP_Hub/ui
npm install
```

This installs all required Node.js packages including Vite and development dependencies.

### Step 2: Build UI Assets

```bash
npm run build
```

**Expected output:**
```
vite v5.4.21 building for production...
✓ built in [time]
✓ [number] modules transformed.
dist/index.html                  [size] kB
dist/assets/index-[hash].js      [size] kB
dist/assets/index-[hash].css     [size] kB
```

The compiled assets are written to `ESP_Hub/data/` and will be uploaded to the ESP32 in the next step.

### Troubleshooting

| Issue | Solution |
|-------|----------|
| `npm: command not found` | Install Node.js from https://nodejs.org |
| `npm install` fails | Try `npm install --legacy-peer-deps` |
| Build errors | Delete `node_modules/` and `package-lock.json`, then run `npm install` again |

---

## Flash ESP_Hub (ESP #3)

The hub is the central coordinator that hosts the Web UI and routes commands to both satellites.

### Step 1: Connect Hardware

1. Connect ESP32-C3 #3 to your computer via USB-C cable
2. The board should appear as a serial port (e.g., `/dev/ttyUSB0` on Linux, `COM3` on Windows)

### Step 2: Flash Firmware

From the repository root:

```bash
cd ESP_Hub
pio run -e esp_hub -t upload
```

**What happens:**
- PlatformIO downloads the ESP32-C3 toolchain (first time only)
- Compiles the hub firmware
- Flashes the firmware to the connected ESP32

**Expected output:**
```
...
Configuring upload protocol...
Uploading .pio/build/esp_hub/firmware.bin
...
Wrote [size] bytes at [address]
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
```

### Step 3: Upload Web UI (LittleFS)

```bash
pio run -e esp_hub -t uploadfs
```

**What happens:**
- Packs the `ESP_Hub/data/` directory into a LittleFS image
- Uploads the filesystem to the ESP32

> **Important:** Always flash firmware **before** filesystem. Flashing firmware after filesystem will erase the LittleFS partition.

### Step 4: Verify Hub Boot

Open the serial monitor to verify the hub is running:

```bash
pio device monitor -b 115200
```

**Expected output:**
```
[HUB] Booting...
[HUB] LittleFS mounted successfully
[HUB] Loading config from NVS...
[HUB] Starting WiFi AP: ESP-Hub
[HUB] Web server started on 192.168.4.1
[HUB] ESP-NOW initialized on channel 6
```

Press `Ctrl+C` to exit the monitor.

---

## Flash ESP_Satellite (ESP #1 and #2)

Both satellites share the same source code; the role (SAT1 or SAT2) is selected via a PlatformIO build environment.

### Flash SAT1 (ESP #1)

1. Connect ESP32-C3 #1 via USB-C
2. From the repository root:

```bash
cd ESP_Satellite
pio run -e esp_sat1 -t upload
```

**Expected output:**
```
Building in RELEASE mode
...
[SAT] Role: SAT1 (ID=1)
...
Uploading .pio/build/esp_sat1/firmware.bin
...
Hard resetting via RTS pin...
```

### Flash SAT2 (ESP #2)

1. Disconnect ESP #1
2. Connect ESP32-C3 #2 via USB-C
3. From `ESP_Satellite` directory:

```bash
pio run -e esp_sat2 -t upload
```

**Expected output:**
```
Building in RELEASE mode
...
[SAT] Role: SAT2 (ID=2)
...
Uploading .pio/build/esp_sat2/firmware.bin
...
Hard resetting via RTS pin...
```

### Verify Satellite Boot

You can verify each satellite's boot by connecting to its serial monitor:

```bash
pio device monitor -b 115200
```

**Expected output:**
```
[SAT1] Booting...
[SAT1] MAC: XX:XX:XX:XX:XX:XX
[SAT1] ESP-NOW initialized on channel 6
[SAT1] Waiting for hub pairing...
```

> **Note:** Satellites will not connect until paired with the hub (see [First Boot & Pairing](#first-boot--pairing)).

---

## Install Teensy Library

The BotConnect library enables communication between Teensy boards and ESP satellites.

### Step 1: Copy Library to Arduino Folder

**On macOS / Linux:**
```bash
cp -r Teensy_lib ~/Arduino/libraries/BotConnect
```

**On Windows (PowerShell):**
```powershell
Copy-Item -Recurse Teensy_lib "$env:USERPROFILE\Documents\Arduino\libraries\BotConnect"
```

### Step 2: Verify Installation

1. Open Arduino IDE
2. Go to **File → Examples**
3. Look for **BotConnect → BasicUsage**
4. If visible, the library is installed correctly

### Step 3: Flash Example to Teensy

1. Open `File → Examples → BotConnect → BasicUsage`
2. Modify the `SAT_ID` in the sketch to match your satellite (1 or 2):
   ```cpp
   BC.begin(Serial1, 1);  // Use 1 for SAT1, 2 for SAT2
   ```
3. Select **Tools → Board → Teensy 4.0**
4. Select **Tools → Port → [your Teensy port]**
5. Click **Upload** (or press `Ctrl+U`)

Repeat for the second Teensy with `SAT_ID = 2`.

> **Detailed usage instructions:** See [Teensy.md](Teensy.md)

---

## First Boot & Pairing

With all firmware flashed, it's time to pair the satellites with the hub.

### Step 1: Power On All Devices

1. Connect all three ESP32-C3 boards to power (via USB or external power)
2. Optionally connect Teensy boards to power (via USB or external power)

### Step 2: Connect to Hub WiFi

The hub creates a WiFi access point for configuration:

- **SSID:** `ESP-Hub`
- **Password:** `hub12345`

Connect your computer or smartphone to this network.

### Step 3: Access Web Interface

Open a web browser and navigate to:

```
http://192.168.4.1
```

You should see the BotConnectingWifiSystem control interface.

> **Troubleshooting:** If you see a 404 or blank page, the LittleFS image was not uploaded correctly. Re-run `pio run -e esp_hub -t uploadfs`.

### Step 4: Scan for Satellites

1. Click on the **Settings** tab (gear icon)
2. Click the **Scan for peers** button
3. Wait 5-10 seconds for the scan to complete

You should see two discovered devices with their MAC addresses.

### Step 5: Assign Roles

For each discovered device:

1. Enter a friendly name (e.g., "Robot 1", "Robot 2")
2. Select the role from the dropdown:
   - **SAT1** for ESP #1
   - **SAT2** for ESP #2
3. Optionally set a per-peer LTK (leave empty for default)

### Step 6: Save Configuration

1. Click **Save Config** at the bottom of the Settings page
2. Wait for confirmation message
3. The hub will automatically reconnect to the satellites

### Step 7: Verify Connection

Return to the **Debug** tab. You should see:

- **SAT1** badge: green (online)
- **SAT2** badge: green (online)

If badges are red (offline), see [Troubleshooting](#troubleshooting).

---

## Verify Installation

### Check Telemetry Flow

1. Go to the **Debug** tab
2. If Teensy boards are connected and running BotConnect example code, you should see telemetry data updating in real-time
3. Example streams: `Mode`, `BallAngle`, `Speed`, etc.

### Test Control Commands

1. Go to the **Manual** tab
2. Select a target robot (SAT1 or SAT2)
3. Move the D-pad or adjust speed/angle sliders
4. If the Teensy is running the example, it should log received commands to its serial monitor

### Test Mode Selection

1. Go to the **Modes** tab
2. Select a target robot
3. Click a mode button (e.g., "PID Control")
4. Verify the mode change is logged on the Teensy serial monitor

---

## Troubleshooting

### Hub Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Cannot connect to `ESP-Hub` WiFi | Hub not booted or crashed | Check hub serial output; reflash if needed |
| Web UI shows 404 / blank page | LittleFS not uploaded | Run `pio run -e esp_hub -t uploadfs` |
| Settings not saved after reboot | LittleFS mount failed | Run `pio run -e esp_hub -t erase`, then reflash firmware and filesystem |

### Satellite Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| SAT badges always offline | Wrong channel or MACs not paired | Re-scan in Settings; verify `channel = 6` |
| No ACK from satellite | Satellite out of range or offline | Check power and distance; verify LTK if set |
| Satellite reboots continuously | Power supply issue | Use quality USB cable/power supply |

### Teensy Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| No telemetry data | Teensy not sending `DBG:` lines | Check `BC.begin(Serial1, 1)` and `BC.process()` is called in loop |
| Commands not received | UART not connected | Check TX/RX wiring (must be crossed!) |
| Wrong robot responds | Incorrect `SAT_ID` | Verify `BC.begin()` uses correct ID (1 or 2) |

### General

| Symptom | Cause | Solution |
|---------|-------|----------|
| Build errors | Outdated PlatformIO | Run `pio upgrade` and `pio pkg update` |
| Web UI build fails | Missing Node.js dependencies | Delete `node_modules/` and re-run `npm install` |
| Unit tests fail | Wrong SAT_ID in test | Run `cmake .. -DSAT_ID=1` explicitly |

---

## Next Steps

- **[Hardware.md](Hardware.md)** – Detailed wiring diagrams and pin assignments
- **[Webserver.md](Webserver.md)** – Web UI features and usage guide
- **[Software.md](Software.md)** – System architecture and message protocol
- **[Bridge.md](Bridge.md)** – P2P bridge implementation details
- **[Teensy.md](Teensy.md)** – BotConnect library API reference

---

## Quick Reference

### Build & Flash Commands

```bash
# Build Web UI
cd ESP_Hub/ui && npm install && npm run build

# Flash Hub
cd ESP_Hub
pio run -e esp_hub -t upload     # firmware
pio run -e esp_hub -t uploadfs   # filesystem

# Flash Satellites
cd ESP_Satellite
pio run -e esp_sat1 -t upload    # SAT1
pio run -e esp_sat2 -t upload    # SAT2

# Run Unit Tests
cd test/unit && mkdir build && cd build
cmake .. -DSAT_ID=1 && make && ctest
```

### Default Settings

- **WiFi SSID:** `ESP-Hub`
- **WiFi Password:** `hub12345`
- **Hub IP:** `192.168.4.1`
- **ESP-NOW Channel:** `6`
- **UART Baud Rate:** `115200`

---

**Document Version:** 1.0
**Last Updated:** 2026-03-19
