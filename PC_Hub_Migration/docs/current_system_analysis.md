# BCWS Current System Analysis
> **Phase:** 1 — Repo Analysis & Architecture Freeze  
> **Date:** 2026-04  
> **Author:** Agent — based on code analysis (no speculation)

---

## 1. Overview

The existing system consists of:
- **ESP32-C3/C6 Hub** (`ESP_Hub/`) — WiFi AP + ESP-NOW hub, WebSocket server, LittleFS UI
- **Two ESP32-C3 Satellites** (`ESP_Satellite/`) — ESP-NOW bridge to Teensy 4.0 via UART
- **Two Teensy 4.0 boards** (`Teensy_lib/`) — robot controllers, send telemetry over UART

---

## 2. Communication Paths

### 2.1 Hub ↔ Satellite (ESP-NOW Radio)

All communication uses the binary `Frame_t` struct defined in `shared/messages.h`:

```
Frame_t (190 bytes):
  [0]   magic       = 0xBE
  [1]   msg_type    = MSG_* constant
  [2]   seq         = rolling 0–255
  [3]   src_role    = ROLE_HUB(0), ROLE_SAT1(1), ROLE_SAT2(2), ROLE_BROADCAST(FF)
  [4]   dst_role    = same
  [5]   flags       = FLAG_ACK_REQ(0x01), FLAG_IS_RESPONSE(0x02)
  [6]   len         = payload byte count (0..180)
  [7]   network_id  = 0x01 (anti-mis-pairing)
  [8..187] payload  = 180 bytes
  [188..189] crc16  = CRC-16/IBM over bytes 0..187
```

**Source:** `shared/messages.h` (verified)

### 2.2 Satellite ↔ Teensy (UART)

- Baud rate: 921600 (verified in `ESP_Satellite/include/sat_config.h`)
- Teensy → Satellite: Lines prefixed `DBG:<name>=<value>` become `MSG_DBG` frames
- Lines without `DBG:` prefix are forwarded as `MSG_UART_RAW` to the peer satellite (transparent P2P bridge)
- Satellite → Teensy: Commands formatted as `V{speed}A{angle}SW{sw}BTN{btn}START{s}\n`, `M{mode}\n`, `CAL_{type}\n`

**Source:** `ESP_Satellite/src/CommandParser.cpp`, `shared/messages.h` (DBG_PREFIX)

---

## 3. Message Types & Directions

| Type | Value | Direction | ACK Required | Notes |
|------|-------|-----------|--------------|-------|
| MSG_DBG | 0x01 | SAT→HUB | No | Telemetry entry, 25-byte payload |
| MSG_CTRL | 0x02 | HUB→SAT | Yes | speed/angle/switches/buttons/start |
| MSG_MODE | 0x03 | HUB→SAT | Yes | mode_id 1..5 |
| MSG_CAL | 0x04 | HUB→SAT | Yes | cal_cmd 0x01..0x05 |
| MSG_PAIR | 0x05 | HUB→SAT | Yes | pairing, uses broadcast MAC |
| MSG_HEARTBEAT | 0x06 | HUB↔SAT | No | uptime_ms + rssi + queue_len |
| MSG_ACK | 0x07 | SAT→HUB | No | ack_seq + status + msg_type |
| MSG_ERROR | 0x08 | SAT→HUB | No | (not currently used in hub code) |
| MSG_SETTINGS | 0x09 | HUB→SAT | Yes | channel, PMK, telemetry_rate_hz |
| MSG_DISCOVERY | 0x0A | Broadcast | No | auto peer discovery |
| MSG_UART_RAW | 0x0B | SAT↔SAT | No | transparent P2P UART bridge |
| MSG_TELEM_DICT | 0x0C | SAT→HUB | No | stream_id → name mapping |
| MSG_TELEM_BATCH | 0x0D | SAT→HUB | No | compact multi-value batch |

**Source:** `shared/messages.h`, `ESP_Hub/src/CommandRouter.cpp`

---

## 4. ACK / Retry Behaviour

### Hub Side (ESP Hub firmware)
- `ESP_Hub` sends commands with `FLAG_ACK_REQ` set.
- The hub does **not** implement retry — it sends once and relies on the satellite's duplicate detection.
- The _satellite_ implements ACK/retry in `ESP_Satellite/src/AckManager.cpp` — but this is for the satellite retrying its own outbound unreliable sends (not ACK of hub commands).

**Note:** The current ESP hub firmware does **not** implement ACK timeout/retry for hub→satellite commands. The PC Hub must implement this as a new capability. Parameters from `protocol_v1.md`: `ACK_TIMEOUT_MS=500`, `ACK_MAX_RETRIES=3`.

**Source:** `ESP_Hub/src/CommandRouter.cpp::_buildAndSend()` — sends once, no retry loop. `ESP_Satellite/src/AckManager.cpp` — satellite manages its own retry queue.

### Satellite ACK back to Hub
When a satellite receives a frame with `FLAG_ACK_REQ`:
- Calls `AckManager::onAck()` after processing
- Sends `MSG_ACK` with `acked_seq` and `status`

