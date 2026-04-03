"""
hub_core/peer_tracker.py — Satellite peer state machine

States: OFFLINE → CONNECTING → ONLINE → OFFLINE
Transitions trigger WebSocket peer_status broadcasts.

Source reference: ESP_Hub/src/PeerRegistry.cpp, protocol_v1.md §11
"""

import asyncio
import logging
import time
from dataclasses import dataclass, field
from typing import Callable, Coroutine, Dict, Optional, Any

from .protocol import (
    ROLE_SAT1, ROLE_SAT2, ROLE_HUB, ROLE_BROADCAST, ROLE_NAME,
    HEARTBEAT_TIMEOUT_MS,
)

logger = logging.getLogger(__name__)

# Peer states
STATE_OFFLINE     = "offline"
STATE_CONNECTING  = "connecting"
STATE_ONLINE      = "online"


@dataclass
class PeerInfo:
    sat_id: str          # "SAT1" or "SAT2"
    role: int            # ROLE_SAT1 or ROLE_SAT2
    mac: str = ""        # "AA:BB:CC:DD:EE:FF" or empty before discovery
    state: str = STATE_OFFLINE
    uptime_ms: int = 0
    rssi: int = 0
    queue_len: int = 0
    last_seen: float = 0.0   # monotonic timestamp of last received frame

    def to_dict(self) -> dict:
        return {
            "sat_id":    self.sat_id,
            "role":      self.role,
            "mac":       self.mac,
            "online":    self.state == STATE_ONLINE,
            "state":     self.state,
            "uptime_ms": self.uptime_ms,
            "rssi":      self.rssi,
            "queue_len": self.queue_len,
            "last_seen": self.last_seen,
        }


StatusCallback = Callable[["PeerTracker"], Coroutine[Any, Any, None]]


class PeerTracker:
    """
    Tracks online/offline state for up to 2 satellites.

    Lifecycle:
    - Call on_frame(frame) for every validated inbound frame.
    - Call tick(timeout_ms) periodically to detect offline peers.
    - Register a status_callback to get notified of state transitions.
    """

    def __init__(
        self,
        timeout_ms: int = HEARTBEAT_TIMEOUT_MS,
        status_callback: Optional[StatusCallback] = None,
    ) -> None:
        self._timeout_s = timeout_ms / 1000.0
        self._status_cb = status_callback
        self._peers: Dict[str, PeerInfo] = {
            "SAT1": PeerInfo(sat_id="SAT1", role=ROLE_SAT1),
            "SAT2": PeerInfo(sat_id="SAT2", role=ROLE_SAT2),
        }

    def _role_to_sat_id(self, role: int) -> Optional[str]:
        if role == ROLE_SAT1:
            return "SAT1"
        if role == ROLE_SAT2:
            return "SAT2"
        return None

    def get_peer(self, sat_id: str) -> Optional[PeerInfo]:
        return self._peers.get(sat_id)

    def all_peers(self) -> list:
        return list(self._peers.values())

    def on_frame(self, frame: "Any") -> None:
        """
        Called for every validated inbound frame.
        Updates last_seen, state machine, and peer metadata.
        """
        sat_id = self._role_to_sat_id(frame.src_role)
        if sat_id is None:
            return

        peer = self._peers[sat_id]
        peer.last_seen = time.monotonic()

        old_state = peer.state
        if peer.state == STATE_OFFLINE:
            peer.state = STATE_CONNECTING
            logger.info("Peer %s: OFFLINE → CONNECTING (first frame)", sat_id)

        # Update MAC from discovery or any frame
        if peer.mac == "" and hasattr(frame, "src_mac"):
            peer.mac = frame.src_mac

        # Process heartbeat payload for richer state
        from .protocol import MSG_HEARTBEAT, parse_heartbeat
        if frame.msg_type == MSG_HEARTBEAT:
            hb = parse_heartbeat(frame.payload)
            if hb:
                peer.uptime_ms = hb.get("uptime_ms", 0)
                peer.rssi      = hb.get("rssi", 0)
                peer.queue_len = hb.get("queue_len", 0)
            if peer.state == STATE_CONNECTING:
                peer.state = STATE_ONLINE
                logger.info("Peer %s: CONNECTING → ONLINE (heartbeat received)", sat_id)

        if old_state != peer.state and self._status_cb is not None:
            asyncio.ensure_future(self._status_cb(self))

    def on_discovery(self, sat_id: str, mac: str) -> None:
        """Update peer MAC from a discovery frame."""
        peer = self._peers.get(sat_id)
        if peer and peer.mac != mac:
            peer.mac = mac
            logger.info("Peer %s discovered at MAC %s", sat_id, mac)

    def tick(self) -> list:
        """
        Check for timed-out peers. Returns list of sat_ids that went offline.
        Call this every ~500 ms.
        """
        now = time.monotonic()
        went_offline = []
        for sat_id, peer in self._peers.items():
            if peer.state in (STATE_CONNECTING, STATE_ONLINE):
                if peer.last_seen > 0 and (now - peer.last_seen) > self._timeout_s:
                    logger.warning("Peer %s TIMEOUT (no frame for %.1fs) → OFFLINE",
                                   sat_id, now - peer.last_seen)
                    peer.state = STATE_OFFLINE
                    went_offline.append(sat_id)
                    if self._status_cb is not None:
                        asyncio.ensure_future(self._status_cb(self))
        return went_offline

    def status_dict(self) -> list:
        """Return list of peer dicts for WebSocket peer_status message."""
        return [p.to_dict() for p in self._peers.values()]
