// ============================================================
//  test/unit/test_command_parser.cpp
//  Host-side unit tests for CommandParser (no Arduino deps)
//  Build: g++ -std=c++17 -I../../shared -I../../ESP_Satellite/include
//              -DSAT_ID=1 -o test_cp test_command_parser.cpp
//              ../../ESP_Satellite/src/CommandParser.cpp && ./test_cp
// ============================================================

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Satisfy includes that pull in crc16.h millis stub
#include "../../shared/messages.h"
#include "../../shared/crc16.h"
#include "../../ESP_Satellite/include/sat_config.h"
#include "../../ESP_Satellite/include/CommandParser.h"

namespace {
int g_tests = 0, g_pass = 0;
}

#define CHECK(expr, msg) do { \
    g_tests++; \
    if (expr) { g_pass++; printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s (line %d)\n", msg, __LINE__); } \
} while(0)

int main() {
    CommandParser p;
    char outBuf[256];

    // ── hubFrameToUart: CTRL ──────────────────────────────────
    CtrlPayload_t ctrl = {200, -45, 3, 0, 1, ROLE_SAT1};
    Frame_t ctrlFrame = {};
    ctrlFrame.magic    = FRAME_MAGIC;
    ctrlFrame.msg_type = MSG_CTRL;
    ctrlFrame.len      = sizeof(ctrl);
    memcpy(ctrlFrame.payload, &ctrl, sizeof(ctrl));

    int n = p.hubFrameToUart(&ctrlFrame, outBuf, sizeof(outBuf));
    CHECK(n > 0, "hubFrameToUart CTRL produces output");
    CHECK(strstr(outBuf, "V200") != nullptr, "CTRL output contains V200");
    CHECK(strstr(outBuf, "A-45") != nullptr, "CTRL output contains A-45");
    CHECK(strstr(outBuf, "START1") != nullptr, "CTRL output contains START1");

    // ── hubFrameToUart: MODE ──────────────────────────────────
    ModePayload_t mode = {3, ROLE_SAT1};
    Frame_t modeFrame = {};
    modeFrame.msg_type = MSG_MODE;
    modeFrame.len      = sizeof(mode);
    memcpy(modeFrame.payload, &mode, sizeof(mode));

    n = p.hubFrameToUart(&modeFrame, outBuf, sizeof(outBuf));
    CHECK(n > 0, "hubFrameToUart MODE produces output");
    CHECK(strstr(outBuf, "M3") != nullptr, "MODE output is 'M3'");

    // ── hubFrameToUart: CAL ───────────────────────────────────
    CalPayload_t cal = {CAL_IR_MAX, ROLE_SAT1};
    Frame_t calFrame = {};
    calFrame.msg_type = MSG_CAL;
    calFrame.len      = sizeof(cal);
    memcpy(calFrame.payload, &cal, sizeof(cal));

    n = p.hubFrameToUart(&calFrame, outBuf, sizeof(outBuf));
    CHECK(n > 0, "hubFrameToUart CAL produces output");
    CHECK(strstr(outBuf, "CAL_IR_MAX") != nullptr, "CAL output is 'CAL_IR_MAX'");

    // ── hubFrameToUart: CAL invalid cmd produces empty string ─
    CalPayload_t calInvalid = {0, ROLE_SAT1};
    memcpy(calFrame.payload, &calInvalid, sizeof(calInvalid));
    n = p.hubFrameToUart(&calFrame, outBuf, sizeof(outBuf));
    CHECK(n == 0, "CAL invalid cmd (0) produces no output");
    CHECK(outBuf[0] == '\0', "CAL invalid cmd (0) buffer is empty");

    CalPayload_t calInvalid2 = {CAL_BNO + 1, ROLE_SAT1};
    memcpy(calFrame.payload, &calInvalid2, sizeof(calInvalid2));
    n = p.hubFrameToUart(&calFrame, outBuf, sizeof(outBuf));
    CHECK(n == 0, "CAL out-of-range cmd produces no output");
    CHECK(outBuf[0] == '\0', "CAL out-of-range cmd buffer is empty");

    // ── uartLineToFrame: int telemetry ────────────────────────
    Frame_t tFrame;
    bool ok = p.uartLineToFrame("DBG1:Speed=200", 1, &tFrame);
    CHECK(ok, "uartLineToFrame parses DBG1: line");
    CHECK(tFrame.msg_type == MSG_DBG, "Telemetry frame type = MSG_DBG");
    CHECK(tFrame.src_role == ROLE_SAT1, "Telemetry src_role = SAT1");
    const TelemetryEntry_t *te =
        reinterpret_cast<const TelemetryEntry_t *>(tFrame.payload);
    CHECK(strcmp(te->name, "Speed") == 0, "Telemetry name = 'Speed'");
    CHECK(te->vtype == 0, "Telemetry type = int");
    CHECK(te->value.i32 == 200, "Telemetry value = 200");

    // ── uartLineToFrame: float telemetry ─────────────────────
    ok = p.uartLineToFrame("DBG2:Angle=3.1416", 2, &tFrame);
    CHECK(ok, "uartLineToFrame parses DBG2: float line");
    CHECK(tFrame.src_role == ROLE_SAT2, "Telemetry src_role = SAT2");
    const TelemetryEntry_t *tf =
        reinterpret_cast<const TelemetryEntry_t *>(tFrame.payload);
    CHECK(tf->vtype == 1, "Float telemetry type = 1");
    CHECK(tf->value.f32 > 3.1f && tf->value.f32 < 3.2f, "Float value ~3.14");

    // ── uartLineToFrame: wrong prefix ────────────────────────
    ok = p.uartLineToFrame("RAW:blah", 1, &tFrame);
    CHECK(!ok, "uartLineToFrame rejects wrong prefix");

    printf("\nResult: %d/%d tests passed\n", g_pass, g_tests);
    return (g_pass == g_tests) ? 0 : 1;
}
