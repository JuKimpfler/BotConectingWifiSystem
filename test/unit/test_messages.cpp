// ============================================================
//  test/unit/test_messages.cpp
//  Host-side unit tests for message framing and struct sizes
//  Build:  g++ -std=c++17 -I../../shared -o test_messages test_messages.cpp && ./test_messages
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>

#define millis() 0UL
#include "../../shared/messages.h"
#include "../../shared/crc16.h"

static int g_tests = 0;
static int g_pass  = 0;

#define CHECK(expr, msg) do { \
    g_tests++; \
    if (expr) { g_pass++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s  (line %d)\n", msg, __LINE__); } \
} while(0)

// Build a frame with CRC and verify it can be decoded
static bool buildAndVerify(uint8_t msgType, const uint8_t *payload, uint8_t payLen) {
    Frame_t frame = {};
    frame.magic    = FRAME_MAGIC;
    frame.msg_type = msgType;
    frame.seq      = 42;
    frame.src_role = ROLE_HUB;
    frame.dst_role = ROLE_SAT1;
    frame.flags    = FLAG_ACK_REQ;
    frame.len      = payLen;
    if (payLen > 0) memcpy(frame.payload, payload, payLen);

    uint16_t crc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + payLen);
    memcpy(frame.payload + payLen, &crc, 2);

    // Verify
    uint16_t calcCrc = crc16_buf((const uint8_t *)&frame, FRAME_HEADER_SIZE + frame.len);
    uint16_t storedCrc;
    memcpy(&storedCrc, frame.payload + frame.len, 2);
    return (calcCrc == storedCrc);
}

int main() {
    // Struct sizes – must be packed
    CHECK(FRAME_HEADER_SIZE == 8, "Frame header size = 8");
    CHECK(sizeof(Frame_t) >= (size_t)(FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + 2),
          "Frame_t covers header + max payload + CRC");

    CHECK(sizeof(CtrlPayload_t)      <=  FRAME_MAX_PAYLOAD, "CtrlPayload fits");
    CHECK(sizeof(ModePayload_t)      <=  FRAME_MAX_PAYLOAD, "ModePayload fits");
    CHECK(sizeof(CalPayload_t)       <=  FRAME_MAX_PAYLOAD, "CalPayload fits");
    CHECK(sizeof(HeartbeatPayload_t) <=  FRAME_MAX_PAYLOAD, "HeartbeatPayload fits");
    CHECK(sizeof(AckPayload_t)       <=  FRAME_MAX_PAYLOAD, "AckPayload fits");
    CHECK(sizeof(TelemetryEntry_t)   <=  FRAME_MAX_PAYLOAD, "TelemetryEntry fits");
    CHECK(sizeof(PairPayload_t)      <=  FRAME_MAX_PAYLOAD, "PairPayload fits");
    CHECK(sizeof(DiscoveryPayload_t) <=  FRAME_MAX_PAYLOAD, "DiscoveryPayload fits");
    CHECK(sizeof(SettingsPayload_t)  <=  FRAME_MAX_PAYLOAD, "SettingsPayload fits");

    // Frame total size stays within ESP-NOW 250 B limit
    uint16_t maxFrame = FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD + 2;
    CHECK(maxFrame <= 250, "Max frame <= 250 B (ESP-NOW limit)");

    // Build and verify frames
    ModePayload_t mode = {3, ROLE_SAT1};
    CHECK(buildAndVerify(MSG_MODE, (const uint8_t *)&mode, sizeof(mode)),
          "Mode frame CRC round-trip");

    CalPayload_t cal = {CAL_IR_MAX, ROLE_SAT1};
    CHECK(buildAndVerify(MSG_CAL, (const uint8_t *)&cal, sizeof(cal)),
          "Cal frame CRC round-trip");

    HeartbeatPayload_t hb = {12345, -60, 0};
    CHECK(buildAndVerify(MSG_HEARTBEAT, (const uint8_t *)&hb, sizeof(hb)),
          "Heartbeat frame CRC round-trip");

    // Flag constants
    CHECK((FLAG_ACK_REQ & 0xFF) != 0,   "FLAG_ACK_REQ non-zero");
    CHECK((FLAG_IS_RESPONSE & 0xFF) != 0, "FLAG_IS_RESPONSE non-zero");

    // Role constants
    CHECK(ROLE_HUB  == 0, "ROLE_HUB = 0");
    CHECK(ROLE_SAT1 == 1, "ROLE_SAT1 = 1");
    CHECK(ROLE_SAT2 == 2, "ROLE_SAT2 = 2");

    printf("\nResult: %d/%d tests passed\n", g_pass, g_tests);
    return (g_pass == g_tests) ? 0 : 1;
}
