# BCWS Hub Migration Summary — ESP32-C3 → Windows PC

> **Version:** 1.0  
> **Date:** 2026-04  
> **Scope:** Hub-only migration. Satellite firmware and Teensy libraries are unchanged.  
> **Target platform:** Windows 10/11 x64 (primary), Linux/macOS secondary

---

## 1. Executive Summary

The BotConectingWifiSystem (BCWS) currently uses an **ESP32-C3** as its hub, managing two ESP satellite robots (each driven by a Teensy 4.0) over ESP-NOW. The hub is responsible for:

- Receiving telemetry from both satellites
- Relaying operator control commands (speed/angle/mode/calibration)
- Hosting a browser-based web UI (currently LittleFS + WebSocket on the ESP)
- Maintaining heartbeat and peer liveness detection

Limitations of the current ESP32-C3 hub:
- Single-core (RISC-V), ~100 KB usable heap
- WebSocket clients limited by RAM
- No persistent telemetry storage
- No high-frequency plotter capability (capped at 20 Hz to browser)
- Minimal debugging tools

**Migration goal:** Run the hub as a Windows background service, connected to the ESP-NOW radio layer via a USB-attached ESP32 bridge device. Satellites and Teensys are completely unchanged.

Performance targets after migration:
- Telemetry ingestion ≥ 40 Hz per satellite (2× improvement)
- End-to-end latency (ESP-NOW → WebSocket → browser) < 20 ms typical
- Persistent telemetry history (SQLite, 24+ h retention)
- High-frequency live plotter via WebSocket (up to 50 Hz UI update)
- Zero-downtime satellite operation during hub restart

---

## 2. Repository Context

```
BotConectingWifiSystem/
├── ESP_Hub/          ← current hub firmware (will be preserved, not deleted)
├── ESP_Satellite/    ← unchanged; both SAT1 and SAT2 run this firmware
├── Teensy_lib/       ← unchanged; Teensy 4.0 library
├── shared/           ← protocol definitions; READ-ONLY during migration
│   ├── messages.h    ← Frame_t, msg types, roles, flags — the protocol contract
│   ├── crc16.h       ← CRC-16/IBM implementation
│   └── config_*.json ← configuration schema and defaults
├── PC_Hub_Migration/ ← NEW: this migration deliverable
│   ├── docs/
│   │   ├── protocol_v1.md       ← frozen protocol specification
│   │   └── hub_migration_summary.md  ← this file
│   └── tools/
│       ├── setup.ps1       ← Windows bootstrap script
│       ├── run-dev.ps1     ← developer run script
│       └── simulator.ps1   ← two-satellite telemetry simulator
└── test/             ← existing unit tests (unchanged)
```

### 2.1 What Must Not Change

| Item | Reason |
|---|---|
| `shared/messages.h` | Protocol contract frozen for v1 |
| `ESP_Satellite/` firmware | Satellite behaviour is correct and tested |
| `Teensy_lib/` library | Teensy control interface is stable |
| Wire format of `Frame_t` | Hardware compatibility |
| CRC algorithm (CRC-16/IBM) | Hardware compatibility |
| `network_id = 0x01` | Anti-mis-pairing |
| ESP-NOW channel 6 | Default used by all existing devices |

---

## 3. Architecture: PC Hub

### 3.1 Component Overview

```
  ┌─────────────────────────────────────────────────────────┐
  │                   Windows PC Hub                        │
  │                                                         │
  │  ┌──────────────┐    ┌──────────────┐   ┌───────────┐  │
  │  │  USB Bridge  │───▶│   Ingress    │──▶│  Message  │  │
  │  │  (Serial RX) │    │   Parser     │   │   Bus     │  │
  │  └──────────────┘    └──────────────┘   └─────┬─────┘  │
  │         ▲                                      │        │
  │         │ TX                          ┌────────▼──────┐ │
  │         │                             │  Processors   │ │
  │  ┌──────┴───────┐                     │  - Telemetry  │ │
  │  │  Command     │◀────────────────────│  - Heartbeat  │ │
  │  │  Serializer  │                     │  - ACK mgr    │ │
  │  └──────────────┘                     └───────┬───────┘ │
  │                                               │         │
  │                                    ┌──────────▼───────┐ │
  │                                    │    Storage        │ │
  │                                    │    (SQLite WAL)   │ │
  │                                    └──────────┬───────┘ │
  │                                               │         │
  │                               ┌───────────────▼──────┐  │
  │                               │  WebSocket + REST API │  │
  │                               └───────────────┬──────┘  │
  └───────────────────────────────────────────────┼─────────┘
                                                  │
                              ┌───────────────────▼──────────┐
                              │   Browser / Plotter UI       │
                              └──────────────────────────────┘

  External (unchanged):
  ┌──────────────┐   ESP-NOW   ┌─────────────────────────────┐
  │ USB Bridge   │◀──────────▶│ SAT1 (ESP32-C3 + Teensy 4.0)│
  │ ESP32-C3     │             └─────────────────────────────┘
  │ (adapter)    │   ESP-NOW   ┌─────────────────────────────┐
  └──────────────┘◀──────────▶│ SAT2 (ESP32-C3 + Teensy 4.0)│
                               └─────────────────────────────┘
```

