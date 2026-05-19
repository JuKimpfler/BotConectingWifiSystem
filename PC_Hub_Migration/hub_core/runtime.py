from __future__ import annotations

import asyncio
import contextlib
import logging
import socket
from pathlib import Path
from typing import Any

from aiohttp import web

from .config import HubConfig
from .protocol import (
    FLAG_ACK_REQ,
    MSG_CAL,
    MSG_CTRL,
    MSG_DBG,
    MSG_HEARTBEAT,
    MSG_MODE,
    ROLE_HUB,
    ROLE_SAT1,
    ROLE_SAT2,
    Frame,
    ProtocolError,
    build_cal_payload,
    build_ctrl_payload,
    build_heartbeat_payload,
    build_mode_payload,
    parse_heartbeat,
    parse_telemetry_entry,
)
from .state import HubState
from .web import create_web_app

LOG = logging.getLogger(__name__)
ROLE_LOOKUP = {ROLE_SAT1: "SAT1", ROLE_SAT2: "SAT2"}


class HubDatagramProtocol(asyncio.DatagramProtocol):
    def __init__(self, runtime: "HubRuntime") -> None:
        self.runtime = runtime
        self.transport: asyncio.DatagramTransport | None = None

    def connection_made(self, transport: asyncio.BaseTransport) -> None:
        self.transport = transport  # type: ignore[assignment]
        self.runtime.transport = self.transport
        sockname = self.transport.get_extra_info("sockname")
        LOG.info("UDP telemetry server listening on %s", sockname)

    def datagram_received(self, data: bytes, addr: tuple[str, int]) -> None:
        try:
            frame = Frame.unpack(data)
            self.runtime.handle_frame(frame, addr)
        except ProtocolError as exc:
            LOG.warning("Dropped invalid UDP frame from %s:%s: %s", addr[0], addr[1], exc)


