# P2P Bridge Documentation – BotConnectingWifiSystem

This document describes the peer-to-peer (P2P) ESP-NOW bridge between SAT1 and SAT2, enabling direct low-latency communication between the two robots.

## Table of Contents

1. [Overview](#overview)
2. [Bridge Architecture](#bridge-architecture)
3. [Message Routing](#message-routing)
4. [P2P Communication Protocol](#p2p-communication-protocol)
5. [Transparent UART Bridge](#transparent-uart-bridge)
6. [Performance Characteristics](#performance-characteristics)
7. [Implementation Details](#implementation-details)
8. [Troubleshooting](#troubleshooting)

---

## Overview

The P2P bridge is a key feature of the BotConnectingWifiSystem that enables **direct communication between the two satellites (SAT1 ↔ SAT2)** independent of the hub.

### Key Benefits

1. **Low Latency:** ~7ms cycle time for P2P messages
2. **Hub Independence:** Continues working even if hub is offline
3. **Transparent UART:** Non-telemetry UART data forwarded to peer robot
4. **Real-Time Coordination:** Enables inter-robot coordination strategies

### Use Cases

- **Robot-to-robot signaling:** Share sensor data directly between robots
- **Cooperative strategies:** Coordinate movements, roles, or tasks
- **Debugging:** Send debug messages between Teensy boards
- **Custom protocols:** Implement proprietary inter-robot communication

---

## Bridge Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                    P2P Bridge Architecture                        │
└──────────────────────────────────────────────────────────────────┘

                          ESP-NOW Wireless
                          Channel 6
                          ~7ms Latency
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
        │                       │                       │
┌───────▼────────┐              │              ┌────────▼───────┐
│  ESP32-C3 #1   │              │              │  ESP32-C3 #2   │
│  (SAT1)        │◄─────────────┴─────────────►│  (SAT2)        │
│                │  Bidirectional P2P Link     │                │
└───────┬────────┘                             └────────┬───────┘
        │                                               │
        │ UART 115200                                   │ UART 115200
        │                                               │
┌───────▼────────┐                             ┌────────▼───────┐
│  Teensy #1     │                             │  Teensy #2     │
│  Robot 1       │                             │  Robot 2       │
└────────────────┘                             └────────────────┘

Data Flow Examples:
─────────────────────
1. Teensy #1 sends "HELLO\n" (no DBG: prefix)
   → SAT1 forwards via P2P → SAT2 outputs to Teensy #2 UART

2. Teensy #1 sends "DBG:Speed=100\n"
   → SAT1 sends to Hub (not to peer)

3. Hub sends MODE command to SAT2
   → SAT2 outputs "M1\n" to Teensy #2 (not to peer)
```

### Communication Paths

The system has **three independent communication paths:**

| Path | Endpoints | Purpose | Latency | Protocol |
|------|-----------|---------|---------|----------|
| **Hub ↔ SAT** | Hub ↔ SAT1/SAT2 | Control commands, telemetry | ~50-100ms | ESP-NOW, ACK for critical |
| **SAT ↔ SAT** | SAT1 ↔ SAT2 | P2P bridge, inter-robot | ~7ms | ESP-NOW, no ACK |
| **SAT ↔ Teensy** | SAT ↔ Teensy | Local commands, telemetry | <1ms | UART 115200 |

---

## Message Routing

### Routing Decision Tree

When a satellite receives data, it routes it based on source and content:

```
┌─────────────────────────────────────────────────────────────┐
│              Satellite Message Routing Logic                 │
└─────────────────────────────────────────────────────────────┘

Input: UART from Teensy
│
├─► Starts with "DBG:" ?
│   ├─► YES → Parse as telemetry → Send to HUB (MSG_DBG)
│   └─► NO  → Transparent data → Send to PEER (MSG_UART_RAW)
│
Input: ESP-NOW from Hub
│
├─► MSG_CTRL, MSG_MODE, MSG_CAL ?
│   └─► YES → Parse command → Output to Teensy UART
│
├─► MSG_HB (heartbeat) ?
│   └─► YES → Update hub last-seen timestamp
│
├─► MSG_ACK, MSG_ERR ?
│   └─► YES → Process response
│
Input: ESP-NOW from Peer Satellite
│
├─► MSG_UART_RAW ?
│   └─► YES → Output raw data to Teensy UART (no prefix)
│
├─► MSG_P2P_HB (P2P heartbeat) ?
│   └─► YES → Update peer last-seen timestamp
```

### Routing Table

| Source | Data Type | Destination | Protocol | Frame Type |
|--------|-----------|-------------|----------|------------|
| Teensy UART | `DBG:...` | Hub | ESP-NOW | MSG_DBG |
| Teensy UART | Other | Peer Satellite | ESP-NOW | MSG_UART_RAW |
| Hub ESP-NOW | MSG_CTRL | Teensy UART | UART | Formatted string |
| Hub ESP-NOW | MSG_MODE | Teensy UART | UART | `M<n>\n` |
| Hub ESP-NOW | MSG_CAL | Teensy UART | UART | `CAL_...\n` |
| Hub ESP-NOW | MSG_HB | (internal) | – | Update timestamp |
| Peer ESP-NOW | MSG_UART_RAW | Teensy UART | UART | Raw string |
| Peer ESP-NOW | MSG_P2P_HB | (internal) | – | Update timestamp |

---

## P2P Communication Protocol

### Message Types for P2P

While the P2P bridge primarily uses `MSG_UART_RAW` for transparent forwarding, it can also support custom message types:

| Message Type | Value | Description | ACK? |
|--------------|-------|-------------|------|
| **MSG_UART_RAW** | 0x0B | Raw UART data forwarding | No |
| **MSG_P2P_HB** | 0x0C | P2P-specific heartbeat (fast) | No |
| **MSG_P2P_SYNC** | 0x0D | Synchronization/coordination | Optional |

> **Note:** `MSG_UART_RAW` is not officially in the main protocol table but is used internally for P2P transparent forwarding.

### P2P Heartbeat

The P2P link has its own fast heartbeat separate from hub heartbeat:

**Characteristics:**
- **Interval:** ~7ms (142 Hz)
- **Timeout:** 100ms (peer considered offline after 100ms of no P2P messages)
- **Payload:** 4-byte uptime in milliseconds

This fast heartbeat ensures very low latency detection of peer offline status.

### P2P Frame Example

**Transparent UART forwarding:**

```
Teensy #1 sends via UART:
"ROBOT2:PASS_BALL\n"

SAT1 receives, creates frame:
┌────────┬──────────┬─────┬──────────┬──────────┬───────┬──────┬──────┬──────────────────────┬────────┐
│  0xBE  │   0x0B   │ 123 │    1     │    2     │   0   │  18  │  0   │ "ROBOT2:PASS_BALL\n" │ CRC-16 │
└────────┴──────────┴─────┴──────────┴──────────┴───────┴──────┴──────┴──────────────────────┴────────┘
 Magic   MSG_UART   Seq   Src=SAT1   Dst=SAT2   Flags   Len    Rsvd   Payload (18 bytes)     CRC

SAT2 receives, extracts payload:
"ROBOT2:PASS_BALL\n"

SAT2 outputs to Teensy #2 UART:
"ROBOT2:PASS_BALL\n"
```

**Key points:**
- No prefix added by SAT2 (transparent)
- Line ending preserved (`\n`)
- Latency: ~7ms from Teensy #1 UART to Teensy #2 UART

---

## Transparent UART Bridge

### Concept

Any data sent by a Teensy that **does not start with `DBG:`** is treated as transparent P2P data and forwarded to the peer robot's Teensy.

### Example 1: Simple Message

**Robot 1 Teensy:**
```cpp
Serial1.println("HELLO_FROM_ROBOT1");
```

**Result:**
- SAT1 receives on UART: `"HELLO_FROM_ROBOT1\n"`
- SAT1 → P2P ESP-NOW → SAT2
- SAT2 outputs to UART: `"HELLO_FROM_ROBOT1\n"`
- Robot 2 Teensy receives: `"HELLO_FROM_ROBOT1\n"`

### Example 2: Binary Data

**Robot 1 Teensy:**
```cpp
uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x0A}; // Note: 0x0A = '\n'
Serial1.write(data, 5);
```

**Result:**
- SAT1 receives bytes until `\n` (0x0A): `{0x01, 0x02, 0x03, 0x04, 0x0A}`
- SAT1 → P2P → SAT2
- SAT2 outputs: `{0x01, 0x02, 0x03, 0x04, 0x0A}`
- Robot 2 Teensy receives: `{0x01, 0x02, 0x03, 0x04, 0x0A}`

> **Important:** The bridge operates on lines terminated by `\n`. Binary data should include `\n` as delimiter.

### Example 3: JSON Data

**Robot 1 Teensy:**
```cpp
Serial1.println("{\"action\":\"pass\",\"angle\":90}");
```

**Result:**
- Robot 2 Teensy receives: `"{\"action\":\"pass\",\"angle\":90}\n"`
- Can parse JSON and respond accordingly

### Telemetry vs. P2P Data

**Telemetry (sent to Hub):**
```cpp
// Robot 1 Teensy
Serial1.println("DBG:BallDetected=1");
```
→ Goes to Hub, appears in Web UI

**P2P Data (sent to peer):**
```cpp
// Robot 1 Teensy
Serial1.println("BallDetected=1");  // NO "DBG:" prefix
```
→ Goes to Robot 2 Teensy via P2P bridge

### Bidirectional Communication

The bridge works in **both directions** simultaneously:

```
Teensy #1                          Teensy #2
    │                                  │
    ├─► "CMD1\n" ───P2P──────────────►│
    │                                  │
    │◄──────────────P2P─── "ACK1\n" ◄─┤
    │                                  │
    ├─► "CMD2\n" ───P2P──────────────►│
    │                                  │
```

---

## Performance Characteristics

### Latency Measurements

| Path | Typical Latency | Max Latency | Notes |
|------|----------------|-------------|-------|
| Teensy #1 → SAT1 UART | 0.1ms | 1ms | 115200 baud |
| SAT1 → SAT2 ESP-NOW | 5-7ms | 15ms | Channel 6, no retry |
| SAT2 UART → Teensy #2 | 0.1ms | 1ms | 115200 baud |
| **Total P2P** | **~7ms** | **~17ms** | End-to-end |

**For comparison:**
- Hub command path: 50-100ms (includes WiFi, WebSocket, retries)
- P2P is **7-14x faster** than hub path

### Throughput

**UART Bandwidth:** 115200 bps = ~11.5 KB/s = ~11.5 characters/ms

**ESP-NOW Bandwidth:**
- Max payload per frame: 180 bytes
- Max frame rate: ~100 frames/sec (10ms interval recommended)
- Theoretical max: 18 KB/s
- Practical max: ~10 KB/s (accounting for overhead)

**Bottleneck:** UART (115200 baud) is the limiting factor, not ESP-NOW.

### Reliability

**P2P Link Characteristics:**
- **No ACK:** Fire-and-forget for low latency
- **No Retry:** Failed frames are dropped
- **Range:** ~10-20 meters line-of-sight
- **Error Detection:** CRC-16 on ESP-NOW frames
- **Success Rate:** >99% at <10m in typical environments

**Hub Link Characteristics (for comparison):**
- **ACK:** Required for critical commands (MODE, CAL, SETTINGS)
- **Retry:** Up to 3 retries with 500ms timeout
- **Range:** ~10-30 meters (WiFi AP)

---

## Implementation Details

### SAT1 Code (ESP_Satellite/src/main.cpp)

**UART Processing:**
```cpp
void loop() {
    // Read from Teensy UART
    if (Serial1.available()) {
        char line[512];
        int len = readLine(Serial1, line, sizeof(line));

        if (line[0] == 'D' && line[1] == 'B' && line[2] == 'G' && line[3] == ':') {
            // Telemetry → send to Hub
            sendDebugToHub(line + 4); // Skip "DBG:" prefix
        } else {
            // Transparent P2P data → send to peer
            sendRawDataToPeer(line, len);
        }
    }

    // Process incoming ESP-NOW from peer
    processP2PMessages();

    // Send P2P heartbeat
    sendP2PHeartbeat();
}
```

**Send Raw Data to Peer:**
```cpp
void sendRawDataToPeer(const char *data, int len) {
    Frame frame;
    frame.magic = 0xBE;
    frame.msg_type = MSG_UART_RAW;
    frame.seq = nextSeq++;
    frame.src_role = MY_ROLE;      // 1 for SAT1
    frame.dst_role = PEER_ROLE;    // 2 for SAT2
    frame.flags = 0;               // No ACK
    frame.len = len;
    frame.reserved = 0;
    memcpy(frame.payload, data, len);

    uint16_t crc = crc16_modbus((uint8_t*)&frame, FRAME_HEADER_SIZE + len);
    memcpy(&frame.payload[len], &crc, 2);

    espNowBridge.sendToPeer(&frame);
}
```

**Receive from Peer:**
```cpp
void onEspNowReceive(const uint8_t *mac, const uint8_t *data, int len) {
    Frame *frame = (Frame*)data;

    // Validate CRC
    if (!validateCRC(frame, len)) {
        return;
    }

    if (frame->msg_type == MSG_UART_RAW) {
        // Output raw data to Teensy UART (transparent)
        Serial1.write(frame->payload, frame->len);
    } else if (frame->msg_type == MSG_P2P_HB) {
        // Update peer heartbeat timestamp
        updatePeerTimestamp();
    }
}
```

### USB Telemetry Injection

Satellites also accept telemetry via USB Serial for debugging:

**USB Input:**
```
DBG:TestValue=123
```

**Processing:**
```cpp
void loop() {
    // Read from USB Serial
    if (Serial.available()) {
        char line[64];
        int len = readLine(Serial, line, sizeof(line));

        if (strncmp(line, "DBG:", 4) == 0) {
            // Telemetry injection → send to hub
            sendDebugToHub(line + 4);
        } else if (strcmp(line, "mac") == 0) {
            // USB command
            printMacInfo();
        }
        // ... other USB commands
    }
}
```

See [USB_PROTOCOL.md](USB_PROTOCOL.md) for full USB command reference.

---

## Troubleshooting

### P2P Bridge Issues

| Symptom | Likely Cause | Solution |
|---------|-------------|----------|
| No P2P communication | Satellites not paired | Verify MAC addresses are correct |
| P2P messages dropped | Distance too far | Move satellites closer (<10m) |
| Intermittent P2P | WiFi interference | Change channel or reduce interference |
| Data corrupted | CRC failures | Check antenna orientation; reduce distance |

### Debugging P2P Link

#### 1. Check P2P Heartbeat

Connect to SAT1 serial monitor:
```bash
cd ESP_Satellite
pio device monitor -b 115200
```

Type USB command:
```
debug
```

**Expected output:**
```
[SAT1] MAC: AA:BB:CC:DD:EE:01
[SAT1] Peer (SAT2) MAC: AA:BB:CC:DD:EE:02
[SAT1] Peer online: YES
[SAT1] Last P2P heartbeat: 5ms ago
```

If peer offline, check:
- SAT2 is powered and running
- MAC addresses match
- Distance <10m

#### 2. Test P2P Data Flow

**On Robot 1 Teensy:**
```cpp
void loop() {
    Serial1.println("PING_FROM_ROBOT1");
    delay(1000);
}
```

**On Robot 2 Teensy serial monitor:**
- Should see: `PING_FROM_ROBOT1` every second

If not received:
1. Check SAT1 UART TX/RX wiring
2. Check SAT2 UART TX/RX wiring
3. Verify P2P heartbeat is active (see above)

#### 3. Monitor ESP-NOW Statistics

Some ESP-NOW debugging can be enabled:

**Edit `platformio.ini`:**
```ini
build_flags =
    -DCORE_DEBUG_LEVEL=4
```

Re-flash and monitor for ESP-NOW send/receive logs.

### Performance Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| High latency (>20ms) | Packet queue buildup | Reduce P2P send rate |
| Frame drops | ESP-NOW buffer full | Add delay between sends |
| UART overflow | Data rate too high | Reduce UART output from Teensy |

### Range Issues

**Improving P2P Range:**

1. **Antenna Positioning:**
   - Keep antennas vertical
   - Avoid metal obstructions
   - Orient boards for line-of-sight

2. **Power:**
   - Use good quality USB cables or power supplies
   - Avoid voltage drops

3. **Environment:**
   - Reduce WiFi congestion on channel 6
   - Avoid 2.4GHz interference sources
   - Test in open space first

---

## Advanced Use Cases

### Example 1: Ball Possession Signaling

**Robot 1 (has ball):**
```cpp
void loop() {
    if (ballDetected) {
        Serial1.println("BALL:MINE");
    } else {
        Serial1.println("BALL:NONE");
    }
    delay(100); // 10Hz update
}
```

**Robot 2 (receives signal):**
```cpp
void loop() {
    if (Serial1.available()) {
        String msg = Serial1.readStringUntil('\n');
        if (msg == "BALL:MINE") {
            // Robot 1 has the ball, adjust strategy
            role = SUPPORT;
        } else if (msg == "BALL:NONE") {
            // Ball is free, go get it
            role = ATTACK;
        }
    }
}
```

### Example 2: Coordinated Movement

**Robot 1 (leader):**
```cpp
void loop() {
    Serial1.print("POS:");
    Serial1.print(myX);
    Serial1.print(",");
    Serial1.println(myY);
    delay(50); // 20Hz
}
```

**Robot 2 (follower):**
```cpp
float leaderX, leaderY;

void loop() {
    if (Serial1.available()) {
        String msg = Serial1.readStringUntil('\n');
        if (msg.startsWith("POS:")) {
            sscanf(msg.c_str(), "POS:%f,%f", &leaderX, &leaderY);
            // Calculate formation position relative to leader
            targetX = leaderX + offsetX;
            targetY = leaderY + offsetY;
        }
    }
    // Drive to target position
}
```

### Example 3: Custom Protocol with ACK

Implement a simple request/response protocol:

**Robot 1:**
```cpp
void sendRequest() {
    Serial1.println("REQ:STATUS");
}

void loop() {
    if (needsStatus && millis() - lastRequest > 500) {
        sendRequest();
        lastRequest = millis();
    }

    if (Serial1.available()) {
        String msg = Serial1.readStringUntil('\n');
        if (msg.startsWith("RESP:")) {
            // Parse response
            processResponse(msg);
            needsStatus = false;
        }
    }
}
```

**Robot 2:**
```cpp
void loop() {
    if (Serial1.available()) {
        String msg = Serial1.readStringUntil('\n');
        if (msg == "REQ:STATUS") {
            // Send response
            Serial1.print("RESP:MODE=");
            Serial1.print(currentMode);
            Serial1.print(",BATT=");
            Serial1.println(batteryVoltage);
        }
    }
}
```

---

## Security Considerations

### P2P Encryption

P2P messages use the same ESP-NOW encryption as hub↔satellite:
- **PMK (Primary Master Key):** Global key, shared by all devices
- **LTK (Local Transport Key):** Per-peer key, optional

**To set encryption keys:**
1. Go to Web UI → Settings
2. Set PMK (applies to all ESP-NOW links)
3. Optionally set per-peer LTK
4. Click Save Config

**Note:** If PMK/LTK mismatch, P2P messages will be dropped silently.

### Data Privacy

Since P2P data is user-defined, ensure sensitive data is not transmitted in plaintext if operating in a competitive environment (e.g., competitions).

Consider:
- Message obfuscation
- Application-level encryption
- Limiting information sent via P2P

---

## Performance Tuning

### Optimizing Latency

**Reduce P2P latency:**
1. Minimize UART line length (<30 characters ideal)
2. Use binary encoding instead of ASCII strings
3. Send only when data changes (not periodic)

**Example binary encoding:**
```cpp
// Instead of: "BALL:MINE\n" (10 bytes)
// Send: {0x01, 0x0A} (2 bytes)

// Robot 1
uint8_t msg[] = {0x01, 0x0A}; // 0x01=BALL_MINE, 0x0A='\n'
Serial1.write(msg, 2);

// Robot 2
if (Serial1.available() >= 2) {
    uint8_t msgType = Serial1.read();
    uint8_t newline = Serial1.read();
    if (msgType == 0x01) {
        // BALL_MINE received
    }
}
```

### Optimizing Bandwidth

**Reduce P2P bandwidth:**
1. Delta encoding (send only changes)
2. Message aggregation (batch multiple values)
3. Compression for larger messages

---

## Further Reading

- **[Software.md](Software.md)** – Complete protocol specification
- **[USB_PROTOCOL.md](USB_PROTOCOL.md)** – USB debugging commands
- **[Hardware.md](Hardware.md)** – UART wiring details
- **[Teensy.md](Teensy.md)** – BotConnect library for Teensy

---

**Document Version:** 1.0
**Last Updated:** 2026-03-19
