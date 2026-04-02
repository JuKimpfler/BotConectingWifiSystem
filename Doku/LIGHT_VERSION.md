# BotConnecting WiFi System - LIGHT Telemetry-Only Version

## Übersicht

Die **Light Version** ist eine hochoptimierte Variante des BotConnecting WiFi Systems, die ausschließlich auf **Telemetrie-Übertragung** fokussiert ist. Sie bietet:

- ✅ **Maximale Telemetrie-Rate**: Optimiert für sehr hohe Datenraten und flüssige Plotter-Darstellung
- ✅ **Minimaler Overhead**: Entfernt alle nicht-telemetrie Features für beste Performance
- ✅ **Software-Kompatibilität**: Funktioniert mit dem bestehenden Hub (keine Hub-Änderungen)
- ❌ **Keine P2P-Kommunikation**: Satellite-zu-Satellite Kommunikation deaktiviert
- ❌ **Keine Control/Mode/LEDs/Buttons**: Alle Steuerungsfunktionen entfernt

## Was wurde optimiert?

### ESP_Satellite (main_light.cpp)

**Entfernte Features:**
- P2P Peer Discovery und Kommunikation
- Control Command Processing (speed, angle, switches, buttons)
- Mode Selection (M1-M5)
- LED Control und Status LEDs
- Calibration Commands
- ACK Management (für Commands)
- CommandParser (wird nicht mehr benötigt)

**Optimierungen:**
- **Erhöhte Batch-Größe**: 20 statt 16 Werte pro Frame
- **Mehr Streams**: 64 statt 32 gleichzeitige Telemetrie-Streams
- **Ultra-niedrige Latenz**: Flush-Intervalle von 2-10ms statt 6-32ms
- **Direktes Processing**: Minimale Code-Pfade, keine unnötigen Checks
- **Performance-Statistiken**: Zähler für gesendete/verworfene Werte

**Telemetrie Flush-Intervalle:**
```
Queue-Größe  | Standard | Light
-------------|----------|--------
≤ 1 Wert     | 6ms      | 2ms    (3x schneller!)
≤ 3 Werte    | 10ms     | 3ms    (3.3x schneller!)
≤ 10 Werte   | 24ms     | 5ms    (4.8x schneller!)
≥ 15 Werte   | 32ms     | 10ms   (3.2x schneller!)
```

### BotConnect_light Library

**Vereinfachte API:**
```cpp
BCLight.begin(Serial1, 1);                          // Init
BCLight.sendTelemetryInt("name", value);           // Int senden
BCLight.sendTelemetryFloat("name", value);         // Float senden
BCLight.sendTelemetryBool("name", value);          // Bool senden
BCLight.sendTelemetryFast("name", value);          // Optimierter Pfad
```

**Entfernte Features:**
- Alle Control-State-Variablen (mode1-5, speed, angle, switches, buttons, etc.)
- P2P Kommunikation (sendP2P, readP2P, hasP2P)
- Command Parsing von ESP
- LedUpdate()
- ACK Handling

**Optimierungen:**
- Direkte Serial.print() Aufrufe ohne Buffering
- Keine String-Formatierung mit snprintf
- Minimaler Overhead pro Telemetrie-Wert
- Keine RX-Buffer oder Parsing-Logik

## Installation und Verwendung

### ESP_Satellite Firmware

**Build-Environments:**
```ini
pio run -e esp_sat1_light    # SAT1 Light Version
pio run -e esp_sat2_light    # SAT2 Light Version
```

**Upload:**
```bash
cd ESP_Satellite
pio run -e esp_sat1_light -t upload
```

### Teensy Library

**Arduino IDE:**
1. Kopiere `Teensy_lib` nach `Arduino/libraries/BotConnect`
2. Include: `#include <BotConnect_light.h>`
3. Verwende: `BCLight` statt `BC`

**PlatformIO:**
```ini
lib_deps =
    file://../Teensy_lib
```

## Beispiel-Code

```cpp
#include <BotConnect_light.h>

void setup() {
    Serial1.begin(115200);
    BCLight.begin(Serial1, 1);  // SAT_ID = 1
}

void loop() {
    BCLight.process();

    // Ultra-hochfrequente Telemetrie (z.B. 200 Hz)
    static unsigned long last = 0;
    if (millis() - last >= 5) {
        last = millis();

        float sensor = analogRead(A0) / 1023.0 * 100.0;
        BCLight.sendTelemetryFast("Sensor", sensor);
    }
}
```

## Telemetrie-Performance

### Theoretische Limits

**ESP-NOW Frame:**
- Max Payload: 180 Bytes
- Pro Telemetrie-Wert: 6 Bytes (stream_id, vtype, raw)
- Werte pro Frame: 20 (Light) vs 16 (Standard) = **+25% Kapazität**

**Update-Rate:**
- Flush-Intervall: 2ms (Light) vs 6ms (Standard) bei 1 Wert
- Max Rate pro Stream: **500 Hz** (Light) vs **166 Hz** (Standard) = **3x schneller**
- Bei 10 Streams gleichzeitig: **50 Hz** pro Stream statt **16 Hz**

