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

- `DBG:<name>=<value>` (für SAT1 und SAT2 gleichermaßen)

Beispiele:

- `DBG:Speed=120`
- `DBG:Angle=45.5`

Wenn eine solche Zeile über USB empfangen wird, verarbeitet der Satellite sie **wie eine
normale Hardware-UART-Zeile mit DBG-Prefix**:

1. Parse in `MSG_DBG` Frame
2. Versand an den Hub (falls bekannt)

> **Hinweis:** Das frühere Format `DBG1:<name>=<value>` / `DBG2:<name>=<value>` ist nicht
> mehr gültig. Nur noch `DBG:` wird als Debug/Telemetrie-Prefix erkannt.

## 3) Transparente UART-Bridge (SAT1 ↔ SAT2)

UART-Zeilen vom Teensy, die **kein** `DBG:`-Prefix haben, werden nicht als Telemetrie
behandelt, sondern als Nutzdaten über die P2P ESP-NOW-Verbindung an den anderen Satellite
weitergeleitet:

- SAT1 empfängt UART-Zeile ohne `DBG:` → sendet als `MSG_UART_RAW` an SAT2
- SAT2 gibt die empfangenen Daten **ohne zusätzlichen Prefix** wieder per UART zum Teensy aus

Diese transparente Bridge funktioniert in beide Richtungen (SAT1 → SAT2 und SAT2 → SAT1).

## 4) Routing-Übersicht

| Quelle          | Prefix         | Ziel                          | Ausgabe am Empfänger       |
|-----------------|----------------|-------------------------------|----------------------------|
| Teensy → SAT    | `DBG:`         | Hub (als `MSG_DBG`)           | (Hub-Webinterface/Telemetrie) |
| Teensy → SAT    | kein Prefix    | Peer-SAT (als `MSG_UART_RAW`) | Teensy UART, ohne Prefix   |
| Hub → SAT       | –              | Teensy UART                   | Mit Format-Prefix (z.B. `V..A..`, `M1`, `CAL_IR_MAX`) |

> Befehle vom Hub (MSG_CTRL, MSG_MODE, MSG_CAL) werden immer mit einem lesbaren Prefix
> auf der Teensy-UART ausgegeben, damit klar erkennbar ist, dass sie vom Hub stammen
> (und nicht vom Peer-Satellite).
