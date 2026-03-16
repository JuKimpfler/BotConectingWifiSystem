# USB Communication (Satellite)

Dieses Dokument beschreibt die USB-Seriell-Kommunikation am ESP-Satellite.

## 1) USB Service-Kommandos

Über den USB-Serial-Monitor können folgende Wartungs-Kommandos gesendet werden:

- `mac` oder `info`
  - Zeigt eigene MAC, Kanal, bekannte Hub/Peer-MAC und Hub-Online-Status.
- `debug`
  - Zeigt erweiterten Status (Uptime, ACK-Queue, bekannte Peers).
- `clearmac`
  - Löscht gespeicherte Hub-/Peer-MACs aus NVS.
- `help`
  - Zeigt verfügbare USB-Kommandos.

## 2) USB Telemetrie-Injection (Weiterleitung wie UART)

Zusätzlich akzeptiert der Satellite Telemetriezeilen über USB im selben Format wie bei
UART-Eingang vom Teensy:

- `DBG1:<name>=<value>` für SAT1
- `DBG2:<name>=<value>` für SAT2

Beispiele:

- `DBG1:Speed=120`
- `DBG2:Angle=45.5`

Wenn eine solche Zeile über USB empfangen wird, verarbeitet der Satellite sie **wie eine
normale Hardware-UART-Zeile**:

1. Parse in `MSG_DBG` Frame
2. Versand an den Hub (falls bekannt)
3. Weiterleitung an den Peer-Satellite (falls bekannt)

Dadurch kann UART-Telemetrie für Tests auch ohne direkte Teensy-UART-Einspeisung erzeugt werden.
