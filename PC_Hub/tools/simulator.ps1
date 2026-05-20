<#
.SYNOPSIS
    BCWS PC-Hub — Two-Satellite Telemetry Simulator
    Simulates SAT1 and SAT2 sending realistic telemetry, heartbeats,
    and events to the PC hub over a loopback TCP/WebSocket connection.

.DESCRIPTION
    Sends realistic JSON telemetry matching the BCWS protocol_v1 WebSocket
    API so you can develop and test the PC hub without physical hardware.

    Simulated streams per satellite:
        - Speed       (int,   0..255, motor-style profile)
        - Angle       (float, -180..180, smooth oscillation)
        - Voltage     (float, 7.2..8.4 V, slow discharge curve)
        - Temp        (float, 25..65 C, heat-up over time)
        - IRLeft      (int,   0..1023, line-sensor noise)
        - IRRight     (int,   0..1023, line-sensor noise)
        - P2PActive   (bool,  random toggle)
        - Mode        (int,   1..5, occasional change)

    Event types injected occasionally:
        - mode_change    (satellite changed mode)
        - cal_complete   (calibration finished)
        - peer_online    (P2P peer appeared)
        - peer_offline   (P2P peer lost)
        - low_battery    (voltage dropped below threshold)

    Also handles optional command ACK emulation:
        - Listens for incoming WebSocket messages from hub
        - Responds with MSG_ACK JSON after a simulated processing delay

.PARAMETER Mode
    "loopback"  — connect to ws://localhost:8765/ws (default)
    "tcp"       — send raw TCP frames to localhost:9000 (legacy mode)
    "console"   — print JSON lines to stdout only (no network)

.PARAMETER RateHz
    Telemetry emission rate per satellite in Hz. Default: 20.
    Max: 50 (throttled to avoid overloading terminal).

.PARAMETER DurationSec
    Run for this many seconds then exit. 0 = run forever. Default: 0.

.PARAMETER PacketLossPct
    Simulate random packet loss (0-100%). Default: 0.

.PARAMETER NoAck
    Disable command ACK emulation.

.PARAMETER Sat1Only
    Only simulate SAT1 (useful for targeted testing).

.PARAMETER Sat2Only
    Only simulate SAT2.

.EXAMPLE
    .\simulator.ps1
    .\simulator.ps1 -Mode console
    .\simulator.ps1 -RateHz 40 -DurationSec 60
    .\simulator.ps1 -PacketLossPct 10
    .\simulator.ps1 -Sat1Only -RateHz 50 -Mode console
#>

