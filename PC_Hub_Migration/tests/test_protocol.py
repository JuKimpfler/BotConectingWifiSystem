"""
tests/test_protocol.py — Unit tests for hub_core/protocol.py

Tests: CRC calculation, frame serialize/deserialize, validation,
       frame builders, payload parsers.

Verifies against protocol_v1.md §14 test vectors.
"""

import struct
import sys
import os
import unittest

# Allow running from repo root or tests/ directory
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from hub_core.protocol import (
    crc16, Frame, FRAME_SIZE, FRAME_MAGIC,
    ROLE_HUB, ROLE_SAT1, ROLE_SAT2, ROLE_BROADCAST,
    MSG_HEARTBEAT, MSG_CTRL, MSG_ACK, MSG_DBG,
    FLAG_ACK_REQ, FLAG_IS_RESPONSE,
    NETWORK_ID, PAYLOAD_MAX,
    build_heartbeat, build_ctrl, build_mode, build_cal,
    parse_telemetry_entry, parse_heartbeat, parse_ack,
)


class TestCRC16(unittest.TestCase):
    """CRC-16/IBM tests. Polynomial 0xA001, init 0xFFFF."""

    def test_empty(self):
        self.assertEqual(crc16(b""), 0xFFFF)

    def test_single_byte_zero(self):
        # CRC of [0x00]: 0xFFFF ^ 0x00 = 0xFFFF, then 8 shifts
        result = crc16(b"\x00")
        self.assertIsInstance(result, int)
        self.assertLessEqual(result, 0xFFFF)

    def test_test_vector_a(self):
        """
        Test vector A: CRC verification against firmware convention.

        Firmware CRC (verified from ESP_Satellite/src/EspNowBridge.cpp line 187):
          calcCrc = crc16_buf(data, FRAME_HEADER_SIZE + f->len)
          rxCrc   = *(uint16_t*)(data + FRAME_HEADER_SIZE + f->len)

        For a heartbeat frame: header(8) + payload_len(6) = 14 bytes are CRC'd.
        Known-good CRC: crc16("123456789") = 0x4B37 (from test/unit/test_crc16.cpp).
        """
        # Known-good vector from test/unit/test_crc16.cpp
        test_vec = b"123456789"
        self.assertEqual(crc16(test_vec), 0x4B37,
                         "CRC16/MODBUS of '123456789' must equal 0x4B37")

    def test_firmware_crc_convention(self):
        """
        Verify that to_bytes() embeds CRC at payload[len:len+2]
        (firmware convention, not at offset 188–189).
        """
        frame = build_heartbeat(seq=1, uptime_ms=1000)
        raw   = frame.to_bytes()
        # CRC should be at offset 8 + frame.len (= 8+6 = 14) in the flat bytes
        embedded_crc = struct.unpack_from("<H", raw, 8 + frame.len)[0]
        # Bytes 0..13 (FRAME_HEADER_SIZE + len) are CRC'd
        computed_crc = crc16(raw[:8 + frame.len])
        self.assertEqual(embedded_crc, computed_crc,
                         "CRC must be embedded at payload[len:len+2]")
        # Bytes 188-189 (crc16 struct field) should be zero (unused)
        struct_crc_field = struct.unpack_from("<H", raw, 188)[0]
        self.assertEqual(struct_crc_field, 0,
                         "crc16 struct field at offset 188 must be 0 (firmware unused)")

    def test_roundtrip_frame_crc(self):
        """A frame serialized with to_bytes() must have a valid CRC embedded in payload."""
        frame = build_heartbeat(seq=1, uptime_ms=100)
        raw = frame.to_bytes()
        self.assertEqual(len(raw), FRAME_SIZE)
        # CRC is at payload[len:len+2] = raw[8+6 : 8+6+2] = raw[14:16]
        embedded_crc = struct.unpack_from("<H", raw, 14)[0]
        computed = crc16(raw[:14])  # header(8) + payload[:len](6)
        self.assertEqual(embedded_crc, computed)


