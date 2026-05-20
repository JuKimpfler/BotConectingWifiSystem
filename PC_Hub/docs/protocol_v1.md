# BCWS PC-Hub Protocol Specification — Version 1 (protocol_v1)

> **Status:** Frozen for Migration Period  
> **Applies to:** PC Hub ↔ ESP Satellite communication  
> **Compatible firmware:** ESP_Satellite (any revision with `ESPNOW_NETWORK_ID = 0x01`)  
> **Author note:** This document is agent-optimised — every section is self-contained and machine-readable.

---

## 1. Overview

The BCWS protocol is a binary framing layer transmitted over ESP-NOW between the hub and up to two satellite robots, each managed locally by a Teensy 4.0 via UART.

The PC Hub replaces the ESP32-C3 hub while preserving:
- Full binary frame compatibility (same `Frame_t` structure)
- CRC-16/IBM (MODBUS) integrity checking
- Network ID anti-mis-pairing (`network_id = 0x01`)
- ACK/retry semantics for reliable commands
- Heartbeat keepalive with 4 s timeout
- Transparent P2P bridge capability between satellites

Transport on the PC side is bridged via a **USB–Serial ESP-NOW bridge** device (a minimal ESP32 acting purely as a radio adapter), communicating with the PC over USB CDC at 921600 baud using the same binary frame framing, with a two-byte SOF prefix.

---

## 2. Physical / Transport Layer

### 2.1 ESP-NOW Radio Layer (Satellite Side)

| Parameter | Value |
|---|---|
| Standard | IEEE 802.11 (ESP-NOW, Espressif proprietary) |
| Frequency | 2.4 GHz |
| Channel | 6 (default, configurable 1–13) |
| Max frame size | 250 bytes |
| Used frame size | 190 bytes |
| Network isolation key | `network_id = 0x01` |

### 2.2 PC Bridge Layer (USB CDC Serial)

The PC receives/sends radio frames via a minimal ESP32 bridge device (e.g. XIAO ESP32-C3) acting as a transparent USB-to-ESP-NOW adapter.

| Parameter | Value |
|---|---|
| Interface | USB CDC (virtual COM port) |
| Baud rate | 921600 |
| Flow control | None |
| Framing | SOF `0xAA 0x55` + 2-byte little-endian length + raw `Frame_t` bytes |
| Direction | Bidirectional |

#### 2.2.1 PC Bridge Wire Format

```
Byte 0:   0xAA  (SOF marker 1)
Byte 1:   0x55  (SOF marker 2)
Byte 2-3: uint16_t frame_len  (little-endian, = sizeof(Frame_t) = 190)
Byte 4+:  raw Frame_t bytes   (190 bytes)
Total:    194 bytes per frame
```

The bridge firmware simply forwards every `Frame_t` it receives over ESP-NOW to the PC, and every `Frame_t` the PC sends is transmitted via ESP-NOW to the destination MAC.

---

## 3. Binary Frame Format

Defined in `shared/messages.h` (do not modify during migration).

```
Offset  Size  Field        Description
------  ----  -----------  --------------------------------------------------
0       1     magic        Always 0xBE
1       1     msg_type     Message type constant (see §4)
2       1     seq          Sequence number 0–255, wraps around
3       1     src_role     Sender role (see §5)
4       1     dst_role     Destination role (see §5)
5       1     flags        Bitmask (see §6)
6       1     len          Payload byte count (0–180)
7       1     network_id   0x01 for this system (anti-mis-pairing)
8       180   payload      Message-type-specific content (see §7)
188     2     crc16        CRC-16/IBM over bytes 0–187 (little-endian)
------  ----  -----------
Total:  190 bytes
```

### 3.1 CRC Calculation

Algorithm: **CRC-16/IBM** (also called CRC-16/MODBUS)

```
Polynomial: 0xA001 (reflected)
Initial:    0xFFFF
Input ref:  true
Output ref: true
XOR out:    0x0000
```

CRC is computed over all bytes from offset 0 to offset 187 inclusive (header + payload, **excluding** the crc16 field itself).

#### Test Vector A
```
Frame (hex, first 8 bytes only, payload=0x00 x 180):
  BE 01 00 00 FF 00 00 01  [00 x 180]
Expected CRC: 0x5ECB  (stored as CB 5E at offsets 188–189)
```

