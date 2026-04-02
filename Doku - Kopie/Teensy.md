# Teensy / BotConnect

## 1. Zweck der Library

`Teensy_lib` stellt die Schnittstelle zwischen Teensy und Satellite bereit:
- Kommandos empfangen (Control/Mode/Calibrate)
- Telemetrie senden (`DBG:`-basierte Streams)
- P2P-Nachrichten senden/empfangen
- optional auch per I2C für SAT↔Hub-Daten (Satellite als Slave, Default-Adresse `0x03`)

---

## 2. Minimalintegration

```cpp
#include "BotConnect.h"

void setup() {
  Serial1.begin(115200);
  BC.begin(Serial1, 1); // 1 für SAT1, 2 für SAT2

  BC.onControl([](int16_t speed, int16_t angle, uint8_t sw, uint8_t btn, uint8_t start) {
    // Motorlogik
  });

  BC.onMode([](uint8_t mode) {
    // Moduslogik
  });

  BC.onCalibrate([](const char *cmd) {
    // Kalibrierung
  });
}

void loop() {
  BC.process();
  BC.sendTelemetryInt("Mode", 1);
}
```

Wichtig:
- `BC.process()` in jeder Loop-Runde aufrufen
- `satId` in `BC.begin()` muss zum geflashten Satellite passen

---

## 3. I2C-Integration (SAT-Hub-Daten)

Für SAT↔Hub-Daten kann alternativ `BotConnect_i2C` genutzt werden.
Der Satellite läuft dabei als I2C-Slave auf Adresse `0x03`, der Teensy ist Master.

```cpp
#include <Wire.h>
#include "BotConnect.h"

void setup() {
  Wire.begin();                 // Teensy als I2C-Master
  BC_I2C.begin(Wire, 0x03);     // Standard-Adresse Satellite
}

void loop() {
  BC_I2C.process();             // holt Ctrl/Mode/Cal vom Satellite
  BC_I2C.sendTelemetryInt("Mode", 1);
}
```

Es ist möglich, **I2C und UART parallel** zu verwenden:
- `BC_I2C` für SAT↔Hub-Informationen (Telemetry, Control/Mode/Calib)
- `BC` weiter für UART-basierte P2P-Nachrichten

---

## 4. API-Kernfunktionen

Callbacks:
- `onControl(...)`
- `onMode(...)`
- `onCalibrate(...)`
- `onP2P(...)`

Telemetrie:
- `sendTelemetryInt`
- `sendTelemetryFloat`
- `sendTelemetryBool`
- `sendTelemetryString`

P2P:
- `sendP2P(...)`
- `hasP2P()`, `readP2P(...)`

---

## 5. UART/Format-Hinweis

- Telemetrie wird als `DBG:<name>=<value>` weitergegeben
- Nicht-DBG-Zeilen können als transparente P2P-Nutzdaten zwischen den Satelliten laufen

---

## 6. Typische Fehler

- Keine Befehle: TX/RX falsch oder kein gemeinsames GND
- Keine Telemetrie in UI: kein `DBG:`-Output / `process()` fehlt
- Falscher Roboter reagiert: `satId` falsch gesetzt