class HubRuntime:
    def __init__(self, config: HubConfig, repo_root: Path) -> None:
        self.config = config
        self.repo_root = repo_root
        self.state = HubState(config)
        self.transport: asyncio.DatagramTransport | None = None
        self.web_runner: web.AppRunner | None = None
        self.site: web.TCPSite | None = None
        self.heartbeat_task: asyncio.Task[None] | None = None
        self.websockets: set[web.WebSocketResponse] = set()
        self.public_base_url = lambda: f"http://{self._best_bind_host()}:{self.config.web_port}"

    async def start(self) -> None:
        loop = asyncio.get_running_loop()
        await loop.create_datagram_endpoint(
            lambda: HubDatagramProtocol(self),
            local_addr=(self.config.bind_host, self.config.telemetry_port),
        )
        app = create_web_app(self)
        self.web_runner = web.AppRunner(app)
        await self.web_runner.setup()
        self.site = web.TCPSite(self.web_runner, self.config.bind_host, self.config.web_port)
        await self.site.start()
        self.heartbeat_task = asyncio.create_task(self._heartbeat_loop(), name="heartbeat-loop")
        LOG.info("Web UI listening on http://%s:%s/mobile", self._best_bind_host(), self.config.web_port)

    async def stop(self) -> None:
        if self.heartbeat_task:
            self.heartbeat_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self.heartbeat_task
        for ws in list(self.websockets):
            await ws.close()
        if self.web_runner:
            await self.web_runner.cleanup()
        if self.transport:
            self.transport.close()
            self.transport = None

    def handle_frame(self, frame: Frame, addr: tuple[str, int]) -> None:
        role = ROLE_LOOKUP.get(frame.src_role)
        if role is None:
            LOG.debug("Ignoring frame with unsupported role %s from %s", frame.src_role, addr)
            return

        if frame.msg_type == MSG_DBG:
            telemetry = parse_telemetry_entry(frame.payload)
            event = self.state.update_telemetry(role, addr[0], addr[1], telemetry)
            self._publish_nowait(event)
            return

        if frame.msg_type == MSG_HEARTBEAT:
            hb = parse_heartbeat(frame.payload)
            event = self.state.update_heartbeat(role, addr[0], addr[1], hb)
            self._publish_nowait(event)
            return

        LOG.info("RX %s frame type=0x%02X from %s:%s", role, frame.msg_type, addr[0], addr[1])
        self.state.update_endpoint(role, addr[0], addr[1])
        self.state.log_event(f"{role} · frame 0x{frame.msg_type:02X} from {addr[0]}:{addr[1]}")
        self._publish_nowait({"type": "frame", "role": role, "msg_type": frame.msg_type})

    async def send_control(self, role: str, *, speed: int, angle: int, switches: int = 0, buttons: int = 0, start: bool = False) -> None:
        role_code = self._role_code(role)
        frame = Frame(
            msg_type=MSG_CTRL,
            seq=self.state.next_seq(),
            src_role=ROLE_HUB,
            dst_role=role_code,
            flags=FLAG_ACK_REQ,
            payload=build_ctrl_payload(speed, angle, switches, buttons, start, role_code),
        )
        self._send_frame(role, frame)
        self.state.log_event(f"HUB · ctrl -> {role} speed={speed} angle={angle} start={int(start)}")
        await self.broadcast({"type": "command", "role": role, "command": "ctrl", "speed": speed, "angle": angle, "start": start})

    async def send_mode(self, role: str, mode_id: int) -> None:
        role_code = self._role_code(role)
        frame = Frame(
            msg_type=MSG_MODE,
            seq=self.state.next_seq(),
            src_role=ROLE_HUB,
            dst_role=role_code,
            flags=FLAG_ACK_REQ,
            payload=build_mode_payload(mode_id, role_code),
        )
        self._send_frame(role, frame)
        self.state.log_event(f"HUB · mode -> {role} #{mode_id}")
        await self.broadcast({"type": "command", "role": role, "command": "mode", "mode": mode_id})

    async def send_calibration(self, role: str, cal_cmd: int) -> None:
        role_code = self._role_code(role)
        frame = Frame(
            msg_type=MSG_CAL,
            seq=self.state.next_seq(),
            src_role=ROLE_HUB,
            dst_role=role_code,
            flags=FLAG_ACK_REQ,
            payload=build_cal_payload(cal_cmd, role_code),
        )
        self._send_frame(role, frame)
        self.state.log_event(f"HUB · calibration -> {role} cmd={cal_cmd}")
        await self.broadcast({"type": "command", "role": role, "command": "cal", "cal": cal_cmd})

    async def broadcast(self, payload: dict[str, Any]) -> None:
        closed: list[web.WebSocketResponse] = []
        if not self.websockets:
            return
        import json

        data = json.dumps(payload)
        for ws in self.websockets:
            if ws.closed:
                closed.append(ws)
                continue
            await ws.send_str(data)
        for ws in closed:
            self.websockets.discard(ws)

    def _publish_nowait(self, payload: dict[str, Any]) -> None:
        try:
            loop = asyncio.get_running_loop()
        except RuntimeError:
            return
        loop.create_task(self.broadcast(payload))

    def _send_frame(self, role: str, frame: Frame) -> None:
        if not self.transport:
            raise RuntimeError("UDP transport not ready")
        target = self.state.resolve_target(role)
        if not target:
            configured = self.config.satellites.get(role)
            if configured and configured.host:
                target = (configured.host, configured.port)
        if not target:
            raise RuntimeError(f"No UDP endpoint known for {role}. Wait for telemetry or configure a fixed host.")
        self.transport.sendto(frame.pack(), target)
        LOG.debug("TX %s type=0x%02X to %s:%s", role, frame.msg_type, target[0], target[1])

    async def _heartbeat_loop(self) -> None:
        while True:
            await asyncio.sleep(max(self.config.heartbeat_interval_ms / 1000.0, 0.2))
            uptime_ms = int(asyncio.get_running_loop().time() * 1000)
            for role in ("SAT1", "SAT2"):
                target = self.state.resolve_target(role)
                if not target:
                    configured = self.config.satellites.get(role)
                    if configured and configured.host:
                        target = (configured.host, configured.port)
                if not target or not self.transport:
                    continue
                payload = build_heartbeat_payload(uptime_ms)
                frame = Frame(
                    msg_type=MSG_HEARTBEAT,
                    seq=self.state.next_seq(),
                    src_role=ROLE_HUB,
                    dst_role=self._role_code(role),
                    payload=payload,
                )
                self.transport.sendto(frame.pack(), target)

    def _role_code(self, role: str) -> int:
        role_upper = role.upper()
        if role_upper == "SAT1":
            return ROLE_SAT1
        if role_upper == "SAT2":
            return ROLE_SAT2
        raise ValueError(f"unsupported role {role!r}")

    def _best_bind_host(self) -> str:
        if self.config.bind_host not in {"0.0.0.0", "::"}:
            return self.config.bind_host
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect(("8.8.8.8", 80))
            host = sock.getsockname()[0]
            sock.close()
            return host
        except OSError:
            return "127.0.0.1"
