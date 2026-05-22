from __future__ import annotations

import argparse
import os
from pathlib import Path
from typing import Any

try:
    from .debug_ingest import DebugIngest
    from .gui_app import DebugGuiApp
except ImportError:  # pragma: no cover
    from debug_ingest import DebugIngest
    from gui_app import DebugGuiApp

try:
    import yaml
except ModuleNotFoundError:  # pragma: no cover
    yaml = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="BCWS PC Hub GUI (V3 Debug)")
    parser.add_argument("--config", type=str, default="", help="Path to hub_config.yaml")
    parser.add_argument("--log-level", type=str, default="INFO", help="Kept for run-dev.ps1 compatibility")
    parser.add_argument("--no-color", action="store_true", help="Kept for run-dev.ps1 compatibility")
    parser.add_argument("--com-port", type=str, default="", help="Kept for run-dev.ps1 compatibility")
    return parser.parse_args()


def load_config(config_path: str) -> dict[str, Any]:
    config: dict[str, Any] = {}
    if not config_path:
        return config
    if not os.path.exists(config_path):
        return config
    if yaml is None:
        return config
    with open(config_path, "r", encoding="utf-8") as fh:
        loaded = yaml.safe_load(fh) or {}
    if isinstance(loaded, dict):
        config = loaded
    return config


def resolve_debug_bind(config: dict[str, Any]) -> tuple[str, int]:
    debug_cfg = config.get("debug", {}) if isinstance(config, dict) else {}
    host = str(debug_cfg.get("bind_host", "0.0.0.0"))
    port_raw = debug_cfg.get("udp_port", 9999)
    try:
        port = int(port_raw)
    except (TypeError, ValueError):
        port = 9999
    return host, max(1, min(65535, port))


def default_config_path() -> str:
    root = Path(__file__).resolve().parents[1]
    return str(root / "config" / "hub_config.yaml")


def main() -> int:
    args = parse_args()
    config_file = args.config or default_config_path()
    cfg = load_config(config_file)
    host, port = resolve_debug_bind(cfg)

    ingest = DebugIngest(bind_host=host, bind_port=port)
    ingest.start()
    app = DebugGuiApp(ingest=ingest)
    app.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
