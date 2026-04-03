"""
tests/test_ack_manager.py — Unit tests for hub_core/ack_manager.py

Tests: ACK resolution, retry, timeout, duplicate ACK handling.
"""

import asyncio
import sys
import os
import unittest
from unittest.mock import AsyncMock, MagicMock

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hub_core.ack_manager import AckManager
from hub_core.protocol import build_ctrl, ROLE_SAT1, ACK_OK, ACK_ERR_BUSY
from hub_core.diagnostics import Diagnostics


def _make_ingress(send_ok: bool = True):
    ingress = MagicMock()
    ingress.send = AsyncMock(return_value=send_ok)
    return ingress


class TestAckManagerBasic(unittest.IsolatedAsyncioTestCase):

    async def asyncSetUp(self):
        self.diag    = Diagnostics()
        self.ingress = _make_ingress()
        self.mgr     = AckManager(timeout_ms=500, max_retries=2, diag=self.diag)
        self.mgr.start(self.ingress)

    async def asyncTearDown(self):
        self.mgr.stop()

    async def test_ack_resolves_future(self):
        """Receiving an ACK should resolve the pending future with status=ok."""
        frame = build_ctrl(seq=1, speed=100, angle=0, switches=0,
                           buttons=0, start=1, target_role=ROLE_SAT1)
        fut = await self.mgr.send_command("SAT1", frame, self.ingress)
        self.ingress.send.assert_awaited_once()
        # Simulate ACK arrival
        self.mgr.on_ack("SAT1", acked_seq=1, status=ACK_OK, msg_type=0x02)
        result = await asyncio.wait_for(fut, timeout=1.0)
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["retries"], 0)

    async def test_rejected_ack(self):
        """ACK with busy status should resolve with status=busy."""
        frame = build_ctrl(seq=2, speed=0, angle=0, switches=0,
                           buttons=0, start=0, target_role=ROLE_SAT1)
        fut = await self.mgr.send_command("SAT1", frame, self.ingress)
        self.mgr.on_ack("SAT1", acked_seq=2, status=ACK_ERR_BUSY, msg_type=0x02)
        result = await asyncio.wait_for(fut, timeout=1.0)
        self.assertEqual(result["status"], "busy")

    async def test_duplicate_ack_ignored(self):
        """A second ACK for the same seq should not raise."""
        frame = build_ctrl(seq=3, speed=0, angle=0, switches=0,
                           buttons=0, start=0, target_role=ROLE_SAT1)
        fut = await self.mgr.send_command("SAT1", frame, self.ingress)
        self.mgr.on_ack("SAT1", acked_seq=3, status=ACK_OK, msg_type=0x02)
        await asyncio.wait_for(fut, timeout=1.0)
        # Second ACK — should not raise
        self.mgr.on_ack("SAT1", acked_seq=3, status=ACK_OK, msg_type=0x02)

    async def test_send_error_resolves_future(self):
        """If ingress.send fails, future should resolve immediately."""
        bad_ingress = _make_ingress(send_ok=False)
        frame = build_ctrl(seq=4, speed=0, angle=0, switches=0,
                           buttons=0, start=0, target_role=ROLE_SAT1)
        fut = await self.mgr.send_command("SAT1", frame, bad_ingress)
        result = await asyncio.wait_for(fut, timeout=1.0)
        self.assertEqual(result["status"], "send_error")

    async def test_ack_wrong_sat_id_not_resolved(self):
        """ACK from a different satellite should not resolve the pending future."""
        frame = build_ctrl(seq=5, speed=0, angle=0, switches=0,
                           buttons=0, start=0, target_role=ROLE_SAT1)
        fut = await self.mgr.send_command("SAT1", frame, self.ingress)
        # ACK from SAT2 — different sat_id
        self.mgr.on_ack("SAT2", acked_seq=5, status=ACK_OK, msg_type=0x02)
        # Future should still be pending
        self.assertFalse(fut.done())
        # Clean up: send correct ACK
        self.mgr.on_ack("SAT1", acked_seq=5, status=ACK_OK, msg_type=0x02)
        await asyncio.wait_for(fut, timeout=1.0)


class TestAckManagerTimeout(unittest.IsolatedAsyncioTestCase):

    async def test_timeout_after_retries(self):
        """With max_retries=1 and short timeout, command should fail with timeout."""
        diag    = Diagnostics()
        ingress = _make_ingress()
        mgr     = AckManager(timeout_ms=50, max_retries=1, diag=diag)
        mgr.start(ingress)

        frame = build_ctrl(seq=10, speed=0, angle=0, switches=0,
                           buttons=0, start=0, target_role=ROLE_SAT1)
        try:
            fut = await mgr.send_command("SAT1", frame, ingress)
            result = await asyncio.wait_for(fut, timeout=2.0)
            self.assertEqual(result["status"], "timeout")
            self.assertEqual(result["retries"], 1)
            # Verify diagnostic counters
            self.assertEqual(diag._counters["SAT1"].tx_command_failures, 1)
        finally:
            mgr.stop()

    async def test_retry_sends_frame_again(self):
        """With timeout and retries, ingress.send should be called multiple times."""
        diag    = Diagnostics()
        ingress = _make_ingress()
        mgr     = AckManager(timeout_ms=50, max_retries=2, diag=diag)
        mgr.start(ingress)

        frame = build_ctrl(seq=11, speed=0, angle=0, switches=0,
                           buttons=0, start=0, target_role=ROLE_SAT1)
        try:
            fut = await mgr.send_command("SAT1", frame, ingress)
            result = await asyncio.wait_for(fut, timeout=3.0)
            self.assertEqual(result["status"], "timeout")
            # Should have been sent 1 (initial) + 2 (retries) = 3 times
            self.assertEqual(ingress.send.await_count, 3)
        finally:
            mgr.stop()


if __name__ == "__main__":
    unittest.main(verbosity=2)
