# BCWS PC Hub — Test Plan
> **Phase:** 7  
> **Date:** 2026-04

---

## 1. Test Layers

### 1.1 Unit Tests (`tests/test_protocol.py`)
| Test | What it verifies |
|------|-----------------|
| `TestCRC16::test_test_vector_a` | CRC-16/MODBUS known vector "123456789" = 0x4B37 |
| `TestCRC16::test_firmware_crc_convention` | CRC embedded at payload[len:len+2], not offset 188 |
| `TestCRC16::test_roundtrip_frame_crc` | to_bytes() produces valid embedded CRC |
| `TestFrameSerialisation::test_roundtrip` | from_bytes(to_bytes(f)) reproduces all fields |
| `TestFrameSerialisation::test_wire_bytes_length` | SOF header + frame = 194 bytes |
| `TestFrameValidation::test_valid_frame_passes` | Valid frame returns None |
| `TestFrameValidation::test_bad_magic` | Returns "bad_magic" error |
| `TestFrameValidation::test_bad_network_id` | Returns "bad_network_id" error |
| `TestFrameValidation::test_crc_mismatch` | Returns "crc_mismatch" error |
| `TestFrameValidation::test_len_overflow` | Returns "len_overflow" error |
| `TestFrameValidation::test_unknown_msg_type` | Returns "unknown_msg_type" error |
| `TestFrameBuilders::*` | build_heartbeat/ctrl/mode/cal produce valid frames |
| `TestPayloadParsers::*` | All payload parsers decode correctly |

### 1.2 Unit Tests (`tests/test_ack_manager.py`)
| Test | What it verifies |
|------|-----------------|
| `test_ack_resolves_future` | On-time ACK resolves future with status=ok |
| `test_rejected_ack` | ACK_ERR_BUSY resolves with status=busy |
| `test_duplicate_ack_ignored` | Second ACK for same seq does not raise |
| `test_send_error_resolves_future` | Send failure resolves immediately |
| `test_ack_wrong_sat_id_not_resolved` | ACK from wrong satellite does not resolve |
| `test_timeout_after_retries` | Exhausted retries → status=timeout |
| `test_retry_sends_frame_again` | Retry calls ingress.send N+1 times |

### 1.3 Unit Tests (`tests/test_peer_tracker.py`)
| Test | What it verifies |
|------|-----------------|
| `test_initial_state_is_offline` | Fresh peer starts OFFLINE |
| `test_first_frame_transitions_to_connecting` | Any frame → CONNECTING |
| `test_heartbeat_transitions_connecting_to_online` | Heartbeat → ONLINE |
| `test_heartbeat_updates_rssi` | RSSI/queue_len/uptime updated from heartbeat |
| `test_hub_frame_ignored` | ROLE_HUB source frames ignored |
| `test_timeout_moves_peer_offline` | Expired last_seen → OFFLINE |
| `test_no_timeout_within_window` | Fresh frame prevents timeout |
| `test_offline_peer_not_timed_out` | OFFLINE peer stays out of timeout list |
| `test_discovery_updates_mac` | Discovery updates MAC |
| `test_status_dict_structure` | status_dict returns SAT1+SAT2 |

### 1.4 Integration Tests (`tests/test_integration.py`)
| Test | What it verifies |
|------|-----------------|
| `test_valid_frame_enqueued` | Valid raw bytes → queue |
| `test_crc_error_not_enqueued` | Corrupted frame → dropped |
| `test_bad_magic_increments_counter` | Bad magic → diagnostic counter |
| `test_heartbeat_triggers_peer_online` | SAT heartbeat → peer ONLINE state |
| `test_telemetry_stored_and_broadcast` | MSG_DBG → storage.add_sample called |
| `test_telemetry_broadcast_format` | WS broadcast has correct JSON keys |
| `test_ack_resolves_command` | WS ctrl command + ACK frame → command_ack broadcast |
| `test_two_satellite_traffic` | SAT1 + SAT2 frames both stored independently |

---

## 2. Running Tests

```bash
cd PC_Hub_Migration
python -m unittest discover -s tests -v
```

Expected: **54 tests, 0 failures**

---

## 3. Performance KPIs

| KPI | Target | Measurement Method |
|-----|--------|--------------------|
| E2E latency (frame received → WS broadcast) | < 5 ms | Timestamp comparison in test_integration |
| Telemetry ingress rate | ≥ 40 Hz × 2 satellites | Soak test (not automated yet) |
| WS push rate cap | ≤ 50 Hz per stream | Rate limiter in command_router |
| CPU usage at 80 frames/s | < 30% single core | Windows Task Manager / py-spy |
| Memory footprint | < 100 MB RSS | Process monitor during soak |
| SQLite write throughput | ≥ 1000 rows/s | storage_flush timer |

---

## 4. Soak Test (Manual)

Duration: 30 min continuous with simulated satellite traffic.

Steps:
1. Start hub: `python -m hub_core.main`
2. Run simulator: `.\tools\simulator.ps1`
3. Monitor: `http://localhost:8765/` → Diagnostics tab
4. After 30 min, check:
   - `rx_parse_errors == 0`
   - `tx_ack_timeouts < 5`
   - Memory stable (no leak)
   - DB file size reasonable

---

## 5. Known Limitations / Nice-to-Haves

- No automated soak test (manual only)
- No automated E2E latency measurement test
- `MSG_SETTINGS` frame builder not implemented (future)
- `MSG_PAIR` broadcast not implemented (future)
- No TLS/WSS for the WebSocket endpoint
- No authentication on REST API
