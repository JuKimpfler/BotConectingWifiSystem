from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field
from threading import RLock
import time
from typing import Any

from .config import HubConfig
from .protocol import Heartbeat, TelemetryValue, role_name


@dataclass(slots=True)
class TelemetryStreamState:
    current: Any
    minimum: Any
    maximum: Any
    vtype: int
    ts_ms: int
    updated_at: float


@dataclass(slots=True)
class SatelliteRuntimeState:
    role: int
    name: str
    host: str = ""
    port: int = 0
    last_seen: float = 0.0
    last_heartbeat: float = 0.0
    uptime_ms: int = 0
    rssi: int = 0
    queue_len: int = 0
    telemetry: dict[str, TelemetryStreamState] = field(default_factory=dict)

    def online(self, timeout_ms: int) -> bool:
        if self.last_seen <= 0:
            return False
        return (time.time() - self.last_seen) * 1000.0 <= timeout_ms


class HubState:
    def __init__(self, config: HubConfig) -> None:
        self._config = config
        self._lock = RLock()
        self._satellites: dict[str, SatelliteRuntimeState] = {
            key: SatelliteRuntimeState(role=value.role, name=value.name, host=value.host, port=value.port)
            for key, value in config.satellites.items()
        }
        self._events: deque[str] = deque(maxlen=100)
        self._sequence = 0

    def next_seq(self) -> int:
        with self._lock:
            value = self._sequence & 0xFF
            self._sequence = (self._sequence + 1) & 0xFF
            return value

    def update_endpoint(self, role: str, host: str, port: int) -> None:
        with self._lock:
            sat = self._satellites[role]
            sat.host = host
            sat.port = port
            sat.last_seen = time.time()

    def update_telemetry(self, role: str, host: str, port: int, telemetry: TelemetryValue) -> dict[str, Any]:
        now = time.time()
        with self._lock:
            sat = self._satellites[role]
            sat.host = host
            sat.port = port
            sat.last_seen = now
            current = sat.telemetry.get(telemetry.name)
            if current is None:
                sat.telemetry[telemetry.name] = TelemetryStreamState(
                    current=telemetry.value,
                    minimum=telemetry.value,
                    maximum=telemetry.value,
                    vtype=telemetry.vtype,
                    ts_ms=telemetry.ts_ms,
                    updated_at=now,
                )
            else:
                current.current = telemetry.value
                current.vtype = telemetry.vtype
                current.ts_ms = telemetry.ts_ms
                current.updated_at = now
                if isinstance(telemetry.value, (int, float)):
                    if telemetry.value < current.minimum:
                        current.minimum = telemetry.value
                    if telemetry.value > current.maximum:
                        current.maximum = telemetry.value
            self._events.appendleft(f"{role} · {telemetry.name}={telemetry.value}")
            stream = sat.telemetry[telemetry.name]
            return {
                "type": "telemetry",
                "role": role,
                "name": telemetry.name,
                "value": telemetry.value,
                "min": stream.minimum,
                "max": stream.maximum,
                "ts_ms": telemetry.ts_ms,
            }

    def update_heartbeat(self, role: str, host: str, port: int, heartbeat: Heartbeat) -> dict[str, Any]:
        now = time.time()
        with self._lock:
            sat = self._satellites[role]
            sat.host = host
            sat.port = port
            sat.last_seen = now
            sat.last_heartbeat = now
            sat.uptime_ms = heartbeat.uptime_ms
            sat.rssi = heartbeat.rssi
            sat.queue_len = heartbeat.queue_len
            self._events.appendleft(f"{role} · heartbeat rssi={heartbeat.rssi} q={heartbeat.queue_len}")
            return {
                "type": "heartbeat",
                "role": role,
                "uptime_ms": heartbeat.uptime_ms,
                "rssi": heartbeat.rssi,
                "queue_len": heartbeat.queue_len,
            }

    def log_event(self, message: str) -> None:
        with self._lock:
            self._events.appendleft(message)

    def resolve_target(self, role: str) -> tuple[str, int] | None:
        with self._lock:
            sat = self._satellites.get(role)
            if not sat or not sat.host:
                return None
            return sat.host, sat.port

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            satellites: dict[str, Any] = {}
            for key, sat in self._satellites.items():
                satellites[key] = {
                    "role": role_name(sat.role),
                    "name": sat.name,
                    "host": sat.host,
                    "port": sat.port,
                    "online": sat.online(self._config.heartbeat_timeout_ms),
                    "last_seen": sat.last_seen,
                    "uptime_ms": sat.uptime_ms,
                    "rssi": sat.rssi,
                    "queue_len": sat.queue_len,
                    "streams": {
                        name: {
                            "current": stream.current,
                            "min": stream.minimum,
                            "max": stream.maximum,
                            "vtype": stream.vtype,
                            "ts_ms": stream.ts_ms,
                            "updated_at": stream.updated_at,
                        }
                        for name, stream in sorted(sat.telemetry.items())
                    },
                }
            return {
                "satellites": satellites,
                "events": list(self._events),
                "mobile_default_role": self._config.mobile_role,
            }
