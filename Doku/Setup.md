# Setup Guide

## 1. Voraussetzungen

### Hardware
- PC
- SAT1/SAT2: 2× Seeed XIAO **ESP32-C3** (unverändert)
- 2× Teensy 4.0
- USB-Kabel und UART-Leitungen
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

## 3. Satelliten flashen (ESP #1/#2)

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

## 4. Teensy anbinden

- BotConnect-Library aus `Teensy_lib/` in Arduino-Libraries kopieren
- Im Sketch:
  - `Serial1.begin(115200);`
  - `BC.begin(Serial1, 1);` (oder `2` für SAT2)
  - `BC.process();` in `loop()` aufrufen

Details: [Teensy.md](Teensy.md)