[CmdletBinding()]
param(
    [ValidateSet("loopback","tcp","console")]
    [string]$Mode = "loopback",

    [ValidateRange(1,50)]
    [int]$RateHz = 20,

    [int]$DurationSec = 0,

    [ValidateRange(0,100)]
    [int]$PacketLossPct = 0,

    [switch]$NoAck,
    [switch]$Sat1Only,
    [switch]$Sat2Only
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ─────────────────────────────────────────────────────────────────────────────
# Colour helpers
# ─────────────────────────────────────────────────────────────────────────────

function cW { param([string]$m,[string]$c="White") Write-Host $m -ForegroundColor $c }
function ts  { return (Get-Date -Format "HH:mm:ss.fff") }

# ─────────────────────────────────────────────────────────────────────────────
# Banner
# ─────────────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Yellow
Write-Host "║   BCWS Satellite Simulator  (2 satellites)                  ║" -ForegroundColor Yellow
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Yellow
Write-Host ""
cW "  Mode           : $Mode"         Gray
cW "  Rate           : $RateHz Hz per satellite" Gray
cW "  Packet loss    : $PacketLossPct %" Gray
cW "  Duration       : $(if ($DurationSec -eq 0) {'unlimited'} else {"$DurationSec s"})" Gray
cW "  ACK emulation  : $(if ($NoAck) {'disabled'} else {'enabled'})" Gray
Write-Host ""

# ─────────────────────────────────────────────────────────────────────────────
# Resolve Python from venv (for loopback WS mode)
# ─────────────────────────────────────────────────────────────────────────────

$ScriptDir    = $PSScriptRoot
$MigrationDir = Split-Path $ScriptDir -Parent
$VenvPython   = Join-Path $MigrationDir ".venv\Scripts\python.exe"

if ($Mode -eq "loopback" -and -not (Test-Path $VenvPython)) {
    Write-Host "    [!] .venv not found — falling back to console mode" -ForegroundColor Yellow
    $Mode = "console"
}

# ─────────────────────────────────────────────────────────────────────────────
# Inline Python simulator script (written to temp file, executed by venv)
# ─────────────────────────────────────────────────────────────────────────────

$PythonSimCode = @'
"""
BCWS Satellite Simulator — Python backend
Called by simulator.ps1 with environment variables set.
Sends realistic telemetry JSON to hub WebSocket or stdout.
"""

import asyncio
import json
import math
import os
import random
import sys
import time

# ── Configuration from environment ───────────────────────────────────────────

MODE         = os.getenv("SIM_MODE", "console")
RATE_HZ      = int(os.getenv("SIM_RATE_HZ", "20"))
DURATION_SEC = int(os.getenv("SIM_DURATION", "0"))
LOSS_PCT     = int(os.getenv("SIM_LOSS_PCT", "0"))
NO_ACK       = os.getenv("SIM_NO_ACK", "0") == "1"
SAT1_ONLY    = os.getenv("SIM_SAT1_ONLY", "0") == "1"
SAT2_ONLY    = os.getenv("SIM_SAT2_ONLY", "0") == "1"
WS_URL       = os.getenv("SIM_WS_URL", "ws://localhost:8765/ws")

INTERVAL_S   = 1.0 / RATE_HZ

SATELLITES = []
if not SAT2_ONLY:
    SATELLITES.append("SAT1")
if not SAT1_ONLY:
    SATELLITES.append("SAT2")

# ── Telemetry state per satellite ─────────────────────────────────────────────

class SatState:
    def __init__(self, sat_id: str):
        self.sat_id    = sat_id
        self.seq       = 0
        self.uptime_ms = 0
        self.mode      = 1
        self.speed     = 0
        self.target_speed = random.randint(80, 200)
        self.angle_phase  = random.uniform(0, math.pi * 2)
        self.voltage   = 8.2
        self.temp      = 27.0
        self.ir_left   = 512
        self.ir_right  = 512
        self.p2p_active= False
        self.last_event_t = 0.0
        self.rssi      = random.randint(-70, -45)

    def next_seq(self) -> int:
        s = self.seq
        self.seq = (self.seq + 1) & 0xFF
        return s

    def step(self, dt_s: float, wall_t: float):
        """Advance simulation state by dt_s seconds."""
        self.uptime_ms += int(dt_s * 1000)

        # Speed: ramp toward target, occasional target change
        diff = self.target_speed - self.speed
        self.speed += int(diff * min(dt_s * 3.0, 1.0))
        if random.random() < dt_s * 0.2:
            self.target_speed = random.randint(0, 255)

        # Angle: slow sinusoidal + small noise
        self.angle_phase += dt_s * 0.4
        base_angle = math.sin(self.angle_phase) * 120.0
        self.angle = base_angle + random.gauss(0, 2.0)

        # Voltage: slow discharge, slight noise
        self.voltage = max(6.8, self.voltage - dt_s * 0.0008 + random.gauss(0, 0.005))

        # Temperature: rises with speed, cools slowly
        target_temp = 28.0 + (self.speed / 255.0) * 35.0
        self.temp += (target_temp - self.temp) * dt_s * 0.05
        self.temp += random.gauss(0, 0.1)

        # IR sensors: simulate crossing lines
        noise = random.gauss(0, 15)
        self.ir_left  = max(0, min(1023, int(512 + noise + math.sin(wall_t * 2.1) * 200)))
        self.ir_right = max(0, min(1023, int(512 - noise + math.sin(wall_t * 1.9) * 200)))

        # P2P: toggle randomly
        if random.random() < dt_s * 0.05:
            self.p2p_active = not self.p2p_active

        # RSSI: wander
        self.rssi = max(-90, min(-30, self.rssi + random.randint(-2, 2)))

    def telemetry_frames(self, wall_t: float) -> list:
        """Return list of telemetry JSON dicts for this tick."""
        rx_ts = wall_t
        frames = []
        streams = [
            ("Speed",     0, int(self.speed)),
            ("Angle",     1, round(self.angle, 2)),
            ("Voltage",   1, round(self.voltage, 3)),
            ("Temp",      1, round(self.temp, 1)),
            ("IRLeft",    0, self.ir_left),
            ("IRRight",   0, self.ir_right),
            ("P2PActive", 2, int(self.p2p_active)),
            ("Mode",      0, self.mode),
        ]
        for name, vtype, value in streams:
            frames.append({
                "type":   "telemetry",
                "sat_id": self.sat_id,
                "name":   name,
                "vtype":  vtype,
                "value":  value,
                "ts_ms":  self.uptime_ms,
                "rx_ts":  rx_ts,
                "seq":    self.next_seq(),
            })
        return frames

    def heartbeat_frame(self, wall_t: float) -> dict:
        return {
            "type":      "heartbeat_sim",
            "sat_id":    self.sat_id,
            "uptime_ms": self.uptime_ms,
            "rssi":      self.rssi,
            "queue_len": random.randint(0, 3),
            "rx_ts":     wall_t,
        }

    def maybe_event(self, wall_t: float) -> dict | None:
        """Occasionally emit a satellite event."""
        if wall_t - self.last_event_t < 8.0:
            return None
        if random.random() > 0.15:
            return None
        self.last_event_t = wall_t

        event_type = random.choice([
            "mode_change", "cal_complete", "peer_online", "peer_offline", "low_battery"
        ])

        detail = {}
        if event_type == "mode_change":
            self.mode = random.randint(1, 5)
            detail = {"new_mode": self.mode}
        elif event_type == "cal_complete":
            detail = {"cal_cmd": random.randint(1, 5), "result": "ok"}
        elif event_type == "peer_online":
            detail = {"peer": "SAT2" if self.sat_id == "SAT1" else "SAT1"}
        elif event_type == "peer_offline":
            detail = {"peer": "SAT2" if self.sat_id == "SAT1" else "SAT1", "reason": "timeout"}
        elif event_type == "low_battery":
            self.voltage = 7.1
            detail = {"voltage": round(self.voltage, 3)}

        return {
            "type":       "event",
            "sat_id":     self.sat_id,
            "event_type": event_type,
            "detail":     detail,
            "ts_ms":      self.uptime_ms,
            "rx_ts":      wall_t,
        }

# ── Console sink ──────────────────────────────────────────────────────────────

class ConsoleSink:
    sat_counters = {}

    async def send(self, frame: dict):
        """Print frame to stdout as JSON line."""
        sat = frame.get("sat_id", "?")
        ftype = frame.get("type", "?")
        # Only print non-telemetry in console mode to avoid flood
        if ftype == "telemetry":
            c = self.sat_counters.get(sat, 0) + 1
            self.sat_counters[sat] = c
            if c % RATE_HZ == 0:
                # Print summary once per second
                name  = frame.get("name", "?")
                value = frame.get("value", "?")
                ts    = frame.get("ts_ms", 0)
                print(f"[{sat}] {name:12s} = {str(value):>10s}  (ts={ts}ms)", flush=True)
        else:
            print(json.dumps(frame), flush=True)

    async def recv(self):
        return None

    async def close(self):
        pass

# ── WebSocket sink ────────────────────────────────────────────────────────────

class WebSocketSink:
    def __init__(self, url: str):
        self.url = url
        self.ws  = None

    async def connect(self):
        try:
            import websockets
            print(f"[Simulator] Connecting to {self.url} ...", flush=True)
            self.ws = await websockets.connect(self.url, ping_interval=10)
            print(f"[Simulator] Connected.", flush=True)
        except Exception as e:
            print(f"[Simulator] WebSocket connect failed: {e}", flush=True)
            print(f"[Simulator] Is the hub running? Try: .\\run-dev.ps1", flush=True)
            self.ws = None

    async def send(self, frame: dict):
        if self.ws is None:
            return
        try:
            await self.ws.send(json.dumps(frame))
        except Exception as e:
            print(f"[Simulator] Send error: {e}", flush=True)
            self.ws = None

    async def recv(self):
        if self.ws is None:
            return None
        try:
            msg = await asyncio.wait_for(self.ws.recv(), timeout=0.001)
            return json.loads(msg)
        except (asyncio.TimeoutError, Exception):
            return None

    async def close(self):
        if self.ws:
            await self.ws.close()

# ── ACK emulation ─────────────────────────────────────────────────────────────

async def handle_incoming(sink, states: dict):
    """Handle commands from hub and send ACK responses."""
    if NO_ACK:
        return
    msg = await sink.recv()
    if msg is None:
        return
    mtype = msg.get("type", "")
    if mtype in ("ctrl", "mode", "cal"):
        sat_id = msg.get("sat_id", "SAT1")
        state  = states.get(sat_id)
        # Simulate processing delay 5–50ms
        await asyncio.sleep(random.uniform(0.005, 0.05))
        ack = {
            "type":     "command_ack",
            "sat_id":   sat_id,
            "cmd_type": mtype,
            "seq":      msg.get("seq", 0),
            "status":   "ok",
            "retries":  0,
        }
        await sink.send(ack)
        print(f"[Simulator] ACK sent for {mtype} to {sat_id}", flush=True)

# ── Main simulation loop ──────────────────────────────────────────────────────

async def run():
    # Build satellite state objects
    states = {sat: SatState(sat) for sat in SATELLITES}

    # Create sink
    if MODE == "console":
        sink = ConsoleSink()
    else:
        sink = WebSocketSink(WS_URL)
        await sink.connect()

    start_wall = time.time()
    last_hb    = {sat: 0.0 for sat in SATELLITES}
    last_tick  = time.perf_counter()
    tick_count = 0

    print(f"[Simulator] Streaming {SATELLITES} at {RATE_HZ} Hz (loss={LOSS_PCT}%)", flush=True)
    print(f"[Simulator] Press Ctrl+C to stop.", flush=True)

    try:
        while True:
            now_wall = time.time()
            now_tick = time.perf_counter()
            dt = now_tick - last_tick
            last_tick = now_tick
            tick_count += 1

            # Check duration
            if DURATION_SEC > 0 and (now_wall - start_wall) >= DURATION_SEC:
                print(f"[Simulator] Duration {DURATION_SEC}s reached, stopping.", flush=True)
                break

            for sat_id, state in states.items():
                state.step(dt, now_wall)

                # Packet loss simulation
                if LOSS_PCT > 0 and random.randint(1, 100) <= LOSS_PCT:
                    continue

                # Telemetry frames
                for frame in state.telemetry_frames(now_wall):
                    await sink.send(frame)

                # Heartbeat (every 1 s)
                if now_wall - last_hb[sat_id] >= 1.0:
                    last_hb[sat_id] = now_wall
                    await sink.send(state.heartbeat_frame(now_wall))

                # Random events
                event = state.maybe_event(now_wall)
                if event:
                    await sink.send(event)
                    print(f"[Simulator] EVENT {sat_id}: {event['event_type']} {event['detail']}",
                          flush=True)

            # Handle incoming commands from hub (ACK emulation)
            await handle_incoming(sink, states)

            # Rate throttle: sleep until next tick
            elapsed = time.perf_counter() - last_tick
            sleep_s = max(0.0, INTERVAL_S - elapsed)
            await asyncio.sleep(sleep_s)

    except KeyboardInterrupt:
        print("\n[Simulator] Stopped by user.", flush=True)
    finally:
        await sink.close()
        elapsed_total = time.time() - start_wall
        total_frames  = tick_count * len(SATELLITES) * 8
        print(f"[Simulator] Sent ~{total_frames} frames in {elapsed_total:.1f}s "
              f"({total_frames/elapsed_total:.0f} fps total)", flush=True)

if __name__ == "__main__":
    asyncio.run(run())
'@

# ─────────────────────────────────────────────────────────────────────────────
# Write Python script to temp file and execute
# ─────────────────────────────────────────────────────────────────────────────

$TempPy = [System.IO.Path]::GetTempFileName() -replace '\.tmp$', '.py'

try {
    Set-Content -Path $TempPy -Value $PythonSimCode -Encoding UTF8

    # Set environment variables for Python script
    $env:SIM_MODE     = $Mode
    $env:SIM_RATE_HZ  = "$RateHz"
    $env:SIM_DURATION = "$DurationSec"
    $env:SIM_LOSS_PCT = "$PacketLossPct"
    $env:SIM_NO_ACK   = if ($NoAck) { "1" } else { "0" }
    $env:SIM_SAT1_ONLY= if ($Sat1Only) { "1" } else { "0" }
    $env:SIM_SAT2_ONLY= if ($Sat2Only) { "1" } else { "0" }
    $env:SIM_WS_URL   = "ws://localhost:8765/ws"

    if ($Mode -eq "console") {
        cW ""
        cW "  Console mode — printing telemetry to terminal" Yellow
        cW "  (One summary line per second per satellite stream)" Gray
        cW ""
        & python $TempPy
    }
    elseif (Test-Path $VenvPython) {
        cW ""
        cW "  Loopback mode — connecting to ws://localhost:8765/ws" Yellow
        cW "  Make sure hub is running: .\tools\run-dev.ps1" Gray
        cW ""
        & $VenvPython $TempPy
    }
    else {
        cW "  .venv not found, using system python" Yellow
        & python $TempPy
    }
}
finally {
    # Clean up temp file
    if (Test-Path $TempPy) {
        Remove-Item $TempPy -Force -ErrorAction SilentlyContinue
    }

    # Clean env vars
    Remove-Item Env:\SIM_MODE     -ErrorAction SilentlyContinue
    Remove-Item Env:\SIM_RATE_HZ  -ErrorAction SilentlyContinue
    Remove-Item Env:\SIM_DURATION -ErrorAction SilentlyContinue
    Remove-Item Env:\SIM_LOSS_PCT -ErrorAction SilentlyContinue
    Remove-Item Env:\SIM_NO_ACK   -ErrorAction SilentlyContinue
    Remove-Item Env:\SIM_SAT1_ONLY -ErrorAction SilentlyContinue
    Remove-Item Env:\SIM_SAT2_ONLY -ErrorAction SilentlyContinue
    Remove-Item Env:\SIM_WS_URL   -ErrorAction SilentlyContinue
}