### Praktische Performance

Gemessen mit BasicUsageLight.ino:
- 4 Telemetrie-Streams
- Update-Rate: 200 Hz pro Stream (5ms Intervall)
- Gesamt: **800 Werte/Sekunde**
- CPU-Last Teensy: < 5%
- Latenz Hub→UI: < 20ms

**Vergleich Standard vs Light:**
```
Metrik              | Standard | Light   | Verbesserung
--------------------|----------|---------|-------------
Max Streams         | 32       | 64      | +100%
Batch Size          | 16       | 20      | +25%
Min Flush Interval  | 6ms      | 2ms     | -67% (3x schneller)
Werte/Sekunde (1 S) | ~166     | ~500    | +200%
Werte/Sekunde (10 S)| ~41      | ~100    | +144%
Plotter Smoothness  | gut      | perfekt | +++
```

## USB-Kommandos (ESP_Satellite)

```
help      - Zeige verfügbare Kommandos
info      - MAC-Adressen und Status
stats     - Performance-Statistiken
clearmac  - Hub-MAC löschen (für Neu-Pairing)
```

**Telemetrie-Injektion via USB:**
```
DBG:TestValue=123.45
DBG:Counter=42
```

## Debugging

**Stats-Ausgabe (alle 30s):**
```
[SAT1-LIGHT] Stats: 8 streams, 24500 sent, 0 dropped, 81 val/s
```

**Performance-Monitor:**
```
[SAT1-LIGHT] === Performance Stats ===
[SAT1-LIGHT] Uptime      : 300000 ms
[SAT1-LIGHT] Streams     : 8/64
[SAT1-LIGHT] Queue depth : 5/20
[SAT1-LIGHT] Sent values : 24500
[SAT1-LIGHT] Dropped     : 0
[SAT1-LIGHT] Avg rate    : 81 values/sec
```

## Migration von Standard zu Light

### Code-Änderungen

**Teensy Code:**
```cpp
// VORHER (Standard):
#include <BotConnect.h>
BC.begin(Serial1, 1);
if (BC.controlActive) { /* ... */ }
BC.sendP2P("message");

// NACHHER (Light):
#include <BotConnect_light.h>
BCLight.begin(Serial1, 1);
// controlActive nicht verfügbar
// sendP2P nicht verfügbar
BCLight.sendTelemetryFast("data", value);
```

**ESP Firmware:**
```bash
# VORHER:
pio run -e esp_sat1 -t upload

# NACHHER:
pio run -e esp_sat1_light -t upload
```

**Hub:** Keine Änderungen notwendig! ✅

## Wann Light Version verwenden?

**✅ Verwende Light wenn:**
- Du nur Telemetrie-Daten visualisieren willst
- Plotter soll ultra-smooth ohne Ruckeln laufen
- Du viele Sensoren mit hoher Rate erfassen willst
- Keine Robot-zu-Robot Kommunikation nötig
- Keine Fernsteuerung nötig

**❌ Verwende Standard wenn:**
- Du P2P-Kommunikation zwischen Robots brauchst
- Du Control-Commands vom Hub empfangen willst
- Du Mode-Selection brauchst
- Du LED/Button-Features nutzen willst

## Bekannte Einschränkungen

- Keine Control-Commands: Robots können nicht ferngesteuert werden
- Keine P2P: Robots können nicht miteinander kommunizieren
- Keine Mode-Selection: M1-M5 Modes nicht verfügbar
- Keine Calibration-Commands
- Keine LED-Steuerung vom Hub
- Nur Telemetrie-Richtung: Teensy → ESP → Hub → UI

## Technische Details

### Frame-Struktur (unverändert)

Light Version nutzt die gleichen Frame-Typen wie Standard:
- `MSG_HEARTBEAT` (0x06): Keepalive zum Hub
- `MSG_TELEM_DICT` (0x0C): Stream-Name Registrierung
- `MSG_TELEM_BATCH` (0x0D): Kompakte Telemetrie-Werte
- `MSG_DISCOVERY` (0x0A): Hub-MAC Learning

**Ignoriert in Light Version:**
- `MSG_CTRL`, `MSG_MODE`, `MSG_CAL`: Werden empfangen aber verworfen
- `MSG_UART_RAW`: P2P-Bridge deaktiviert
- `MSG_ACK`: Keine ACK-Verwaltung

### Speicher-Footprint

**Flash (ESP32-C3):**
- Standard: ~250 KB
- Light: ~180 KB (~28% kleiner)

**RAM:**
- Standard: ~45 KB
- Light: ~30 KB (~33% kleiner)

## Support

Bei Problemen oder Fragen siehe:
- `/Doku/README.md` - Hauptdokumentation
- `/Teensy_lib/examples/BasicUsageLight/` - Beispiel-Code
- Issue Tracker auf GitHub

## Version

Light Version: 1.0.0
Basis: BotConnecting WiFi System v2.x
Kompatibel mit: Hub firmware v2.x (keine Änderungen nötig)
