# Teensy Library Documentation – BotConnect

This document describes the BotConnect Arduino library for Teensy 4.0, which enables communication between Teensy boards and ESP32 satellites in the BotConnectingWifiSystem.

## Table of Contents

1. [Overview](#overview)
2. [Installation](#installation)
3. [Quick Start](#quick-start)
4. [API Reference](#api-reference)
5. [Examples](#examples)
6. [Message Protocol](#message-protocol)
7. [Best Practices](#best-practices)
8. [Troubleshooting](#troubleshooting)

---

## Overview

The **BotConnect** library provides a simple interface for Teensy 4.0 to:
- **Receive commands** from the web UI (via ESP satellite)
- **Send telemetry** to the web UI
- **Communicate with peer robot** via P2P bridge

### Features

- ✅ **Non-blocking:** All operations are asynchronous
- ✅ **Callback-based:** Register handlers for commands
- ✅ **Multiple telemetry types:** Integer, float, boolean, string
- ✅ **UART-based:** Uses Serial1 (hardware UART)
- ✅ **Simple API:** Minimal boilerplate code

### Architecture

```
┌──────────────────────────────────────────────────────────┐
│                   BotConnect Library                      │
├──────────────────────────────────────────────────────────┤
│  User Sketch                                             │
│    ├─► BC.begin(Serial1, SAT_ID)                        │
│    ├─► BC.onMode([](uint8_t id) { ... })               │
│    ├─► BC.onControl([](speed, angle, ...) { ... })     │
│    ├─► BC.onCalibrate([](const char *cmd) { ... })     │
│    └─► BC.process()  // in loop()                       │
│                                                          │
│  BotConnect.cpp                                          │
│    ├─► UART RX: Parse commands (V..A.., M.., CAL_..)   │
│    ├─► UART TX: Send telemetry (DBG:name=value)        │
│    └─► Callback dispatch                                │
└──────────────────────────────────────────────────────────┘
                         │
                         │ UART (Serial1)
                         │
┌──────────────────────────────────────────────────────────┐
│              ESP32 Satellite                              │
│  ├─► Forwards commands from Hub                         │
│  └─► Forwards telemetry to Hub                          │
└──────────────────────────────────────────────────────────┘
```

---

## Installation

### Method 1: Arduino IDE

1. Download or clone this repository
2. Copy the `Teensy_lib/` folder to your Arduino libraries directory:

**macOS / Linux:**
```bash
cp -r Teensy_lib ~/Arduino/libraries/BotConnect
```

**Windows (PowerShell):**
```powershell
Copy-Item -Recurse Teensy_lib "$env:USERPROFILE\Documents\Arduino\libraries\BotConnect"
```

3. Restart Arduino IDE
4. Verify installation: **File → Examples → BotConnect**

### Method 2: PlatformIO

Add to `platformio.ini`:
```ini
[env:teensy40]
platform = teensy
board = teensy40
framework = arduino
lib_deps =
    file://path/to/Teensy_lib
```

### Verify Installation

Open Arduino IDE and check:
```
File → Examples → BotConnect → BasicUsage
```

If visible, installation successful.

---

## Quick Start

### Minimal Example

```cpp
#include "BotConnect.h"

void setup() {
    Serial1.begin(115200);

    // Initialize with Serial1 and SAT_ID (1 or 2)
    BC.begin(Serial1, 1);  // 1 for SAT1, 2 for SAT2

    // Register callback for control commands
    BC.onControl([](int16_t speed, int16_t angle, uint8_t sw, uint8_t btn, uint8_t start) {
        // Control your motors here
        Serial.printf("Speed: %d, Angle: %d\n", speed, angle);
    });
}

void loop() {
    BC.process();  // MUST be called every loop iteration

    // Send telemetry
    BC.sendTelemetryInt("Speed", 100);
    BC.sendTelemetryFloat("Angle", 45.5);

    delay(50);  // ~20Hz update rate
}
```

---

## API Reference

### Initialization

#### BC.begin()

Initialize the BotConnect library.

```cpp
void begin(Stream &serial, uint8_t sat_id);
```

**Parameters:**
- `serial`: Serial port (typically `Serial1` on Teensy)
- `sat_id`: Satellite ID (`1` for SAT1, `2` for SAT2)

**Example:**
```cpp
BC.begin(Serial1, 1);
```

**Notes:**
- Must be called in `setup()`
- Call `Serial1.begin(115200)` before `BC.begin()`
- SAT_ID must match the satellite firmware

---

### Command Callbacks

Register handlers for incoming commands from the web UI.

#### BC.onControl()

Handle manual control commands (D-pad, sliders, switches, buttons).

```cpp
void onControl(ControlCallback callback);
```

**Callback signature:**
```cpp
void callback(int16_t speed, int16_t angle, uint8_t switches, uint8_t buttons, uint8_t start);
```

**Parameters:**
- `speed`: Speed value (-1000 to 1000)
- `angle`: Angle value (-180 to 180)
- `switches`: Bitfield (bit0=SW1, bit1=SW2, bit2=SW3)
- `buttons`: Bitfield (bit0=B1, bit1=B2, bit2=B3, bit3=B4)
- `start`: Start button state (0=off, 1=on)

**Example:**
```cpp
BC.onControl([](int16_t speed, int16_t angle, uint8_t sw, uint8_t btn, uint8_t start) {
    if (start) {
        // Robot enabled
        setMotorSpeed(speed);
        setMotorAngle(angle);
    } else {
        // Robot disabled
        stopMotors();
    }

    // Check switches
    if (sw & 0x01) {
        // SW1 is on
        enableTurbo();
    }

    // Check buttons
    if (btn & 0x01) {
        // B1 is pressed
        shoot();
    }
});
```

#### BC.onMode()

Handle mode selection commands.

```cpp
void onMode(ModeCallback callback);
```

**Callback signature:**
```cpp
void callback(uint8_t mode_id);
```

**Parameters:**
- `mode_id`: Mode number (1-5)

**Example:**
```cpp
BC.onMode([](uint8_t mode_id) {
    switch (mode_id) {
        case 1: enterPIDMode(); break;
        case 2: enterBallApproachMode(); break;
        case 3: enterGoalRotateMode(); break;
        case 4: enterHomingMode(); break;
        case 5: enterDefenderMode(); break;
    }
    Serial.printf("Mode changed to: %d\n", mode_id);
});
```

#### BC.onCalibrate()

Handle calibration commands.

```cpp
void onCalibrate(CalibrateCallback callback);
```

**Callback signature:**
```cpp
void callback(const char *command);
```

**Parameters:**
- `command`: Calibration command string (e.g., "IR_MAX", "BNO")

**Example:**
```cpp
BC.onCalibrate([](const char *cmd) {
    if (strcmp(cmd, "IR_MAX") == 0) {
        calibrateIRMax();
    } else if (strcmp(cmd, "IR_MIN") == 0) {
        calibrateIRMin();
    } else if (strcmp(cmd, "LINE_MAX") == 0) {
        calibrateLineMax();
    } else if (strcmp(cmd, "LINE_MIN") == 0) {
        calibrateLineMin();
    } else if (strcmp(cmd, "BNO") == 0) {
        calibrateBNO055();
    }
    Serial.printf("Calibration: %s\n", cmd);
});
```

---

### Telemetry Functions

Send telemetry data to the web UI.

#### BC.sendTelemetryInt()

Send integer telemetry.

```cpp
void sendTelemetryInt(const char *name, int32_t value);
```

**Parameters:**
- `name`: Stream name (max 31 characters)
- `value`: Integer value

**Example:**
```cpp
BC.sendTelemetryInt("Speed", 120);
BC.sendTelemetryInt("Mode", 1);
BC.sendTelemetryInt("LineValue", 512);
```

#### BC.sendTelemetryFloat()

Send floating-point telemetry.

```cpp
void sendTelemetryFloat(const char *name, float value);
```

**Parameters:**
- `name`: Stream name (max 31 characters)
- `value`: Float value

**Example:**
```cpp
BC.sendTelemetryFloat("BallAngle", 45.5);
BC.sendTelemetryFloat("BatteryVoltage", 7.4);
BC.sendTelemetryFloat("Temperature", 25.3);
```

#### BC.sendTelemetryBool()

Send boolean telemetry.

```cpp
void sendTelemetryBool(const char *name, bool value);
```

**Parameters:**
- `name`: Stream name (max 31 characters)
- `value`: Boolean value (true/false)

**Example:**
```cpp
BC.sendTelemetryBool("BallDetected", true);
BC.sendTelemetryBool("IsCalibrated", false);
```

#### BC.sendTelemetryString()

Send string telemetry.

```cpp
void sendTelemetryString(const char *name, const char *value);
```

**Parameters:**
- `name`: Stream name (max 31 characters)
- `value`: String value (max 63 characters)

**Example:**
```cpp
BC.sendTelemetryString("Status", "Ready");
BC.sendTelemetryString("Error", "Sensor timeout");
```

---

### Processing

#### BC.process()

Process incoming UART data and dispatch callbacks.

```cpp
void process();
```

**Must be called in `loop()` every iteration.**

**Example:**
```cpp
void loop() {
    BC.process();  // Process commands

    // Your robot logic here
}
```

**Notes:**
- Non-blocking (returns immediately)
- Reads available UART data
- Parses complete lines (terminated by `\n`)
- Calls registered callbacks

---

## Examples

### Example 1: Basic Usage

Full example from `Teensy_lib/examples/BasicUsage/BasicUsage.ino`:

```cpp
#include "BotConnect.h"

// Global variables for control state
int16_t g_speed = 0;
int16_t g_angle = 0;
uint8_t g_mode = 1;

void setup() {
    // USB Serial for debugging
    Serial.begin(115200);
    Serial.println("BotConnect BasicUsage Example");

    // UART to ESP satellite
    Serial1.begin(115200);

    // Initialize BotConnect (SAT_ID = 1 for SAT1, 2 for SAT2)
    BC.begin(Serial1, 1);

    // Register callback for mode changes
    BC.onMode([](uint8_t id) {
        g_mode = id;
        Serial.printf("Mode changed to: %d\n", id);
    });

    // Register callback for control commands
    BC.onControl([](int16_t spd, int16_t ang, uint8_t sw, uint8_t btn, uint8_t start) {
        g_speed = spd;
        g_angle = ang;
        Serial.printf("Control: speed=%d angle=%d sw=%d btn=%d start=%d\n",
                      spd, ang, sw, btn, start);

        // Control motors based on received values
        // setMotorSpeed(spd);
        // setMotorAngle(ang);
    });

    // Register callback for calibration commands
    BC.onCalibrate([](const char *cmd) {
        Serial.printf("Calibrate: %s\n", cmd);
        // Perform calibration
        // if (strcmp(cmd, "IR_MAX") == 0) { ... }
    });
}

void loop() {
    // MUST call process() every loop
    BC.process();

    // Send telemetry to web UI
    BC.sendTelemetryInt("Mode", g_mode);
    BC.sendTelemetryInt("Speed", g_speed);
    BC.sendTelemetryInt("Angle", g_angle);
    BC.sendTelemetryFloat("BallAngle", 45.5);
    BC.sendTelemetryBool("BallDetected", true);

    delay(50);  // ~20Hz telemetry rate
}
```

### Example 2: Motor Control

```cpp
#include "BotConnect.h"

// Motor pins
#define MOTOR_L_PWM 5
#define MOTOR_L_DIR 6
#define MOTOR_R_PWM 9
#define MOTOR_R_DIR 10

void setMotors(int16_t speed, int16_t angle) {
    // Convert speed/angle to left/right motor speeds
    int16_t leftSpeed = speed + angle;
    int16_t rightSpeed = speed - angle;

    // Constrain
    leftSpeed = constrain(leftSpeed, -1000, 1000);
    rightSpeed = constrain(rightSpeed, -1000, 1000);

    // Set motor directions
    digitalWrite(MOTOR_L_DIR, leftSpeed >= 0 ? HIGH : LOW);
    digitalWrite(MOTOR_R_DIR, rightSpeed >= 0 ? HIGH : LOW);

    // Set motor PWM (map -1000..1000 to 0..255)
    analogWrite(MOTOR_L_PWM, map(abs(leftSpeed), 0, 1000, 0, 255));
    analogWrite(MOTOR_R_PWM, map(abs(rightSpeed), 0, 1000, 0, 255));
}

void setup() {
    Serial1.begin(115200);
    BC.begin(Serial1, 1);

    pinMode(MOTOR_L_PWM, OUTPUT);
    pinMode(MOTOR_L_DIR, OUTPUT);
    pinMode(MOTOR_R_PWM, OUTPUT);
    pinMode(MOTOR_R_DIR, OUTPUT);

    BC.onControl([](int16_t spd, int16_t ang, uint8_t sw, uint8_t btn, uint8_t start) {
        if (start) {
            setMotors(spd, ang);
        } else {
            setMotors(0, 0);  // Stop
        }
    });
}

void loop() {
    BC.process();

    // Send motor feedback
    BC.sendTelemetryInt("LeftMotor", getLeftMotorSpeed());
    BC.sendTelemetryInt("RightMotor", getRightMotorSpeed());

    delay(50);
}
```

### Example 3: Sensor Telemetry

```cpp
#include "BotConnect.h"

// Sensor pins
#define IR_SENSOR A0
#define LINE_SENSOR A1

void setup() {
    Serial1.begin(115200);
    BC.begin(Serial1, 1);

    pinMode(IR_SENSOR, INPUT);
    pinMode(LINE_SENSOR, INPUT);
}

void loop() {
    BC.process();

    // Read sensors
    int irValue = analogRead(IR_SENSOR);
    int lineValue = analogRead(LINE_SENSOR);

    // Send telemetry
    BC.sendTelemetryInt("IR_Raw", irValue);
    BC.sendTelemetryInt("Line_Raw", lineValue);

    // Calculate derived values
    float irDistance = mapIRToDistance(irValue);
    bool lineDetected = lineValue > 512;

    BC.sendTelemetryFloat("IR_Distance", irDistance);
    BC.sendTelemetryBool("LineDetected", lineDetected);

    delay(50);
}
```

---

## Message Protocol

### UART Messages

#### Received Commands (ESP → Teensy)

**Control Command:**
```
V<speed>A<angle>SW<switches>BTN<buttons>START<start>\n
```

Example: `V500A90SW3BTN5START1\n`

**Mode Command:**
```
M<mode_id>\n
```

Example: `M2\n`

**Calibration Command:**
```
CAL_<command>\n
```

Examples:
- `CAL_IR_MAX\n`
- `CAL_LINE_MIN\n`
- `CAL_BNO\n`

#### Sent Telemetry (Teensy → ESP)

**Format:**
```
DBG:<name>=<value>\n
```

**Examples:**
```
DBG:Speed=120\n
DBG:BallAngle=45.5\n
DBG:BallDetected=1\n
DBG:Status=Ready\n
```

**Rules:**
- Must start with `DBG:` prefix
- Name and value separated by `=`
- Terminated by `\n`
- Max line length: 511 characters

---

## Best Practices

### 1. Always Call BC.process()

```cpp
void loop() {
    BC.process();  // REQUIRED
    // ... rest of your code
}
```

Failing to call `BC.process()` means commands won't be processed.

### 2. Non-Blocking Code

Avoid `delay()` in callbacks:

**Bad:**
```cpp
BC.onMode([](uint8_t id) {
    delay(1000);  // Blocks BC.process()
    enterMode(id);
});
```

**Good:**
```cpp
BC.onMode([](uint8_t id) {
    scheduleModeChange(id);  // Non-blocking
});
```

### 3. Limit Telemetry Rate

Don't send telemetry too fast (wastes bandwidth):

```cpp
void loop() {
    BC.process();

    static uint32_t lastTelemetry = 0;
    if (millis() - lastTelemetry > 50) {  // 20Hz max
        BC.sendTelemetryInt("Speed", speed);
        lastTelemetry = millis();
    }
}
```

### 4. Use Descriptive Names

Use clear, consistent telemetry names:

**Good:**
```cpp
BC.sendTelemetryFloat("BallAngle", angle);
BC.sendTelemetryInt("LineValue", value);
```

**Bad:**
```cpp
BC.sendTelemetryFloat("a", angle);  // Unclear
BC.sendTelemetryInt("val", value);  // Ambiguous
```

### 5. Handle Start Button

Always check the start button for safety:

```cpp
BC.onControl([](int16_t spd, int16_t ang, uint8_t sw, uint8_t btn, uint8_t start) {
    if (start) {
        // Safe to move
        setMotors(spd, ang);
    } else {
        // Emergency stop
        setMotors(0, 0);
    }
});
```

---

## Troubleshooting

### No Commands Received

| Symptom | Cause | Solution |
|---------|-------|----------|
| Callbacks never called | `BC.process()` not called | Add `BC.process()` in `loop()` |
| No UART data | Wrong baud rate | Use `Serial1.begin(115200)` |
| Wrong satellite | Wrong SAT_ID | Verify SAT_ID matches firmware |

### No Telemetry in Web UI

| Symptom | Cause | Solution |
|---------|-------|----------|
| Telemetry not appearing | Missing `DBG:` prefix | Use `BC.sendTelemetry*()` functions (not raw Serial1.print) |
| Telemetry delayed | Too much data | Reduce telemetry rate |
| Garbled telemetry | Line too long | Keep lines <512 characters |

### Wiring Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| No UART communication | TX/RX not crossed | Verify ESP TX → Teensy RX, ESP RX → Teensy TX |
| Intermittent errors | Loose connection | Check wire connections |
| Constant errors | Wrong pins | Teensy Serial1 is pins 0 (RX) and 1 (TX) |

### Code Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| Compilation error | Library not installed | Verify library in `~/Arduino/libraries/BotConnect` |
| Callbacks not working | Lambda syntax error | Check callback signature matches |
| Buffer overflow | Telemetry name/value too long | Limit name to 31 chars, value to 63 chars |

---

## Advanced Usage

### P2P Communication

Send custom messages to peer robot (via P2P bridge):

```cpp
void sendToPeer(const char *msg) {
    // Don't use DBG: prefix (that goes to hub)
    Serial1.println(msg);  // Directly to UART
}

void receiveFromPeer() {
    if (Serial1.available()) {
        String msg = Serial1.readStringUntil('\n');

        // Check if it's a telemetry line (ignore)
        if (msg.startsWith("DBG:")) {
            return;
        }

        // Process P2P message
        if (msg == "BALL:MINE") {
            // Peer has the ball
            adjustStrategy();
        }
    }
}
```

> **Note:** See [Bridge.md](Bridge.md) for P2P bridge details.

---

## Further Reading

- **[Setup.md](Setup.md)** – How to flash Teensy firmware
- **[Hardware.md](Hardware.md)** – Wiring ESP ↔ Teensy
- **[Bridge.md](Bridge.md)** – P2P communication details
- **[Webserver.md](Webserver.md)** – Web UI controls

---

**Document Version:** 1.0
**Last Updated:** 2026-03-19
