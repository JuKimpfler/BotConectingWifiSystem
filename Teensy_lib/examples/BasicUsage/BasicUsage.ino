// ============================================================
//  Teensy_lib/examples/BasicUsage/BasicUsage.ino
//  Minimal example for Teensy 4.0 with BotConnect library
// ============================================================

#include "BotConnect.h"

// Serial1 is connected to the ESP satellite (TX=pin1, RX=pin0)
// Adjust pins and baud as needed for your wiring

void onMode(uint8_t modeId) {
    Serial.printf("Mode selected: %d\n", modeId);
    // Set your robot mode variable here
}

void onCalibrate(const char *calCmd) {
    Serial.printf("Calibrate: %s\n", calCmd);
    // Handle calibration command
}

void onControl(int16_t speed, int16_t angle,
               uint8_t switches, uint8_t buttons, uint8_t start) {
    Serial.printf("Ctrl: V=%d A=%d SW=0x%02X BTN=0x%02X START=%d\n",
                  speed, angle, switches, buttons, start);
    // Drive your motors here
}

void setup() {
    Serial.begin(115200);
    Serial1.begin(115200);

    // SAT_ID=1 means this Teensy is attached to ESP_Satellite with SAT_ID=1
    BC.begin(Serial1, 1);
    BC.onMode(onMode);
    BC.onCalibrate(onCalibrate);
    BC.onControl(onControl);
    BC.setDebugEnabled(true);

    Serial.println("BotConnect ready");
}

void loop() {
    BC.process();

    // Example: send telemetry every 50 ms
    static uint32_t lastTelem = 0;
    if (millis() - lastTelem >= 50) {
        lastTelem = millis();
        BC.sendTelemetryFloat("BallAngle", 12.5f);
        BC.sendTelemetryFloat("BallDist",  120.0f);
        BC.sendTelemetryBool("Start",      true);
        BC.sendTelemetryInt("Mode",        1);
    }
}