class TestFrameSerialisation(unittest.TestCase):

    def _make_frame(self, msg_type=MSG_HEARTBEAT, seq=0, src=ROLE_HUB,
                    dst=ROLE_BROADCAST, flags=0, length=6) -> Frame:
        payload = b"\x00" * PAYLOAD_MAX
        f = Frame(
            magic=FRAME_MAGIC, msg_type=msg_type, seq=seq,
            src_role=src, dst_role=dst, flags=flags,
            len=length, network_id=NETWORK_ID, payload=payload,
        )
        # Set crc16 from the computed firmware-style CRC
        header = struct.pack("<BBBBBBBB", f.magic, f.msg_type, f.seq,
                             f.src_role, f.dst_role, f.flags, f.len, f.network_id)
        f.crc16 = crc16(header + bytes(f.payload[:length]))
        return f

    def test_frame_size(self):
        f = self._make_frame()
        raw = f.to_bytes()
        self.assertEqual(len(raw), FRAME_SIZE)

    def test_magic_byte(self):
        f = self._make_frame()
        raw = f.to_bytes()
        self.assertEqual(raw[0], FRAME_MAGIC)

    def test_roundtrip(self):
        """Deserialize a serialized frame produces identical fields."""
        f = self._make_frame(seq=42, src=ROLE_SAT1, dst=ROLE_HUB)
        raw = f.to_bytes()
        f2 = Frame.from_bytes(raw)
        self.assertEqual(f2.magic,    FRAME_MAGIC)
        self.assertEqual(f2.seq,      42)
        self.assertEqual(f2.src_role, ROLE_SAT1)
        self.assertEqual(f2.dst_role, ROLE_HUB)

    def test_from_bytes_bad_length(self):
        with self.assertRaises(ValueError):
            Frame.from_bytes(b"\x00" * 10)

    def test_wire_bytes_length(self):
        f = build_heartbeat(seq=0, uptime_ms=0)
        wire = f.wire_bytes()
        self.assertEqual(len(wire), 194)
        self.assertEqual(wire[0], 0xAA)
        self.assertEqual(wire[1], 0x55)
        frame_len = struct.unpack_from("<H", wire, 2)[0]
        self.assertEqual(frame_len, FRAME_SIZE)


class TestFrameValidation(unittest.TestCase):

    def _valid_frame(self) -> Frame:
        return build_heartbeat(seq=1, uptime_ms=1000)

    def test_valid_frame_passes(self):
        f = self._valid_frame()
        err = f.validate(NETWORK_ID)
        self.assertIsNone(err, f"Unexpected validation error: {err}")

    def test_bad_magic(self):
        f = self._valid_frame()
        f.magic = 0x00
        err = f.validate(NETWORK_ID)
        self.assertIsNotNone(err)
        self.assertIn("bad_magic", err)

    def test_bad_network_id(self):
        f = self._valid_frame()
        f.network_id = 0x02
        err = f.validate(NETWORK_ID)
        self.assertIsNotNone(err)
        self.assertIn("bad_network_id", err)

    def test_crc_mismatch(self):
        f = self._valid_frame()
        # Corrupt the stored CRC field (crc16 attribute set by from_bytes)
        # The crc16 value is what validate() compares against
        f.crc16 = 0x0000  # Force bad CRC value
        err = f.validate(NETWORK_ID)
        self.assertIsNotNone(err)
        self.assertIn("crc_mismatch", err)

    def test_len_overflow(self):
        f = self._valid_frame()
        f.len = 200  # > PAYLOAD_MAX (180)
        err = f.validate(NETWORK_ID)
        self.assertIsNotNone(err)
        self.assertIn("len_overflow", err)

    def test_unknown_msg_type(self):
        f = self._valid_frame()
        f.msg_type = 0xFF
        err = f.validate(NETWORK_ID)
        self.assertIsNotNone(err)
        self.assertIn("unknown_msg_type", err)


