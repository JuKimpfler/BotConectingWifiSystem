# BotConnectingWifiSystem

Kurzfassung: Dieses Projekt verbindet **2 Roboter (Teensy 4.0)** drahtlos über **2 ESP32‑C3 Satelliten** mit einem **ESP32‑C3 oder ESP32‑C6 Hub** inkl. Weboberfläche.

```text
  PC als Hub mit GUI und Joystick UI
        |\
        | \  UDP per PC Hotspot
        |  \
   ESP #1 SAT1 <----> ESP #2 SAT2   (P2P zwischen Satelliten)
      | UART             | UART
   Teensy #1          Teensy #2
```

---

## Schnellstart

### V3.0 PC-Hub (Windows, UDP + native GUI)

1. **Windows-Hotspot starten**
   - 2.4 GHz, fester Kanal (z. B. 6)
   - SSID/Passwort müssen mit `ESP_Satellite/include/sat_config.h` übereinstimmen
2. **PC-Hub Setup**
   ```powershell
   cd PC_Hub/tools
   .\Install-Hub.bat
   ```
3. **Satelliten flashen (ESP #1 / #2)**
   ```bash
   cd ESP_Satellite
   pio run -e esp_sat1 -t upload
   pio run -e esp_sat2 -t upload
   ```
4. **PC-Hub starten**
   - `PC_Hub/Start-Hub.bat` (oder Desktop-Shortcut)
5. **Mobile UI öffnen**
   - `http://<PC-IP>:8080/mobile?role=SAT1`

---

## Wichtigste Hardware-Verbindung (Satellit ↔ Teensy)

| Signal | XIAO ESP32-C3 | Teensy 4.0 |
|---|---|---|
| TX | D6 / GPIO21 | RX1 / Pin 0 |
| RX | D7 / GPIO20 | TX1 / Pin 1 |
| GND | GND | GND |

TX/RX müssen gekreuzt sein.

---

## Doku-Index
V3 Ordner mit Dokumentation
