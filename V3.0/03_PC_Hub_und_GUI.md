# V3.0 – PC-Hub und native GUI (Windows)

## 1. Ziel
Der Windows-PC ersetzt den ESP-Hub vollständig und bietet:
- UDP-Hub für SAT-Kommunikation
- Native GUI im Stil der bisherigen Web-UI
- integrierten Mini-Webserver für mobile Joystick-UI

---

## 2. Technologie-Optionen

## Option A: Python
- Runtime: Python 3.11+
- GUI: Tkinter (minimaler Footprint) oder PySide6 (moderner)
- Netzwerk: `asyncio` + `socket`
- Vorteil: schnelle Iteration, sehr gut für Agent-gestützte Entwicklung

## Option B: Electron
- Runtime: Node.js
- GUI: HTML/CSS/JS Desktop Shell
- Netzwerk: Node `dgram` (UDP), `ws` (WebSocket)
- Vorteil: UI-Reuse aus Web-Welt

**Empfehlung für V3.0 Start:** Python + Tkinter (geringer Setup-Aufwand auf Windows).

---

## 3. Modulaufteilung (Soll-Struktur)

- `hub_main` (Start/Shutdown)
- `udp_server` (Steuerung ausgehend, Debug eingehend)
- `sat_registry` (SAT-Status, Last Seen, RSSI/Health)
- `control_router` (GUI/WebSocket → UDP Command)
- `debug_ingest` (UDP Frames, Reassembly optional, Logging)
- `gui_app` (native Bedienoberfläche)
- `mobile_ui_server` (HTTP + WebSocket)

---

## 4. Native GUI – Funktionsumfang

- Verbindungsstatus SAT1/SAT2 (Online, Last Seen, Paketverlust)
- Steuer-Panel (Buttons/Mode/Not-Aus)
- Debug-Ansicht (Rate, Drops, optional Filter)
- Netzwerk-Panel (Hotspot-IP, Ports, Kanalinfo soweit verfügbar)

Designziel: visuell nah an bisheriger Web-UI, aber nativ und stabil auf Windows.

---

## 5. UDP-Kommunikationsregeln im Hub

- Pro SAT eigener Command-Zielport.
- Steuerpakete priorisiert senden (eigene Queue).
- Debugpakete nur entgegennehmen/anzeigen/speichern; kein Einfluss auf Control-Timing.
- Heartbeat-Timeout (z. B. 1–2 s) markiert SAT als degraded/offline.

---

## 6. Windows-Betrieb

- Start als normale Desktop-App.
- Optional später: Autostart beim Login.
- Firewall-Regeln per Setup-Skript setzen (UDP + HTTP/WS Port).
- Logging lokal (rotierend), damit Debugdaten nicht Systemlaufwerk fluten.

