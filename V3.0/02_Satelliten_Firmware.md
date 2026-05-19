# V3.0 – Satelliten-Firmware (ESP32-C3)

## 1. Ziel
Firmware-Design mit harter Priorisierung:
1. SAT-SAT ESP-NOW
2. PC-SAT Steuerung (UDP)
3. SAT-PC Debug (UDP, große Mengen)

---

## 2. FreeRTOS-Taskmodell (QoS)

## 2.1 Empfohlene Tasks
- `taskEspNowRxTx` (Prio 5, Core default)
- `taskUdpControlRx` (Prio 4)
- `taskUartIngress` (Prio 3)
- `taskUdpDebugTx` (Prio 2)
- `taskHousekeeping` (Prio 1)

## 2.2 Regeln
- `taskEspNowRxTx` darf nicht durch lange Kopier-/String-Operationen blockiert werden (Richtwert: keine Einzeloperation > 1 ms im kritischen Pfad).
- `taskUdpDebugTx` muss regelmäßig `vTaskDelay(1)`/yield nutzen.
- Für jede Queue High-/Low-Watermark-Monitoring vorsehen.

---

## 3. Queue- und Buffer-Design

## 3.1 Separate Queues
- `q_espnow_high`: SAT-SAT Nutzdaten + kritische interne Events
- `q_udp_ctrl`: vom PC empfangene Steuerdaten
- `q_udp_debug`: aus UART/Diagnose kommende Debugdaten

## 3.2 Backpressure-Regeln
- Bei Last zuerst Drop/Rate-Limit in `q_udp_debug`.
- `q_espnow_high` niemals durch Debug verdrängen.

---

## 4. Echtzeit-UDP-Streaming der Debug-Daten

## 4.1 Warum
Die Debugdaten vom Teensy liegen pro eingehendem Paket nur bei ca. **150 Bytes** und fallen mit rund **33 Hz** an. Dieses Volumen ist klein genug, um jedes Paket sofort weiterzuleiten, ohne das WLAN zu überlasten.

## 4.2 Zielwerte
- Jedes eingehende Teensy-Debugpaket wird **1:1 als einzelnes UDP-Datagramm** an den PC gesendet
- Typische UDP-Payload: ca. **150 Bytes**
- Datenrate: ca. **33 Hz × 150 Bytes** pro Satellit
- Latenzziel SAT → PC: **< 5 ms** im Normalbetrieb

## 4.3 Ablauf
1. UART/Diagnosepfad empfängt ein vollständiges Debugpaket vom Teensy.
2. Paket wird direkt in eine UDP-Sendenachricht überführt (inkl. `sat_id`, `seq`, `timestamp`, `flags`).
3. `taskUdpDebugTx` sendet das Paket ohne zusätzlichen 1400-Byte-Sammelbuffer sofort an den PC.
4. Bei Sendefehler: Zähler erhöhen, kein blockierendes Nachaggregieren; Debugdaten dürfen bei Last verworfen werden.
5. FreeRTOS-Prioritäten bleiben unverändert: `taskEspNowRxTx` behält Vorrang vor `taskUdpDebugTx`, damit die SAT-SAT-Kommunikation geschützt bleibt.

## 4.4 Begründung der Netzlast
- Selbst bei zwei Satelliten liegt die reine Debuglast nur bei wenigen Kilobyte pro Sekunde und bleibt damit weit unter einer kritischen WLAN-Auslastung.
- Der Verzicht auf Aggregation vermeidet zusätzliches Warten im Satelliten und verbessert die Sichtbarkeit im PC-Plotter.
- Der Echtzeitvorteil ist wichtiger als die kleine Protokollersparnis durch größere UDP-Blöcke.

---

## 5. Robustheit

- Sequenznummern für Control/Debug getrennt.
- Heartbeat zum PC (z. B. 5–10 Hz) mit Statusfeldern:
  - WiFi RSSI
  - Queue-Füllstände
  - Drop-Counter
  - letzter Kanal
- Watchdog-freundlich: Keine langen Blockaden im Loop.

---

## 6. Implementierungsreihenfolge in Firmware

1. STA + ESP-NOW Kanal-Sync stabilisieren.
2. Task-Prioritäten und Queues einführen.
3. UDP-Control-RX integrieren.
4. UDP-Debug als Echtzeit-Streaming ohne Aggregation aktivieren.
5. Telemetrie/Counter für Diagnose ergänzen.
