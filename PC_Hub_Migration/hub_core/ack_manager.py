"""
hub_core/ack_manager.py — ACK/retry manager for reliable hub→satellite commands

Implements the ACK/retry pattern from protocol_v1.md §8:
- Tracks pending commands by (sat_id, seq)
- Retries on timeout (ACK_TIMEOUT_MS × ACK_MAX_RETRIES)
- Reports result via asyncio.Future

New for PC Hub (ESP hub did not implement hub-side retry).
Ref: ADR-005, protocol_v1.md §8.2
"""

import asyncio
import logging
import time
from dataclasses import dataclass, field
from typing import Dict, Optional, Tuple, Any

from .protocol import (
    ACK_TIMEOUT_MS, ACK_MAX_RETRIES,
    ACK_OK, ACK_STATUS_NAME,
    MSG_NAMES,
)
from .diagnostics import Diagnostics

logger = logging.getLogger(__name__)


@dataclass
class PendingCommand:
    sat_id:   str
    seq:      int
    msg_type: int
    frame:    Any              # hub_core.protocol.Frame
    retries:  int = 0
    sent_at:  float = field(default_factory=time.monotonic)
    future:   Optional[asyncio.Future] = field(default=None, compare=False)


class AckManager:
    """
    Manages pending ACK tracking with automatic retry and timeout.

    Usage:
        future = await ack_mgr.send_command(sat_id, frame, ingress)
        result = await asyncio.wait_for(future, timeout=5.0)

    `result` is a dict:
        {"status": "ok", "retries": 0}  or  {"status": "timeout", "retries": 3}
    """

    def __init__(
        self,
        timeout_ms: int = ACK_TIMEOUT_MS,
        max_retries: int = ACK_MAX_RETRIES,
        diag: Optional[Diagnostics] = None,
    ) -> None:
        self._timeout_s  = timeout_ms / 1000.0
        self._max_retries = max_retries
        self._diag       = diag
        self._pending: Dict[Tuple[str, int], PendingCommand] = {}
        self._ingress: Optional[Any] = None
        self._task:    Optional[asyncio.Task] = None

    def start(self, ingress: Any) -> None:
        """Start the retry ticker. ingress must have async send(frame)."""
        self._ingress = ingress
        self._task = asyncio.ensure_future(self._tick_loop())
        logger.info("AckManager started (timeout=%.3fs retries=%d)",
                    self._timeout_s, self._max_retries)

    def stop(self) -> None:
        if self._task:
            self._task.cancel()
            self._task = None

    async def send_command(
        self,
        sat_id: str,
        frame: Any,
        ingress: Optional[Any] = None,
    ) -> asyncio.Future:
        """
        Send a command frame and return a Future that resolves when ACK'd or failed.
        """
        if ingress is None:
            ingress = self._ingress

        key = (sat_id, frame.seq)
        loop = asyncio.get_event_loop()
        fut: asyncio.Future = loop.create_future()

        cmd = PendingCommand(
            sat_id   = sat_id,
            seq      = frame.seq,
            msg_type = frame.msg_type,
            frame    = frame,
            future   = fut,
        )
        self._pending[key] = cmd

        ok = await ingress.send(frame)
        if not ok:
            del self._pending[key]
            if not fut.done():
                fut.set_result({"status": "send_error", "retries": 0})
            return fut

        logger.debug("CMD sent %s seq=%d type=%s (pending ACK)",
                     sat_id, frame.seq, MSG_NAMES.get(frame.msg_type, "?"))
        return fut

    def on_ack(self, sat_id: str, acked_seq: int, status: int, msg_type: int) -> None:
        """
        Called when a MSG_ACK frame is received.
        Resolves the corresponding pending future.
        """
        key = (sat_id, acked_seq)
        cmd = self._pending.pop(key, None)
        if cmd is None:
            logger.debug("ACK for unknown/expired seq=%d from %s (duplicate?)",
                         acked_seq, sat_id)
            return
        status_str = ACK_STATUS_NAME.get(status, f"status_{status}")
        logger.info("ACK %s seq=%d type=%s status=%s retries=%d",
                    sat_id, acked_seq, MSG_NAMES.get(msg_type, "?"),
                    status_str, cmd.retries)
        if cmd.future and not cmd.future.done():
            cmd.future.set_result({"status": status_str, "retries": cmd.retries})

    async def _tick_loop(self) -> None:
        """Periodically check for timed-out commands and retry or fail them."""
        while True:
            try:
                await asyncio.sleep(self._timeout_s / 2)
                await self._tick()
            except asyncio.CancelledError:
                break
            except Exception as exc:  # noqa: BLE001
                logger.error("AckManager tick error: %s", exc, exc_info=True)

    async def _tick(self) -> None:
        now = time.monotonic()
        expired_keys = []
        for key, cmd in list(self._pending.items()):
            elapsed = now - cmd.sent_at
            if elapsed < self._timeout_s:
                continue

            if cmd.retries >= self._max_retries:
                # Give up
                expired_keys.append(key)
                if self._diag:
                    self._diag.tx_ack_timeout(cmd.sat_id)
                    self._diag.tx_command_failure(cmd.sat_id)
                logger.warning("CMD FAILED %s seq=%d type=%s — all %d retries exhausted",
                               cmd.sat_id, cmd.seq,
                               MSG_NAMES.get(cmd.msg_type, "?"),
                               self._max_retries)
                if cmd.future and not cmd.future.done():
                    cmd.future.set_result({"status": "timeout", "retries": cmd.retries})
            else:
                # Retry
                cmd.retries  += 1
                cmd.sent_at   = now
                if self._diag:
                    self._diag.tx_ack_retry(cmd.sat_id)
                logger.info("CMD RETRY %s seq=%d type=%s retry=%d/%d",
                            cmd.sat_id, cmd.seq,
                            MSG_NAMES.get(cmd.msg_type, "?"),
                            cmd.retries, self._max_retries)
                if self._ingress:
                    await self._ingress.send(cmd.frame)

        for key in expired_keys:
            self._pending.pop(key, None)
