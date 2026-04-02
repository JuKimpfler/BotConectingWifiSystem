// ============================================================
//  Teensy_lib/examples/BasicUsage/BasicUsage.ino
//  Minimal example for Teensy 4.0 with BotConnect library
// ============================================================

#include "BotConnect.h"

// Serial3 is connected to the ESP satellite (TX=pin1, RX=pin0)
// Adjust pins and baud as needed for your wiring

void setup() {
    Serial.begin(115200);
    Serial3.begin(115200);

    // SAT_ID=1 means this Teensy is attached to ESP_Satellite with SAT_ID=1
    BC.begin(Serial3, 1);

    // Optional: register P2P callback for messages from the peer robot
    BC.onP2P([](const char *msg) {
        Serial.printf("P2P from peer: %s\n", msg);
    });

    BC.setDebugEnabled(true);
    Serial.println("BotConnect ready");
}

void loop() {
    // Must be called every loop iteration to process incoming UART data
    // and update the state variables.
    BC.process();

    // ── Mode ─────────────────────────────────────────────────
    // Exactly one modeX variable is true at a time (after first mode command).
    if (BC.mode1) { /* autonomous mode */ }
    if (BC.mode2) { /* remote control mode */ }
    if (BC.mode3) { /* test mode */ }
    if (BC.mode4) { /* mode 4 */ }
    if (BC.mode5) { /* mode 5 */ }

    // ── Calibration ──────────────────────────────────────────
    // The last received calibration command's variable is true.
    if (BC.calIrMax)   { /* run IR max calibration */ }
    if (BC.calIrMin)   { /* run IR min calibration */ }
    if (BC.calLineMax) { /* run line max calibration */ }
    if (BC.calLineMin) { /* run line min calibration */ }
    if (BC.calBno)     { /* run BNO sensor calibration */ }

    // ── Control ───────────────────────────────────────────────
    // controlActive is true while commands were received within the last 500 ms.
    if (BC.controlActive) {
        // Use the current control values to drive motors etc.
        Serial.printf("Ctrl: V=%d A=%d SW=0x%02X BTN=0x%02X START=%d\n",
                      BC.speed, BC.angle, BC.switches, BC.buttons, BC.start);
    }

    // ── Telemetry ─────────────────────────────────────────────
    static uint32_t lastTelem = 0;
    if (millis() - lastTelem >= 50) {
        lastTelem = millis();
        BC.sendTelemetryFloat("BallAngle", 12.5f);
        BC.sendTelemetryFloat("BallDist",  120.0f);
        BC.sendTelemetryBool("Start",      BC.start != 0);
        BC.sendTelemetryBool("CtrlActive", BC.controlActive);
        BC.led1 = BC.start != 0;
        BC.led2 = BC.mode1;
        BC.led3 = BC.mode2;
        BC.led4 = BC.controlActive;
        BC.LedUpdate();
    }
}
