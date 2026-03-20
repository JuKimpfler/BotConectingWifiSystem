# Hardware

Diese Datei beschreibt die reale Verdrahtung auf Basis der aktuellen Firmware-Konstanten.

## 1. Systemübersicht

- **Hub (ESP32-C3 #3):** LEDs, Reset-Taster, Batterie-Messung, optionaler Lade-Status-Pin
- **SAT1/SAT2 (ESP32-C3 #1/#2):** UART zu je einem Teensy
- **Teensy 4.0 (2×):** Motor-/Sensorsteuerung

---

## 2. Hub-Pins (ESP #3)

Referenz: `ESP_Hub/include/hub_config.h`

| Funktion | Pin | Bemerkung |
|---|---|---|
| Power-LED | D10 | dauerhaft an (Firmware-Status) |
| Batterie-/Lade-LED | D2 | an bei low battery, blinkt bei Laden |
| Webserver-LED | D3 | an wenn Webserver aktiv |
| SAT1-LED | D4 | Linkstatus SAT1 |
| SAT2-LED | D5 | Linkstatus SAT2 |
| Reset-Taster | D6 | active-low, `INPUT_PULLUP`, ~800 ms halten |
| **Charge Status (STAT)** | **D7** | **optional, active-low Eingang** |
| Batterie-Messung | A1 | ADC-Eingang über Spannungsteiler |

Batterie-Logik (Firmware):
- `BATTERY_VDIVIDER = 2.0`
- `BATTERY_LOW_MV = 3600`
- `BATTERY_CHARGE_BLINK_MS = 500`

---

## 3. Satellit ↔ Teensy Verdrahtung

Referenz: `ESP_Satellite/include/sat_config.h`

| Signal | Satellite | Teensy |
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
- **LOW an D7 → „lädt“**
- **HIGH an D7 → „lädt nicht“**

### 4.1 Anschlussprinzip (integrierter XIAO-Lader)

1. Nutze den **STAT/CHG-Status des integrierten Lade-ICs auf dem XIAO ESP32-C3** (kein externer Lader erforderlich).
2. Falls dieses Signal auf deiner Revision als Pad/Pin verfügbar ist: **STAT/CHG mit D7** verbinden.
3. Da es derselbe XIAO ist, ist **GND bereits gemeinsam** (kein separates Lade-Board nötig).
4. Kein externer Pull-up nötig: D7 läuft in der Firmware mit `INPUT_PULLUP`.

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

## 5. Batterie-Spannungsmessung (A1)

A1 misst über Spannungsteiler. Bei `BATTERY_VDIVIDER = 2.0` gilt z. B.:
- 100k / 100k Teiler
- ADC sieht etwa die halbe Batteriespannung

Formel in Firmware:
- `scaled_mV = analogReadMilliVolts(A1) * BATTERY_VDIVIDER`

Achte darauf, dass A1 elektrisch nie > 3.3V sieht.

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