#### Test Vector B (Heartbeat, len=6)
```
magic=0xBE, msg_type=0x06, seq=0x00, src_role=0x00, dst_role=0xFF,
flags=0x00, len=0x06, network_id=0x01
payload: [00 00 00 64  D8  00  00 00 ... (174 zero bytes)]
         uptime_ms=100 (0x00000064 LE), rssi=-40 (0xD8), queue_len=0
CRC over 188 bytes → compute at runtime (see §9 test harness)
```

---

## 4. Message Types

| Constant | Value | Direction | ACK Required | Description |
|---|---|---|---|---|
| `MSG_DBG` | 0x01 | SAT → HUB | No | Telemetry stream entry |
| `MSG_CTRL` | 0x02 | HUB → SAT | Yes | Manual speed/angle control |
| `MSG_MODE` | 0x03 | HUB → SAT | Yes | Mode selection (1–5) |
| `MSG_CAL` | 0x04 | HUB → SAT | Yes | Calibration command |
| `MSG_HEARTBEAT` | 0x06 | HUB ↔ SAT | No | Keepalive |
| `MSG_ACK` | 0x07 | SAT → HUB | No | ACK for reliable commands |
| `MSG_DISCOVERY` | 0x0A | Broadcast | No | Peer discovery |
| `MSG_UART_RAW` | 0x0B | HUB/SAT → SAT | No | Transparent P2P bridge |
| `MSG_TELEM_BATCH` | 0x0D | SAT → HUB | No | Compact multi-value telemetry |

---

## 5. Roles

| Constant | Value | Description |
|---|---|---|
| `ROLE_HUB` | 0x00 | Hub (PC or ESP) |
| `ROLE_SAT1` | 0x01 | Satellite 1 |
| `ROLE_SAT2` | 0x02 | Satellite 2 |
| `ROLE_BROADCAST` | 0xFF | All devices |

---

## 6. Flags Bitmask

| Bit | Mask | Name | Meaning |
|---|---|---|---|
| 0 | 0x01 | `FLAG_ACK_REQ` | Sender requests ACK |
| 1 | 0x02 | `FLAG_IS_RESPONSE` | This frame is a response (ACK/NAK) |
| 2 | 0x04 | `FLAG_ENCRYPTED` | Payload encrypted (reserved, not used in v1) |
| 3–7 | — | Reserved | Must be 0 |

---

## 7. Payload Schemas

### 7.1 MSG_DBG — Telemetry Entry (src: SAT, dst: HUB)

```
Offset  Size  Type    Field      Description
0       16    char[]  name       Null-terminated stream name (e.g. "Speed")
16      1     uint8   vtype      Value type: 0=int32, 1=float32, 2=bool, 3=string
17      4     union   value      int32/float32/bool/string[8] depending on vtype
21      4     uint32  ts_ms      millis() timestamp on satellite (little-endian)
25+     0     —       (padding to len)
```

**Value union detail:**
- `vtype=0`: bytes 17–20 = int32_t little-endian
- `vtype=1`: bytes 17–20 = IEEE 754 float32 little-endian
- `vtype=2`: byte 17 = uint8 (0=false, 1=true); bytes 18–20 unused
- `vtype=3`: bytes 17–24 = null-terminated string (max 8 bytes)

**Example decoded:**
```json
{ "name": "Speed", "vtype": 0, "value": 1200, "ts_ms": 5420 }
```

### 7.2 MSG_CTRL — Control Command (src: HUB, dst: SAT1|SAT2)

```
Offset  Size  Type    Field        Description
0       2     int16   speed        Signed speed (-32768..32767)
2       2     int16   angle        Signed angle (-32768..32767)
4       1     uint8   switches     Bitmask: bit0=SW1, bit1=SW2, bit2=SW3
5       1     uint8   buttons      Bitmask: bit0=B1..bit3=B4
6       1     uint8   start        0=off, 1=on
7       1     uint8   target_role  ROLE_SAT1 or ROLE_SAT2
```

Flags: `FLAG_ACK_REQ = 1`

### 7.3 MSG_MODE — Mode Select (src: HUB, dst: SAT)

```
Offset  Size  Type    Field        Description
0       1     uint8   mode_id      Mode 1..5
1       1     uint8   target_role  ROLE_SAT1 or ROLE_SAT2
```

