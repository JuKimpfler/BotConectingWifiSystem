// ============================================================
//  BasicUsageLight.ino
//  Example: LIGHT TELEMETRY-ONLY version
//  Demonstrates ultra-high-rate telemetry for smooth plotting
//  NO P2P / NO CONTROL / NO MODES / NO LEDs / NO BUTTONS
// ============================================================

#include <BotConnect_light.h>

// Hardware serial connected to ESP satellite
HardwareSerial &EspSerial = Serial1;

void setup() {
    Serial.begin(115200);
    EspSerial.begin(115200);

    // Initialize light version with SAT_ID=1 (or 2)
    BCLight.begin(EspSerial, 1);

    Serial.println("BotConnect Light - Telemetry-Only Example");
    Serial.println("Ultra-optimized for high-rate smooth plotting");
}

void loop() {
    // Process minimal overhead (no command parsing in light version)
    BCLight.process();

    // Example: send high-frequency sensor telemetry
    // In light version, these go directly to ESP with minimal overhead

    // Simulate sensor readings at high rate
    static unsigned long lastSend = 0;
    static int counter = 0;

    // Send telemetry at very high rate (every 5ms = 200 Hz per value)
    if (millis() - lastSend >= 5) {
        lastSend = millis();

        // Simulate various sensor types
        float sensorA = sin(counter * 0.01) * 100.0;
        float sensorB = cos(counter * 0.02) * 50.0;
        int32_t sensorC = counter % 1000;
        bool statusD = (counter % 100) < 50;

        // Ultra-fast telemetry sending
        BCLight.sendTelemetryFast("SensorA", sensorA);
        BCLight.sendTelemetryFast("SensorB", sensorB);
        BCLight.sendTelemetryFast("SensorC", sensorC);
        BCLight.sendTelemetryBool("StatusD", statusD);

        counter++;
    }

    // Example: batch multiple telemetry updates
    static unsigned long lastBatch = 0;
    if (millis() - lastBatch >= 100) {
        lastBatch = millis();

        BCLight.beginBatch();
        BCLight.sendTelemetryInt("Uptime", millis());
        BCLight.sendTelemetryInt("Counter", counter);
        BCLight.sendTelemetryFloat("Frequency", counter / (millis() / 1000.0));
        BCLight.endBatch();
    }

    // Local processing can continue without interference
    // The light version has minimal overhead for maximum performance
}

// Example: typical use case with motor control robot
void exampleRobotTelemetry() {
    // Read sensors at very high frequency
    float leftMotorSpeed = analogRead(A0) / 1023.0 * 100.0;
    float rightMotorSpeed = analogRead(A1) / 1023.0 * 100.0;
    float batteryVoltage = analogRead(A2) / 1023.0 * 12.0;
    int linePosition = analogRead(A3);

    // Send all telemetry rapidly for smooth plotting
    BCLight.sendTelemetryFast("MotorL", leftMotorSpeed);
    BCLight.sendTelemetryFast("MotorR", rightMotorSpeed);
    BCLight.sendTelemetryFloat("Battery", batteryVoltage);
    BCLight.sendTelemetryFast("LinePos", linePosition);
}
