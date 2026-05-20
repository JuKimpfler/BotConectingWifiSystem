from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from aiohttp import WSMsgType, web

from .protocol import CAL_BNO, CAL_IR_MAX, CAL_IR_MIN, CAL_LINE_MAX, CAL_LINE_MIN


def create_web_app(runtime: Any) -> web.Application:
    app = web.Application()
    app["runtime"] = runtime
    app.router.add_get("/", _redirect_mobile)
    app.router.add_get("/mobile", _mobile_page)
    app.router.add_get("/mobile.css", _mobile_css)
    app.router.add_get("/mobile.js", _mobile_js)
    app.router.add_get("/api/state", _api_state)
    app.router.add_post("/api/control", _api_control)
    app.router.add_post("/api/mode", _api_mode)
    app.router.add_post("/api/calibration", _api_calibration)
    app.router.add_get("/ws", _websocket)
    return app


def _asset_path(filename: str) -> Path:
    return Path(__file__).resolve().parent.parent / "hub_ui" / filename


async def _redirect_mobile(_: web.Request) -> web.Response:
    raise web.HTTPFound("/mobile")


async def _mobile_page(_: web.Request) -> web.Response:
    return web.FileResponse(_asset_path("mobile.html"))


async def _mobile_css(_: web.Request) -> web.Response:
    return web.FileResponse(_asset_path("mobile.css"))


async def _mobile_js(_: web.Request) -> web.Response:
    return web.FileResponse(_asset_path("mobile.js"))


async def _api_state(request: web.Request) -> web.Response:
    runtime = request.app["runtime"]
    return web.json_response(runtime.state.snapshot())


async def _api_control(request: web.Request) -> web.Response:
    runtime = request.app["runtime"]
    payload = await request.json()
    await runtime.send_control(
        str(payload.get("role", runtime.config.mobile_role)),
        speed=int(payload.get("speed", 0)),
        angle=int(payload.get("angle", 0)),
        switches=int(payload.get("switches", 0)),
        buttons=int(payload.get("buttons", 0)),
        start=bool(payload.get("start", False)),
    )
    return web.json_response({"status": "ok"})


async def _api_mode(request: web.Request) -> web.Response:
    runtime = request.app["runtime"]
    payload = await request.json()
    await runtime.send_mode(str(payload.get("role", "SAT1")), int(payload.get("mode", 1)))
    return web.json_response({"status": "ok"})


async def _api_calibration(request: web.Request) -> web.Response:
    runtime = request.app["runtime"]
    payload = await request.json()
    cal = int(payload.get("cal", CAL_IR_MAX))
    await runtime.send_calibration(str(payload.get("role", "SAT1")), cal)
    return web.json_response({"status": "ok", "cal": cal})


async def _websocket(request: web.Request) -> web.WebSocketResponse:
    runtime = request.app["runtime"]
    ws = web.WebSocketResponse(heartbeat=15)
    await ws.prepare(request)
    runtime.websockets.add(ws)
    await ws.send_str(json.dumps({"type": "snapshot", **runtime.state.snapshot()}))

    async for msg in ws:
        if msg.type == WSMsgType.TEXT:
            try:
                payload = json.loads(msg.data)
            except json.JSONDecodeError:
                continue
            msg_type = str(payload.get("type", "")).lower()
            if msg_type == "control":
                await runtime.send_control(
                    str(payload.get("role", runtime.config.mobile_role)),
                    speed=int(payload.get("speed", 0)),
                    angle=int(payload.get("angle", 0)),
                    start=bool(payload.get("start", False)),
                    switches=int(payload.get("switches", 0)),
                    buttons=int(payload.get("buttons", 0)),
                )
            elif msg_type == "mode":
                await runtime.send_mode(str(payload.get("role", "SAT1")), int(payload.get("mode", 1)))
            elif msg_type == "calibration":
                await runtime.send_calibration(str(payload.get("role", "SAT1")), int(payload.get("cal", CAL_IR_MAX)))
            elif msg_type == "snapshot":
                await ws.send_str(json.dumps({"type": "snapshot", **runtime.state.snapshot()}))
        elif msg.type == WSMsgType.ERROR:
            break

    runtime.websockets.discard(ws)
    return ws
