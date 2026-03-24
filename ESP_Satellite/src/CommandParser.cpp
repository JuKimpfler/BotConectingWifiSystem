// ============================================================
//  ESP_Satellite/src/CommandParser.cpp
//  Translates hub frames <-> Teensy UART strings
// ============================================================

#include "CommandParser.h"
#include "crc16.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef ARDUINO
static inline unsigned long _millis_stub() { return 0; }
#undef millis
#define millis() _millis_stub()
#endif

// ─── Hub frame → Teensy UART string ──────────────────────────
int CommandParser::hubFrameToUart(const Frame_t *frame, char *outBuf, int maxLen) {
    switch (frame->msg_type) {
    case MSG_CTRL: {
        const CtrlPayload_t *ctrl = reinterpret_cast<const CtrlPayload_t *>(frame->payload);
        _buildCtrlCmd(ctrl, outBuf, maxLen);
        return strnlen(outBuf, maxLen);
    }
    case MSG_MODE: {
        const ModePayload_t *mode = reinterpret_cast<const ModePayload_t *>(frame->payload);
        _buildModeCmd(mode, outBuf, maxLen);
        return strnlen(outBuf, maxLen);
    }
    case MSG_CAL: {
        const CalPayload_t *cal = reinterpret_cast<const CalPayload_t *>(frame->payload);
        _buildCalCmd(cal, outBuf, maxLen);
        return strnlen(outBuf, maxLen);
    }
    default:
        return 0;
    }
}

// ─── Teensy UART line → hub telemetry frame ──────────────────
bool CommandParser::uartLineToFrame(const char *line, uint8_t satId,
                                    Frame_t *outFrame) {
    TelemetryEntry_t entry = {};
    if (!uartLineToEntry(line, &entry)) return false;

    // Build frame
    memset(outFrame, 0, sizeof(Frame_t));
    outFrame->magic    = FRAME_MAGIC;
    outFrame->msg_type = MSG_DBG;
    outFrame->seq      = 0; // Caller sets seq
    outFrame->src_role = (satId == 1) ? ROLE_SAT1 : ROLE_SAT2;
    outFrame->dst_role = ROLE_HUB;
    outFrame->flags    = 0;
    outFrame->len      = sizeof(TelemetryEntry_t);
    memcpy(outFrame->payload, &entry, sizeof(entry));

    uint16_t crc = crc16_buf((const uint8_t *)outFrame, FRAME_HEADER_SIZE + outFrame->len);
    memcpy(outFrame->payload + outFrame->len, &crc, 2);

    return true;
}

bool CommandParser::uartLineToEntry(const char *line, TelemetryEntry_t *outEntry) {
    if (!line || !outEntry) return false;
    // Lines from Teensy with DBG: prefix carry telemetry data
    // Format: "DBG:<name>=<value>"
    const char *prefix = DBG_PREFIX;
    size_t prefixLen = strlen(prefix);

    if (strncmp(line, prefix, prefixLen) != 0) return false;
    const char *payload = line + prefixLen;

    // Parse "name=value"
    const char *eq = strchr(payload, '=');
    if (!eq) return false;

    size_t nameLen = eq - payload;
    if (nameLen == 0) return false;  // reject empty names
    if (nameLen >= sizeof(outEntry->name)) nameLen = sizeof(outEntry->name) - 1;
    memset(outEntry, 0, sizeof(TelemetryEntry_t));
    memcpy(outEntry->name, payload, nameLen);
    outEntry->name[nameLen] = '\0';

    const char *valStr = eq + 1;
    // Try int, then float
    char *end = nullptr;
    long ival = strtol(valStr, &end, 10);
    if (end != valStr && (*end == '\0' || *end == '\r' || *end == '\n')) {
        outEntry->vtype    = 0;
        outEntry->value.i32 = (int32_t)ival;
    } else {
        float fval = strtof(valStr, &end);
        outEntry->vtype    = 1;
        outEntry->value.f32 = fval;
    }
    outEntry->ts_ms = millis();
    return true;
}

// ─── Private: build Teensy command strings ────────────────────
void CommandParser::_buildCtrlCmd(const CtrlPayload_t *ctrl, char *buf, int max) {
    snprintf(buf, max, "V%dA%dSW%dBTN%dSTART%d\n",
             ctrl->speed, ctrl->angle,
             ctrl->switches, ctrl->buttons,
             ctrl->start);
}

void CommandParser::_buildModeCmd(const ModePayload_t *mode, char *buf, int max) {
    snprintf(buf, max, "M%d\n", mode->mode_id);
}

void CommandParser::_buildCalCmd(const CalPayload_t *cal, char *buf, int max) {
    const char *names[] = {"", "IR_MAX", "IR_MIN", "LINE_MAX", "LINE_MIN", "BNO"};
    int idx = cal->cal_cmd;
    // Validate against defined CAL_* range (CAL_IR_MAX=1 .. CAL_BNO=5)
    if (idx < CAL_IR_MAX || idx > CAL_BNO) {
        buf[0] = '\0';
        return;
    }
    snprintf(buf, max, "CAL_%s\n", names[idx]);
}
