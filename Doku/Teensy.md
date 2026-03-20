# Teensy / BotConnect

## 1. Zweck der Library

`Teensy_lib` stellt die Schnittstelle zwischen Teensy und Satellite bereit:
- Kommandos empfangen (Control/Mode/Calibrate)
- Telemetrie senden (`DBG:`-basierte Streams)
- P2P-Nachrichten senden/empfangen

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

## 3. API-Kernfunktionen

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

## 4. UART/Format-Hinweis

- Telemetrie wird als `DBG:<name>=<value>` weitergegeben
- Nicht-DBG-Zeilen können als transparente P2P-Nutzdaten zwischen den Satelliten laufen

---

## 5. Typische Fehler

- Keine Befehle: TX/RX falsch oder kein gemeinsames GND
- Keine Telemetrie in UI: kein `DBG:`-Output / `process()` fehlt
- Falscher Roboter reagiert: `satId` falsch gesetzt