### 3.2 Technology Stack (Recommended)

| Layer | Technology | Rationale |
|---|---|---|
| Hub backend | Python 3.11+ (asyncio) | Fast iteration, rich ecosystem, easy debug |
| Serial bridge | `pyserial-asyncio` | Async COM port, no thread overhead |
| Message bus | `asyncio.Queue` (bounded) | Zero-dependency, low latency |
| Storage | SQLite 3 (WAL mode) | Embedded, fast batch writes, no extra service |
| API | `websockets` + `aiohttp` | Async, low overhead |
| Browser UI | Vanilla JS + Chart.js or ECharts | No build step required for dev |
| Windows service | NSSM or Task Scheduler | Easy setup, restart on crash |

Performance upgrade path: If Python CPU load exceeds 60% at target rate → migrate hot path (ingress parser + queue insert) to a C extension or a small C++ sidecar process with a shared-memory ring buffer.

---

## 4. Phased Rollout Plan

### Phase 0 — Protocol Freeze & Environment Setup (Day 0–1)

**Goal:** Stable contract and working dev environment before any hub code is written.

Tasks:
- [x] Create `PC_Hub_Migration/docs/protocol_v1.md` (frozen protocol specification)
- [x] Create `PC_Hub_Migration/tools/setup.ps1` (environment bootstrap)
- [ ] Flash USB bridge firmware to an ESP32-C3/C6
  - Bridge firmware: minimal sketch that reads `Frame_t` from ESP-NOW and relays over USB CDC at 921600 baud with SOF framing `0xAA 0x55 <len_LE> <frame_bytes>`
- [ ] Validate bridge by running `setup.ps1` and observing heartbeat frames in console

Exit: `setup.ps1` runs clean, COM port detected, protocol_v1.md approved by team.

---

### Phase 1 — Minimal Hub Skeleton (Day 1–3)

**Goal:** PC can receive telemetry and respond with heartbeats. Satellites continue to work.

Tasks:
- [ ] `hub_core/ingress.py` — async serial reader, SOF frame sync, CRC validation
- [ ] `hub_core/frame_parser.py` — deserialize `Frame_t` to Python dataclass
- [ ] `hub_core/heartbeat.py` — periodic heartbeat TX to SAT1 and SAT2
- [ ] `hub_core/peer_tracker.py` — online/offline state machine (§11 of protocol_v1.md)
- [ ] Coloured console log: incoming frame type, satellite ID, seq, latency
- [ ] Config: `config/hub_config.yaml` with COM port, channel, network_id, log level

Exit criterion: `run-dev.ps1` shows live telemetry from both satellites with < 100 ms visible latency, no crashes over 10 min.

---

### Phase 2 — Storage Pipeline (Day 3–6)

**Goal:** All telemetry persisted; queryable history available.

Tasks:
- [ ] `hub_core/storage.py` — async SQLite writer, WAL mode, batch flush every 200 ms
- [ ] Schema:
  - `samples(id, ts_real, sat_role, stream_name, vtype, value_i, value_f, value_s)`
  - `events(id, ts_real, sat_role, event_type, detail_json)`
  - `commands(id, ts_real, sat_role, cmd_type, payload_json, status, retries)`
  - `peer_status(id, ts_real, sat_role, online, uptime_ms, rssi, queue_len)`
- [ ] Retention policy: purge rows older than 24 h (configurable)
- [ ] Benchmark write throughput: target ≥ 1000 rows/s sustained

Exit criterion: 1 h run with two simulated satellites at 40 Hz each → all samples in DB, zero write errors.

---

### Phase 3 — Command Path & ACK Manager (Day 4–7)

**Goal:** PC hub can issue reliable commands (ctrl/mode/cal) with retry/ACK.

Tasks:
- [ ] `hub_core/command_router.py` — translate WebSocket JSON → `Frame_t` and send via bridge
- [ ] `hub_core/ack_manager.py` — track pending ACKs, retry on timeout (500 ms × 3), report result
- [ ] WebSocket endpoint: `ws://localhost:8765/ws`
  - Accept `ctrl`, `mode`, `cal` messages (see protocol_v1.md §12.2)
  - Emit `command_ack` on completion/failure
