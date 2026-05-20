#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <cstddef>
#endif

// ── Protocol constants ─────────────────────────────────────────
#define PROTO_VERSION 0x01

#define FRAME_MAGIC        0xBE
#define FRAME_HEADER_SIZE  8
#define FRAME_MAX_PAYLOAD  180
#define FRAME_SIZE         (FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + 2)

// ── Roles ──────────────────────────────────────────────────────
#define ROLE_HUB       0x00
#define ROLE_SAT1      0x01
#define ROLE_SAT2      0x02
#define ROLE_BROADCAST 0xFF

// ── Message Types ──────────────────────────────────────────────
#define MSG_DBG         0x01
#define MSG_CTRL        0x02
#define MSG_MODE        0x03
#define MSG_CAL         0x04
#define MSG_HEARTBEAT   0x06
#define MSG_ACK         0x07
#define MSG_DISCOVERY   0x0A
#define MSG_UART_RAW    0x0B
#define MSG_TELEM_DICT  0x0C
#define MSG_TELEM_BATCH 0x0D

// ── Flags ──────────────────────────────────────────────────────
#define FLAG_ACK_REQ     0x01
#define FLAG_IS_RESPONSE 0x02
#define FLAG_ENCRYPTED   0x04

// ── Calibration commands ───────────────────────────────────────
#define CAL_IR_MAX   0x01
#define CAL_IR_MIN   0x02
#define CAL_LINE_MAX 0x03
#define CAL_LINE_MIN 0x04
#define CAL_BNO      0x05

// ── ACK status codes ───────────────────────────────────────────
#define ACK_OK        0x00
#define ACK_REJECTED  0x01
#define ACK_BUSY      0x02

// ── Telemetry value types ─────────────────────────────────────
#define TELEM_INT    0
#define TELEM_FLOAT  1
#define TELEM_BOOL   2
#define TELEM_STRING 3

// DBG prefix expected on UART telemetry lines
#define DBG_PREFIX       "DBG:"
#define DBG_PREFIX_SAT1  "DBG1:"
#define DBG_PREFIX_SAT2  "DBG2:"

#pragma pack(push, 1)

typedef struct {
    uint8_t magic;
    uint8_t msg_type;
    uint8_t seq;
    uint8_t src_role;
    uint8_t dst_role;
    uint8_t flags;
    uint8_t len;
    uint8_t network_id;
    uint8_t payload[FRAME_MAX_PAYLOAD + 2];
} Frame_t;

typedef struct {
    int16_t speed;
    int16_t angle;
    uint8_t switches;
    uint8_t buttons;
    uint8_t start;
    uint8_t target_role;
} CtrlPayload_t;

typedef struct {
    uint8_t mode_id;
    uint8_t target_role;
} ModePayload_t;

typedef struct {
    uint8_t cal_cmd;
    uint8_t target_role;
} CalPayload_t;

typedef struct {
    uint32_t uptime_ms;
    int8_t rssi;
    uint8_t queue_len;
} HeartbeatPayload_t;

typedef struct {
    uint8_t ack_seq;
    uint8_t status;
    uint8_t msg_type;
} AckPayload_t;

typedef struct {
    char name[16];
    uint8_t vtype;
    union {
        int32_t i32;
        float f32;
        uint8_t b;
        char str[8];
        uint8_t raw[8];
    } value;
    uint32_t ts_ms;
} TelemetryEntry_t;

typedef struct {
    uint8_t id;
    uint8_t vtype;
    union {
        int32_t i32;
        float f32;
        uint8_t b;
        char str[8];
        uint8_t raw[8];
    } value;
    uint32_t ts_ms;
} TelemetryCompactValue_t;

#define TELEM_BATCH_MAX_VALUES 12
#define TELEM_DICT_MAX_ENTRIES 8

typedef struct {
    uint8_t id;
    char name[16];
} TelemetryDictEntry_t;

typedef struct {
    uint8_t count;
    TelemetryDictEntry_t entries[TELEM_DICT_MAX_ENTRIES];
} TelemetryDictPayload_t;

typedef struct {
    uint8_t role;
    uint8_t mac[6];
} PairPayload_t;

typedef struct {
    uint8_t action;   // 0=request, 1=announce
    uint8_t role;
    uint8_t channel;
    char name[16];
    uint8_t mac[6];
} DiscoveryPayload_t;

typedef struct {
    uint8_t key;
    uint8_t value;
} SettingsPayload_t;

#pragma pack(pop)
