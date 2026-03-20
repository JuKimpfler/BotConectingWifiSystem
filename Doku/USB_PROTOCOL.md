# USB-Protokoll (Satellite)

Diese Datei beschreibt die USB-Seriell-Schnittstelle der Satellite-Firmware.

## 1. Service-Kommandos

Über den USB-Monitor am Satellite verfügbar:

- `mac` / `info`
  - zeigt eigene MAC, Channel, bekannte Hub-/Peer-MAC, Hub-Online-Status
- `debug`
  - erweiterter Status (Uptime, ACK-Queue, Peer-Status)
- `clearmac`
  - löscht gespeicherte Hub-/Peer-MACs in NVS
- `help`
  - zeigt Kommandoliste
- `Modi+Web`, `Modi+Bridge`, `Modi+Status`
  - schaltet Monitoring-Modi (siehe Ausgabe im Monitor)

Referenz: `ESP_Satellite/src/main.cpp` (`handleSerialCmd`)

---

## 2. USB Telemetrie-Injection

Gültiges Prefix:
- `DBG:<name>=<value>`

Beispiele:
- `DBG:Speed=120`
- `DBG:Angle=45.5`

Diese Zeilen werden intern wie UART-DBG behandelt und als `MSG_DBG` zum Hub gesendet.

---

## 3. Transparente Bridge

Zeilen **ohne** `DBG:` werden als Nutzdaten betrachtet und als `MSG_UART_RAW` zum Peer-Satellite übertragen.

Das Ziel-Satellite gibt die Daten auf seiner Teensy-UART wieder aus.

---

## 4. Routing-Übersicht

| Quelle | Inhalt | Weiterleitung |
|---|---|---|
| USB/UART | `DBG:`-Zeile | Hub (`MSG_DBG`) |
| USB/UART | ohne Prefix | Peer-SAT (`MSG_UART_RAW`) |
| Hub | CTRL/MODE/CAL | lokale Teensy-UART |

---

## 5. Hinweis zu `DBG1:` / `DBG2:`

In der aktuellen Firmware gilt primär `DBG:` als gemeinsames Prefix für Telemetrie-Routing.
