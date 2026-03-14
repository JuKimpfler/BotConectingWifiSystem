#pragma once
// ============================================================
//  ESP_Satellite/include/CommandParser.h
//  Parses UART commands from Teensy and routes to ESP-NOW
//  Also builds Teensy-bound commands from hub frames
// ============================================================

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstdint>
#include <cstring>
#include <cstdio>
// millis() stub for host builds
static inline unsigned long millis() { return 0; }
#endif

#include "messages.h"

class CommandParser {
public:
    // Parse an incoming hub frame and produce a UART string for Teensy
    // Returns number of bytes written to outBuf, or 0 if no output needed
    int  hubFrameToUart(const Frame_t *frame, char *outBuf, int maxLen);

    // Parse a line from Teensy UART (DBG1:/DBG2: prefix expected)
    // Returns true if the line produced a telemetry frame to send to hub
    bool uartLineToFrame(const char *line, uint8_t satId,
                         Frame_t *outFrame);

private:
    void _buildCtrlCmd(const CtrlPayload_t *ctrl, char *buf, int max);
    void _buildModeCmd(const ModePayload_t *mode, char *buf, int max);
    void _buildCalCmd(const CalPayload_t   *cal,  char *buf, int max);
};
