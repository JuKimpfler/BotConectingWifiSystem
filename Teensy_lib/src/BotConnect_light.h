#pragma once
// ============================================================
//  Teensy_lib/src/BotConnect_light.h
//  LIGHT TELEMETRY-ONLY VERSION
//  Optimized for maximum telemetry throughput and smooth plotting
//  NO P2P / NO CONTROL / NO MODES / NO LEDs / NO BUTTONS
// ============================================================

#include <Arduino.h>

class BotConnectLight {
public:
    // ── Initialisation ─────────────────────────────────────────
    // serial: the HardwareSerial connected to the ESP satellite
    // satId:  1 or 2 – determines SAT identity
    void begin(HardwareSerial &serial, uint8_t satId = 1);

    // ── Main loop (minimal processing) ────────────────────────
    // Light version: no command processing, purely telemetry output
    void process();

    // ── Optimized telemetry helpers ───────────────────────────
    // Send a named integer stream value
    void sendTelemetryInt(const char *name, int32_t value);
    // Send a named float stream value (4 decimal places)
    void sendTelemetryFloat(const char *name, float value);
    // Send a named bool stream value
    void sendTelemetryBool(const char *name, bool value);
    // Send a named string stream value
    void sendTelemetryString(const char *name, const char *value);

    // ── High-frequency telemetry for ultra-smooth plotting ────
    // Optimized fast path for high-rate telemetry
    void sendTelemetryFast(const char *name, float value);
    void sendTelemetryFast(const char *name, int32_t value);

    // ── Batch operations (for multi-value updates) ────────────
    // Begin a batch of telemetry updates (implicit in light version)
    void beginBatch();
    // End batch and flush
    void endBatch();

private:
    HardwareSerial *_serial = nullptr;
    uint8_t         _satId  = 1;
};

// Global instance (compatible with BC usage pattern)
extern BotConnectLight BCLight;
