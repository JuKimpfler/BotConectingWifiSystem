#pragma once
#include <stdint.h>

// ============================================================
//  shared/messages.h
//  ESP-NOW message framing: types, roles, flags, frame struct
//  Used by ESP_Hub, ESP_Satellite, and test harnesses
// ============================================================

// ── Protocol version ────────────────────────────────────────
#define PROTO_VERSION       0x01

// ── Role identifiers ────────────────────────────────────────
#define ROLE_HUB            0x00
#define ROLE_SAT1           0x01
#define ROLE_SAT2           0x02
#define ROLE_BROADCAST      0xFF

// ── Message types ───────────────────────────────────────────
#define MSG_DBG             0x01  // Debug / telemetry stream (no ACK)
#define MSG_CTRL            0x02  // Control command (speed/angle/buttons)
#define MSG_MODE            0x03  // Mode select (M1-M5, ACK required)
#define MSG_CAL             0x04  // Calibrate command (ACK required)
#define MSG_PAIR            0x05  // Pairing request/response (ACK required)
#define MSG_HEARTBEAT       0x06  // Keepalive (no ACK)
#define MSG_ACK             0x07  // Acknowledgement frame
#define MSG_ERROR           0x08  // Error response
#define MSG_SETTINGS        0x09  // Settings update (ACK required)
#define MSG_DISCOVERY       0x0A  // Peer discovery broadcast
#define MSG_UART_RAW        0x0B  // Transparent UART bridge data (SAT1 <-> SAT2, no ACK)
#define MSG_TELEM_DICT      0x0C  // Telemetry dictionary update (stream_id -> name)
#define MSG_TELEM_BATCH     0x0D  // Compact telemetry batch (ID + value pairs)

// ── Flag bits ───────────────────────────────────────────────
#define FLAG_ACK_REQ        0x01  // Sender requests ACK
#define FLAG_IS_RESPONSE    0x02  // This frame is a response
#define FLAG_PRIORITY       0x04  // High-priority delivery hint
#define FLAG_ENCRYPTED      0x08  // Payload is encrypted (future use)

// ── ACK status codes ────────────────────────────────────────
#define ACK_OK              0x00
#define ACK_ERR_UNKNOWN     0x01
#define ACK_ERR_BUSY        0x02
#define ACK_ERR_TIMEOUT     0x03
#define ACK_ERR_INVALID     0x04

// ── Frame layout ────────────────────────────────────────────
// Total header = 8 bytes; max payload = 180 bytes; CRC16 = 2 bytes
// Maximum frame size = 190 bytes (within ESP-NOW 250 B limit)
#define FRAME_HEADER_SIZE   8
#define FRAME_MAX_PAYLOAD   180
#define FRAME_MAGIC         0xBE  // Start-of-frame magic byte

// ── DBG UART prefix constants ─────────────────────────────────
// Unified debug prefix for telemetry lines from the Teensy.
// Lines with this prefix are sent as MSG_DBG to the hub.
// Lines without this prefix are forwarded as MSG_UART_RAW to the peer satellite
// (transparent UART bridge, SAT1 <-> SAT2).
#define DBG_PREFIX           "DBG:"
// Legacy aliases (kept for backward compatibility; use DBG_PREFIX in new code)
#define DBG_PREFIX_SAT1      "DBG1:"
#define DBG_PREFIX_SAT2      "DBG2:"

#pragma pack(push, 1)

typedef struct {
    uint8_t  magic;       // 0xBE – start-of-frame marker
    uint8_t  msg_type;    // MSG_* constant
    uint8_t  seq;         // Sequence number (rolls over at 255)
    uint8_t  src_role;    // ROLE_* of sender
    uint8_t  dst_role;    // ROLE_* of intended recipient
    uint8_t  flags;       // FLAG_* bitmask
    uint8_t  len;         // Payload length in bytes (0..180)
    uint8_t  network_id;  // Anti-mis-pairing: set to ESPNOW_NETWORK_ID / HUB_NETWORK_ID.
                          // 0x00 = legacy / accept-any; 0x01-0xFF = system-specific.
                          // Frames whose network_id doesn't match the receiver's are dropped.
    uint8_t  payload[FRAME_MAX_PAYLOAD];
    uint16_t crc16;       // CRC-16/IBM over bytes [0..header+payload-1]
} Frame_t;

