"""
hub_core/config.py — Configuration loading (YAML + env overrides)

Environment variable format: BCWS_<SECTION>_<KEY> (uppercase)
e.g. BCWS_HUB_WS_PORT=9000, BCWS_BRIDGE_COM_PORT=COM5
"""

import os
import logging
from pathlib import Path
from typing import Any

try:
    import yaml
except ImportError:
    yaml = None  # type: ignore

logger = logging.getLogger(__name__)

# Default configuration (used if yaml file not found)
DEFAULTS: dict = {
    "hub": {
        "bind_host": "127.0.0.1",
        "ws_port": 8765,
        "rest_port": 8765,
        "log_level": "INFO",
        "log_dir": "logs",
    },
    "bridge": {
        "com_port": "auto",
        "baud_rate": 921600,
        "network_id": 1,
        "channel": 6,
    },
    "telemetry": {
        "max_rate_hz": 50,
        "batch_flush_ms": 200,
    },
    "heartbeat": {
        "interval_ms": 1000,
        "timeout_ms": 4000,
    },
    "ack": {
        "timeout_ms": 500,
        "max_retries": 3,
    },
    "storage": {
        "db_path": "data/telemetry.db",
        "retention_hours": 24,
        "vacuum_interval_hours": 6,
    },
    "satellites": {
        "SAT1": {"role": 1, "mac": ""},
        "SAT2": {"role": 2, "mac": ""},
    },
}


def _deep_merge(base: dict, override: dict) -> dict:
    """Recursively merge override into base, returning a new dict."""
    result = dict(base)
    for key, val in override.items():
        if key in result and isinstance(result[key], dict) and isinstance(val, dict):
            result[key] = _deep_merge(result[key], val)
        else:
            result[key] = val
    return result


def _apply_env_overrides(cfg: dict) -> dict:
    """
    Apply BCWS_ environment variable overrides.
    BCWS_HUB_WS_PORT=9000 → cfg["hub"]["ws_port"] = 9000
    Supports two-level keys only.
    """
    prefix = "BCWS_"
    for env_key, env_val in os.environ.items():
        if not env_key.startswith(prefix):
            continue
        rest = env_key[len(prefix):]
        parts = rest.split("_", 1)
        if len(parts) != 2:
            continue
        section, key = parts[0].lower(), parts[1].lower()
        if section not in cfg:
            continue
        if not isinstance(cfg[section], dict):
            continue
        # Type coercion: try int, then float, then bool, then str
        typed_val: Any = env_val
        try:
            typed_val = int(env_val)
        except ValueError:
            try:
                typed_val = float(env_val)
            except ValueError:
                if env_val.lower() in ("true", "1", "yes"):
                    typed_val = True
                elif env_val.lower() in ("false", "0", "no"):
                    typed_val = False
        cfg[section][key] = typed_val
        logger.debug("Config override from env: %s.%s = %r", section, key, typed_val)
    return cfg


def load_config(config_path: str | None = None) -> dict:
    """
    Load hub configuration.

    Search order:
    1. Explicit config_path argument
    2. BCWS_CONFIG_PATH environment variable
    3. config/hub_config.yaml (relative to this file's parent directory)
    4. DEFAULTS (built-in)

    Then apply BCWS_ environment variable overrides on top.
    """
    # Determine config file location
    if config_path is None:
        config_path = os.environ.get("BCWS_CONFIG_PATH")
    if config_path is None:
        default_path = Path(__file__).parent.parent / "config" / "hub_config.yaml"
        if default_path.exists():
            config_path = str(default_path)

    cfg = dict(DEFAULTS)

    if config_path and yaml is not None:
        try:
            with open(config_path, "r", encoding="utf-8") as fh:
                file_cfg = yaml.safe_load(fh) or {}
            cfg = _deep_merge(cfg, file_cfg)
            logger.debug("Loaded config from %s", config_path)
        except Exception as exc:
            logger.warning("Failed to load config file %s: %s — using defaults", config_path, exc)
    elif config_path and yaml is None:
        logger.warning("PyYAML not installed; using built-in defaults")

    cfg = _apply_env_overrides(cfg)
    return cfg


def get(cfg: dict, *keys: str, default: Any = None) -> Any:
    """Safe nested key access: get(cfg, 'hub', 'ws_port', default=8765)."""
    node = cfg
    for k in keys:
        if not isinstance(node, dict) or k not in node:
            return default
        node = node[k]
    return node
