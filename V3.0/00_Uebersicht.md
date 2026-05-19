# V3.0 Vorplanung – Übersicht

## Zielbild
V3.0 trennt strikt zwischen **Echtzeit-Steuerpfad** und **Bulk-Datenpfad**:

- **Prio 1 (kritisch):** SAT1 ↔ SAT2 direkt via ESP-NOW (unverändert, höchste Priorität)
- **Prio 2 (hoch):** PC-Hub ↔ SAT Steuerbefehle via UDP im WLAN (Windows-Hotspot)
- **Prio 3 (niedrig):** SAT → PC Debug-/Telemetriedaten via UDP (große Datenmengen, aggregiert)

Hub wird von ESP auf **Windows-PC als Standalone-Hub** verlagert.

---

## Prioritätenmatrix

| Priorität | Datenfluss | Transport | Max. Latenz-Ziel | Verlusttoleranz | Bemerkung |
|---|---|---|---|---|---|
| P1 | SAT1 ↔ SAT2 | ESP-NOW | so gering wie möglich (Best Effort im selben Kanal, Ziel < 10 ms) | niedrig | Muss stets Vorrang vor allen anderen Tasks haben |
| P2 | PC → SAT (Steuerung) | UDP Unicast | niedrig (Ziel < 20 ms Ende-zu-Ende) | mittel-niedrig | Sequenznummer + optionales ACK/Heartbeat |
| P3 | SAT → PC (Debug) | UDP Unicast/Burst | unkritisch | hoch | Aggregation (~1400 Byte Payload), ggf. Drop bei Last |

---

## Architekturkonzept V3.0

1. **PC als Hub/Hotspot**
   - Windows-PC stellt 2.4-GHz-Hotspot.
   - PC führt UDP-Hub-Logik, Native GUI und WebSocket-Bridge für Mobile UI aus.

2. **Satelliten im Dual-Mode (STA + ESP-NOW)**
   - `WiFi.mode(WIFI_STA)` + `esp_now_init()`.
   - SATs verbinden sich als WLAN-Clients mit PC-Hotspot.
   - ESP-NOW bleibt für SAT-SAT direkt aktiv.

3. **Kanalbindung als harte Bedingung**
   - WLAN-Kanal des Hotspots ist führend.
   - ESP-NOW muss auf exakt diesem Kanal laufen.

4. **QoS in Firmware**
   - FreeRTOS-Tasks so priorisieren, dass SAT-SAT niemals von Debug-Bursts blockiert wird.

---

## Nicht-Ziele in V3.0

- Keine Hardwareänderung an Satelliten/Teensy zwingend erforderlich.
- Kein TSN/EtherCAT über WLAN (technisch nicht passend für ESP32-C3/Wi-Fi Stack).
- Keine Rückmigration auf reinen ESP-Hub.

---

## Ergebnis der Vorplanung

Die V3.0-Architektur ermöglicht:
- prioritätsgerechte Kommunikation,
- deutlich höhere Debug-Datenrate durch UDP-Aggregation,
- bessere Wartbarkeit durch PC-zentrierten Hub,
- mobile Steuerung (Joystick) über minimale Web UI im gleichen Netzwerk.