- [ ] Command audit log (CSV/SQLite)

Exit criterion: Send `mode 2` to SAT1 via WebSocket → satellite executes → ACK received → browser notified, confirmed in logs.

---

### Phase 4 — Observability & Debug Tools (Day 6–9)

**Goal:** Any failure diagnosable within 10 minutes using built-in tools.

Tasks:
- [ ] Per-satellite status dashboard page (HTML):
  - online/offline, last seen, RSSI, uptime, packet loss %
- [ ] Message trace viewer: filterable log of all frames (type, seq, direction)
- [ ] REST endpoints:
  - `GET /health` → `{"status": "ok", "uptime_s": 300}`
  - `GET /metrics` → error counters JSON
  - `GET /telemetry/history?sat=SAT1&stream=Speed&since=<ts>&limit=1000`
- [ ] Rolling log files: `logs/hub_YYYY-MM-DD.log` (JSON lines)
- [ ] Daily log rotation, keep last 7 days

Exit criterion: Introduce a 30 % packet loss in simulator → hub logs `rx_crc_errors` in metrics, UI shows satellite RSSI degraded.

---

### Phase 5 — Live Plotter UI (Day 8–12)

**Goal:** Smooth real-time visualisation of dual-satellite telemetry.

Tasks:
- [ ] `hub_ui/index.html` — minimal single-page app
- [ ] WebSocket consumer: subscribe to selected streams at chosen rate (≤ 50 Hz)
- [ ] Live chart component: Chart.js or ECharts (prefer ECharts for performance)
  - Scrolling time-series per stream
  - Multi-satellite overlay
  - Pause / Resume
  - Export to CSV
- [ ] Historical query panel: select time range, replay data
- [ ] Alarms: highlight stream if value outside configured band

Exit criterion: UI shows dual-satellite telemetry at 40 Hz with no visible stutter on mid-range PC.

---

### Phase 6 — Shadow Mode & Cutover (Day 12–15)

**Goal:** Validate PC hub against live system before switching command authority.

Tasks:
- [ ] Run PC hub in **read-only shadow mode** alongside original ESP hub
  - PC hub receives all frames (passive sniff via bridge)
  - PC hub does NOT send commands or heartbeats
- [ ] Compare telemetry stream data between ESP hub log and PC hub log
- [ ] Verify no timing degradation on satellite side
- [ ] Cutover checklist:
  1. Stop ESP hub heartbeats (power off ESP hub device)
  2. PC hub enters active mode (sends heartbeats)
  3. Confirm both satellites show ONLINE within 5 s
  4. Send test `mode 1` to each satellite → verify ACK
  5. Run full 10-min mission profile
- [ ] Keep flashed ESP hub device available for rollback

Exit criterion: PC hub operates independently for 30 min; zero satellite disconnects; telemetry history matches shadow comparison within 0.1 %.

---

## 5. Risk Register

| Risk | Probability | Impact | Mitigation |
|---|---|---|---|
| USB bridge serial glitch causes frame corruption | Medium | Medium | CRC validation + SOF sync recovery logic |
| Python event loop stalls under burst telemetry | Low | High | Bounded queues + backpressure; profiling before deployment |
| SQLite write contention | Low | Medium | WAL mode + batch writes every 200 ms |
| Windows COM port numbering changes | Low | Low | Config file COM port setting; auto-detect logic in setup.ps1 |
| Protocol drift between PC hub and satellite | Low | High | Protocol v1 frozen; schema validation at ingest |
| PC enters sleep/idle during mission | Medium | High | Windows power plan: disable sleep, run as service |
| ACK storms (both satellites ACK simultaneously) | Low | Low | Per-satellite async ACK queues; seq wraps gracefully |
| Satellite P2P disrupted during hub switchover | Low | Medium | P2P is sat-to-sat; hub switchover does not affect it |

---

## 6. Rollback Plan

### 6.1 Immediate Rollback (< 5 minutes)

1. Stop PC hub service: `Stop-Service BCWSHub` (or close `run-dev.ps1`)
2. Power on original ESP32-C3 hub device
3. Satellites will reconnect to ESP hub on next heartbeat cycle (< 4 s)
4. Confirm satellites ONLINE in ESP hub web UI at `192.168.4.1`

### 6.2 Full Rollback (< 15 minutes)

1. Immediate rollback steps above
2. If ESP hub firmware was modified during migration: reflash from `ESP_Hub/` using PlatformIO
3. Restore config from `shared/config_default.json` if needed
4. Verify satellite telemetry appears in ESP hub web UI

### 6.3 Data Preservation

