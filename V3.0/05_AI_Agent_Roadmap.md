# V3.0 – AI Agent Roadmap (Umsetzungsplan)

## Ziel
Schrittweise, risikoarme Umsetzung der V3.0-Architektur mit klaren Abnahmekriterien.

---

## Phase 0 – Vorbereitung
- Branch/Ordnerstruktur festlegen
- Protokollfelder finalisieren (Control, Debug, Heartbeat)
- Test-/Messplan definieren (Latenz, Durchsatz, Drop-Rate)

**Done-Kriterium:** Spezifikation stabil, keine offenen Formatfragen.

---

## Phase 1 – Satelliten-Basis (Dual-Mode + Kanal-Sync)
- STA-Verbindung zum Windows-Hotspot
- ESP-NOW parallel initialisieren (Kanal aus STA übernehmen)
- Reconnect-Handling mit ESP-NOW Reinit

**Done-Kriterium:** SAT-SAT ESP-NOW bleibt unter WLAN-Betrieb stabil.

---

## Phase 2 – QoS/Tasking in Satelliten
- FreeRTOS-Tasks gemäß Prioritätsmodell umsetzen
- Separate Queues inkl. Backpressure
- Metriken (Queue-Länge, Drops, Loop-Zeit)

**Done-Kriterium:** Unter Last bleibt ESP-NOW Pfad reaktionsschnell.

---

## Phase 3 – UDP Control + Debug Aggregation
- UDP-Control RX implementieren
- UDP-Debug TX mit ~1400-Byte Aggregation einführen
- Heartbeat mit Statusfeldern zum PC senden

**Done-Kriterium:** Steuerung stabil, Debug-Durchsatz signifikant erhöht.

---

## Phase 4 – PC-Hub Kern
- UDP-Server, SAT-Registry, Control-Router
- Logging/Monitoring im Hub
- Fehlerbehandlung (Timeouts, Disconnects, malformed packets)

**Done-Kriterium:** PC kann beide SATs parallel steuern und Debugdaten erfassen.

---

## Phase 5 – Native Windows GUI
- GUI-Grundlayout analog alter Web-UI
- Status, Steuerung, Debug-Monitor integrieren
- Bedienpfad für täglichen Betrieb fertigstellen

**Done-Kriterium:** Bedienung ohne Browser für Hub-Funktionen möglich.

---

## Phase 6 – Mobile Joystick Web UI
- Minimalseite (Start + Joystick)
- WebSocket-Bridge im PC-Hub
- Safety-Mechanismen (Deadman/Disconnect-Stop)

**Done-Kriterium:** Tablet/Handy kann live steuern, Hub bleibt führend.

---

## Phase 7 – Systemtest und Tuning
- Lasttests mit realen Debug-Datenmengen
- Messung: Steuerlatenz, Jitter, Paketverlust, CPU-Last
- Parameter-Tuning (Rates, Buffer, Prioritäten)

**Done-Kriterium:** Prioritätsziele erfüllt, stabile Dauerläufe.

---

## Phase 8 – Abschluss
- Betriebsdokumentation
- Recovery-/Fallback-Anleitung
- Release-Tag V3.0-Plan freigeben

**Done-Kriterium:** Umsetzung kann von AI Agent reproduzierbar gestartet werden.