// ── Compact DBG telemetry payload (stream entry) ────────────
#define TELEM_NAME_MAX_LEN       16
#define TELEM_BATCH_MAX_VALUES   16

typedef struct {
    char     name[TELEM_NAME_MAX_LEN];  // Stream name (null-terminated)
    uint8_t  vtype;       // 0=int32, 1=float, 2=bool, 3=string
    union {
        int32_t  i32;
        float    f32;
        uint8_t  b;
        char     str[8];
    } value;
    uint32_t ts_ms;       // Timestamp millis()
} TelemetryEntry_t;

typedef struct {
    uint8_t stream_id;     // 0..255 telemetry stream ID (per source role)
    char    name[TELEM_NAME_MAX_LEN];
} TelemetryDictPayload_t;

typedef struct {
    uint8_t stream_id;     // Stream ID previously announced via MSG_TELEM_DICT
    uint8_t vtype;         // 0=int32, 1=float, 2=bool
    int32_t raw;           // int32 value or float bit-pattern
} TelemetryCompactValue_t;

// ── CTRL payload ─────────────────────────────────────────────
typedef struct {
    int16_t  speed;       // Signed speed value
    int16_t  angle;       // Signed angle value
    uint8_t  switches;    // Bitmask: bit0=SW1, bit1=SW2, bit2=SW3
    uint8_t  buttons;     // Bitmask: bit0=B1..bit3=B4
    uint8_t  start;       // 0=off, 1=on
    uint8_t  target_role; // ROLE_SAT1 or ROLE_SAT2
} CtrlPayload_t;

// ── MODE payload ─────────────────────────────────────────────
typedef struct {
    uint8_t  mode_id;     // 1..5
    uint8_t  target_role;
} ModePayload_t;

// ── CAL payload ──────────────────────────────────────────────
#define CAL_IR_MAX    0x01
#define CAL_IR_MIN    0x02
#define CAL_LINE_MAX  0x03
#define CAL_LINE_MIN  0x04
#define CAL_BNO       0x05

typedef struct {
    uint8_t  cal_cmd;     // CAL_* constant
    uint8_t  target_role;
} CalPayload_t;

// ── HEARTBEAT payload ────────────────────────────────────────
typedef struct {
    uint32_t uptime_ms;
    int8_t   rssi;
    uint8_t  queue_len;
} HeartbeatPayload_t;

// ── ACK payload ──────────────────────────────────────────────
typedef struct {
    uint8_t  ack_seq;     // Sequence number being acknowledged
    uint8_t  status;      // ACK_* status code
    uint8_t  msg_type;    // Type of the acknowledged message
} AckPayload_t;

// ── PAIR payload ─────────────────────────────────────────────
typedef struct {
    uint8_t  action;      // 0=request, 1=response, 2=unpair
    uint8_t  role;        // Requested/assigned role
    char     name[16];    // Human-readable name
    uint8_t  mac[6];
} PairPayload_t;

// ── DISCOVERY payload ────────────────────────────────────────
typedef struct {
    uint8_t  action;      // 0=scan, 1=announce
    uint8_t  role;
    char     name[16];
    uint8_t  mac[6];
    uint8_t  channel;
} DiscoveryPayload_t;

// ── SETTINGS payload ─────────────────────────────────────────
typedef struct {
    uint8_t  channel;
    uint8_t  pmk[16];
    uint8_t  telemetry_rate_hz;
    uint8_t  flags;       // bit0=encrypt_en
} SettingsPayload_t;

#pragma pack(pop)
