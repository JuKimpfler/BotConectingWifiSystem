"""
hub_core/main.py — BCWS PC Hub Entry Point

Starts all async services:
  1. Ingress (serial bridge reader)
  2. Heartbeat TX
  3. ACK manager retry ticker
  4. Frame dispatcher loop
  5. API server (REST + WebSocket)
  6. Periodic hub_status broadcast

Usage:
    python -m hub_core.main [--config path/to/hub_config.yaml]
    python hub_core/main.py
"""

import asyncio
import argparse
import json
import logging
import signal
import sys
import time
from pathlib import Path

from .config import load_config, get
from .logging_setup import setup_logging
from .protocol import NETWORK_ID, ROLE_SAT1, ROLE_SAT2
from .diagnostics import Diagnostics
from .ingress import BridgeIngress
from .peer_tracker import PeerTracker
from .heartbeat import HeartbeatService
from .ack_manager import AckManager
from .storage import TelemetryStorage
from .command_router import CommandRouter
from .api import HubAPI

logger = logging.getLogger(__name__)

_SHUTDOWN = asyncio.Event()


def _install_signal_handlers() -> None:
    """Register SIGINT / SIGTERM for graceful shutdown."""
    loop = asyncio.get_event_loop()

    def _handle(sig: signal.Signals) -> None:
        logger.info("Received signal %s — shutting down", sig.name)
        _SHUTDOWN.set()

    for sig in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(sig, _handle, sig)
        except (NotImplementedError, RuntimeError):
            # Windows: signal handlers not supported in add_signal_handler
            signal.signal(sig, lambda s, f: _SHUTDOWN.set())


async def _frame_dispatch_loop(
    frame_queue: asyncio.Queue,
    command_router: CommandRouter,
) -> None:
    """Main frame processing loop — dequeues frames and routes them."""
    while not _SHUTDOWN.is_set():
        try:
            frame = await asyncio.wait_for(frame_queue.get(), timeout=0.5)
            await command_router.on_frame(frame)
            frame_queue.task_done()
        except asyncio.TimeoutError:
            continue
        except asyncio.CancelledError:
            break
        except Exception as exc:  # noqa: BLE001
            logger.error("Frame dispatch error: %s", exc, exc_info=True)


async def _hub_status_loop(
    command_router: CommandRouter,
    interval_s: float = 10.0,
) -> None:
    """Periodically broadcast hub_status to all WebSocket clients."""
    while not _SHUTDOWN.is_set():
        try:
            await asyncio.sleep(interval_s)
            await command_router.broadcast_hub_status()
        except asyncio.CancelledError:
            break
        except Exception as exc:  # noqa: BLE001
            logger.error("hub_status loop error: %s", exc, exc_info=True)


async def run(config_path: str | None = None) -> None:
    """Main async entry point."""
    cfg = load_config(config_path)

    # Setup logging first
    setup_logging(
        log_level=get(cfg, "hub", "log_level", default="INFO"),
        log_dir=get(cfg, "hub", "log_dir", default="logs"),
    )

    logger.info("BCWS PC Hub starting...")
    logger.info("Config: bind=%s port=%d bridge=%s",
                get(cfg, "hub", "bind_host"),
                get(cfg, "hub", "ws_port"),
                get(cfg, "bridge", "com_port"))

    network_id  = int(get(cfg, "bridge", "network_id", default=NETWORK_ID))
    baud_rate   = int(get(cfg, "bridge", "baud_rate", default=921600))
    com_port    = str(get(cfg, "bridge", "com_port", default="auto"))
    bind_host   = str(get(cfg, "hub", "bind_host", default="127.0.0.1"))
    port        = int(get(cfg, "hub", "ws_port", default=8765))
    hb_interval = int(get(cfg, "heartbeat", "interval_ms", default=1000))
    hb_timeout  = int(get(cfg, "heartbeat", "timeout_ms", default=4000))
    ack_timeout = int(get(cfg, "ack", "timeout_ms", default=500))
    ack_retries = int(get(cfg, "ack", "max_retries", default=3))
    db_path     = str(get(cfg, "storage", "db_path", default="data/telemetry.db"))
    retention_h = int(get(cfg, "storage", "retention_hours", default=24))
    flush_ms    = int(get(cfg, "telemetry", "batch_flush_ms", default=200))
    max_hz      = float(get(cfg, "telemetry", "max_rate_hz", default=50))

    # Create shared objects
    diag         = Diagnostics()
    frame_queue  = asyncio.Queue(maxsize=512)
    storage      = TelemetryStorage(db_path, flush_ms, retention_h)
    ingress      = BridgeIngress(com_port, baud_rate, frame_queue, diag, network_id)
    peer_tracker = PeerTracker(timeout_ms=hb_timeout)
    ack_mgr      = AckManager(timeout_ms=ack_timeout, max_retries=ack_retries, diag=diag)
    heartbeat    = HeartbeatService(interval_ms=hb_interval, network_id=network_id)
    cmd_router   = CommandRouter(
        ingress      = ingress,
        ack_manager  = ack_mgr,
        peer_tracker = peer_tracker,
        storage      = storage,
        diag         = diag,
        network_id   = network_id,
    )
    cmd_router.set_max_rate_hz(max_hz)

    api = HubAPI(
        bind_host     = bind_host,
        port          = port,
        diag          = diag,
        ingress       = ingress,
        peer_tracker  = peer_tracker,
        storage       = storage,
        command_router = cmd_router,
    )

    # Open storage
    await storage.open()

    # Start services
    ack_mgr.start(ingress)
    heartbeat.start(ingress, peer_tracker)
    await api.start()

    _install_signal_handlers()

    # Run concurrent tasks
    tasks = [
        asyncio.ensure_future(ingress.run()),
        asyncio.ensure_future(_frame_dispatch_loop(frame_queue, cmd_router)),
        asyncio.ensure_future(_hub_status_loop(cmd_router)),
    ]

    logger.info("Hub running. WebSocket: ws://%s:%d/ws  REST: http://%s:%d/",
                bind_host, port, bind_host, port)
    logger.info("Press Ctrl+C to stop.")

    # Wait for shutdown signal
    await _SHUTDOWN.wait()

    logger.info("Shutting down...")
    ingress.stop()
    heartbeat.stop()
    ack_mgr.stop()

    for t in tasks:
        t.cancel()
    await asyncio.gather(*tasks, return_exceptions=True)

    await api.stop()
    await storage.close()
    logger.info("Shutdown complete.")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="BCWS PC Hub — Windows ESP-NOW satellite hub"
    )
    parser.add_argument(
        "--config", "-c",
        default=None,
        help="Path to hub_config.yaml (default: auto-detect)",
    )
    args = parser.parse_args()

    try:
        asyncio.run(run(config_path=args.config))
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        print(f"Fatal error: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
