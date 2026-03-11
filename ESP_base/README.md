# ESP32-C3 Mesh Hub – Projektdokumentation

## Architektur

```
  Browser
    │  HTTP/WebSocket
    ▼
┌─────────────────────────────┐
│   ESP_2 – HUB               │  WiFi AP: "ESP-Mesh-Hub"
│   IP: 192.168.4.1           │  Passwort: "mesh1234"
│   Webserver (Port 80)       │
│   WebSocket (/ws)           │
│   ESP-NOW Kanal 6           │
└────────┬──────────┬─────────┘
    7ms  │          │  100ms
    ◄────┘          └────►
┌────────┐        ┌────────┐
│ ESP_1  │◄──────►│ ESP_3  │
│ NODE 1 │ 100ms  │ NODE 3 │
└────────┘        └────────┘
```

## Inbetriebnahme

### Schritt 1 – MAC-Adressen auslesen
Jeden ESP einzeln flashen mit diesem Sketch:
```cpp
#include <WiFi.h>
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  Serial.println(WiFi.macAddress());
}
void loop() {}
```
MACs notieren und in `include/config.h` eintragen.

### Schritt 2 – Flashen (PlatformIO)
```bash
# Hub (ESP_2)
pio run -e hub --target upload

# Node 1 (ESP_1)
pio run -e node1 --target upload

# Node 3 (ESP_3)
pio run -e node3 --target upload
```

### Schritt 3 – Webinterface öffnen
1. Mit WLAN `ESP-Mesh-Hub` verbinden (Passwort: `mesh1234`)
2. Browser öffnen → `http://192.168.4.1`

## Dateistruktur

```
esp32_mesh_project/
├── include/
│   ├── config.h        ← MACs + Konstanten (hier anpassen!)
│   ├── espnow_mesh.h   ← ESP-NOW Kern-Bibliothek
│   └── hub_html.h      ← Webinterface HTML (eingebettet)
├── src/
│   ├── esp2_hub.cpp    ← Hub: Webserver + ESP-NOW
│   ├── esp1_node.cpp   ← Node 1
│   └── esp3_node.cpp   ← Node 3
└── platformio.ini
```

## Webinterface

| Element         | Funktion                                      |
|-----------------|-----------------------------------------------|
| Linkes Panel    | Eingehende Nachrichten von ESP_1 (grün)       |
| Rechtes Panel   | Eingehende Nachrichten von ESP_3 (amber)      |
| Eingabezeile    | Text senden → geht an ESP_1 UND ESP_3        |
| Status-Dots     | Verbindungsstatus (WS + Node-Aktivität)       |
| Footer          | Live-Statistiken (TX/RX/FAIL/DROP)            |

## Wichtige Hinweise

### Kanal-Synchronisierung
Alle Geräte **müssen** auf Kanal 6 funken:
- Hub setzt Kanal via `WiFi.softAP(..., 6)`
- Nodes setzen Kanal via `esp_wifi_set_channel(6, ...)`

### Nachrichtenlänge
`msg.payload` ist auf **9 Bytes** begrenzt (inkl. Null-Terminator = 8 Zeichen).
Für längere Daten: mehrere Pakete aufteilen oder `msgId` als Sequenznummer nutzen.

### Busy-Guard beim Senden
`meshSend()` wartet max. 50ms auf den vorherigen TX-Callback.
Bei 7ms Intervall und schnellem ACK kein Problem – bei Congestion werden
Pakete als FAILED gezählt (im Footer sichtbar).

### Abhängigkeiten
- `ESPAsyncWebServer-esphome` (nicht das originale, hat ESP32-C3 Fixes)
- `ArduinoJson` v7
