# V3.0 – Mobile Web UI (Tablet/Handy)

## 1. Ziel
Mobile Oberfläche nur für Live-Steuerung:
- **Start-Button**
- **Joystick**

Keine komplexe Hub-Administration mobil.

---

## 2. Betriebsmodell

1. PC-Hub startet kleinen HTTP-Server im Hotspot-Netz.
2. Tablet/Handy verbindet sich mit demselben WLAN.
3. Browseraufruf z. B. `http://<pc-ip>:8080`.
4. Web UI sendet Joystick-Daten via WebSocket an PC-Hub.
5. PC-Hub mappt WebSocket-Eingaben auf UDP-Steuerpakete an SATs.

---

## 3. UI-Scope (bewusst minimal)

- Seite mit:
  - Titel/Verbindungsstatus
  - Start/Arm-Button
  - Joystick-Komponente (X/Y)
- Optional Safety:
  - Deadman-Switch (bei Loslassen -> Neutral/Stop)
  - Auto-Stop bei WebSocket-Disconnect

---

## 4. Nachrichtenmodell WebSocket

## 4.1 Client -> Hub
- `start` / `stop`
- `joy` mit normalisierten Werten `x`, `y` in `[-1.0, 1.0]`
- optional `seq`, `ts`

## 4.2 Hub -> Client
- `ack`
- `status` (connected, sat health)
- `warn/error`

---

## 5. Timing und Sicherheit

- Joystick-Rate: z. B. 20–50 Hz.
- Hub-seitiges Rate-Limit + Plausibilitätsprüfung.
- CORS/Origin prüfen (nur lokales Hotspot-Netz).
- Optional einfacher Session-Code auf Startseite.

---

## 6. Fehlerverhalten

- Bei WS-Abbruch: Hub sendet sofort Neutral/Stop an SAT.
- Bei hoher Last: Priorisierung im Hub garantiert, dass Joystick-Befehle vor Debug-Verarbeitung laufen.

