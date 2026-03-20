# BotConnectingWifiSystem

Kurzfassung: Dieses Projekt verbindet **2 Roboter (Teensy 4.0)** drahtlos über **2 ESP32‑C3 Satelliten** mit einem **ESP32‑C3 Hub** inkl. Weboberfläche.

```text
Browser (WebSocket)
        |
ESP #3 HUB (WiFi AP + UI)
        |\
        | \  ESP‑NOW
        |  \
   ESP #1 SAT1 <----> ESP #2 SAT2   (P2P zwischen Satelliten)
      | UART             | UART
   Teensy #1          Teensy #2
```

---

## Schnellstart

1. **Web-UI bauen**
   ```bash
   cd ESP_Hub/ui
   npm install
   npm run build
   ```

2. **Hub flashen (ESP #3)**
   ```bash
   cd ../
   pio run -e esp_hub -t upload
   pio run -e esp_hub -t uploadfs
   ```

3. **Satelliten flashen (ESP #1 / #2)**
   ```bash
   cd ../ESP_Satellite
   pio run -e esp_sat1 -t upload
   pio run -e esp_sat2 -t upload
   ```

4. **Verbinden**
   - WLAN: `ESP-Hub`
   - Passwort: `hub12345`
   - Browser: `http://192.168.4.1`

5. **In Settings Satelliten scannen und dauerhaft speichern**

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

- [Doku/README.md](Doku/README.md) – Einstieg in die komplette Dokumentation
- [Doku/Setup.md](Doku/Setup.md) – vollständige Inbetriebnahme
- [Doku/Hardware.md](Doku/Hardware.md) – Hardware, Pinout, Verdrahtung, Akku/STAT
- [Doku/Software.md](Doku/Software.md) – Architektur und Nachrichtenformat
- [Doku/Bridge.md](Doku/Bridge.md) – P2P/UART-Bridge zwischen SAT1/SAT2
- [Doku/Webserver.md](Doku/Webserver.md) – Weboberfläche und Einstellungen
- [Doku/Teensy.md](Doku/Teensy.md) – BotConnect-Library für Teensy
- [Doku/USB_PROTOCOL.md](Doku/USB_PROTOCOL.md) – USB-Servicekommandos am Satellite

---

## Lokale Tests

```bash
cd test/unit
mkdir -p build && cd build
cmake .. -DSAT_ID=1
make
ctest --output-on-failure
```
