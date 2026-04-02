// ============================================================
//  Teensy_lib/src/BotConnect_light.cpp
//  LIGHT TELEMETRY-ONLY VERSION
//  Optimized for maximum telemetry throughput and smooth plotting
//  NO P2P / NO CONTROL / NO MODES / NO LEDs / NO BUTTONS
// ============================================================

#include "BotConnect_light.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

BotConnectLight BCLight;

void BotConnectLight::begin(HardwareSerial &serial, uint8_t satId) {
    _serial = &serial;
    _satId  = satId;
}

// ─── Main loop (minimal processing for max performance) ──────
void BotConnectLight::process() {
    // Light version has no incoming command processing
    // All focus on outgoing telemetry performance
}

// ─── Optimized telemetry send helpers ─────────────────────────
// Wire format: DBG:name=value\n
// Optimized for speed: minimal overhead, direct writes

void BotConnectLight::sendTelemetryInt(const char *name, int32_t value) {
    if (!_serial) return;
    _serial->print("DBG:");
    _serial->print(name);
    _serial->print('=');
    _serial->print(value);
    _serial->print('\n');
}

void BotConnectLight::sendTelemetryFloat(const char *name, float value) {
    if (!_serial) return;
    _serial->print("DBG:");
    _serial->print(name);
    _serial->print('=');
    _serial->print(value, 4);  // 4 decimal places
    _serial->print('\n');
}

void BotConnectLight::sendTelemetryBool(const char *name, bool value) {
    if (!_serial) return;
    _serial->print("DBG:");
    _serial->print(name);
    _serial->print('=');
    _serial->print(value ? 1 : 0);
    _serial->print('\n');
}

void BotConnectLight::sendTelemetryString(const char *name, const char *value) {
    if (!_serial) return;
    _serial->print("DBG:");
    _serial->print(name);
    _serial->print('=');
    _serial->print(value);
    _serial->print('\n');
}

// Batch send optimization: send multiple values in rapid succession
void BotConnectLight::beginBatch() {
    // In light version, batching is implicit (no locks needed)
}

void BotConnectLight::endBatch() {
    // In light version, batching is implicit (no flush needed)
}

// High-frequency telemetry helpers for smooth plotting
void BotConnectLight::sendTelemetryFast(const char *name, float value) {
    // Same as regular but optimized call path
    sendTelemetryFloat(name, value);
}

void BotConnectLight::sendTelemetryFast(const char *name, int32_t value) {
    // Same as regular but optimized call path
    sendTelemetryInt(name, value);
}