SQLite database and logs remain on PC regardless of rollback. No satellite or Teensy data is at risk.

---

## 7. Performance Targets & Acceptance Criteria

| Metric | Baseline (ESP Hub) | Target (PC Hub) | Hard Limit |
|---|---|---|---|
| Max telemetry rate per satellite | 20 Hz | 40 Hz | 50 Hz |
| Ingress → WebSocket latency (p95) | ~80 ms | < 20 ms | < 100 ms |
| Peer offline detection latency | 4 s | 4 s ± 200 ms | 4.5 s |
| Command round-trip (send → ACK) | 500–1500 ms | < 200 ms (typical) | 1500 ms |
| UI plotter frame rate | 20 Hz | 40 Hz | 50 Hz |
| DB write throughput | N/A | ≥ 1000 rows/s | — |
| 24 h soak test (no crash) | Passes | Must pass | — |
| Setup time (fresh PC) | N/A | ≤ 15 min | — |
| Failure diagnosis time | >30 min | ≤ 10 min | — |

---

## 8. Windows Service Setup (Summary)

Full details in `tools/setup.ps1`. Quick reference:

```powershell
# 1. Bootstrap environment (run once)
.\PC_Hub_Migration\tools\setup.ps1

# 2. Start hub in dev mode (foreground, coloured logs)
.\PC_Hub_Migration\tools\run-dev.ps1

# 3. Start simulator (separate terminal)
.\PC_Hub_Migration\tools\simulator.ps1

# 4. Install as Windows service (production)
.\PC_Hub_Migration\tools\setup.ps1 -InstallService
```

Recommended Windows power settings for hub operation:
- Control Panel → Power Options → High Performance
- Sleep: Never
- Hibernate: Off
- USB selective suspend: Disabled

---

## 9. Security Considerations

- PC hub listens only on `127.0.0.1` by default (local machine)
- To expose on LAN: change `bind_host` in `config/hub_config.yaml` to `0.0.0.0`
- No authentication in v1 (LAN-only, trusted environment)
- COM port access requires no elevation on modern Windows (user in `dialout` group not needed)
- SQLite database stored in `data/` subdirectory — no special permissions needed

---

## 10. Immediate Next Steps

1. **Run `setup.ps1`** to prepare the Windows environment.
2. **Flash USB bridge firmware** to a spare ESP32-C3 (see Phase 0).
3. **Run `simulator.ps1`** to validate the full software stack before connecting real hardware.
4. **Implement Phase 1 hub skeleton** starting with `hub_core/ingress.py`.
5. **Benchmark** after Phase 2 storage is added.

---

## Appendix A: File Structure for PC Hub Implementation

Recommended layout within `PC_Hub_Migration/` (to be created during implementation phases):

```
PC_Hub_Migration/
├── docs/
│   ├── protocol_v1.md          ← protocol spec (this repo, frozen)
│   └── hub_migration_summary.md ← this file
├── tools/
│   ├── setup.ps1               ← Windows bootstrap
│   ├── run-dev.ps1             ← dev mode launcher
│   └── simulator.ps1           ← two-satellite simulator
├── hub_core/                   ← Phase 1–3: to be created
│   ├── main.py
│   ├── ingress.py
│   ├── frame_parser.py
│   ├── heartbeat.py
│   ├── peer_tracker.py
│   ├── command_router.py
│   ├── ack_manager.py
│   └── storage.py
├── hub_ui/                     ← Phase 5: to be created
│   └── index.html
├── config/
│   └── hub_config.yaml         ← runtime configuration
├── data/                       ← SQLite database (runtime)
├── logs/                       ← rolling log files (runtime)
└── requirements.txt            ← Python dependencies
```

---

## Appendix B: Configuration Reference (hub_config.yaml)

```yaml
# BCWS PC Hub Configuration
hub:
  bind_host: "127.0.0.1"
  ws_port: 8765
  rest_port: 8766
  log_level: "INFO"         # DEBUG / INFO / WARNING / ERROR
  log_dir: "logs"

bridge:
  com_port: "COM3"          # USB bridge COM port (auto-detect if "auto")
  baud_rate: 921600
  network_id: 0x01
  channel: 6

telemetry:
  max_rate_hz: 50           # Max WebSocket push rate per stream
  batch_flush_ms: 200       # SQLite batch write interval

heartbeat:
  interval_ms: 1000
  timeout_ms: 4000

ack:
  timeout_ms: 500
  max_retries: 3

storage:
  db_path: "data/telemetry.db"
  retention_hours: 24
  vacuum_interval_hours: 6

satellites:
  SAT1:
    role: 1
    mac: ""                 # Leave empty for auto-discovery
  SAT2:
    role: 2
    mac: ""
```
