# Bridge (SAT1 ↔ SAT2)

Diese Datei beschreibt die direkte Kommunikation zwischen den Satelliten.

## 1. Ziel

Die Satelliten sollen Nutzdaten der beiden Teensys untereinander austauschen, auch wenn der Hub nicht aktiv mitmischt.

---

## 2. Routing-Regel im Satellite

Eingehende UART-Zeile vom Teensy:

1. Zeile beginnt mit `DBG:`
   - Parse als Telemetrie
   - Sende als `MSG_DBG` Richtung Hub

2. Zeile ohne `DBG:`
   - Behandele als transparente Nutzdaten
   - Sende als `MSG_UART_RAW` an Peer-Satellite

Am Ziel-Satellite wird `MSG_UART_RAW` wieder auf die lokale Teensy-UART geschrieben.

---

## 3. Verhalten ohne Hub

- SAT↔SAT P2P-Verkehr kann weiterlaufen, wenn Hub offline ist.
- Hub-Heartbeat beeinflusst Hub-Online-Status, aber nicht die grundsätzliche P2P-Bridge-Fähigkeit.

---

## 4. Peer-Lernen

Satellites lernen Peer-MACs aus empfangenen Frames und speichern diese in NVS.
Zusätzlich können Discovery-Frames genutzt werden, bis ein Peer bekannt ist.

---

## 5. Typische Debug-Punkte

- Keine P2P-Daten?
  - Prüfen, ob Zeilen fälschlich mit `DBG:` beginnen
  - Peer-MAC in NVS prüfen (`mac`/`debug` USB-Kommando)
- Falsches Zielsystem?
  - `network_id` von Hub/Satellites abgleichen

---

## 6. Relevante Message-Typen

- `MSG_DISCOVERY`
- `MSG_UART_RAW`
- `MSG_HEARTBEAT`
- `MSG_ACK`
