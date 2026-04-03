"""
hub_core/heartbeat.py — Periodic hub→satellite heartbeat transmitter

Sends MSG_HEARTBEAT to ROLE_BROADCAST every interval_ms.
Ref: ESP_Hub/src/HeartbeatService.cpp, protocol_v1.md §5 §11
"""

import asyncio
import logging
import time
from typing import Optional, Any

from .protocol import build_heartbeat, ROLE_NAME, HEARTBEAT_INTERVAL_MS

logger = logging.getLogger(__name__)


class HeartbeatService:
    """
    Periodic heartbeat transmitter.
    Calls ingress.send(frame) at every interval.
    Also triggers peer_tracker.tick() to detect offline peers.
    """

    def __init__(
        self,
        interval_ms: int = HEARTBEAT_INTERVAL_MS,
        network_id: int = 0x01,
    ) -> None:
        self._interval_s  = interval_ms / 1000.0
        self._network_id  = network_id
        self._seq:        int = 0
        self._task:       Optional[asyncio.Task] = None
        self._start_time: float = time.monotonic()

    def start(self, ingress: Any, peer_tracker: Any) -> None:
        """Start the heartbeat loop. Call once after event loop is running."""
        self._ingress      = ingress
        self._peer_tracker = peer_tracker
        self._task = asyncio.ensure_future(self._loop())
        logger.info("Heartbeat service started (interval=%.1fs)", self._interval_s)

    def stop(self) -> None:
        """Stop the heartbeat loop."""
        if self._task:
            self._task.cancel()
            self._task = None

    async def _loop(self) -> None:
        while True:
            try:
                await asyncio.sleep(self._interval_s)
                await self._send_heartbeat()
                went_offline = self._peer_tracker.tick()
                if went_offline:
                    logger.info("Peers went offline: %s", went_offline)
            except asyncio.CancelledError:
                break
            except Exception as exc:  # noqa: BLE001
                logger.error("Heartbeat loop error: %s", exc, exc_info=True)

    async def _send_heartbeat(self) -> None:
        uptime_ms = int((time.monotonic() - self._start_time) * 1000)
        frame = build_heartbeat(
            seq=self._seq,
            uptime_ms=uptime_ms,
            network_id=self._network_id,
        )
        self._seq = (self._seq + 1) & 0xFF
        ok = await self._ingress.send(frame)
        if ok:
            logger.debug("Heartbeat TX seq=%d uptime_ms=%d", frame.seq, uptime_ms)
        else:
            logger.debug("Heartbeat TX skipped (bridge not connected)")
