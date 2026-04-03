# BCWS PC Hub — Architecture Decisions
> **Phase:** 1 — Architecture Freeze  
> **Date:** 2026-04  
> **Status:** Frozen for implementation

---

## ADR-001: Python asyncio as Hub Backend

**Decision:** Use Python 3.11+ with `asyncio` as the hub runtime.

**Rationale:**
- Fast iteration and rich ecosystem (pyserial-asyncio, aiosqlite, aiohttp, websockets)
- Single event loop eliminates locking overhead on hot paths
- Easy debugging: structured logging, REPL inspection, no recompile cycle
- Migration path available: replace hot-path ingress with C extension if CPU > 60%
- Matches recommendation in `hub_migration_summary.md` §3.2

**Alternatives considered:**
- C++ (too much boilerplate, slower iteration, no benefit for I/O-bound hub)
- Node.js (weaker binary protocol handling, less familiar to embedded team)

---

## ADR-002: USB Bridge Framing with SOF Sync Recovery

**Decision:** Implement a streaming SOF-sync byte-by-byte scanner in `ingress.py` that re-syncs on `0xAA 0x55` after any framing error.

**Rationale:**
- USB CDC serial can deliver partial writes or corrupt data on reconnect
- A simple state machine (`WAIT_SOF1 → WAIT_SOF2 → READ_LEN → READ_FRAME`) handles partial frames without blocking
- On CRC failure: discard frame, log error, increment counter, continue scanning

**Source:** `protocol_v1.md` §2.2, §9.1

---

## ADR-003: In-Memory asyncio.Queue as Message Bus

**Decision:** Use `asyncio.Queue(maxsize=512)` as the internal message bus between ingress and processors.

**Rationale:**
- Zero-dependency, zero-latency in the happy path
- `maxsize=512` provides backpressure: if processors fall behind, older frames are dropped with a log warning (bounded memory)
- Avoids threading overhead

---

## ADR-004: SQLite WAL Mode for Telemetry Storage

**Decision:** Use SQLite 3 in WAL (Write-Ahead Logging) mode with `aiosqlite` and batch writes every 200 ms.

**Rationale:**
- Single-file, zero-install, adequate for ≥1000 rows/s on modern SSD
- WAL allows concurrent readers while writing
- Batch writes amortize transaction overhead
- Matches `hub_migration_summary.md` §3.2 recommendation

**Schema:**
```sql
samples(id, ts_real, sat_role, stream_name, vtype, value_i, value_f, value_s, ts_sat_ms)
events(id, ts_real, sat_role, event_type, detail_json)
commands(id, ts_real, sat_role, cmd_type, payload_json, status, retries)
peer_status(id, ts_real, sat_role, online, uptime_ms, rssi, queue_len)
```

---

## ADR-005: ACK/Retry is Hub-Side (New Feature)

**Decision:** Implement ACK/retry on the PC Hub side, not relying on the ESP Hub firmware (which does not implement retry).

**Rationale:**
- Code analysis (`ESP_Hub/src/CommandRouter.cpp::_buildAndSend()`) confirms ESP hub sends commands once with no retry loop
- The `AckManager` in `ESP_Satellite/src/AckManager.cpp` is the satellite's own retry queue for its outbound messages
- Adding hub-side ACK/retry improves command reliability with zero firmware changes
- Parameters: `ACK_TIMEOUT_MS=500`, `ACK_MAX_RETRIES=3` from `protocol_v1.md` §8.2

---

## ADR-006: WebSocket + REST via aiohttp

**Decision:** Use `aiohttp` for both the REST API and WebSocket endpoint.

**Rationale:**
- Single library for both HTTP and WebSocket, one server process
- Native asyncio integration, no thread overhead
- Default port: WS on `/ws`, REST on same server (port 8765)

**Endpoints:**
- `GET /health` — liveness check
- `GET /ready` — readiness check (bridge connected)
- `GET /metrics` — error counters JSON
- `GET /api/devices` — peer status
- `GET /api/telemetry/history` — historical samples query
- `WS /ws` — bidirectional command/telemetry stream

---

## ADR-007: Rate-Limited WebSocket Push

**Decision:** Implement per-stream rate limiting in the WebSocket broadcaster (default ≤50 Hz).

**Rationale:**
- Browser rendering at >60 FPS is wasteful; Chart.js/ECharts handle ~50 Hz well
- Prevents UI stutter under burst telemetry
- Configurable via `subscribe` message or `telemetry.max_rate_hz` in config

---

## ADR-008: Preserve ESP Hub Firmware (No Deletion)

**Decision:** The `ESP_Hub/` directory and all satellite/Teensy files are read-only for this migration.

**Rationale:**
- Rollback capability: flashing the original ESP hub takes < 5 minutes
- No risk of breaking satellite compatibility
- `shared/messages.h` and `shared/crc16.h` are the frozen protocol contract

---

## ADR-009: Configuration via YAML + Environment Overrides

**Decision:** Primary config in `config/hub_config.yaml`; environment variables override any value using prefix `BCWS_`.

**Rationale:**
- YAML is human-readable, easy to edit on Windows
- Environment overrides allow CI/test injection without modifying files
- No secrets in config file (COM port is not a secret)

---

## ADR-010: Structured JSON Logging + Human Console

**Decision:** Log in JSON Lines format to rotating files (`logs/hub_YYYY-MM-DD.log`) while simultaneously emitting coloured human-readable output to the console.

**Rationale:**
- JSON logs are machine-parseable for post-hoc analysis
- Console output aids live debugging during development
- Python's `logging` module with a custom JSON formatter satisfies both
- Daily rotation with 7-day retention matches `hub_migration_summary.md` §4 Phase 4

---

## ADR-011: Browser UI as Single Static HTML File (No Build Step)

**Decision:** `hub_ui/index.html` is a self-contained single-page app with inlined CSS/JS. CDN links for Chart.js/ECharts.

**Rationale:**
- Zero build toolchain — developer opens file directly or serves via hub's HTTP
- Matches "easy setup" goal: no npm, no webpack
- ECharts chosen over Chart.js for better performance at 50 Hz (canvas-based renderer)

---

## ADR-012: CRC Implementation Ported to Python

**Decision:** Port `shared/crc16.h` CRC-16/IBM algorithm to Python in `hub_core/protocol.py`.

**Rationale:**
- Must match exactly: polynomial 0xA001, init 0xFFFF
- Test vector from `protocol_v1.md` §14.1 is used as an automated unit test
- Python implementation is fast enough at target frame rate (40 Hz × 2 sat = 80 frames/s)
