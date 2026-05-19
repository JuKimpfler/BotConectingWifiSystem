# V3.0 – Netzwerk und Windows-Hotspot

## 1. Ziel
Parallelbetrieb von:
- ESP-NOW (SAT-SAT, Prio 1)
- WLAN/UDP (PC-SAT, Prio 2/3)

auf dem ESP32-C3 ohne Kanal-Konflikt.

---

## 2. Windows-Hotspot (Pflichtparameter)

### 2.1 Rahmenbedingungen
- PC: Windows 10/11
- Hotspot: **2.4 GHz** (ESP32-C3 ist 2.4 GHz)
- SSID/Passwort fix dokumentieren
- DHCP aktiv (Standard)

### 2.2 Konfigurationsvorgaben
- Hotspot möglichst auf **festen Kanal** betreiben (kein häufiger Kanalwechsel).
- Energiesparfunktionen der WLAN-Karte prüfen/deaktivieren, falls sie Kanal/Leistung dynamisch ändern.
- Gleiche Firewall-Regeln für App-Ports (UDP + WebSocket/HTTP) definieren.

> Wichtig: Wenn Windows den Kanal wechselt, kann ESP-NOW parallel instabil werden.

---

## 3. Zwingende Kanal-Synchronisation

## 3.1 Grundregel
ESP-NOW und STA-WLAN teilen ein Funkmodul. Daher muss ESP-NOW auf dem Kanal des verbundenen WLANs laufen.

## 3.2 Ablauf (Satellit)
1. WLAN-STA verbinden (`WiFi.begin(...)`).
2. Warten bis `WL_CONNECTED`.
3. Kanal ermitteln (z. B. via `WiFi.channel()`).
4. ESP-NOW initialisieren und Peers mit genau diesem Kanal setzen.
5. Bei Reconnect/Kanalwechsel: ESP-NOW sauber neu initialisieren.

## 3.3 Fehlerfälle
- **Kanal mismatch:** SAT-SAT Pakete verlieren/Timeout.
- **Hotspot springt Kanal:** nach Reconnect muss ESP-NOW neu gebunden werden.
- **STA Disconnect:** SAT-SAT kann kurz weiterlaufen, aber Re-Sync ist Pflicht vor stabilem Parallelbetrieb.

---

## 4. UDP-Topologie PC ↔ SAT

## 4.1 Ports (Beispiel)
- SAT Command In: `4000 + sat_id`
- PC Debug In: `5000`
- Heartbeat: `5100`

## 4.2 Transportregeln
- Steuerdaten: kleine UDP-Pakete, hohe Sendefrequenz, optional redundante Wiederholung.
- Debugdaten: große aggregierte UDP-Pakete, Burst-fähig, dropbar.

---

## 5. Netzwerk-Sicherheitsbasis

- WPA2/WPA3 für Hotspot.
- Optional App-internes Session-Token in Nutzdaten.
- Eingehende UDP-Pakete im PC-Hub strikt nach Quell-IP/Port/Frame-Signatur validieren.

---

## 6. Verifikation (Abnahme)

1. SAT1/SAT2 gleichzeitig im Hotspot verbunden.
2. ESP-NOW Ping zwischen SAT1/SAT2 stabil.
3. Steuerpakete vom PC unter Debug-Last weiterhin mit niedriger Latenz.
4. Debug-Burst (mehrere 100 KB/s kumuliert) ohne Zusammenbruch des Steuerpfads.
