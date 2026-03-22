# Webserver & UI

## 1. Zugriff

Der Hub startet ein WLAN-AP mit:
- SSID: `ESP-Hub`
- Passwort: `hub12345`

Weboberfläche:
- `http://192.168.4.1`

WebSocket:
- `ws://<host>/ws`

Referenz: `ESP_Hub/include/hub_config.h`, `ESP_Hub/ui/src/ws.js`

---

## 2. UI-Struktur (Tabs)

Aus `ESP_Hub/ui/index.html`:
- Debug
- Manual
- Modes
- Calibrate
- Settings

### Debug
- Telemetrie-Tabelle
- Raw-Monitor

### Manual
- Joystick / Geschwindigkeit / Winkel
- Schalter SW1..SW3
- Buttons B1..B4
- START

### Modes
- 5 feste Channels (1..5), Name in UI frei konfigurierbar
- Mini-Menü zum Hinzufügen/Umbenennen von Mode-Buttons pro Channel

### Calibrate
- 5 feste Channels (1..5), Name in UI frei konfigurierbar
- Mini-Menü zum Hinzufügen/Umbenennen von Calib-Buttons pro Channel

### Settings
- Channel
- PMK
- Network-ID
- Peer-Scan und manuelles Peer-Add
- Save / Reset / Factory-Reset
- Download der vollständigen aktuellen Konfiguration als JSON (`/api/config_export`)

---

## 3. Kommunikationsmodell

- Browser sendet JSON-Nachrichten per WebSocket
- Hub verarbeitet Kommandos und routet per ESP-NOW
- Status/Telemetrie werden vom Hub zurück an Browser gepusht

`wsSend()` liefert `true/false` (offene Verbindung oder nicht). UI-Kommandos sollten den Rückgabewert berücksichtigen.

---

## 4. Build- und Deploy-Flow

```bash
cd ESP_Hub/ui
npm install
npm run build

cd ..
pio run -e esp_hub -t upload
pio run -e esp_hub -t uploadfs
```

---

## 5. Betriebsrelevante Einstellungen

- **Channel**: muss zu Satelliten passen
- **Network-ID**: muss zu `ESPNOW_NETWORK_ID` passen
- **Peer-Liste**: langfristig speichern, damit Reconnect nach Reboot möglich ist
- **Mode/Calib Labels**: werden in der Hub-Config gespeichert; wirken nur auf die Web-UI-Anzeige

---

## 6. Fehlerbilder

- UI lädt nicht: LittleFS/`uploadfs` fehlt
- WS offline: AP-Verbindung oder Hub-Prozess prüfen
- Befehle ohne Wirkung: Ziel-SAT offline oder WS nicht offen
