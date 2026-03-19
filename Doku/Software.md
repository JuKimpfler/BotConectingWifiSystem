# Software Architecture – BotConnectingWifiSystem

This document describes the software architecture, message protocol, configuration, and implementation details of the BotConnectingWifiSystem.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Folder Structure](#folder-structure)
3. [Message Protocol](#message-protocol)
4. [Hub Software](#hub-software)
5. [Satellite Software](#satellite-software)
6. [Configuration System](#configuration-system)
7. [Unit Tests](#unit-tests)
8. [Development Guide](#development-guide)

---

## Architecture Overview

The system implements a 3-node ESP-NOW mesh with asymmetric roles:

```
┌──────────────────────────────────────────────────────────┐
│                      System Architecture                  │
└──────────────────────────────────────────────────────────┘

   [Browser] ──────► WebSocket ──────► [HUB - ESP #3]
                                          │
                        ESP-NOW Channel 6 │
                        Encrypted (PMK+LTK) │
                                          │
        ┌─────────────────────────────────┴─────────────────┐
        │                                                    │
   [SAT1 - ESP #1] ◄──────────────────────────► [SAT2 - ESP #2]
        │          P2P Fast Bridge (~7ms)                   │
        │                                                    │
   UART 115200                                         UART 115200
        │                                                    │
   [Teensy #1]                                         [Teensy #2]
```

### Key Design Principles

1. **Hub-Satellite Asymmetry:**
   - Hub: WiFi AP, WebSocket server, command routing
   - Satellites: ESP-NOW bridge, UART I/O, P2P communication

2. **P2P Bridge Priority:**
   - SAT1 ↔ SAT2 direct link operates at ~7ms cycle
   - Independent of hub availability (resilient)
   - Used for real-time inter-robot communication

3. **Command vs. Telemetry Flow:**
   - **Commands:** Browser → Hub → Satellite → Teensy (low rate, ~100ms)
   - **Telemetry:** Teensy → Satellite → Hub → Browser (throttled to 20Hz)

4. **Reliability:**
   - ACK mechanism for critical commands (MODE, CAL, PAIR, SETTINGS)
   - Up to 3 retries with 500ms timeout
   - Heartbeat at 1Hz with 4s offline timeout

5. **Security:**
   - ESP-NOW encryption with PMK (global) + LTK (per-peer)
   - Keys configurable via Web UI
   - Stored in NVS (non-volatile storage)

---

## Folder Structure

```
BotConnectingWifiSystem/
│
├── shared/                     ← Protocol definitions (used by all targets)
│   ├── messages.h              ← Frame structs, message types, roles, flags
│   ├── crc16.h                 ← CRC-16/MODBUS implementation (inline)
│   ├── config_schema.json      ← JSON schema for hub configuration
│   └── config_default.json     ← Factory default configuration
│
├── ESP_Hub/                    ← PlatformIO project for HUB (ESP #3)
│   ├── platformio.ini          ← Build configuration
│   ├── include/                ← Hub-specific headers
│   │   ├── hub_config.h        ← Hub constants & limits
│   │   ├── EspNowManager.h     ← ESP-NOW send/receive
│   │   ├── PeerRegistry.h      ← Known peer tracking
│   │   ├── ConfigStore.h       ← NVS config persistence
│   │   ├── TelemetryBuffer.h   ← Telemetry aggregation & throttling
│   │   ├── HeartbeatService.h  ← Keepalive service
│   │   └── CommandRouter.h     ← Route commands to satellites
│   ├── src/                    ← Hub implementation
│   │   ├── main.cpp            ← Main loop, WiFi AP, WebSocket
│   │   ├── EspNowManager.cpp
│   │   ├── PeerRegistry.cpp
│   │   ├── ConfigStore.cpp
│   │   ├── TelemetryBuffer.cpp
│   │   ├── HeartbeatService.cpp
│   │   └── CommandRouter.cpp
│   ├── data/                   ← LittleFS image (built by Vite)
│   └── ui/                     ← Vite Web UI source
│       ├── index.html
│       ├── package.json
│       ├── vite.config.js
│       └── src/
│           ├── main.js         ← UI logic, WebSocket handling
│           ├── ws.js           ← WebSocket wrapper
│           └── style.css       ← UI styling
│
├── ESP_Satellite/              ← PlatformIO project for SAT1 & SAT2
│   ├── platformio.ini          ← Build environments (esp_sat1, esp_sat2)
│   ├── include/
│   │   ├── sat_config.h        ← Satellite constants
│   │   ├── EspNowBridge.h      ← ESP-NOW send/receive with P2P
│   │   ├── AckManager.h        ← ACK tracking for non-cyclic commands
│   │   └── CommandParser.h     ← Parse hub commands → UART strings
│   └── src/
│       ├── main.cpp            ← Main loop, UART I/O, ESP-NOW handling
│       ├── EspNowBridge.cpp
│       ├── AckManager.cpp
│       └── CommandParser.cpp
│
├── Teensy_lib/                 ← Arduino library for Teensy 4.0
│   ├── src/
│   │   ├── BotConnect.h        ← Public API
│   │   └── BotConnect.cpp      ← Implementation
│   └── examples/
│       └── BasicUsage/BasicUsage.ino
│
└── test/unit/                  ← Host-side unit tests (CMake)
    ├── CMakeLists.txt
    ├── test_crc16.cpp          ← CRC-16 algorithm test
    ├── test_messages.cpp       ← Frame serialization/deserialization
    └── test_command_parser.cpp ← Command string generation
```

---

## Message Protocol

### Frame Format

All ESP-NOW messages use a common frame format with CRC-16 integrity check.

```
┌────────┬──────────┬─────┬──────────┬──────────┬───────┬─────┬──────┬─────────┬────────┐
│ Magic  │ Msg Type │ Seq │ Src Role │ Dst Role │ Flags │ Len │ Rsvd │ Payload │ CRC-16 │
├────────┼──────────┼─────┼──────────┼──────────┼───────┼─────┼──────┼─────────┼────────┤
│ 1 byte │  1 byte  │ 1 B │  1 byte  │  1 byte  │ 1 B   │ 1 B │  1 B │ 0-180 B │  2 B   │
└────────┴──────────┴─────┴──────────┴──────────┴───────┴─────┴──────┴─────────┴────────┘

Offset  Size  Field       Description
──────────────────────────────────────────────────────────────────────────────
  0      1    magic       Start-of-frame marker (0xBE)
  1      1    msg_type    Message type (see table below)
  2      1    seq         Sequence number (0-255, rolling)
  3      1    src_role    Source role (0=HUB, 1=SAT1, 2=SAT2, 0xFF=broadcast)
  4      1    dst_role    Destination role
  5      1    flags       Control flags (see below)
  6      1    len         Payload length in bytes (0-180)
  7      1    reserved    Reserved for future use (must be 0)
 8..N    N    payload     Message-specific data
N+1..2   2    crc16       CRC-16/MODBUS over bytes 0..N
```

**Maximum frame size:** 190 bytes (within ESP-NOW 250-byte limit)

### Frame Header Flags

```
Bit 0: ACK_REQ       – Sender requests acknowledgement
Bit 1: IS_RESPONSE   – This message is a response/ACK to a previous request
Bit 2: PRIORITY      – High priority message (reserved, not used)
Bits 3-7: Reserved
```

### CRC-16 Algorithm

**Algorithm:** CRC-16/MODBUS
- **Polynomial:** 0xA001 (reversed 0x8005)
- **Initial value:** 0xFFFF
- **Final XOR:** 0x0000
- **Reflect input:** Yes
- **Reflect output:** Yes

**Test vector:** `"123456789"` → `0x4B37`

**Implementation:** See `shared/crc16.h`

---

## Message Types

### Message Type Table

| Type | Value | Description | ACK Required | Cyclic |
|------|-------|-------------|--------------|--------|
| **DBG** | 0x01 | Debug/Telemetry from Teensy | No | Yes (continuous) |
| **CTRL** | 0x02 | Control (speed, angle, switches, buttons) | No | Yes (~10Hz) |
| **MODE** | 0x03 | Mode selection (1-5) | **Yes** | No |
| **CAL** | 0x04 | Calibration command | **Yes** | No |
| **PAIR** | 0x05 | Pairing request/response | **Yes** | No |
| **HB** | 0x06 | Heartbeat (keepalive) | No | Yes (1Hz) |
| **ACK** | 0x07 | Acknowledgement | No | No |
| **ERR** | 0x08 | Error response | No | No |
| **SET** | 0x09 | Settings update | **Yes** | No |
| **DISC** | 0x0A | Discovery broadcast/announce | No | No |

### Message Payloads

#### MSG_CTRL (0x02) – Control Command

Payload (9 bytes):
```
Offset  Size  Field       Range
────────────────────────────────────
  0      2    speed       int16_t (-1000 to 1000)
  2      2    angle       int16_t (-180 to 180)
  4      1    switches    uint8_t (bitfield: SW1, SW2, SW3)
  5      1    buttons     uint8_t (bitfield: B1, B2, B3, B4)
  6      1    start       uint8_t (0 or 1)
  7      2    reserved    0
```

**UART output to Teensy:**
```
V<speed>A<angle>SW<switches>BTN<buttons>START<start>\n
```

Example: `V500A90SW3BTN5START1\n`

#### MSG_MODE (0x03) – Mode Selection

Payload (1 byte):
```
Offset  Size  Field       Range
────────────────────────────────────
  0      1    mode_id     1-5
```

**Modes:**
1. PID Control
2. Ball Approach
3. Goal Rotate
4. Homing
5. Defender

**UART output:**
```
M<mode_id>\n
```

Example: `M1\n`

#### MSG_CAL (0x04) – Calibration

Payload (variable, null-terminated string):
```
Offset  Size  Field          Values
──────────────────────────────────────────────
  0      N    command_str    "IR_Max", "IR_Min", "Line_Max", "Line_Min", "BNO"
```

**UART output:**
```
CAL_<COMMAND>\n
```

Examples:
- `CAL_IR_MAX\n`
- `CAL_BNO\n`

#### MSG_DBG (0x01) – Telemetry

Payload (variable, null-terminated string):
```
Format: <name>=<value>

Examples:
  Speed=120
  BallAngle=45.5
  Mode=1
```

Satellites forward telemetry from Teensy UART to hub. Hub aggregates and throttles to UI at max 20Hz.

#### MSG_HB (0x06) – Heartbeat

Payload (4 bytes):
```
Offset  Size  Field       Description
────────────────────────────────────────────
  0      4    uptime_ms   Sender uptime in milliseconds
```

Sent bidirectionally (Hub ↔ Satellite) at 1Hz. Peer marked offline if no HB received for 4 seconds.

#### MSG_PAIR (0x05) – Pairing

Payload (variable, implementation-defined):
Used during initial peer discovery and role assignment. Typically contains MAC address and role information.

#### MSG_ACK (0x07) – Acknowledgement

Payload (1 byte):
```
Offset  Size  Field       Description
────────────────────────────────────
  0      1    seq         Sequence number being acknowledged
```

Sent in response to messages with `FLAG_ACK_REQ` set.

#### MSG_ERR (0x08) – Error

Payload (variable, null-terminated string):
```
Error description string
```

Example: `"Invalid mode ID"`

---

## Hub Software

### Components

#### 1. EspNowManager

**Responsibilities:**
- Initialize ESP-NOW
- Set PMK/LTK encryption keys
- Send/receive ESP-NOW frames
- Register peer MAC addresses

**Key Functions:**
```cpp
void init();
bool addPeer(const uint8_t *mac, uint8_t role, const uint8_t *ltk);
bool send(const uint8_t *mac, const Frame *frame);
void onReceive(esp_now_recv_cb_t callback);
```

#### 2. PeerRegistry

**Responsibilities:**
- Track known satellites (SAT1, SAT2)
- Store MAC addresses, roles, names
- Track online/offline status
- Last-seen timestamps

**Key Data:**
```cpp
struct PeerInfo {
    uint8_t mac[6];
    uint8_t role;           // 1=SAT1, 2=SAT2
    char name[32];
    bool online;
    uint32_t last_seen_ms;
};
```

#### 3. ConfigStore

**Responsibilities:**
- Load/save configuration from NVS
- Parse JSON configuration
- Apply default configuration on first boot
- Hex string ↔ byte array conversion

**Configuration Keys:**
- `channel`: ESP-NOW / WiFi channel (1-13)
- `pmk`: Primary Master Key (32 hex chars)
- `peers[]`: Array of peer configurations
- `telemetry.max_rate_hz`: UI update rate
- `heartbeat.interval_ms`: Heartbeat send interval
- `heartbeat.timeout_ms`: Peer offline timeout

#### 4. TelemetryBuffer

**Responsibilities:**
- Aggregate telemetry from both satellites
- Throttle telemetry to max 20Hz per stream
- Track min/max values for each stream
- JSON serialization for WebSocket

**Data Structure:**
```cpp
struct TelemetryStream {
    char name[32];
    float current;
    float min;
    float max;
    uint32_t last_update_ms;
};
```

#### 5. HeartbeatService

**Responsibilities:**
- Send heartbeat to all peers at 1Hz
- Monitor incoming heartbeats
- Mark peers offline after 4s timeout
- Notify on status changes

#### 6. CommandRouter

**Responsibilities:**
- Route WebSocket commands to correct satellite
- Add ACK request flag for non-cyclic commands
- Retry failed commands (up to 3 times)
- Track pending ACKs with timeout

**ACK Tracking:**
```cpp
struct PendingAck {
    uint8_t seq;
    uint8_t dst_role;
    uint8_t retry_count;
    uint32_t sent_ms;
};
```

### Main Loop (Hub)

```cpp
void loop() {
    // 1. Process incoming ESP-NOW messages
    processEspNowQueue();

    // 2. Process WebSocket messages from browser
    processWebSocketQueue();

    // 3. Send heartbeats (1Hz)
    heartbeatService.process();

    // 4. Check ACK timeouts and retry
    commandRouter.processRetries();

    // 5. Send throttled telemetry to WebSocket (20Hz max)
    sendTelemetryToUI();

    delay(1); // Yield to WiFi/ESP-NOW tasks
}
```

---

## Satellite Software

### Components

#### 1. EspNowBridge

**Responsibilities:**
- Initialize ESP-NOW
- Send/receive frames to/from Hub and peer satellite
- P2P bridge: forward non-DBG UART data to peer
- Handle encryption keys

**P2P Bridge Logic:**
```
UART Input (from Teensy):
  - If starts with "DBG:" → Parse as telemetry → Send to Hub
  - Otherwise → Send to peer satellite (transparent bridge)

ESP-NOW Input (from peer):
  - If MSG_UART_RAW → Output to Teensy UART (no prefix)
  - If MSG_CTRL/MODE/CAL → Parse and output to Teensy UART
```

#### 2. AckManager

**Responsibilities:**
- Track received commands that require ACK
- Send ACK responses
- Deduplicate repeated commands (same sequence number)

#### 3. CommandParser

**Responsibilities:**
- Convert binary command frames to UART strings
- Format control, mode, calibration commands
- Add appropriate prefixes for Teensy parsing

**Examples:**
```cpp
MSG_CTRL → "V500A90SW3BTN5START1\n"
MSG_MODE → "M1\n"
MSG_CAL  → "CAL_IR_MAX\n"
```

### Main Loop (Satellite)

```cpp
void loop() {
    // 1. Read UART from Teensy
    if (Serial1.available()) {
        char line[512];
        readLine(Serial1, line, sizeof(line));

        if (startsWith(line, "DBG:")) {
            // Telemetry → send to hub
            sendDebugToHub(line);
        } else {
            // Raw data → send to peer satellite
            sendRawToPeer(line);
        }
    }

    // 2. Process incoming ESP-NOW messages
    processEspNowQueue();

    // 3. Send P2P heartbeat to peer (fast, ~7ms cycle)
    sendP2PHeartbeat();

    // 4. Send heartbeat to hub (1Hz)
    sendHubHeartbeat();

    delay(1); // Yield
}
```

### USB Serial Commands (Satellite)

For debugging and maintenance, satellites accept USB serial commands:

| Command | Description |
|---------|-------------|
| `mac` or `info` | Display MAC address, channel, hub status |
| `debug` | Show extended status (uptime, ACK queue, peers) |
| `clearmac` | Clear stored hub/peer MACs from NVS |
| `help` | List available commands |

Additionally, satellites accept telemetry injection via USB:
```
DBG:<name>=<value>
```

This is treated as if received from Teensy UART and forwarded to hub.

> **Note:** See [USB_PROTOCOL.md](USB_PROTOCOL.md) for details.

---

## Configuration System

### Configuration Schema

The hub configuration is stored as JSON in NVS and follows the schema defined in `shared/config_schema.json`.

**Example configuration:**
```json
{
  "channel": 6,
  "pmk": "0102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20",
  "peers": [
    {
      "role": "SAT1",
      "mac": "AA:BB:CC:DD:EE:01",
      "name": "Robot 1",
      "ltk": ""
    },
    {
      "role": "SAT2",
      "mac": "AA:BB:CC:DD:EE:02",
      "name": "Robot 2",
      "ltk": ""
    }
  ],
  "telemetry": {
    "max_rate_hz": 20
  },
  "heartbeat": {
    "interval_ms": 1000,
    "timeout_ms": 4000
  }
}
```

### Configuration Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `channel` | int | 6 | WiFi/ESP-NOW channel (1-13) |
| `pmk` | string | `""` | Primary Master Key (64 hex chars = 32 bytes) |
| `peers[].role` | string | – | `"SAT1"` or `"SAT2"` |
| `peers[].mac` | string | – | MAC address in `AA:BB:CC:DD:EE:FF` format |
| `peers[].name` | string | – | Friendly name for UI |
| `peers[].ltk` | string | `""` | Local Transport Key (64 hex chars = 32 bytes), optional |
| `telemetry.max_rate_hz` | int | 20 | Maximum telemetry update rate to UI |
| `heartbeat.interval_ms` | int | 1000 | Heartbeat send interval |
| `heartbeat.timeout_ms` | int | 4000 | Peer offline timeout |

### NVS Keys

Configuration is stored in ESP32 NVS (Non-Volatile Storage) with these keys:

| Key | Type | Description |
|-----|------|-------------|
| `config` | String (JSON) | Full configuration as JSON string |

### Factory Reset

To reset to factory defaults:
1. Via Web UI: Settings → Factory Reset button
2. Via PlatformIO: `pio run -e esp_hub -t erase` (erases entire flash)

---

## Unit Tests

### Test Suite

Located in `test/unit/`, these host-side tests verify core protocol logic without hardware.

#### test_crc16.cpp

Tests the CRC-16/MODBUS implementation:
- Known test vectors
- Empty data
- Single byte
- Multi-byte sequences

**Test vector:** `"123456789"` → `0x4B37`

#### test_messages.cpp

Tests frame serialization/deserialization:
- Frame header encoding
- Payload packing
- CRC calculation and validation
- Frame parsing

#### test_command_parser.cpp

Tests command string generation:
- Control command formatting
- Mode command formatting
- Calibration command formatting

### Running Tests

```bash
cd test/unit
mkdir build && cd build
cmake .. -DSAT_ID=1
make
ctest --output-on-failure
```

**Expected output:**
```
Test project /home/user/BotConnectingWifiSystem/test/unit/build
    Start 1: test_crc16
1/3 Test #1: test_crc16 .......................   Passed    0.01 sec
    Start 2: test_messages
2/3 Test #2: test_messages ....................   Passed    0.01 sec
    Start 3: test_command_parser
3/3 Test #3: test_command_parser ..............   Passed    0.01 sec

100% tests passed, 0 tests failed out of 3
```

---

## Development Guide

### Prerequisites

- **PlatformIO:** For compiling and flashing ESP32 firmware
- **Node.js:** For building the Web UI
- **CMake:** For running unit tests
- **Git:** For version control

### Building Firmware

**Hub:**
```bash
cd ESP_Hub
pio run -e esp_hub
```

**Satellites:**
```bash
cd ESP_Satellite
pio run -e esp_sat1  # or esp_sat2
```

### Building Web UI

```bash
cd ESP_Hub/ui
npm install
npm run build
```

Output goes to `ESP_Hub/data/`.

### Flashing

**Hub firmware:**
```bash
cd ESP_Hub
pio run -e esp_hub -t upload
```

**Hub filesystem:**
```bash
pio run -e esp_hub -t uploadfs
```

**Satellites:**
```bash
cd ESP_Satellite
pio run -e esp_sat1 -t upload  # or esp_sat2
```

### Debugging

**Serial Monitor (Hub):**
```bash
cd ESP_Hub
pio device monitor -b 115200
```

**Serial Monitor (Satellite):**
```bash
cd ESP_Satellite
pio device monitor -b 115200
```

**Enable verbose logging:**

Edit `platformio.ini` and add:
```ini
build_flags =
    -DCORE_DEBUG_LEVEL=4  ; 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
```

### Code Style

- **Indentation:** 4 spaces (no tabs)
- **Braces:** K&R style (opening brace on same line)
- **Naming:**
  - Classes: `PascalCase`
  - Functions: `camelCase`
  - Variables: `snake_case`
  - Constants: `UPPER_SNAKE_CASE`

### Adding New Message Types

1. Add message type to `shared/messages.h`:
   ```cpp
   #define MSG_NEW_TYPE 0x0B
   ```

2. Define payload struct (if needed):
   ```cpp
   struct NewTypePayload {
       uint8_t field1;
       uint16_t field2;
   };
   ```

3. Update hub `CommandRouter` to handle new type
4. Update satellite `CommandParser` to handle new type
5. Add unit test in `test_command_parser.cpp`

### Adding New Web UI Features

1. Edit `ESP_Hub/ui/src/main.js` for UI logic
2. Edit `ESP_Hub/ui/src/style.css` for styling
3. Edit `ESP_Hub/ui/index.html` for structure
4. Rebuild UI: `npm run build`
5. Re-flash filesystem: `pio run -e esp_hub -t uploadfs`

### Performance Considerations

- **ESP-NOW packet rate:** Max ~100 packets/sec per peer
- **UART baud rate:** 115200 bps = ~11.5 KB/s
- **WiFi AP + ESP-NOW:** Use same channel (default: 6)
- **WebSocket throttling:** Telemetry limited to 20Hz to avoid browser overload
- **P2P cycle:** ~7ms cycle between satellites (priority traffic)

---

## API Reference

### Hub Classes

#### EspNowManager

```cpp
void init();
bool addPeer(const uint8_t *mac, uint8_t role, const uint8_t *ltk);
bool send(const uint8_t *mac, const Frame *frame);
void setPMK(const uint8_t *pmk);
```

#### PeerRegistry

```cpp
bool addPeer(uint8_t role, const uint8_t *mac, const char *name);
PeerInfo* getPeerByRole(uint8_t role);
void markOnline(uint8_t role);
void markOffline(uint8_t role);
```

#### ConfigStore

```cpp
bool load();
bool save();
const char* getJSON();
void setFromJSON(const char *json);
void factoryReset();
```

#### TelemetryBuffer

```cpp
void update(const char *name, float value, uint8_t sat_id);
const char* toJSON();  // Returns JSON array of streams
void clear();
```

### Satellite Classes

#### EspNowBridge

```cpp
void init(uint8_t sat_id);
bool sendToHub(const Frame *frame);
bool sendToPeer(const Frame *frame);
void onReceive(esp_now_recv_cb_t callback);
```

#### AckManager

```cpp
void addPending(uint8_t seq, uint8_t msg_type);
bool shouldAck(uint8_t seq);
void sendAck(uint8_t seq);
```

#### CommandParser

```cpp
void parseControl(const CtrlPayload *payload, char *out, size_t len);
void parseMode(uint8_t mode_id, char *out, size_t len);
void parseCal(const char *cmd, char *out, size_t len);
```

---

## Troubleshooting

### Build Issues

| Issue | Solution |
|-------|----------|
| PlatformIO can't find platform | Run `pio pkg update` |
| ESP32 toolchain download fails | Check internet connection; retry |
| Web UI build fails | Delete `node_modules/`, run `npm install` |
| Unit tests don't compile | Ensure CMake ≥ 3.14 and C++17 compiler |

### Runtime Issues

| Issue | Likely Cause | Solution |
|-------|-------------|----------|
| ESP-NOW send fails | Channel mismatch | Verify all devices on channel 6 |
| ACK timeout | Encryption key mismatch | Check PMK/LTK in Settings |
| Telemetry not updating | Teensy not sending `DBG:` | Verify BotConnect library usage |
| WebSocket disconnects | WiFi interference | Change channel in config |

---

## Further Reading

- **[Setup.md](Setup.md)** – Complete setup instructions
- **[Hardware.md](Hardware.md)** – Wiring and pin assignments
- **[Bridge.md](Bridge.md)** – P2P bridge implementation
- **[Webserver.md](Webserver.md)** – Web UI documentation
- **[Teensy.md](Teensy.md)** – BotConnect library API
- **[USB_PROTOCOL.md](USB_PROTOCOL.md)** – USB debugging commands

---

**Document Version:** 1.0
**Last Updated:** 2026-03-19
