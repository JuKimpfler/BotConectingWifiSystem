"""
hub_core/api.py — aiohttp WebSocket + REST API server

Endpoints:
  GET  /health               — liveness
  GET  /ready                — readiness (bridge connected)
  GET  /metrics              — error counters JSON
  GET  /api/devices          — peer status
  GET  /api/telemetry/history — historical samples
  WS   /ws                   — bidirectional command/telemetry

Ref: protocol_v1.md §12, hub_migration_summary.md §4 Phase 3/4
"""

import asyncio
import json
import logging
import time
from typing import Any, Optional, Set

try:
    from aiohttp import web
    HAS_AIOHTTP = True
except ImportError:
    HAS_AIOHTTP = False

from .diagnostics import Diagnostics

logger = logging.getLogger(__name__)


class HubAPI:
    """
    Runs the aiohttp web server exposing REST + WebSocket endpoints.
    """

    def __init__(
        self,
        bind_host: str,
        port: int,
        diag: Diagnostics,
        ingress: Any,
        peer_tracker: Any,
        storage: Any,
        command_router: Any,
    ) -> None:
        self._bind_host     = bind_host
        self._port          = port
        self._diag          = diag
        self._ingress       = ingress
        self._peer_tracker  = peer_tracker
        self._storage       = storage
        self._cmd_router    = command_router
        self._ws_clients:   Set[Any] = set()
        self._runner:       Optional[Any] = None
        self._start_time    = time.monotonic()

    async def start(self) -> None:
        """Start the HTTP server."""
        if not HAS_AIOHTTP:
            logger.warning("aiohttp not available — API server disabled")
            return

        app = web.Application()
        app.router.add_get("/health", self._handle_health)
        app.router.add_get("/ready",  self._handle_ready)
        app.router.add_get("/metrics", self._handle_metrics)
        app.router.add_get("/api/devices", self._handle_devices)
        app.router.add_get("/api/telemetry/history", self._handle_telem_history)
        app.router.add_get("/ws", self._handle_ws)
        app.router.add_get("/", self._handle_ui)

        self._runner = web.AppRunner(app)
        await self._runner.setup()
        site = web.TCPSite(self._runner, self._bind_host, self._port)
        await site.start()
        logger.info("API server listening on http://%s:%d", self._bind_host, self._port)

        # Register broadcast callback with command router
        self._cmd_router.set_broadcast_callback(self._broadcast_to_all)

    async def stop(self) -> None:
        if self._runner:
            await self._runner.cleanup()

    # ── REST handlers ─────────────────────────────────────────

    async def _handle_health(self, request: Any) -> Any:
        uptime_s = round(time.monotonic() - self._start_time, 1)
        return web.Response(
            content_type="application/json",
            text=json.dumps({"status": "ok", "uptime_s": uptime_s}),
        )

    async def _handle_ready(self, request: Any) -> Any:
        ready = self._ingress.connected
        status = 200 if ready else 503
        return web.Response(
            status=status,
            content_type="application/json",
            text=json.dumps({"ready": ready, "bridge_connected": ready}),
        )

    async def _handle_metrics(self, request: Any) -> Any:
        snap = self._diag.snapshot()
        return web.Response(
            content_type="application/json",
            text=json.dumps(snap),
        )

    async def _handle_devices(self, request: Any) -> Any:
        peers = self._peer_tracker.status_dict()
        return web.Response(
            content_type="application/json",
            text=json.dumps({"devices": peers}),
        )

    async def _handle_telem_history(self, request: Any) -> Any:
        # Query params: sat=SAT1, stream=Speed, since=<unix_ts>, limit=1000
        sat_param    = request.rel_url.query.get("sat")
        stream_param = request.rel_url.query.get("stream")
        since_param  = request.rel_url.query.get("since")
        until_param  = request.rel_url.query.get("until")
        limit_param  = request.rel_url.query.get("limit", "1000")

        # Map sat name to role id
        sat_role = None
        if sat_param == "SAT1":
            sat_role = 1
        elif sat_param == "SAT2":
            sat_role = 2

        since = float(since_param) if since_param else None
        until = float(until_param) if until_param else None
        try:
            limit = max(1, min(int(limit_param), 10000))
        except (ValueError, TypeError):
            limit = 1000

        rows = await self._storage.query_samples(
            sat_role    = sat_role,
            stream_name = stream_param,
            since       = since,
            until       = until,
            limit       = limit,
        )
        return web.Response(
            content_type="application/json",
            text=json.dumps({"samples": rows, "count": len(rows)}),
        )

    async def _handle_ui(self, request: Any) -> Any:
        """Serve the embedded UI HTML file."""
        import os
        ui_path = os.path.join(os.path.dirname(__file__), "..", "hub_ui", "index.html")
        ui_path = os.path.normpath(ui_path)
        if os.path.exists(ui_path):
            with open(ui_path, "r", encoding="utf-8") as fh:
                html = fh.read()
            return web.Response(content_type="text/html", text=html)
        return web.Response(
            content_type="text/html",
            text="<h1>BCWS PC Hub</h1><p>UI not found. Connect to /ws for WebSocket.</p>",
        )

    # ── WebSocket handler ─────────────────────────────────────

    async def _handle_ws(self, request: Any) -> Any:
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        ws_id = id(ws)
        self._ws_clients.add(ws)
        logger.info("WS client connected #%d (total: %d)", ws_id, len(self._ws_clients))

        try:
            # Send initial state
            await ws.send_str(json.dumps({
                "type":  "peer_status",
                "peers": self._peer_tracker.status_dict(),
            }))

            async for msg in ws:
                if msg.type == 1:  # WSMsgType.TEXT
                    await self._cmd_router.on_ws_message(str(ws_id), msg.data)
                elif msg.type in (8, 258):  # CLOSE / ERROR
                    break
        except Exception as exc:  # noqa: BLE001
            logger.debug("WS client #%d error: %s", ws_id, exc)
        finally:
            self._ws_clients.discard(ws)
            logger.info("WS client disconnected #%d (remaining: %d)",
                        ws_id, len(self._ws_clients))

        return ws

    # ── Broadcast ─────────────────────────────────────────────

    async def _broadcast_to_all(self, json_str: str) -> None:
        """Send a JSON string to all connected WebSocket clients."""
        if not self._ws_clients:
            return
        dead = set()
        for ws in list(self._ws_clients):
            try:
                await ws.send_str(json_str)
            except Exception:  # noqa: BLE001
                dead.add(ws)
        self._ws_clients -= dead
