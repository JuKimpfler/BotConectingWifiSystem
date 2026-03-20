# Dokumentation – BotConnectingWifiSystem

Diese Dokumentation wurde neu strukturiert und ist auf den aktuellen Code-Stand ausgerichtet.

## Was ist das System?

Ein 3‑Knoten‑System mit:
- **Hub (ESP32‑C3)**: stellt WLAN + Weboberfläche bereit, routet Befehle
- **SAT1 + SAT2 (ESP32‑C3)**: UART‑Brücke zu je einem Teensy + direkte P2P‑Kommunikation
- **2× Teensy 4.0**: eigentliche Roboterlogik (Motoren/Sensorik)

## Dokumente

- [Setup.md](Setup.md) – Werkzeuge, Build, Flash, Erststart
- [Hardware.md](Hardware.md) – Pinbelegung, Verdrahtung, Stromversorgung, **STAT-Pin Anschluss**
- [Software.md](Software.md) – Komponenten, Frame-Format, Rollen, ACK/Network-ID
- [Bridge.md](Bridge.md) – transparente UART/P2P-Bridge (SAT↔SAT)
- [Webserver.md](Webserver.md) – UI-Tabs, WebSocket, Konfiguration
- [Teensy.md](Teensy.md) – BotConnect API und Integrationsmuster
- [USB_PROTOCOL.md](USB_PROTOCOL.md) – USB-Kommandos am Satellite

## Empfohlene Lesereihenfolge

1. Setup
2. Hardware
3. Webserver
4. Teensy
5. Software / Bridge (für Debugging und Erweiterungen)

## Praxis-Hinweis

Wenn mehrere Teams/Systeme gleichzeitig im selben Funkbereich arbeiten:
- eindeutige **Network ID** setzen (Hub UI + Satelliten-Firmware)
- gleiche **Channel**-Konfiguration verwenden
