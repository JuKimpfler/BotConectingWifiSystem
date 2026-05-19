from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml


@dataclass(slots=True)
class SatelliteTargetConfig:
    role: int
    name: str
    host: str = ""
    port: int = 5006


@dataclass(slots=True)
class HubConfig:
    bind_host: str = "0.0.0.0"
    telemetry_port: int = 5005
    web_port: int = 8080
    log_level: str = "INFO"
    gui_refresh_ms: int = 150
    heartbeat_interval_ms: int = 1000
    heartbeat_timeout_ms: int = 4000
    mobile_role: str = "SAT1"
    satellites: dict[str, SatelliteTargetConfig] = field(
        default_factory=lambda: {
            "SAT1": SatelliteTargetConfig(role=1, name="SAT1"),
            "SAT2": SatelliteTargetConfig(role=2, name="SAT2"),
        }
    )

    @classmethod
    def load(cls, path: str | Path) -> "HubConfig":
        path = Path(path)
        data: dict[str, Any] = {}
        if path.exists():
            loaded = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
            if isinstance(loaded, dict):
                data = loaded

        hub = data.get("hub", {}) if isinstance(data.get("hub", {}), dict) else {}
        network = data.get("network", {}) if isinstance(data.get("network", {}), dict) else {}
        gui = data.get("gui", {}) if isinstance(data.get("gui", {}), dict) else {}
        heartbeat = data.get("heartbeat", {}) if isinstance(data.get("heartbeat", {}), dict) else {}
        mobile = data.get("mobile", {}) if isinstance(data.get("mobile", {}), dict) else {}
        sat_data = data.get("satellites", {}) if isinstance(data.get("satellites", {}), dict) else {}

        cfg = cls(
            bind_host=str(hub.get("bind_host", cls.bind_host)),
            telemetry_port=int(network.get("telemetry_port", cls.telemetry_port)),
            web_port=int(hub.get("web_port", cls.web_port)),
            log_level=str(hub.get("log_level", cls.log_level)).upper(),
            gui_refresh_ms=int(gui.get("refresh_ms", cls.gui_refresh_ms)),
            heartbeat_interval_ms=int(heartbeat.get("interval_ms", cls.heartbeat_interval_ms)),
            heartbeat_timeout_ms=int(heartbeat.get("timeout_ms", cls.heartbeat_timeout_ms)),
            mobile_role=str(mobile.get("default_role", cls.mobile_role)).upper(),
        )

        satellites: dict[str, SatelliteTargetConfig] = {}
        for role_name, default_role in (("SAT1", 1), ("SAT2", 2)):
            entry = sat_data.get(role_name, {}) if isinstance(sat_data.get(role_name, {}), dict) else {}
            satellites[role_name] = SatelliteTargetConfig(
                role=int(entry.get("role", default_role)),
                name=str(entry.get("name", role_name)),
                host=str(entry.get("host", "")),
                port=int(entry.get("port", 5006)),
            )
        cfg.satellites = satellites
        return cfg

    def as_dict(self) -> dict[str, Any]:
        return {
            "hub": {
                "bind_host": self.bind_host,
                "web_port": self.web_port,
                "log_level": self.log_level,
            },
            "network": {
                "telemetry_port": self.telemetry_port,
            },
            "gui": {
                "refresh_ms": self.gui_refresh_ms,
            },
            "heartbeat": {
                "interval_ms": self.heartbeat_interval_ms,
                "timeout_ms": self.heartbeat_timeout_ms,
            },
            "mobile": {
                "default_role": self.mobile_role,
            },
            "satellites": {
                key: {
                    "role": value.role,
                    "name": value.name,
                    "host": value.host,
                    "port": value.port,
                }
                for key, value in self.satellites.items()
            },
        }
