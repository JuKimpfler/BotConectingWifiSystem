# Setup Guide

## 1. Voraussetzungen

### Hardware
- Hub: 1× Seeed XIAO **ESP32-C3** oder 1× Seeed XIAO **ESP32-C6**
- SAT1/SAT2: 2× Seeed XIAO **ESP32-C3** (unverändert)
- 2× Teensy 4.0
- USB-Kabel und UART-Leitungen

### Software
- PlatformIO
- Node.js + npm
- CMake + C++ Compiler (für Unit-Tests)
- Arduino IDE/Teensyduino (falls Teensy-Sketches klassisch geflasht werden)

---

## 2. V3.0 PC-Hub (Windows)

1. **Windows-Hotspot starten**
   - 2.4 GHz, fester Kanal (z. B. 6)
   - SSID/Passwort und Hub-IP müssen zu `ESP_Satellite/include/sat_config.h` passen
2. **PC-Hub Setup**
   ```powershell
   cd PC_Hub_Migration/tools
   .\Install-Hub.bat
   ```
3. **PC-Hub starten**
   - `PC_Hub_Migration/Start-Hub.bat` (oder Desktop-Shortcut)
4. **Mobile UI**
   - `http://<PC-IP>:8080/mobile?role=SAT1`

---

## 3. Weboberfläche bauen (Legacy ESP-Hub)

```bash
cd ESP_Hub/ui
npm install
npm run build
```

Hinweis: `npm run build` braucht lokale Node-Dependencies (u. a. `vite`).

---

## 4. Hub flashen (ESP #3, Legacy)

### 3a. Hub auf ESP32-C3 (Standard)

```bash
cd ESP_Hub
pio run -e esp_hub -t upload
pio run -e esp_hub -t uploadfs
```

### 3b. Hub auf ESP32-C6

```bash
cd ESP_Hub
pio run -e esp_hub_c6 -t upload
pio run -e esp_hub_c6 -t uploadfs
```

Das `-DBCWS_TARGET_C6`-Flag wird durch das Environment automatisch gesetzt.  
Details zu Pin-/ADC-Unterschieden: [Hardware.md](Hardware.md).

- Firmware und LittleFS sind getrennte Schritte.
- `uploadfs` enthält die gebaute UI aus `ESP_Hub/data`.

---

## 5. Satelliten flashen (ESP #1/#2)

Satelliten laufen immer auf ESP32-C3 – hier ändert sich nichts:

```bash
cd ESP_Satellite
pio run -e esp_sat1 -t upload
pio run -e esp_sat2 -t upload
```

Optional (USB-Bridge statt HW-UART):
- `esp_sat1_usb_bridge`
- `esp_sat2_usb_bridge`

---

## 6. Teensy anbinden

- BotConnect-Library aus `Teensy_lib/` in Arduino-Libraries kopieren
- Im Sketch:
  - `Serial1.begin(115200);`
  - `BC.begin(Serial1, 1);` (oder `2` für SAT2)
  - `BC.process();` in `loop()` aufrufen

Details: [Teensy.md](Teensy.md)

---

## 7. Erstinbetriebnahme (Legacy ESP-Hub)

1. Alle ESPs einschalten
2. Mit WLAN `ESP-Hub` verbinden (Passwort `hub12345`)
3. `http://192.168.4.1` öffnen
4. In **Settings**: Satelliten scannen und speichern
5. SAT1/SAT2 Status prüfen

---

## 8. Unit-Tests

```bash
cd test/unit
mkdir -p build && cd build
cmake .. -DSAT_ID=1
make
ctest --output-on-failure
```

Aktuell enthalten: `test_crc16`, `test_messages`, `test_command_parser`, `test_routing`.

---

## 9. Häufige Probleme

- **UI leer/404:** `uploadfs` vergessen
- **Satelliten offline:** Channel/Pairing prüfen
- **Keine Telemetrie:** Teensy sendet kein `DBG:` oder UART falsch verdrahtet
- **Cross-Talk mit anderem System:** Network-ID anpassen (Hub + Satellite)
- **C6-Hub LED leuchtet nicht:** PIN_LED_STATUS = GPIO15; onboard-LED ist GPIO15, nicht GPIO10
