# BCWS PC Hub — Cutover Runbook
> **Phase:** 8  
> **Date:** 2026-04

---

## Prerequisites

- [ ] PC Hub software installed and tested (`.\tools\setup.ps1` completed)
- [ ] At least one satellite reachable and heartbeat stable in ESP Hub
- [ ] USB bridge cable connected (ESP32 USB CDC bridge)
- [ ] Backup: ESP Hub firmware flashed and ready on a spare ESP32

---

## Step 1: Shadow Mode (Run Both Systems in Parallel)

1. Start PC Hub in shadow mode (no commands sent, RX only):
   ```powershell
   $env:BCWS_BRIDGE_COM_PORT = "COM3"   # Set to your actual port
   .\tools\run-dev.ps1 --shadow
   ```
2. Keep ESP Hub running normally.
3. Monitor PC Hub diagnostics: `http://127.0.0.1:8765/metrics`
4. Verify:
   - Both SAT1 and SAT2 show **ONLINE** in PC Hub UI
   - RX frame counts increase
   - `rx_parse_errors == 0`
   - Telemetry values match ESP Hub WebSocket output

Shadow mode duration: **≥ 15 minutes** with active satellite traffic.

---

## Step 2: Command Path Validation

1. Switch PC Hub to active mode:
   ```powershell
   .\tools\run-prod.ps1
   ```
2. Send one test command per satellite:
   - MODE M1 to SAT1 → verify ACK in UI
   - MODE M1 to SAT2 → verify ACK in UI
3. Verify telemetry continues to flow after commands.

---

## Step 3: Traffic Cutover

1. Power down ESP Hub (or disconnect it from WiFi AP).
2. Confirm satellites switch to P2P mode (SAT1↔SAT2 direct, expected — no hub required).
3. Confirm PC Hub continues receiving heartbeats and telemetry.
4. Connect browser to `http://<PC_IP>:8765/` — verify Live Plotter shows data.

---

## Step 4: Go-Live Checklist

- [ ] PC Hub `ready` endpoint returns 200: `curl http://127.0.0.1:8765/ready`
- [ ] Both satellites ONLINE in `/api/devices`
- [ ] Telemetry streaming in Plotter tab (no flat lines)
- [ ] MODE command to SAT1 → ACK status=ok
- [ ] MODE command to SAT2 → ACK status=ok
- [ ] `rx_parse_errors == 0` in `/metrics`
- [ ] Log file created at `logs/hub.log`
- [ ] DB file exists at `data/telemetry.db`
- [ ] History tab returns data for last 60s

---

## Timing

| Phase | Duration |
|-------|----------|
| Shadow mode | 15 min |
| Command validation | 5 min |
| Traffic cutover | 2 min |
| Total | ~22 min |
