"""
tests/test_peer_tracker.py — Unit tests for hub_core/peer_tracker.py

Tests: state machine transitions, heartbeat processing, timeout detection.
"""

import asyncio
import time
import sys
import os
import unittest
from unittest.mock import MagicMock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hub_core.peer_tracker import PeerTracker, STATE_OFFLINE, STATE_CONNECTING, STATE_ONLINE
from hub_core.protocol import (
    MSG_HEARTBEAT, MSG_DBG, ROLE_SAT1, ROLE_SAT2,
    build_heartbeat, Frame, FRAME_MAGIC, NETWORK_ID, PAYLOAD_MAX,
)


def _make_frame(msg_type: int, src_role: int) -> Frame:
    """Build a minimal frame for testing."""
    import struct
    payload = b"\x00" * PAYLOAD_MAX
    if msg_type == MSG_HEARTBEAT:
        hb_payload = struct.pack("<IbB", 1000, -50, 0)
        payload = hb_payload + b"\x00" * (PAYLOAD_MAX - len(hb_payload))
    f = Frame(
        magic=FRAME_MAGIC, msg_type=msg_type, seq=1,
        src_role=src_role, dst_role=0x00,
        flags=0, len=6 if msg_type == MSG_HEARTBEAT else 0,
        network_id=NETWORK_ID, payload=payload,
    )
    raw = f.to_bytes()
    import struct as s
    f.crc16 = s.unpack_from("<H", raw, 188)[0]
    return f


class TestPeerTrackerStateMachine(unittest.TestCase):

    def setUp(self):
        self.tracker = PeerTracker(timeout_ms=4000)

    def test_initial_state_is_offline(self):
        p = self.tracker.get_peer("SAT1")
        self.assertIsNotNone(p)
        self.assertEqual(p.state, STATE_OFFLINE)

    def test_first_frame_transitions_to_connecting(self):
        frame = _make_frame(MSG_DBG, ROLE_SAT1)
        self.tracker.on_frame(frame)
        p = self.tracker.get_peer("SAT1")
        self.assertEqual(p.state, STATE_CONNECTING)

    def test_heartbeat_transitions_connecting_to_online(self):
        # First frame: OFFLINE → CONNECTING
        frame = _make_frame(MSG_DBG, ROLE_SAT1)
        self.tracker.on_frame(frame)
        # Heartbeat: CONNECTING → ONLINE
        hb_frame = _make_frame(MSG_HEARTBEAT, ROLE_SAT1)
        self.tracker.on_frame(hb_frame)
        p = self.tracker.get_peer("SAT1")
        self.assertEqual(p.state, STATE_ONLINE)

    def test_heartbeat_updates_rssi(self):
        import struct
        hb_payload = struct.pack("<IbB", 5000, -62, 3)
        hb_payload += b"\x00" * (PAYLOAD_MAX - len(hb_payload))
        f = Frame(
            magic=FRAME_MAGIC, msg_type=MSG_HEARTBEAT, seq=2,
            src_role=ROLE_SAT1, dst_role=0x00,
            flags=0, len=6, network_id=NETWORK_ID, payload=hb_payload,
        )
        raw = f.to_bytes()
        f.crc16 = struct.unpack_from("<H", raw, 188)[0]
        self.tracker.on_frame(f)
        p = self.tracker.get_peer("SAT1")
        self.assertEqual(p.rssi, -62)
        self.assertEqual(p.queue_len, 3)
        self.assertEqual(p.uptime_ms, 5000)

    def test_sat2_is_independent(self):
        hb1 = _make_frame(MSG_HEARTBEAT, ROLE_SAT1)
        hb2 = _make_frame(MSG_HEARTBEAT, ROLE_SAT2)
        self.tracker.on_frame(hb1)
        self.tracker.on_frame(hb2)
        self.assertEqual(self.tracker.get_peer("SAT1").state, STATE_ONLINE)
        self.assertEqual(self.tracker.get_peer("SAT2").state, STATE_ONLINE)

    def test_hub_frame_ignored(self):
        """Frames from ROLE_HUB should not change any peer state."""
        import struct
        f = Frame(
            magic=FRAME_MAGIC, msg_type=MSG_HEARTBEAT, seq=1,
            src_role=0x00, dst_role=0xFF,  # ROLE_HUB → BROADCAST
            flags=0, len=6, network_id=NETWORK_ID,
            payload=b"\x00" * PAYLOAD_MAX,
        )
        raw = f.to_bytes()
        f.crc16 = struct.unpack_from("<H", raw, 188)[0]
        self.tracker.on_frame(f)
        # Neither SAT1 nor SAT2 should have changed
        self.assertEqual(self.tracker.get_peer("SAT1").state, STATE_OFFLINE)
        self.assertEqual(self.tracker.get_peer("SAT2").state, STATE_OFFLINE)


class TestPeerTrackerTimeout(unittest.TestCase):

    def test_no_timeout_within_window(self):
        tracker = PeerTracker(timeout_ms=4000)
        frame = _make_frame(MSG_HEARTBEAT, ROLE_SAT1)
        tracker.on_frame(frame)
        # last_seen is fresh — no timeout
        went_offline = tracker.tick()
        self.assertEqual(went_offline, [])
        self.assertEqual(tracker.get_peer("SAT1").state, STATE_ONLINE)

    def test_timeout_moves_peer_offline(self):
        tracker = PeerTracker(timeout_ms=100)  # 100 ms timeout for test speed
        frame = _make_frame(MSG_HEARTBEAT, ROLE_SAT1)
        tracker.on_frame(frame)
        self.assertEqual(tracker.get_peer("SAT1").state, STATE_ONLINE)
        # Manually set last_seen to past
        tracker.get_peer("SAT1").last_seen = time.monotonic() - 1.0
        went_offline = tracker.tick()
        self.assertIn("SAT1", went_offline)
        self.assertEqual(tracker.get_peer("SAT1").state, STATE_OFFLINE)

    def test_offline_peer_not_timed_out(self):
        """Peers in OFFLINE state should never appear in timeout list."""
        tracker = PeerTracker(timeout_ms=100)
        # SAT1 never received any frame — last_seen=0
        went_offline = tracker.tick()
        self.assertNotIn("SAT1", went_offline)


class TestPeerTrackerDiscovery(unittest.TestCase):

    def test_discovery_updates_mac(self):
        tracker = PeerTracker()
        tracker.on_discovery("SAT1", "AA:BB:CC:DD:EE:FF")
        p = tracker.get_peer("SAT1")
        self.assertEqual(p.mac, "AA:BB:CC:DD:EE:FF")

    def test_status_dict_structure(self):
        tracker = PeerTracker()
        status = tracker.status_dict()
        self.assertEqual(len(status), 2)
        sat_ids = {s["sat_id"] for s in status}
        self.assertEqual(sat_ids, {"SAT1", "SAT2"})
        for s in status:
            self.assertIn("online", s)
            self.assertIn("state", s)
            self.assertIn("role", s)


if __name__ == "__main__":
    unittest.main(verbosity=2)