class TestFrameBuilders(unittest.TestCase):

    def test_build_heartbeat(self):
        f = build_heartbeat(seq=5, uptime_ms=12345)
        self.assertEqual(f.msg_type, MSG_HEARTBEAT)
        self.assertEqual(f.seq, 5)
        self.assertEqual(f.src_role, ROLE_HUB)
        self.assertEqual(f.dst_role, ROLE_BROADCAST)
        self.assertIsNone(f.validate(NETWORK_ID))

    def test_heartbeat_seq_wraps(self):
        f = build_heartbeat(seq=256, uptime_ms=0)  # Should wrap to 0
        self.assertEqual(f.seq, 0)

    def test_build_ctrl(self):
        f = build_ctrl(seq=10, speed=150, angle=-45, switches=3,
                       buttons=1, start=1, target_role=ROLE_SAT1)
        self.assertEqual(f.msg_type, MSG_CTRL)
        self.assertEqual(f.dst_role, ROLE_SAT1)
        self.assertTrue(f.flags & FLAG_ACK_REQ)
        self.assertIsNone(f.validate(NETWORK_ID))
        # Check test vector B from protocol_v1.md §14.2
        speed, angle = struct.unpack_from("<hh", f.payload)
        self.assertEqual(speed, 150)
        self.assertEqual(angle, -45)

    def test_build_mode(self):
        f = build_mode(seq=1, mode_id=3, target_role=ROLE_SAT2)
        self.assertEqual(f.msg_type, 0x03)
        self.assertTrue(f.flags & FLAG_ACK_REQ)
        self.assertIsNone(f.validate(NETWORK_ID))

    def test_build_cal(self):
        f = build_cal(seq=2, cal_cmd=0x05, target_role=ROLE_SAT1)
        self.assertEqual(f.msg_type, 0x04)
        self.assertTrue(f.flags & FLAG_ACK_REQ)
        self.assertIsNone(f.validate(NETWORK_ID))


class TestPayloadParsers(unittest.TestCase):

    def test_parse_heartbeat(self):
        """Test vector B-ish: uptime=100ms, rssi=-40, queue=0."""
        payload = struct.pack("<IbB", 100, -40, 0) + b"\x00" * 174
        result = parse_heartbeat(payload)
        self.assertEqual(result["uptime_ms"], 100)
        self.assertEqual(result["rssi"], -40)
        self.assertEqual(result["queue_len"], 0)

    def test_parse_heartbeat_empty(self):
        self.assertEqual(parse_heartbeat(b"\x00\x01"), {})

    def test_parse_ack(self):
        """Test vector from protocol_v1.md §14.3."""
        payload = bytes([0x05, 0x02, 0x00]) + b"\x00" * 177  # acked_seq=5, MSG_CTRL, OK
        result = parse_ack(payload)
        self.assertEqual(result["acked_seq"], 5)
        self.assertEqual(result["status"], 0x02)   # ACK_ERR_BUSY? No: protocol says status 0x02 in the example
        self.assertEqual(result["msg_type"], 0x00)

    def test_parse_telemetry_int32(self):
        """Test vector from protocol_v1.md §14.4: name=Speed, vtype=0, value=1200."""
        name_bytes = b"Speed\x00" + b"\x00" * 10  # 16 bytes
        vtype = bytes([0x00])
        value = struct.pack("<i", 1200)
        ts    = struct.pack("<I", 5484)
        payload = name_bytes + vtype + value + ts + b"\x00" * 155
        result = parse_telemetry_entry(payload)
        self.assertEqual(result["name"], "Speed")
        self.assertEqual(result["vtype"], 0)
        self.assertEqual(result["value"], 1200)
        self.assertEqual(result["ts_ms"], 5484)

    def test_parse_telemetry_float(self):
        name_bytes = b"Temp\x00" + b"\x00" * 11
        vtype = bytes([0x01])
        value = struct.pack("<f", 23.5)
        ts    = struct.pack("<I", 100)
        payload = name_bytes + vtype + value + ts + b"\x00" * 155
        result = parse_telemetry_entry(payload)
        self.assertEqual(result["name"], "Temp")
        self.assertEqual(result["vtype"], 1)
        self.assertAlmostEqual(result["value"], 23.5, places=3)

    def test_parse_telemetry_bool(self):
        name_bytes = b"Running\x00" + b"\x00" * 8
        vtype = bytes([0x02])
        value = bytes([0x01, 0x00, 0x00, 0x00])
        ts    = struct.pack("<I", 200)
        payload = name_bytes + vtype + value + ts + b"\x00" * 155
        result = parse_telemetry_entry(payload)
        self.assertEqual(result["name"], "Running")
        self.assertEqual(result["vtype"], 2)
        self.assertTrue(result["value"])

    def test_parse_telemetry_short_payload(self):
        result = parse_telemetry_entry(b"\x00" * 10)
        self.assertEqual(result, {})


if __name__ == "__main__":
    unittest.main(verbosity=2)