Flags: `FLAG_ACK_REQ = 1`

### 7.4 MSG_CAL — Calibration (src: HUB, dst: SAT)

```
Offset  Size  Type    Field        Description
0       1     uint8   cal_cmd      0x01=IR_MAX, 0x02=IR_MIN, 0x03=LINE_MAX,
                                   0x04=LINE_MIN, 0x05=BNO
1       1     uint8   target_role  ROLE_SAT1 or ROLE_SAT2
```

Flags: `FLAG_ACK_REQ = 1`

Cal command constants:
| Constant | Value | Meaning |
|---|---|---|
| `CAL_IR_MAX` | 0x01 | IR sensor max calibration |
| `CAL_IR_MIN` | 0x02 | IR sensor min calibration |
| `CAL_LINE_MAX` | 0x03 | Line sensor max calibration |
| `CAL_LINE_MIN` | 0x04 | Line sensor min calibration |
| `CAL_BNO` | 0x05 | BNO IMU calibration |

### 7.5 MSG_HEARTBEAT — Keepalive (bidirectional)

```
Offset  Size  Type    Field       Description
0       4     uint32  uptime_ms   Sender uptime in milliseconds (little-endian)
4       1     int8    rssi        Last received RSSI in dBm (0 from hub on TX)
5       1     uint8   queue_len   Number of pending outbound frames
```

No ACK required. Sent every 1000 ms. Peer offline if no heartbeat for 4000 ms.

### 7.6 MSG_ACK — Acknowledgement (src: SAT, dst: HUB)

```
Offset  Size  Type    Field        Description
0       1     uint8   acked_seq    Sequence number being acknowledged
1       1     uint8   acked_type   msg_type being acknowledged
2       1     uint8   status       0=OK, 1=REJECTED, 2=BUSY
```

Flags: `FLAG_IS_RESPONSE = 1`

### 7.7 MSG_DISCOVERY — Peer Discovery (broadcast)

```
Offset  Size  Type    Field    Description
0       1     uint8   role     Role of sender
1       6     uint8[] mac      MAC address of sender (6 bytes)
```

Sent as broadcast. No ACK. Satellites use this to auto-register peer MACs.

### 7.8 MSG_UART_RAW — Transparent P2P Bridge (src: SAT, dst: SAT)

```
Offset  Size  Type    Field    Description
0       N     char[]  data     Raw UART bytes (not null-terminated, len field gives count)
```

No ACK. Max 180 bytes payload. Forwarded verbatim to peer satellite's UART.

### 7.9 MSG_TELEM_BATCH — Compact Telemetry Batch (src: SAT, dst: HUB)

```
Offset  Size  Type    Field    Description
0       1     uint8   count    Number of TelemetryEntry_t entries (1..N)
1+      25×N  struct  entries  Array of TelemetryEntry_t (see §7.1 layout)
```

Entries are packed with no padding. Max `floor(180/25) = 7` entries per batch.

---

## 8. Command / ACK Pattern

### 8.1 Reliable Command Flow

```
PC Hub                              Satellite
   |                                   |
   |--- Frame(seq=N, FLAG_ACK_REQ) --->|
   |                                   |  (processes command)
   |<-- Frame(MSG_ACK, acked_seq=N) ---|
   |  [success]                        |
```

### 8.2 Retry on Timeout

```
PC Hub                              Satellite
   |                                   |
   |--- Frame(seq=N, FLAG_ACK_REQ) --->|
   |   [timeout 500ms, no ACK]         |
   |--- Frame(seq=N, FLAG_ACK_REQ) --->|  (retry 1)
   |   [timeout 500ms, no ACK]         |
   |--- Frame(seq=N, FLAG_ACK_REQ) --->|  (retry 2)
   |   [timeout 500ms, no ACK]         |
   |   [give up, log COMMAND_FAILED]   |
```

Retry parameters:
| Parameter | Value |
|---|---|
| `ACK_TIMEOUT_MS` | 500 |
| `ACK_MAX_RETRIES` | 3 |
| Total max wait | ~1500 ms |

### 8.3 Duplicate Detection

Satellites track the last accepted `seq` per source role. If a retransmitted frame with the same `seq` and `msg_type` is received, the satellite re-sends ACK but does not re-execute the command.

---

## 9. Error Handling

