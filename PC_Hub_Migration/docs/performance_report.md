# BCWS PC Hub — Performance Report
> **Date:** 2026-04  
> **Status:** Initial baseline (no real hardware available in sandbox)

---

## 1. Theoretical Throughput

| Metric | Calculation | Result |
|--------|-------------|--------|
| Max ESP-NOW frame rate | 250 bytes × 40 Hz × 2 sats | 20 kB/s inbound |
| USB CDC throughput | 921600 baud, 194 byte frames | ~4743 frames/s max |
| Python asyncio overhead | ~0.1 ms/frame dispatch (estimated) | Bottleneck: serial read |
| SQLite WAL write speed | ~5000–50000 rows/s on SSD | Not bottleneck at 80 Hz |

---

## 2. Design Choices for Low Latency

- **asyncio single event loop**: No thread synchronisation overhead
- **0.5 ms poll interval** in ingress serial read: worst-case 0.5 ms add to E2E latency
- **asyncio.Queue(maxsize=512)**: O(1) enqueue/dequeue, no copy
- **Rate-limited WS push (50 Hz)**: Prevents browser stutter, not a latency bottleneck
- **Batch SQLite writes (200 ms)**: Async, off the hot path
- **No blocking I/O** in frame dispatch path

---

## 3. Expected E2E Latency

```
ESP-NOW RX on satellite → USB bridge → PC ingress → queue → dispatch → WS push
    ≈ 1 ms             +   0 ms    +   0.5 ms  +  0 ms  +  0.05 ms +  <1 ms
                                                                    = ≈ 2–3 ms total
```

Note: The actual serial port polling adds ~0.5 ms worst case per the 0.5 ms sleep.
For sub-ms latency, replace polling sleep with `asyncio.get_event_loop().run_in_executor`
with blocking `serial.read()`.

---

## 4. Measured KPIs (Placeholder)

| KPI | Target | Measured | Notes |
|-----|--------|----------|-------|
| E2E latency | < 5 ms | Not yet measured | Pending real hardware |
| Peak frame rate | ≥ 80 frames/s | Estimated ~4700/s | Serial bandwidth limit |
| CPU (80 fps) | < 30% | Not yet measured | Pending hardware test |
| RAM footprint | < 100 MB | Not yet measured | |
| SQLite throughput | ≥ 1000 rows/s | Estimated ≥ 5000 rows/s | WAL mode |

---

## 5. Optimization Opportunities (if needed)

1. **Replace serial polling** with OS blocking read in a thread + `asyncio.Queue`
2. **C extension** for CRC computation (Python crc16 is fast enough at 80 Hz)
3. **msgpack** instead of JSON for WS broadcast (saves ~30% bandwidth)
4. **NumPy ring buffer** for charting data if browser chart lag appears
