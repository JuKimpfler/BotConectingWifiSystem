# Hardware

Diese Datei beschreibt die reale Verdrahtung auf Basis der aktuellen Firmware-Konstanten.

## 1. Systemübersicht

- **Hub:** ESP32-C3 **oder** ESP32-C6 (wählbar per Compile-Flag – siehe Abschnitt 2.1)
- **SAT1/SAT2 (ESP32-C3 #1/#2):** UART zu je einem Teensy
- **Teensy 4.0 (2×):** Motor-/Sensorsteuerung

---

## 2. Hub-Pins

### 2.1 Target-Auswahl (C3 ↔ C6)

Der Hub-Firmware liegt ein zentraler Compile-Time-Switch zugrunde.  
Die Umschaltung erfolgt über ein Build-Flag in `ESP_Hub/platformio.ini`:

| Ziel-Chip | PlatformIO-Environment | Build-Flag |
|---|---|---|
| Seeed XIAO **ESP32-C3** | `esp_hub` | `-DBCWS_TARGET_C3` (Default) |
| Seeed XIAO **ESP32-C6** | `esp_hub_c6` | `-DBCWS_TARGET_C6` |

Flash-Befehle:
```bash
# C3-Hub
pio run -e esp_hub    -t upload
pio run -e esp_hub    -t uploadfs

# C6-Hub
pio run -e esp_hub_c6 -t upload
pio run -e esp_hub_c6 -t uploadfs
```

Das Flag wird automatisch gesetzt, wenn das passende Environment gewählt wird.  
Der Header `hub_config.h` erkennt außerdem `CONFIG_IDF_TARGET_ESP32C6` automatisch,  
sodass bei korrekter Board-Auswahl auch ohne explizites Flag die richtigen Pins aktiv sind.

---

### 2.2 Hub-Pinbelegung – ESP32-C3 (Seeed XIAO ESP32-C3)

Referenz: `ESP_Hub/include/hub_config.h` → `BCWS_TARGET_C3`

| Funktion | Symbol | Arduino-D-Pin | GPIO |
|---|---|---|---|
| Power-LED (onboard) | `PIN_LED_STATUS` | D10 | GPIO10 |
| Batterie-/Lade-LED | `PIN_LED_BAT_LOW` | D2 | GPIO4 |
| Webserver-LED | `PIN_LED_WEBSERVER` | D3 | GPIO5 |
| SAT1-LED | `PIN_LED_SAT1` | D4 | GPIO6 |
| SAT2-LED | `PIN_LED_SAT2` | D5 | GPIO7 |
| Reset-Taster | `PIN_BTN_RESET` | D6 | GPIO21 |
| Charge Status (STAT) | `PIN_CHARGE_STATUS` | D7 | GPIO20 |
| Batterie-Messung | `PIN_BATTERY_SENSE` | A1 | GPIO3 |

---

### 2.3 Hub-Pinbelegung – ESP32-C6 (Seeed XIAO ESP32-C6)

Referenz: `ESP_Hub/include/hub_config.h` → `BCWS_TARGET_C6`

| Funktion | Symbol | Arduino-D-Pin | GPIO |
|---|---|---|---|
| Power-LED (onboard) | `PIN_LED_STATUS` | – | GPIO15 |
| Batterie-/Lade-LED | `PIN_LED_BAT_LOW` | D2 | GPIO4 |
| Webserver-LED | `PIN_LED_WEBSERVER` | D3 | GPIO5 |
| SAT1-LED | `PIN_LED_SAT1` | D4 | GPIO6 |
| SAT2-LED | `PIN_LED_SAT2` | D5 | GPIO7 |
| Reset-Taster | `PIN_BTN_RESET` | D6 | GPIO21 |
| Charge Status (STAT) | `PIN_CHARGE_STATUS` | D7 | GPIO22 |
| Batterie-Messung | `PIN_BATTERY_SENSE` | A1 | GPIO3 |

Hinweise C6 vs. C3:
- Der **onboard-LED-Pin** unterscheidet sich (GPIO15 statt GPIO10).  
  Alle externen LEDs/Taster verwenden dieselben D-Pin-Bezeichnungen (D2–D7), die vom Arduino-BSP auf den richtigen GPIO gemappt werden.
- `PIN_CHARGE_STATUS` (D7) ist auf C6 an GPIO22 statt GPIO20 (C3).  
  Die Firmware nutzt die symbolische `D7`-Konstante des BSP, keine hardcodierten GPIO-Nummern.

---

Gemeinsame Batterie-Logik (beide Targets):
- `BATTERY_VDIVIDER = 2.0` (100 kΩ / 100 kΩ Spannungsteiler)
- `BATTERY_LOW_MV = 3600`
- `BATTERY_CHARGE_BLINK_MS = 500`

---

## 3. Satellit ↔ Teensy Verdrahtung

Referenz: `ESP_Satellite/include/sat_config.h`

| Signal | Satellite (ESP32-C3) | Teensy |
|---|---|---|
| TX | D6 / GPIO21 | RX1 / Pin 0 |
| RX | D7 / GPIO20 | TX1 / Pin 1 |
| GND | GND | GND |

Wichtig: TX/RX gekreuzt verbinden.

---

## 4. STAT-/State-Pin zur Akku-Ladeüberwachung (konkret)

Die Firmware behandelt `PIN_CHARGE_STATUS` als **active-low** Eingang:
- Initialisierung: `pinMode(D7, INPUT_PULLUP)`
- Bewertung: `charging = (digitalRead(D7) == LOW)`

Das bedeutet:
- **LOW an D7 → „lädt"**
- **HIGH an D7 → „lädt nicht"**

### 4.1 Anschlussprinzip (integrierter XIAO-Lader)

1. Nutze den **STAT/CHG-Status des integrierten Lade-ICs auf dem XIAO** (kein externer Lader erforderlich).
2. Prüfe im offiziellen Pinout deiner Board-Revision, ob ein **STAT/CHG-Pad** herausgeführt ist. Nur dann: **STAT/CHG mit D7** verbinden.
3. Da es derselbe XIAO ist, ist **GND bereits gemeinsam** (kein separates Lade-Board nötig).
4. Kein externer Pull-up nötig: D7 läuft in der Firmware mit `INPUT_PULLUP`.

Wenn deine Revision **kein zugängliches STAT/CHG-Pad** hat, bleibt die Verbindung zu D7 ungenutzt; die restliche Batterie-Logik (A1/Low-Battery) funktioniert weiterhin.

### 4.2 Warum das so funktioniert

Der integrierte XIAO-Lader meldet den Ladezustand typischerweise über ein active-low Statussignal. Genau dieses Verhalten erwartet die Firmware durch `INPUT_PULLUP` + LOW-Abfrage.

### 4.3 Pegel-/Sicherheitsregeln

- D7 ist ein **3.3V-Logikpin**.
- STAT darf den Pin **nicht über 3.3V treiben**.
- Bei Push-Pull-STAT-Ausgängen mit 5V-Pegel: Pegelwandlung/Spannungsteiler einsetzen.
- Open-Drain mit Pull-up auf 3.3V ist der sichere Standard.

### 4.4 Sichtbares Verhalten

- Wenn `charging=true` (STAT LOW), blinkt die Batterie-LED auf D2 mit ~500 ms.
- Wenn nicht geladen, zeigt D2 nur den Low-Battery-Zustand (unter Schwellwert).

---

## 5. Batterie-Spannungsmessung

### 5.1 Hardware-Schaltung (C3 und C6 identisch)

A1 misst über externen Spannungsteiler. Bei `BATTERY_VDIVIDER = 2.0`:
- 100 kΩ / 100 kΩ Teiler
- ADC sieht etwa die halbe Batteriespannung
- Formel: `scaled_mV = analogReadMilliVolts(A1) × BATTERY_VDIVIDER`

Achte darauf, dass A1 elektrisch nie > 3.3 V sieht.

### 5.2 ADC-Kalibrierung – C3 vs. C6

| Chip | Kalibrierungsverfahren | Typische Genauigkeit |
|---|---|---|
| ESP32-C3 | Attenuation-basierte Linearisierung | ±20–30 mV |
| ESP32-C6 | eFuse Two-Point-Kalibrierung (automatisch über SDK) | ±5–10 mV |

Auf dem ESP32-C6 wendet `analogReadMilliVolts()` die eFuse-Kalibrierung **automatisch** an.  
Es ist kein zusätzlicher Kalibrierungsaufruf erforderlich – der Vorteil greift transparent.

### 5.3 Software-API

```cpp
// Spannung in Millivolt (0 wenn noch kein gültiger Messwert)
uint32_t mv = batteryMon.getBatteryVoltageMv();

// Vollständiger Zustand
const BatteryState &s = batteryMon.state();
// s.voltage  – Spannung in Volt
// s.valid    – true wenn Messung plausibel
// s.low      – true wenn unter BATTERY_LOW_MV
// s.charging – true wenn STAT-Pin LOW (Laden erkannt)
```

---

## 6. Stromversorgung

- Entwicklungsbetrieb: USB
- Robotikbetrieb: stabile 5V-Versorgung pro Knoten
- Immer gemeinsame GND-Bezüge zwischen logisch verbundenen Boards

---

## 7. Hardware-Checkliste

- [ ] SAT1 TX/RX gekreuzt zum Teensy
- [ ] SAT2 TX/RX gekreuzt zum Teensy
- [ ] Gemeinsame GND pro UART-Verbindung
- [ ] Hub A1 korrekt über Teiler beschaltet
- [ ] Hub D7 korrekt an STAT + GND-Referenz
- [ ] Keine 5V direkt auf 3.3V-Logikpins
- [ ] Bei C6: `esp_hub_c6` Environment in PlatformIO gewählt