### 9.1 Frame Validation Checklist (PC Hub must reject if any fails)

1. Frame length == 190 bytes
2. `magic == 0xBE`
3. `network_id == 0x01`
4. CRC-16 over bytes 0–187 matches bytes 188–189
5. `len <= 180`
6. `msg_type` is a known constant
7. `src_role` is valid (0x00, 0x01, 0x02, 0xFF)
8. `dst_role` is valid (0x00, 0x01, 0x02, 0xFF)

Frames failing any check: **silently discard** and increment per-type error counter in diagnostics.

### 9.2 Error Counter Registry

The PC Hub must maintain counters per satellite per session:

| Counter | Trigger |
|---|---|
| `rx_crc_errors` | CRC mismatch |
| `rx_magic_errors` | Wrong magic byte |
| `rx_network_id_errors` | network_id mismatch |
| `rx_unknown_type` | Unknown msg_type |
| `rx_len_overflow` | len > 180 |
| `tx_ack_timeouts` | ACK not received within timeout |
| `tx_ack_retries` | Retry count > 0 |
| `tx_command_failures` | All retries exhausted |

---

## 10. Versioning

### 10.1 Protocol Version Field

`PROTO_VERSION = 0x01` is defined in `shared/messages.h`. This field is **not** in the frame header in v1 (implicit). Future versions must add it to the frame.

### 10.2 Version Bump Trigger Conditions

A `protocol_v2` must be created when any of the following change:
- Frame header layout (offsets, sizes)
- CRC algorithm
- Any payload schema

### 10.3 Backward Compatibility Rules

- PC Hub v1 MUST reject frames from unknown `network_id` values.
- PC Hub v1 SHOULD log unknown `msg_type` values and discard them.
- Adding new `msg_type` values that are ignored by old firmware is allowed without a version bump.

---

## 11. Heartbeat & Peer State Machine

```
States: OFFLINE → CONNECTING → ONLINE → OFFLINE

OFFLINE    → CONNECTING : First frame received from peer
CONNECTING → ONLINE     : Heartbeat received within timeout
ONLINE     → OFFLINE    : No heartbeat for HEARTBEAT_TIMEOUT_MS (4000ms)
ONLINE     → ONLINE     : Heartbeat received (timer reset)
```

PC Hub must emit `peer_status` WebSocket event on every state transition.

---

## 12. WebSocket API (PC Hub → Browser/Clients)

The PC Hub exposes a WebSocket endpoint at `ws://localhost:<port>/ws` (default port: 8765).

All messages are JSON. Each message has a `"type"` field.

### 12.1 Hub → Client Messages

#### 12.1.1 telemetry

```json
{
  "type": "telemetry",
  "sat_id": "SAT1",
  "name": "Speed",
  "vtype": 0,
  "value": 1200,
  "ts_ms": 5420,
  "rx_ts": 1712140000.123
}
```

`rx_ts` is the PC wall-clock time (Unix epoch float) when the frame was received.

#### 12.1.2 peer_status

```json
{
  "type": "peer_status",
  "peers": [
    {
      "sat_id": "SAT1",
      "role": 1,
      "mac": "AA:BB:CC:DD:EE:FF",
      "online": true,
      "uptime_ms": 123456,
      "rssi": -55,
      "queue_len": 0,
      "last_seen": 1712140000.0
    }
  ]
}
```

#### 12.1.3 command_ack

```json
{
  "type": "command_ack",
  "sat_id": "SAT1",
  "cmd_type": "ctrl",
  "seq": 42,
  "status": "ok",
  "retries": 0
}
```

`status` values: `"ok"`, `"rejected"`, `"busy"`, `"timeout"`

#### 12.1.4 hub_status

```json
{
  "type": "hub_status",
  "uptime_s": 300,
  "rx_frames": 12050,
  "tx_frames": 600,
  "errors": {
    "SAT1": { "rx_crc_errors": 0, "tx_ack_timeouts": 1 },
    "SAT2": { "rx_crc_errors": 0, "tx_ack_timeouts": 0 }
  }
}
```

### 12.2 Client → Hub Messages

#### 12.2.1 ctrl

```json
{
  "type": "ctrl",
  "sat_id": "SAT1",
  "speed": 100,
  "angle": 45,
  "switches": 3,
  "buttons": 1,
  "start": 1
}
```

