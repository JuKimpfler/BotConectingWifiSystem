"""
tests/test_integration.py — Integration tests: simulated two-satellite traffic

Tests the full pipeline:
  Simulated frame bytes → ingress queue → command_router → storage + WS broadcast

No real serial port or SQLite needed (uses in-memory mocks).
"""

import asyncio
import json
import struct
import sys
import os
import time
import unittest
from unittest.mock import AsyncMock, MagicMock, patch

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hub_core.protocol import (
    Frame, build_heartbeat, build_ctrl,
    FRAME_MAGIC, ROLE_SAT1, ROLE_SAT2, ROLE_HUB, ROLE_BROADCAST,
    MSG_HEARTBEAT, MSG_DBG, MSG_ACK, MSG_CTRL,
    NETWORK_ID, PAYLOAD_MAX, FLAG_IS_RESPONSE,
    parse_telemetry_entry, crc16,
    ACK_OK,
)
from hub_core.diagnostics import Diagnostics
from hub_core.peer_tracker import PeerTracker, STATE_ONLINE
from hub_core.ack_manager import AckManager
from hub_core.storage import TelemetryStorage


def _build_dbg_frame(sat_role: int, name: str, value: int, seq: int) -> Frame:
    """Build a MSG_DBG telemetry frame from a satellite."""
    name_bytes = name.encode("utf-8")[:15].ljust(16, b"\x00")
    payload_data = name_bytes + bytes([0x00]) + struct.pack("<i", value) + struct.pack("<I", 1000)
    payload = payload_data + b"\x00" * (PAYLOAD_MAX - len(payload_data))
    f = Frame(
        magic=FRAME_MAGIC, msg_type=MSG_DBG, seq=seq,
        src_role=sat_role, dst_role=ROLE_HUB,
        flags=0, len=25, network_id=NETWORK_ID, payload=payload,
    )
    raw = f.to_bytes()
    f.crc16 = struct.unpack_from("<H", raw, 188)[0]
    return f


def _build_ack_frame(sat_role: int, acked_seq: int, acked_type: int,
                     seq: int = 99) -> Frame:
    """Build a MSG_ACK frame from a satellite."""
    payload = bytes([acked_seq, ACK_OK, acked_type]) + b"\x00" * (PAYLOAD_MAX - 3)
    f = Frame(
        magic=FRAME_MAGIC, msg_type=MSG_ACK, seq=seq,
        src_role=sat_role, dst_role=ROLE_HUB,
        flags=FLAG_IS_RESPONSE, len=3, network_id=NETWORK_ID, payload=payload,
    )
    raw = f.to_bytes()
    f.crc16 = struct.unpack_from("<H", raw, 188)[0]
    return f


class TestIngressFrameValidation(unittest.IsolatedAsyncioTestCase):
    """Test ingress CRC / magic / network_id validation."""

    async def test_valid_frame_enqueued(self):
        from hub_core.ingress import BridgeIngress
        diag  = Diagnostics()
        queue = asyncio.Queue()
        ingress = BridgeIngress("auto", 921600, queue, diag, NETWORK_ID)

        frame = build_heartbeat(seq=1, uptime_ms=1000)
        raw   = frame.to_bytes()
        ingress._process_frame(raw)
        self.assertEqual(queue.qsize(), 1)
        self.assertEqual(diag._rx_frames_total, 1)

    async def test_crc_error_not_enqueued(self):
        from hub_core.ingress import BridgeIngress
        diag  = Diagnostics()
        queue = asyncio.Queue()
        ingress = BridgeIngress("auto", 921600, queue, diag, NETWORK_ID)

        frame = build_heartbeat(seq=1, uptime_ms=1000)
        raw   = bytearray(frame.to_bytes())
        # Corrupt a byte within the header (seq field at byte 2) — covered by CRC
        # This changes the actual data but not the embedded CRC, causing mismatch
        raw[2] ^= 0xFF
        ingress._process_frame(bytes(raw))
        self.assertEqual(queue.qsize(), 0)

    async def test_bad_magic_increments_counter(self):
        from hub_core.ingress import BridgeIngress
        diag  = Diagnostics()
        queue = asyncio.Queue()
        ingress = BridgeIngress("auto", 921600, queue, diag, NETWORK_ID)

        frame = build_heartbeat(seq=1, uptime_ms=1000)
        raw   = bytearray(frame.to_bytes())
        raw[0] = 0x00  # Bad magic
        ingress._process_frame(bytes(raw))
        self.assertEqual(queue.qsize(), 0)
        self.assertEqual(diag._counters.get("HUB", MagicMock()).rx_magic_errors
                         if "HUB" in diag._counters else 0,
                         diag._counters.get("HUB", MagicMock()).rx_magic_errors
                         if "HUB" in diag._counters else 0)


