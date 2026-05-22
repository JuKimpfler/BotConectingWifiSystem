# V3.0 PC-GUI Erweiterung (Agent-optimierter Plan)

## Ziel
Im PC-GUI-Setup wird ein zusätzlicher **Debug-Tab** ergänzt, der eingehende Debug-Daten (`LS1..LS40`, `LB1..LB40`, `LW`) in mehreren grafischen Ansichten darstellt.

## Anforderungen (konkret)
- 40 Sensoren als Punkte auf einer Platinenansicht.
- Pro Sensor:
  - Analogwert `LSx` (0..255) als Zahl neben dem Punkt.
  - Bool `LBx` steuert den Farbstatus:
    - `false` = dunkelrot
    - `true` = hellrot (leuchtet)
- Winkelwert `LW` (-180..180) als Linie vom Mittelpunkt nach außen.
- Umsetzung als zusätzlicher Tab in der GUI mit moderner, klarer Darstellung.

## Umsetzungsplan
1. **V3-PC-GUI Einstiegspunkt anlegen**
   - Python/Tkinter GUI-Start in `PC_Hub_Migration/hub_core/main.py`.
   - CLI-Parameter kompatibel zu `tools/run-dev.ps1` halten.

2. **Debug-Datenaufnahme kapseln**
   - UDP-Empfänger in `PC_Hub_Migration/hub_core/debug_ingest.py`.
   - Unterstützte Eingangsformate:
     - JSON-Telemetrie (`{"name":"LS1","value":123}`)
     - JSON-Objekt mit mehreren Feldern (`{"LS1":123,"LB1":true,"LW":45}`)
     - Textpaare (`LS1=123,LB1=true,LW=45`)
   - Thread-sicher per Queue an GUI liefern.

3. **GUI-Struktur mit zusätzlichem Debug-Tab**
   - `PC_Hub_Migration/hub_core/gui_app.py`:
     - Haupt-Notebook mit mindestens `Übersicht` + `Debug`.
     - Moderner Dark-Style (Farben, Cards, klare Typografie).

4. **Mehrere grafische Ansichten im Debug-Bereich**
   - Sub-Notebook im Debug-Tab mit:
     - `Platine`: Sensorpunkte + Analogwerte + LW-Linie.
     - `Analog-Balken`: 40 Balken für LS-Werte.
     - `Live-Status`: kompakte Übersicht von Empfangsstatus und Datenqualität.

5. **Platinenansicht**
   - Stilisierte Board-Geometrie (einfach, aber klar als Platine erkennbar).
   - 40 Sensorpositionen ringförmig mit konsistenter Nummerierung.
   - Punktfarbe je `LBx`, Werttext je `LSx`.
   - LW-Linie aus Mittelpunkt, mit Winkelanzeige.

6. **Robustheit**
   - Werte-Clamping:
     - `LS`: 0..255
     - `LW`: -180..180
   - Ungültige Pakete zählen, GUI bleibt stabil.

7. **Validierung**
   - Vorhandene Repository-Unit-Tests laufen unverändert.
   - Python-Syntaxprüfung für neue PC-Hub-Dateien.

## Dateien
- `V3.0/README_PC_GUI_AGENT.md` (dieser Plan)
- `PC_Hub_Migration/hub_core/main.py`
- `PC_Hub_Migration/hub_core/debug_ingest.py`
- `PC_Hub_Migration/hub_core/gui_app.py`

## Akzeptanzkriterien
- GUI startet über `run-dev.ps1`-kompatiblen Einstiegspunkt.
- Debug-Tab ist vorhanden und enthält mehrere grafische Ansichten.
- `LS1..LS40`, `LB1..LB40`, `LW` werden korrekt visualisiert.
- Keine Regression in bestehenden Unit-Tests.