**Source:** `ESP_Satellite/src/main.cpp` (dispatch in receive callback)

---

## 5. Heartbeat / Timeout Logic

### Hub sends heartbeat
- Every `heartbeat_interval_ms` (default 1000 ms) to all registered peers (broadcast MAC or unicast)
- Payload: `uptime_ms`, `rssi=0`, `queue_len=0`
- **Source:** `ESP_Hub/src/HeartbeatService.cpp::tick()`

### Peer timeout detection
- `PeerRegistry::tickTimeouts(timeout_ms)` marks peers offline after `timeout_ms` (default 4000 ms) without any received frame
- State transition: online → offline triggers WebSocket `peer_status` broadcast
- **Source:** `ESP_Hub/src/PeerRegistry.cpp`, `ESP_Hub/src/HeartbeatService.cpp`

### Satellite heartbeat back to hub
- Satellites send periodic `MSG_HEARTBEAT` frames to hub MAC
- Payload includes satellite uptime and RSSI of last hub frame
- **Source:** `ESP_Satellite/src/main.cpp`

---

## 6. Peer Discovery

- Hub broadcasts `MSG_DISCOVERY` (action=0 = scan) from broadcast MAC
- Satellites respond with `MSG_DISCOVERY` (action=1 = announce) unicast to hub MAC
- Satellites also auto-learn peer MACs from any received non-hub frame and persist to NVS
- **Source:** `ESP_Hub/src/CommandRouter.cpp::_handlePair()`, `ESP_Satellite/src/main.cpp`

---

## 7. Telemetry Flow (Detailed)

### MSG_DBG (individual entry)
```
TelemetryEntry_t (25 bytes):
  [0..15]  name    — null-terminated stream name
  [16]     vtype   — 0=int32, 1=float32, 2=bool, 3=string
  [17..20] value   — union: i32 / f32 / bool / str[8]
  [21..24] ts_ms   — millis() on satellite
```

### MSG_TELEM_BATCH (compact batch)
- Used for high-frequency telemetry
- Requires prior `MSG_TELEM_DICT` to establish stream_id→name mapping
- Batch payload: `count` + array of `TelemetryCompactValue_t` (stream_id + vtype + raw)
- **Source:** `ESP_Hub/src/CommandRouter.cpp`, `shared/messages.h`

---

## 8. P2P Bridge

- `MSG_UART_RAW` frames carry raw UART bytes between satellites
- Satellite forwards to peer satellite's UART when received
- Hub must **pass through** `MSG_UART_RAW` frames without modification
- **Source:** `ESP_Satellite/src/main.cpp`, `shared/messages.h`

---

## 9. Critical Timing Requirements

| Requirement | Value | Source |
|-------------|-------|--------|
| Heartbeat TX interval | 1000 ms ±100 ms | `HeartbeatService.cpp` |
| Peer offline timeout | 4000 ms | `PeerRegistry::tickTimeouts()` |
| ACK timeout | 500 ms | `protocol_v1.md` §8.2 |
| ACK max retries | 3 | `protocol_v1.md` §8.2 |
| Max telemetry rate | ≥40 Hz per satellite | migration target |
| WebSocket push rate | ≤50 Hz | `protocol_v1.md` §13 |

---

## 10. Network ID Anti-Mis-Pairing

- `network_id = 0x01` in every frame
- Satellite drops frames where `network_id != ESPNOW_NETWORK_ID` (= 0x01)
- Hub drops frames where `network_id != HUB_NETWORK_ID` (= 0x01)
- Legacy value 0x00 = accept-any (not used in production)
- **Source:** `ESP_Satellite/src/EspNowBridge.cpp::_onRecv()`, `ESP_Hub/src/EspNowManager.cpp::_onRecv()`

---

## 11. USB Bridge Wire Format (PC Side)

Frames on the USB CDC serial link use a 4-byte framing header:
```
[0]   0xAA   SOF byte 1
[1]   0x55   SOF byte 2
[2-3] uint16_t frame_len = 190 (little-endian)
[4+]  Frame_t bytes (190 bytes)
Total: 194 bytes per frame
```
**Source:** `protocol_v1.md` §2.2

---

## 12. Summary: What the PC Hub Must Replicate

| ESP Hub Component | PC Hub Equivalent | Notes |
|---|---|---|
| ESP-NOW radio RX/TX | USB bridge serial + SOF framing | Same Frame_t |
| Peer registry | `peer_tracker.py` | Same state machine |
| Heartbeat service | `heartbeat.py` | Same interval/timeout |
| ACK/retry (add new) | `ack_manager.py` | Not in ESP hub, new for PC |
| Command router | `command_router.py` | Same WebSocket JSON API |
| Telemetry buffer | SQLite + in-memory queue | Persistent, larger |
| WebSocket server | `api.py` (aiohttp) | Same message format |
| LittleFS UI | `hub_ui/index.html` | Enhanced with plotter |
| Config via LittleFS | `config/hub_config.yaml` | YAML + env overrides |