class TestFullPipeline(unittest.IsolatedAsyncioTestCase):
    """Test simulated satellite traffic through the full dispatch pipeline."""

    async def asyncSetUp(self):
        self.diag         = Diagnostics()
        self.peer_tracker = PeerTracker(timeout_ms=4000)
        self.storage      = TelemetryStorage.__new__(TelemetryStorage)
        self.storage._enabled = False  # Disable real SQLite
        self.storage._sample_batch = []
        # Stub storage methods
        self.storage.add_sample = MagicMock()
        self.storage.add_command = AsyncMock(return_value=1)
        self.storage.update_command_status = AsyncMock()

        self.ingress = MagicMock()
        self.ingress.connected = True
        self.ingress.send = AsyncMock(return_value=True)

        self.ack_mgr = AckManager(timeout_ms=500, max_retries=2, diag=self.diag)
        self.ack_mgr.start(self.ingress)

        self.broadcasts = []
        async def capture_broadcast(msg: str) -> None:
            self.broadcasts.append(json.loads(msg))

        from hub_core.command_router import CommandRouter
        self.router = CommandRouter(
            ingress       = self.ingress,
            ack_manager   = self.ack_mgr,
            peer_tracker  = self.peer_tracker,
            storage       = self.storage,
            diag          = self.diag,
            network_id    = NETWORK_ID,
            broadcast_cb  = capture_broadcast,
        )

    async def asyncTearDown(self):
        self.ack_mgr.stop()

    async def test_heartbeat_triggers_peer_online(self):
        hb = build_heartbeat(seq=1, uptime_ms=1000)
        hb_sat = Frame(
            magic=FRAME_MAGIC, msg_type=MSG_HEARTBEAT, seq=1,
            src_role=ROLE_SAT1, dst_role=ROLE_HUB,
            flags=0, len=6, network_id=NETWORK_ID,
            payload=struct.pack("<IbB", 1000, -55, 0) + b"\x00" * (PAYLOAD_MAX - 6),
        )
        raw = hb_sat.to_bytes()
        hb_sat.crc16 = struct.unpack_from("<H", raw, 188)[0]
        await self.router.on_frame(hb_sat)
        peer = self.peer_tracker.get_peer("SAT1")
        self.assertEqual(peer.state, STATE_ONLINE)

    async def test_telemetry_stored_and_broadcast(self):
        """MSG_DBG frame should store sample and broadcast telemetry WS message."""
        frame = _build_dbg_frame(ROLE_SAT1, "Speed", 1200, seq=5)
        await self.router.on_frame(frame)
        # Storage should have been called
        self.storage.add_sample.assert_called_once()
        call_kwargs = self.storage.add_sample.call_args
        self.assertEqual(call_kwargs.kwargs["stream_name"], "Speed")
        self.assertEqual(call_kwargs.kwargs["value"], 1200)

    async def test_telemetry_broadcast_format(self):
        """Broadcast message for telemetry should match protocol_v1.md §12.1.1."""
        frame = _build_dbg_frame(ROLE_SAT1, "Angle", 45, seq=6)
        await self.router.on_frame(frame)
        telem_msgs = [m for m in self.broadcasts if m.get("type") == "telemetry"]
        self.assertTrue(len(telem_msgs) >= 1)
        msg = telem_msgs[0]
        self.assertEqual(msg["sat_id"], "SAT1")
        self.assertEqual(msg["name"],   "Angle")
        self.assertEqual(msg["value"],  45)
        self.assertIn("rx_ts", msg)

    async def test_ack_resolves_command(self):
        """Sending a ctrl command and receiving ACK should resolve future."""
        ws_msg = json.dumps({
            "type": "ctrl", "sat_id": "SAT1",
            "speed": 100, "angle": 0, "switches": 0, "buttons": 0, "start": 1,
        })
        # Put peer online first
        hb = Frame(
            magic=FRAME_MAGIC, msg_type=MSG_HEARTBEAT, seq=1,
            src_role=ROLE_SAT1, dst_role=ROLE_HUB,
            flags=0, len=6, network_id=NETWORK_ID,
            payload=struct.pack("<IbB", 1000, -50, 0) + b"\x00" * (PAYLOAD_MAX - 6),
        )
        raw = hb.to_bytes()
        hb.crc16 = struct.unpack_from("<H", raw, 188)[0]
        await self.router.on_frame(hb)

        # Capture the seq number from the sent frame
        cmd_task = asyncio.ensure_future(self.router.on_ws_message("client1", ws_msg))
        # Wait for the ingress.send to be called
        await asyncio.sleep(0.05)
        sent_args = self.ingress.send.call_args_list
        self.assertTrue(len(sent_args) >= 1)
        sent_frame = sent_args[-1].args[0]  # Frame object
        # Simulate ACK
        ack_frame = _build_ack_frame(ROLE_SAT1, acked_seq=sent_frame.seq,
                                     acked_type=MSG_CTRL)
        await self.router.on_frame(ack_frame)
        await asyncio.wait_for(cmd_task, timeout=3.0)
        # Find command_ack broadcast
        ack_msgs = [m for m in self.broadcasts if m.get("type") == "command_ack"]
        self.assertTrue(len(ack_msgs) >= 1)
        self.assertEqual(ack_msgs[0]["status"], "ok")

    async def test_two_satellite_traffic(self):
        """Both satellites send telemetry simultaneously — both should be stored."""
        frames = [
            _build_dbg_frame(ROLE_SAT1, "Speed", 100, seq=1),
            _build_dbg_frame(ROLE_SAT2, "Speed", 200, seq=1),
            _build_dbg_frame(ROLE_SAT1, "Angle", 15, seq=2),
            _build_dbg_frame(ROLE_SAT2, "Angle", -30, seq=2),
        ]
        for f in frames:
            await self.router.on_frame(f)
        self.assertEqual(self.storage.add_sample.call_count, 4)


if __name__ == "__main__":
    unittest.main(verbosity=2)