#### 12.2.2 mode

```json
{
  "type": "mode",
  "sat_id": "SAT1",
  "mode_id": 2
}
```

#### 12.2.3 cal

```json
{
  "type": "cal",
  "sat_id": "SAT1",
  "cal_cmd": 1
}
```

#### 12.2.4 subscribe

```json
{
  "type": "subscribe",
  "streams": ["SAT1/Speed", "SAT2/Angle"],
  "rate_limit_hz": 20
}
```

---

## 13. Timing Requirements

| Metric | Target | Hard Limit |
|---|---|---|
| Ingress → WebSocket latency | < 20 ms | < 100 ms |
| Heartbeat interval (hub TX) | 1000 ms | ±100 ms jitter |
| Peer offline detection | 4000 ms | 4500 ms |
| ACK timeout | 500 ms | — |
| Command retry interval | 500 ms | — |
| Max telemetry ingest rate | ≥ 20 Hz per satellite | — |
| WebSocket frame rate (UI) | ≤ 50 Hz (configurable) | — |

---

## 14. Test Vectors

### 14.1 Heartbeat Frame (binary, 190 bytes)

Used to verify bridge framing, CRC, and parser correctness.

```
magic=0xBE, msg_type=0x06, seq=0x01, src_role=0x00, dst_role=0xFF,
flags=0x00, len=0x06, network_id=0x01
payload[0..5]: 64 00 00 00  D8  00   (uptime_ms=100, rssi=-40, queue_len=0)
payload[6..179]: 0x00 (174 bytes)
crc16: computed over bytes 0..187
```

Hex (first 12 bytes): `BE 06 01 00 FF 00 06 01 64 00 00 00`

### 14.2 Control Frame (MSG_CTRL)

```
magic=0xBE, msg_type=0x02, seq=0x05, src_role=0x00, dst_role=0x01,
flags=0x01 (ACK_REQ), len=0x08, network_id=0x01
payload[0..7]:
  speed=150  → 96 00  (int16 LE)
  angle=-45  → D3 FF  (int16 LE)
  switches=0x03
  buttons=0x01
  start=0x01
  target_role=0x01
payload[8..179]: 0x00
```

### 14.3 ACK Frame (MSG_ACK)

```
magic=0xBE, msg_type=0x07, seq=0x10, src_role=0x01, dst_role=0x00,
flags=0x02 (IS_RESPONSE), len=0x03, network_id=0x01
payload[0..2]: 05 02 00  (acked_seq=5, acked_type=MSG_CTRL, status=OK)
payload[3..179]: 0x00
```

### 14.4 Telemetry (MSG_DBG)

```
magic=0xBE, msg_type=0x01, seq=0x2A, src_role=0x01, dst_role=0x00,
flags=0x00, len=0x19, network_id=0x01
payload (25 bytes):
  name: 53 70 65 65 64 00 [10 zero bytes]  ("Speed\0" + padding to 16)
  vtype: 00  (int32)
  value: B0 04 00 00  (1200 LE)
  ts_ms: 6C 15 00 00  (5484 LE)
payload[25..179]: 0x00
```

---

## 15. Appendix: Role / Type Quick Reference

```python
# Python constants matching shared/messages.h
PROTO_VERSION  = 0x01
FRAME_MAGIC    = 0xBE
FRAME_SIZE     = 190
PAYLOAD_OFFSET = 8
PAYLOAD_MAX    = 180

MSG_DBG        = 0x01
MSG_CTRL       = 0x02
MSG_MODE       = 0x03
MSG_CAL        = 0x04
MSG_HEARTBEAT  = 0x06
MSG_ACK        = 0x07
MSG_DISCOVERY  = 0x0A
MSG_UART_RAW   = 0x0B
MSG_TELEM_BATCH= 0x0D

ROLE_HUB       = 0x00
ROLE_SAT1      = 0x01
ROLE_SAT2      = 0x02
ROLE_BROADCAST = 0xFF

FLAG_ACK_REQ     = 0x01
FLAG_IS_RESPONSE = 0x02

NETWORK_ID     = 0x01

ACK_TIMEOUT_MS = 500
ACK_MAX_RETRIES = 3
HEARTBEAT_INTERVAL_MS = 1000
HEARTBEAT_TIMEOUT_MS  = 4000
```
