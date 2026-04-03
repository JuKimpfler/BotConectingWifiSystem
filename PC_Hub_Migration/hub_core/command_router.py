"""
hub_core/command_router.py — WebSocket JSON → Frame_t command dispatcher

Translates inbound WebSocket messages to Frame_t and routes ACK results back.
Handles: ctrl, mode, cal, subscribe, get_status

Ref: protocol_v1.md §12.2, ESP_Hub/src/CommandRouter.cpp
"""

import asyncio
import json
import logging
import time
from typing import Any, Callable, Coroutine, Optional, Set

from .protocol import (
    build_ctrl, build_mode, build_cal,
    ROLE_SAT1, ROLE_SAT2,
    MSG_NAMES, NETWORK_ID,
    parse_discovery,
    MSG_DISCOVERY, MSG_ACK, MSG_HEARTBEAT, MSG_DBG, MSG_TELEM_BATCH, MSG_TELEM_DICT,
    parse_ack, parse_telemetry_entry, parse_telem_batch, parse_telem_dict,
)
from .diagnostics import Diagnostics

logger = logging.getLogger(__name__)

BroadcastCb = Callable[[str], Coroutine[Any, Any, None]]
_SAT_ROLE = {"SAT1": ROLE_SAT1, "SAT2": ROLE_SAT2}


