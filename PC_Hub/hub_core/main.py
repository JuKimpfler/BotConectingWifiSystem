from __future__ import annotations

import argparse
import asyncio
import logging
from pathlib import Path
import socket
import threading
from typing import Any

from .config import HubConfig
from .runtime import HubRuntime


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="BCWS V3.0 PC hub")
    parser.add_argument("--config", default=str(Path(__file__).resolve().parents[1] / "config" / "hub_config.yaml"))
    parser.add_argument("--log-level", default="INFO")
    parser.add_argument("--headless", action="store_true", help="Start without the native Tk GUI")
    return parser.parse_args()


def configure_logging(level: str) -> None:
    logging.basicConfig(
        level=getattr(logging, level.upper(), logging.INFO),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    )


def best_public_host(bind_host: str) -> str:
    if bind_host not in {"0.0.0.0", "::"}:
        return bind_host
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.connect(("8.8.8.8", 80))
        host = sock.getsockname()[0]
        sock.close()
        return host
    except OSError:
        return "127.0.0.1"


async def _run_until_stopped(runtime: HubRuntime) -> None:
    stop_event = asyncio.Event()
    await runtime.start()
    try:
        await stop_event.wait()
    finally:
        await runtime.stop()


async def _start_runtime(runtime: HubRuntime, ready: threading.Event, stop_event: asyncio.Event) -> None:
    await runtime.start()
    ready.set()
    try:
        await stop_event.wait()
    finally:
        await runtime.stop()


def main() -> int:
    args = parse_args()
    config = HubConfig.load(args.config)
    config.log_level = args.log_level.upper()
    configure_logging(config.log_level)

    repo_root = Path(__file__).resolve().parents[2]
    runtime = HubRuntime(config, repo_root)
    runtime.public_base_url = lambda: f"http://{best_public_host(config.bind_host)}:{config.web_port}"

    if args.headless:
        try:
            asyncio.run(_run_until_stopped(runtime))
        except KeyboardInterrupt:
            pass
        return 0

    loop = asyncio.new_event_loop()
    ready = threading.Event()
    stop_event: asyncio.Event | None = None

    def _runner() -> None:
        nonlocal stop_event
        asyncio.set_event_loop(loop)
        stop_event = asyncio.Event()
        loop.run_until_complete(_start_runtime(runtime, ready, stop_event))

    thread = threading.Thread(target=_runner, name="bcws-runtime", daemon=True)
    thread.start()
    ready.wait(timeout=10)

    def submit_async(coro: Any) -> None:
        future = asyncio.run_coroutine_threadsafe(coro, loop)
        future.add_done_callback(lambda f: f.result() if not f.exception() else logging.getLogger(__name__).error("Async UI action failed: %s", f.exception()))

    from .gui import run_gui

    try:
        run_gui(runtime, submit_async)
    finally:
        if stop_event is not None:
            loop.call_soon_threadsafe(stop_event.set)
        thread.join(timeout=2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
