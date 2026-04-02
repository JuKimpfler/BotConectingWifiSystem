# Software

## 1. Komponenten

- `shared/` – gemeinsames Protokoll (`messages.h`, `crc16.h`)
- `ESP_Hub/` – Hub-Firmware + Web-UI
- `ESP_Satellite/` – gemeinsame Firmware für SAT1/SAT2
- `Teensy_lib/` – BotConnect-Library
- `test/unit/` – Host-Unit-Tests

---

## 2. Rollenmodell

- `ROLE_HUB = 0x00`
- `ROLE_SAT1 = 0x01`
- `ROLE_SAT2 = 0x02`
- `ROLE_BROADCAST = 0xFF`

---

## 3. Nachrichtenrahmen

Referenz: `shared/messages.h`

Frame (`Frame_t`):
- `magic` (`0xBE`)
- `msg_type`
- `seq`
- `src_role`
- `dst_role`
- `flags`
- `len`
- `network_id`
- `payload[0..180]`
- `crc16`

CRC:
- CRC-16/IBM(MODBUS), Init `0xFFFF`, Poly `0xA001`
- Implementierung: `shared/crc16.h`

---

## 4. Nachrichtentypen (Auszug)

- `MSG_DBG` – Telemetrie (`DBG:`)
- `MSG_CTRL` – manuelle Steuerung
- `MSG_MODE` – Moduswechsel
- `MSG_CAL` – Kalibrierung
- `MSG_DISCOVERY` – Peer-Discovery
- `MSG_UART_RAW` – transparente SAT↔SAT UART-Nutzdaten
- `MSG_ACK` – ACK-Frames

---

## 5. Anti-Mis-Pairing / Network-ID

`network_id` im Header trennt Systeme im selben Funkbereich.

- Hub-Compile-Time: `HUB_NETWORK_ID` (`ESP_Hub/include/hub_config.h`)
- Satellite-Compile-Time: `ESPNOW_NETWORK_ID` (`ESP_Satellite/include/sat_config.h`)

Frames mit unpassender ID werden verworfen (außer Legacy `0x00`).

---

## 6. Hub-Firmware

Wesentliche Aufgaben:
- Start AP (`ESP-Hub` / `hub12345`)
- HTTP+WebSocket (Port 80, Pfad `/ws`)
- Pairing/Settings verwalten
- Routing Hub↔Satellites
- LED-/Batteriestatus aktualisieren

---

## 7. Satellite-Firmware

Wesentliche Aufgaben:
- ESP-NOW Kommunikation
- UART-Bridge zum Teensy (`115200`)
- Routing-Regel:
  - `DBG:` => `MSG_DBG` an Hub
  - ohne Prefix => `MSG_UART_RAW` an Peer-Satellite
- ACK-Handling und Retries
- NVS-MAC-Lernen/Recovery

---

## 8. Build-/Test-Kommandos

Unit-Tests:
```bash
cd test/unit
mkdir -p build && cd build
cmake .. -DSAT_ID=1
make
ctest --output-on-failure
```

Hub UI:
```bash
cd ESP_Hub/ui
npm install
npm run build
```

---

## 9. Erweiterungshinweise

- Neue Nachrichtentypen zuerst in `shared/messages.h` definieren
- Danach Hub und Satellite-Routing ergänzen
- Bei Protokolländerungen Unit-Tests aktualisieren/ergänzen