class CommandRouter:
    """
    Routes:
    - WebSocket JSON commands  → Frame_t  (ctrl / mode / cal)
    - Inbound Frame_t          → WebSocket JSON broadcasts (telemetry, peer_status, ack)
    - Telemetry dict caching   (stream_id → name per satellite)
    """

    def __init__(
        self,
        ingress: Any,
        ack_manager: Any,
        peer_tracker: Any,
        storage: Any,
        diag: Diagnostics,
        network_id: int = NETWORK_ID,
        broadcast_cb: Optional[BroadcastCb] = None,
    ) -> None:
        self._ingress      = ingress
        self._ack_mgr      = ack_manager
        self._peer_tracker = peer_tracker
        self._storage      = storage
        self._diag         = diag
        self._network_id   = network_id
        self._broadcast_cb = broadcast_cb  # async fn(json_str) → None
        self._seq: int     = 0
        # Telemetry dict: {sat_id: {stream_id: name}}
        self._telem_dict: dict = {"SAT1": {}, "SAT2": {}}
        # Rate limiting: {stream_key: last_push_time}
        self._last_push: dict = {}
        self._max_rate_hz: float = 50.0
        # Per-client subscriptions: {ws_id: set of "SAT1/Speed" stream keys}
        self._subscriptions: dict = {}

    def set_broadcast_callback(self, cb: BroadcastCb) -> None:
        self._broadcast_cb = cb

    def set_max_rate_hz(self, hz: float) -> None:
        self._max_rate_hz = max(1.0, hz)

    def _next_seq(self) -> int:
        seq = self._seq
        self._seq = (self._seq + 1) & 0xFF
        return seq

    # ── Inbound WebSocket message handling ───────────────────

    async def on_ws_message(self, ws_id: str, data: str) -> None:
        """Called for every text frame received from a WebSocket client."""
        try:
            msg = json.loads(data)
        except json.JSONDecodeError:
            logger.warning("Invalid JSON from ws client %s: %r", ws_id, data[:80])
            return

        msg_type = msg.get("type", "")

        if msg_type == "ctrl":
            await self._handle_ctrl(msg)
        elif msg_type == "mode":
            await self._handle_mode(msg)
        elif msg_type == "cal":
            await self._handle_cal(msg)
        elif msg_type == "subscribe":
            self._handle_subscribe(ws_id, msg)
        elif msg_type == "get_status":
            await self._broadcast_peer_status()
        else:
            logger.debug("Unknown WS message type: %s", msg_type)

    # ── Inbound ESP-NOW frame handling ────────────────────────

    async def on_frame(self, frame: Any) -> None:
        """Called for every validated inbound frame from ingress queue."""
        sat_id = "SAT1" if frame.src_role == ROLE_SAT1 else (
                 "SAT2" if frame.src_role == ROLE_SAT2 else None)

        # Update peer tracker for all satellite frames
        self._peer_tracker.on_frame(frame)

        if frame.msg_type == MSG_ACK:
            await self._handle_ack_frame(sat_id, frame)

        elif frame.msg_type == MSG_DBG:
            await self._handle_dbg_frame(sat_id, frame)

        elif frame.msg_type == MSG_TELEM_BATCH:
            await self._handle_telem_batch(sat_id, frame)

        elif frame.msg_type == MSG_TELEM_DICT:
            self._handle_telem_dict(sat_id, frame)

        elif frame.msg_type == MSG_DISCOVERY:
            self._handle_discovery(frame)

        elif frame.msg_type == MSG_HEARTBEAT:
            # Peer tracker already handled; broadcast status if needed
            await self._broadcast_peer_status()

        # MSG_UART_RAW and others: no hub-side action needed

    # ── Private: frame handlers ───────────────────────────────

    async def _handle_ack_frame(self, sat_id: Optional[str], frame: Any) -> None:
        ack = parse_ack(frame.payload)
        if not ack or sat_id is None:
            return
        self._ack_mgr.on_ack(
            sat_id      = sat_id,
            acked_seq   = ack["acked_seq"],
            status      = ack["status"],
            msg_type    = ack["msg_type"],
        )
        # WebSocket notification is delivered via ack future in send_command callers

    async def _handle_dbg_frame(self, sat_id: Optional[str], frame: Any) -> None:
        if sat_id is None:
            return
        entry = parse_telemetry_entry(frame.payload)
        if not entry:
            return
        now = time.time()
        self._storage.add_sample(
            ts_real     = now,
            sat_role    = frame.src_role,
            stream_name = entry["name"],
            vtype       = entry["vtype"],
            value       = entry["value"],
            ts_sat_ms   = entry["ts_ms"],
        )
        stream_key = f"{sat_id}/{entry['name']}"
        if self._should_push(stream_key):
            msg = json.dumps({
                "type":    "telemetry",
                "sat_id":  sat_id,
                "name":    entry["name"],
                "vtype":   entry["vtype"],
                "value":   entry["value"],
                "ts_ms":   entry["ts_ms"],
                "rx_ts":   now,
            })
            await self._broadcast(msg)

    async def _handle_telem_batch(self, sat_id: Optional[str], frame: Any) -> None:
        if sat_id is None:
            return
        entries = parse_telem_batch(frame.payload, frame.len)
        now = time.time()
        dict_map = self._telem_dict.get(sat_id, {})
        for entry in entries:
            stream_id  = entry["stream_id"]
            name       = dict_map.get(stream_id, f"stream_{stream_id}")
            vtype      = entry["vtype"]
            raw        = entry["raw"]
            # Decode value
            if vtype == 1:
                import struct
                value: Any = struct.unpack("<f", struct.pack("<i", raw))[0]
            elif vtype == 2:
                value = bool(raw)
            else:
                value = raw
            self._storage.add_sample(
                ts_real     = now,
                sat_role    = frame.src_role,
                stream_name = name,
                vtype       = vtype,
                value       = value,
            )
            stream_key = f"{sat_id}/{name}"
            if self._should_push(stream_key):
                msg = json.dumps({
                    "type":    "telemetry",
                    "sat_id":  sat_id,
                    "name":    name,
                    "vtype":   vtype,
                    "value":   value,
                    "rx_ts":   now,
                })
                await self._broadcast(msg)

    def _handle_telem_dict(self, sat_id: Optional[str], frame: Any) -> None:
        if sat_id is None:
            return
        entry = parse_telem_dict(frame.payload)
        if entry:
            self._telem_dict.setdefault(sat_id, {})[entry["stream_id"]] = entry["name"]
            logger.debug("TelemDict %s: stream_id=%d → %s",
                         sat_id, entry["stream_id"], entry["name"])

    def _handle_discovery(self, frame: Any) -> None:
        disc = parse_discovery(frame.payload)
        if not disc:
            return
        sat_id = "SAT1" if disc.get("role") == ROLE_SAT1 else (
                 "SAT2" if disc.get("role") == ROLE_SAT2 else None)
        if sat_id and disc.get("mac"):
            self._peer_tracker.on_discovery(sat_id, disc["mac"])

    # ── Private: outbound command builders ───────────────────

    async def _handle_ctrl(self, msg: dict) -> None:
        sat_id = msg.get("sat_id", "SAT1")
        role   = _SAT_ROLE.get(sat_id, ROLE_SAT1)
        frame  = build_ctrl(
            seq         = self._next_seq(),
            speed       = int(msg.get("speed",    0)),
            angle       = int(msg.get("angle",    0)),
            switches    = int(msg.get("switches", 0)),
            buttons     = int(msg.get("buttons",  0)),
            start       = int(msg.get("start",    0)),
            target_role = role,
            network_id  = self._network_id,
        )
        await self._dispatch_command(sat_id, frame, "ctrl", msg)

    async def _handle_mode(self, msg: dict) -> None:
        sat_id  = msg.get("sat_id", "SAT1")
        role    = _SAT_ROLE.get(sat_id, ROLE_SAT1)
        mode_id = int(msg.get("mode_id", 1))
        frame   = build_mode(
            seq         = self._next_seq(),
            mode_id     = mode_id,
            target_role = role,
            network_id  = self._network_id,
        )
        await self._dispatch_command(sat_id, frame, "mode", msg)

    async def _handle_cal(self, msg: dict) -> None:
        sat_id  = msg.get("sat_id", "SAT1")
        role    = _SAT_ROLE.get(sat_id, ROLE_SAT1)
        cal_cmd = int(msg.get("cal_cmd", 1))
        frame   = build_cal(
            seq         = self._next_seq(),
            cal_cmd     = cal_cmd,
            target_role = role,
            network_id  = self._network_id,
        )
        await self._dispatch_command(sat_id, frame, "cal", msg)

    async def _dispatch_command(
        self, sat_id: str, frame: Any, cmd_type: str, payload: dict
    ) -> None:
        # Log command to storage
        row_id = await self._storage.add_command(
            ts_real  = time.time(),
            sat_role = frame.dst_role,
            cmd_type = cmd_type,
            payload  = payload,
            status   = "pending",
        )
        try:
            fut = await self._ack_mgr.send_command(sat_id, frame, self._ingress)
            result = await asyncio.wait_for(fut, timeout=5.0)
        except asyncio.TimeoutError:
            result = {"status": "timeout", "retries": self._ack_mgr._max_retries}

        status  = result.get("status", "unknown")
        retries = result.get("retries", 0)

        await self._storage.update_command_status(row_id, status, retries)

        ack_msg = json.dumps({
            "type":     "command_ack",
            "sat_id":   sat_id,
            "cmd_type": cmd_type,
            "seq":      frame.seq,
            "status":   status,
            "retries":  retries,
        })
        await self._broadcast(ack_msg)
        logger.info("CMD %s %s seq=%d → status=%s retries=%d",
                    cmd_type, sat_id, frame.seq, status, retries)

    def _handle_subscribe(self, ws_id: str, msg: dict) -> None:
        streams = msg.get("streams", [])
        self._subscriptions[ws_id] = set(streams)
        rate = msg.get("rate_limit_hz")
        if rate is not None:
            self.set_max_rate_hz(float(rate))

    # ── Rate limiting ─────────────────────────────────────────

    def _should_push(self, stream_key: str) -> bool:
        """Check if the stream is within rate limit."""
        min_interval = 1.0 / self._max_rate_hz
        now = time.monotonic()
        last = self._last_push.get(stream_key, 0.0)
        if (now - last) >= min_interval:
            self._last_push[stream_key] = now
            return True
        return False

    # ── Broadcast helpers ─────────────────────────────────────

    async def _broadcast(self, json_str: str) -> None:
        if self._broadcast_cb:
            try:
                await self._broadcast_cb(json_str)
            except Exception as exc:  # noqa: BLE001
                logger.debug("Broadcast error: %s", exc)

    async def _broadcast_peer_status(self) -> None:
        msg = json.dumps({
            "type":  "peer_status",
            "peers": self._peer_tracker.status_dict(),
        })
        await self._broadcast(msg)

    async def broadcast_hub_status(self) -> None:
        """Push hub_status metrics to all WS clients."""
        snap = self._diag.snapshot()
        msg = json.dumps({
            "type":     "hub_status",
            "uptime_s": snap["uptime_s"],
            "rx_frames": snap["rx_frames"],
            "tx_frames": snap["tx_frames"],
            "errors":    snap["errors"],
        })
        await self._broadcast(msg)
