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

## 4. UDP-Daten-Aggregation (~1400 Bytes)

## 4.1 Warum
Viele kleine Pakete erzeugen hohen Overhead. Aggregation erhöht Netto-Durchsatz deutlich.

## 4.2 Zielwerte
- UDP-Payload: ca. **1200–1400 Bytes**
- Flush-Bedingung A: Buffergröße erreicht
- Flush-Bedingung B: Timeout (z. B. 5–15 ms), um nicht unbegrenzt zu puffern

## 4.3 Ablauf
1. Debug-Bytes in Ringbuffer schreiben.
2. Aggregator kopiert in `tx_buffer` bis Zielgröße.
3. Header ergänzen (sat_id, seq, timestamp, flags).
4. UDP send.
5. Bei Sendefehler: Zähler erhöhen, optional einzelnes Retry, dann Drop.

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
4. UDP-Debug-Aggregation aktivieren.
5. Telemetrie/Counter für Diagnose ergänzen.
