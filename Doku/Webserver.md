# Web Server & UI Documentation – BotConnectingWifiSystem

This document describes the web server, user interface, and all features available in the browser-based control panel.

## Table of Contents

1. [Overview](#overview)
2. [Accessing the Web UI](#accessing-the-web-ui)
3. [UI Architecture](#ui-architecture)
4. [Debug Tab](#debug-tab)
5. [Manual Control Tab](#manual-control-tab)
6. [Modes Tab](#modes-tab)
7. [Calibrate Tab](#calibrate-tab)
8. [Settings Tab](#settings-tab)
9. [WebSocket Protocol](#websocket-protocol)
10. [Development Guide](#development-guide)
11. [Troubleshooting](#troubleshooting)

---

## Overview

The hub ESP32 hosts a complete web-based control interface accessible via WiFi. The interface provides:

- **Real-time telemetry visualization** from both robots
- **Manual control** with gamepad-style D-pad and controls
- **Mode selection** for autonomous behaviors
- **Calibration tools** for sensors
- **Configuration management** for pairing and settings

### Technology Stack

| Component | Technology |
|-----------|------------|
| **Frontend Framework** | Vanilla JavaScript (ES6+) |
| **Build Tool** | Vite 5.4 |
| **Styling** | Custom CSS with CSS Grid/Flexbox |
| **Communication** | WebSocket (real-time bidirectional) |
| **Backend** | ESP32 AsyncWebServer + WebSocket |
| **Storage** | LittleFS (filesystem on ESP32 flash) |

---

## Accessing the Web UI

### Step 1: Connect to Hub WiFi

1. Power on the hub ESP32
2. On your device (computer, tablet, or smartphone), connect to the WiFi network:
   - **SSID:** `ESP-Hub`
   - **Password:** `hub12345`

### Step 2: Open Browser

Navigate to:
```
http://192.168.4.1
```

Or alternatively:
```
http://esp-hub.local
```
(mDNS name, may not work on all devices)

### Step 3: Wait for Connection

The UI will automatically connect via WebSocket. You should see:
- Connection status indicator turn green
- Satellite badges (SAT1, SAT2) appear with online status

---

## UI Architecture

### Layout

```
┌────────────────────────────────────────────────────────────────┐
│  BotConnectingWifiSystem                    [SAT1] [SAT2]  ●   │  Header
├────────────────────────────────────────────────────────────────┤
│  [Debug] [Manual] [Modes] [Calibrate] [Settings]              │  Tab Bar
├────────────────────────────────────────────────────────────────┤
│                                                                │
│                                                                │
│                    Tab Content Area                            │
│                                                                │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### Header Elements

- **Title:** "BotConnectingWifiSystem"
- **SAT1 Badge:** Shows online/offline status (green/red)
- **SAT2 Badge:** Shows online/offline status (green/red)
- **Connection Indicator:** WebSocket connection status (green dot = connected)

### Tab Navigation

Click any tab to switch views:
- **Debug:** Telemetry and logs
- **Manual:** Manual robot control
- **Modes:** Autonomous mode selection
- **Calibrate:** Sensor calibration tools
- **Settings:** Configuration and pairing

---

## Debug Tab

The Debug tab provides real-time visibility into telemetry streams from both robots.

### Features

#### 1. Telemetry Table

Displays all telemetry streams in a sortable table:

| Column | Description |
|--------|-------------|
| **Stream** | Telemetry stream name (e.g., "BallAngle", "Speed") |
| **Current** | Most recent value |
| **Min** | Minimum value observed (since last reset) |
| **Max** | Maximum value observed (since last reset) |
| **SAT** | Source satellite (SAT1 or SAT2) |

**Example streams:**
```
Stream          Current    Min      Max      SAT
──────────────────────────────────────────────────
BallAngle       45.5       -90.0    90.0     SAT1
Speed           120        0        200      SAT1
Mode            1          1        5        SAT1
LineValue       512        0        1023     SAT2
BatteryVoltage  7.4        7.2      8.4      SAT2
```

#### 2. Raw Log Monitor

Displays raw telemetry lines as received:
```
[13:45:12] SAT1: BallAngle=45.5
[13:45:12] SAT1: Speed=120
[13:45:12] SAT2: LineValue=512
[13:45:13] SAT1: Mode=1
```

**Log Features:**
- Auto-scroll to bottom
- Timestamps for each entry
- Color-coded by satellite
- Clear button to reset log

#### 3. Controls

- **Clear Stats:** Reset min/max values for all streams
- **Clear Log:** Clear the raw log monitor
- **Pause:** Pause telemetry updates (useful for inspection)

### Telemetry Update Rate

- Default: 20 Hz (configurable in Settings)
- Throttled to prevent browser overload
- Each stream updated independently

### Usage Tips

**Debugging sensor issues:**
1. Go to Debug tab
2. Watch relevant stream (e.g., "LineValue")
3. Observe min/max range
4. If values are stuck or out of range, check sensor wiring/calibration

**Monitoring performance:**
1. Watch "LoopTime" or "UpdateRate" streams (if implemented)
2. High values indicate processing bottleneck

---

## Manual Control Tab

The Manual tab provides gamepad-style controls for driving robots.

### Layout

```
┌──────────────────────────────────────────────────┐
│  Target: ● SAT1  ○ SAT2                          │
├──────────────────────────────────────────────────┤
│                                                  │
│            ▲                                     │
│         ◄  +  ►   D-Pad                          │
│            ▼                                     │
│                                                  │
│  Speed: [=========>          ] -1000 to 1000    │
│  Angle: [=========>          ] -180 to 180      │
│                                                  │
│  Switches:  [SW1]  [SW2]  [SW3]                 │
│  Buttons:   [B1]  [B2]  [B3]  [B4]              │
│  Start:     [●]  (toggle)                       │
│                                                  │
│  [STOP ALL]                                     │
└──────────────────────────────────────────────────┘
```

### Controls

#### Target Selection

Select which robot to control:
- **SAT1 (Robot 1)**
- **SAT2 (Robot 2)**

#### D-Pad

Four directional buttons for quick movement:
- **↑ (Up):** Move forward
- **↓ (Down):** Move backward
- **← (Left):** Turn left
- **→ (Right):** Turn right

**Behavior:**
- Press and hold: continuous movement
- Release: stops (sends speed=0)
- Can combine with switches/buttons

#### Speed Slider

- **Range:** -1000 to 1000
- **Negative:** Backward movement
- **Positive:** Forward movement
- **0:** Stop
- **Real-time update:** Sent immediately on change

#### Angle Slider

- **Range:** -180° to 180°
- **Negative:** Left turn
- **Positive:** Right turn
- **0:** Straight
- **Use case:** Holonomic drives, steering angle

#### Switches (SW1, SW2, SW3)

Toggle switches for auxiliary functions:
- **SW1:** User-defined (e.g., enable/disable feature)
- **SW2:** User-defined (e.g., mode modifier)
- **SW3:** User-defined (e.g., turbo mode)

**State:** ON (blue) / OFF (gray)

#### Buttons (B1, B2, B3, B4)

Momentary buttons for actions:
- **B1-B4:** User-defined (e.g., shoot, kick, grab, release)

**Behavior:** Press and hold (not latching)

#### Start Button

Toggle button:
- **OFF (gray):** Robot idle/disabled
- **ON (green):** Robot active/enabled

**Use case:** Global enable/disable for safety

#### STOP ALL

Emergency stop button:
- Sets speed = 0, angle = 0
- Clears all switches and buttons
- Sends to both SAT1 and SAT2

---

## Modes Tab

The Modes tab provides one-click selection of autonomous robot behaviors.

### Layout

```
┌────────────────────────────────────┐
│  Target: ● SAT1  ○ SAT2            │
├────────────────────────────────────┤
│                                    │
│  [  Mode 1: PID Control     ]     │
│  [  Mode 2: Ball Approach   ]     │
│  [  Mode 3: Goal Rotate     ]     │
│  [  Mode 4: Homing          ]     │
│  [  Mode 5: Defender        ]     │
│                                    │
│  Current Mode: Mode 1              │
│  ACK Status: ✓ Acknowledged        │
└────────────────────────────────────┘
```

### Mode Descriptions

| Mode | ID | Description | Use Case |
|------|----|-----------|----|
| **PID Control** | 1 | Basic PID-based motor control | Testing, manual tuning |
| **Ball Approach** | 2 | Autonomous ball tracking and approach | Offense, ball collection |
| **Goal Rotate** | 3 | Rotate to face goal | Shooting alignment |
| **Homing** | 4 | Return to home/start position | Reset, defense |
| **Defender** | 5 | Defensive positioning | Goalie, blocking |

> **Note:** Mode behaviors are implemented in the Teensy firmware, not the ESP32.

### Behavior

1. Select target robot (SAT1 or SAT2)
2. Click a mode button
3. Hub sends MODE command to satellite
4. Satellite forwards to Teensy: `M<n>\n` (e.g., `M1\n`)
5. ACK expected within 500ms
6. UI shows confirmation or timeout error

### ACK Status Indicators

- **✓ Acknowledged:** Mode command received by satellite
- **⏱ Waiting for ACK...:** Command sent, waiting for response
- **✗ Timeout:** No ACK received within 500ms (retry or check connection)

---

## Calibrate Tab

The Calibrate tab provides tools for sensor calibration.

### Layout

```
┌────────────────────────────────────┐
│  Target: ● SAT1  ○ SAT2            │
├────────────────────────────────────┤
│  IR Sensor Calibration:            │
│    [  IR Max  ]  [  IR Min  ]     │
│                                    │
│  Line Sensor Calibration:          │
│    [ Line Max ]  [ Line Min ]     │
│                                    │
│  IMU Calibration:                  │
│    [  BNO055 Calibrate  ]         │
│                                    │
│  Status: Ready                     │
└────────────────────────────────────┘
```

### Calibration Commands

#### IR Sensor

**IR Max:**
- **When to use:** Place IR sensor over maximum reflectivity surface (white)
- **Action:** Captures maximum IR value

**IR Min:**
- **When to use:** Place IR sensor over minimum reflectivity surface (black)
- **Action:** Captures minimum IR value

#### Line Sensor

**Line Max:**
- **When to use:** Place line sensor over white/bright surface
- **Action:** Captures maximum line sensor value

**Line Min:**
- **When to use:** Place line sensor over black line
- **Action:** Captures minimum line sensor value

#### IMU (BNO055)

**BNO055 Calibrate:**
- **When to use:** IMU needs recalibration (after power cycle, position change)
- **Action:** Starts IMU calibration routine
- **Procedure:** Follow on-screen instructions (rotate robot in figure-8, etc.)

### Calibration Process

1. Select target robot
2. Position robot/sensor as required
3. Click calibration button
4. Wait for ACK confirmation
5. Teensy executes calibration and saves to EEPROM/flash

**UART output to Teensy:**
```
CAL_IR_MAX\n
CAL_IR_MIN\n
CAL_LINE_MAX\n
CAL_LINE_MIN\n
CAL_BNO\n
```

### Status Messages

- **Ready:** Idle, waiting for command
- **Sent:** Command sent, waiting for ACK
- **✓ Success:** Calibration acknowledged
- **✗ Failed:** Timeout or error

---

## Settings Tab

The Settings tab provides configuration, pairing, and system management.

### Layout

```
┌────────────────────────────────────────────────────┐
│  Peer Discovery                                    │
│    [  Scan for Peers  ]                           │
│                                                    │
│  Discovered Devices:                               │
│  ┌────────────────────────────────────────────┐  │
│  │ MAC: AA:BB:CC:DD:EE:01                      │  │
│  │ Name: [Robot 1        ]  Role: [SAT1 ▼]   │  │
│  │ LTK:  [________________] (optional)        │  │
│  └────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────┐  │
│  │ MAC: AA:BB:CC:DD:EE:02                      │  │
│  │ Name: [Robot 2        ]  Role: [SAT2 ▼]   │  │
│  │ LTK:  [________________] (optional)        │  │
│  └────────────────────────────────────────────┘  │
│                                                    │
│  Network Settings                                  │
│    Channel: [6  ▼]                                │
│    PMK: [0102...1F20_____] (64 hex chars)         │
│                                                    │
│  Telemetry                                         │
│    Max Rate: [20 Hz ▼]                            │
│                                                    │
│  Heartbeat                                         │
│    Interval: [1000 ms]                            │
│    Timeout:  [4000 ms]                            │
│                                                    │
│  [  Save Config  ]  [  Factory Reset  ]          │
└────────────────────────────────────────────────────┘
```

### Peer Discovery & Pairing

#### Scan for Peers

1. Click **Scan for Peers**
2. Hub broadcasts discovery request
3. Satellites respond with MAC address
4. Discovered devices appear in list

#### Assign Roles

For each discovered device:
1. Enter a friendly **Name** (e.g., "Robot 1")
2. Select **Role** from dropdown:
   - **SAT1:** First satellite
   - **SAT2:** Second satellite
3. Optionally enter **LTK** (64 hex characters) for per-peer encryption
   - Leave empty to use global PMK only

#### Save Configuration

Click **Save Config** to:
- Store peer information in NVS
- Apply encryption keys
- Reconnect to satellites

### Network Settings

#### Channel

WiFi and ESP-NOW channel (1-13):
- **Default:** 6
- **Must match** on all devices
- Change if experiencing interference

#### PMK (Primary Master Key)

Global encryption key for all ESP-NOW communication:
- **Length:** 64 hexadecimal characters (32 bytes)
- **Format:** `0102030405...1F20`
- **Leave empty** for no encryption (not recommended)

**Generate PMK:**
Use a random hex generator or:
```bash
openssl rand -hex 32
```

### Telemetry Settings

#### Max Rate

Maximum telemetry update rate to browser:
- **Range:** 1-100 Hz
- **Default:** 20 Hz
- **Lower values** reduce CPU/network load

### Heartbeat Settings

#### Interval

How often to send heartbeat to satellites:
- **Default:** 1000 ms (1 Hz)
- **Range:** 500-5000 ms

#### Timeout

How long to wait before marking peer offline:
- **Default:** 4000 ms (4 seconds)
- **Should be:** 3-4× interval

### Factory Reset

**Caution:** Erases all configuration!

1. Click **Factory Reset**
2. Confirm action
3. Hub loads default configuration
4. All peer pairings are lost
5. Re-scan and re-pair satellites

---

## WebSocket Protocol

### Connection

**WebSocket URL:**
```
ws://192.168.4.1/ws
```

**Auto-reconnect:**
- UI attempts reconnection every 3 seconds if disconnected
- Connection indicator shows status

### Message Format

All messages are JSON strings.

#### Client → Server (UI → Hub)

**Control Command:**
```json
{
  "type": "ctrl",
  "target": 1,
  "speed": 500,
  "angle": 90,
  "switches": 3,
  "buttons": 5,
  "start": 1
}
```

**Mode Command:**
```json
{
  "type": "mode",
  "target": 1,
  "mode": 2
}
```

**Calibration Command:**
```json
{
  "type": "cal",
  "target": 1,
  "command": "IR_MAX"
}
```

**Settings Update:**
```json
{
  "type": "settings",
  "config": { /* full config JSON */ }
}
```

**Scan Request:**
```json
{
  "type": "scan"
}
```

#### Server → Client (Hub → UI)

**Telemetry Update:**
```json
{
  "type": "telemetry",
  "streams": [
    {"name": "BallAngle", "current": 45.5, "min": -90, "max": 90, "sat": 1},
    {"name": "Speed", "current": 120, "min": 0, "max": 200, "sat": 1}
  ]
}
```

**Status Update:**
```json
{
  "type": "status",
  "sat1_online": true,
  "sat2_online": false
}
```

**ACK Response:**
```json
{
  "type": "ack",
  "seq": 123,
  "target": 1
}
```

**Error:**
```json
{
  "type": "error",
  "message": "ACK timeout for MODE command"
}
```

**Discovery Result:**
```json
{
  "type": "discovery",
  "devices": [
    {"mac": "AA:BB:CC:DD:EE:01"},
    {"mac": "AA:BB:CC:DD:EE:02"}
  ]
}
```

---

## Development Guide

### Building the Web UI

#### Prerequisites

- Node.js ≥ 18
- npm (bundled with Node.js)

#### Install Dependencies

```bash
cd ESP_Hub/ui
npm install
```

#### Development Server

For rapid development with hot-reload:

```bash
npm run dev
```

This starts Vite dev server at `http://localhost:5173`.

**Note:** WebSocket will try to connect to `ws://192.168.4.1/ws` (the hub). Make sure hub is running and accessible.

#### Production Build

```bash
npm run build
```

Output goes to `ESP_Hub/data/`:
- `index.html`
- `assets/index-[hash].js`
- `assets/index-[hash].css`

#### Upload to ESP32

```bash
cd ESP_Hub
pio run -e esp_hub -t uploadfs
```

### File Structure

```
ESP_Hub/ui/
├── index.html          ← Main HTML structure
├── package.json        ← Node dependencies
├── vite.config.js      ← Vite configuration
└── src/
    ├── main.js         ← UI logic, event handlers
    ├── ws.js           ← WebSocket wrapper
    └── style.css       ← Styling
```

### Key Code Sections

#### WebSocket Connection (ws.js)

```javascript
class WSClient {
    connect(url) {
        this.ws = new WebSocket(url);
        this.ws.onopen = () => this.onOpen();
        this.ws.onmessage = (evt) => this.onMessage(evt);
        this.ws.onerror = (err) => this.onError(err);
        this.ws.onclose = () => this.onClose();
    }

    send(data) {
        if (this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(data));
            return true;
        }
        return false;
    }
}
```

#### Sending Control Command (main.js)

```javascript
function sendControl() {
    const msg = {
        type: 'ctrl',
        target: currentTarget, // 1 or 2
        speed: speedSlider.value,
        angle: angleSlider.value,
        switches: getSwitchState(),
        buttons: getButtonState(),
        start: startToggle.checked ? 1 : 0
    };
    wsSend(msg);
}
```

#### Handling Telemetry (main.js)

```javascript
function onTelemetryUpdate(data) {
    data.streams.forEach(stream => {
        updateTelemetryTable(stream);
        appendToLog(stream);
    });
}
```

### Customization

#### Adding a New Tab

1. **HTML (index.html):**
   ```html
   <div class="tab-button" data-tab="mytab">My Tab</div>
   ```

2. **Content area:**
   ```html
   <div id="mytab-content" class="tab-content">
       <!-- Your content here -->
   </div>
   ```

3. **JavaScript (main.js):**
   ```javascript
   function initMyTab() {
       // Initialize your tab
   }
   ```

4. Rebuild and upload

#### Adding a New Control

1. Add HTML element
2. Add event listener in `main.js`
3. Define WebSocket message format
4. Update hub firmware to handle new message type

---

## Troubleshooting

### Web UI Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| 404 / Blank page | LittleFS not uploaded | Run `pio run -e esp_hub -t uploadfs` |
| Old UI after update | Browser cache | Hard reload: `Ctrl+Shift+R` / `Cmd+Shift+R` |
| WebSocket won't connect | Hub not running | Check hub serial output |
| Controls unresponsive | JavaScript error | Open browser console (`F12`), check errors |

### Connection Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Can't connect to ESP-Hub WiFi | Hub not booted | Check hub power and serial output |
| Connected but can't load page | IP address wrong | Try `192.168.4.1` or `http://esp-hub.local` |
| WebSocket disconnects | WiFi interference | Change channel; move closer to hub |

### Pairing Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Scan finds no devices | Satellites not running | Check satellite power |
| Satellites offline after pairing | MAC mismatch | Re-scan and verify MAC addresses |
| ACK timeout on commands | Encryption key mismatch | Verify PMK/LTK in Settings |

### Performance Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| UI laggy | Telemetry rate too high | Lower max rate in Settings (try 10Hz) |
| Browser freezes | JavaScript infinite loop | Check browser console for errors |
| WebSocket overload | Too many messages | Increase throttling on hub side |

---

## Further Reading

- **[Setup.md](Setup.md)** – Initial setup and configuration
- **[Software.md](Software.md)** – WebSocket protocol details
- **[Teensy.md](Teensy.md)** – How Teensy processes commands

---

**Document Version:** 1.0
**Last Updated:** 2026-03-19
