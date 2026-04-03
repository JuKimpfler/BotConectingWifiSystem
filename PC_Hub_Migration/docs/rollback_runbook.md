# BCWS PC Hub — Rollback Runbook
> **Phase:** 8  
> **Date:** 2026-04

---

## When to Roll Back

Trigger rollback if any of the following occur after cutover:
- Both satellites show OFFLINE for > 30 seconds
- Command ACK failure rate > 50% over 5 minutes
- Telemetry stops completely (rx_frames not incrementing)
- PC crashes or process exits unexpectedly

---

## Rollback Steps (< 5 minutes)

### 1. Stop PC Hub

```powershell
# In the terminal running run-prod.ps1, press Ctrl+C
# Or:
Stop-Process -Name python -Force
```

### 2. Power on / connect ESP Hub

- If ESP Hub is available: connect it to USB power
- Satellites will automatically reconnect to ESP Hub via ESP-NOW within 4 seconds (heartbeat timeout)

### 3. Verify ESP Hub is running

- Open `http://<ESP_HUB_IP>/` in browser
- Confirm SAT1 + SAT2 show online
- Send test command to verify command path

### 4. Diagnose PC Hub failure

```powershell
# View last log lines
Get-Content logs\hub.log -Tail 50

# Collect full diagnostics
.\tools\collect-diagnostics.ps1
```

---

## ESP Hub Firmware Recovery

If ESP Hub firmware is corrupted or unavailable:

```powershell
# Flash from repo
cd ESP_Hub
pio run -e esp_hub_light -t upload
pio run -e esp_hub_light -t uploadfs
```

Default ESP Hub WiFi SSID: `BCWS_HUB` (see ESP_Hub/include/hub_config.h).

---

## Data Recovery

- Telemetry DB is at `data/telemetry.db` (SQLite WAL, safe to copy)
- Log files are at `logs/hub_YYYY-MM-DD.log`
- No data loss: PC Hub writes to disk every 200 ms

---

## Re-attempting Cutover

After fixing the root cause:
1. Review `logs/hub.log` for error messages
2. Run tests: `python -m unittest discover -s tests -v`
3. Fix issue and restart from Shadow Mode (Step 1 of cutover)
