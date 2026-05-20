import unittest

from PC_Hub_Migration.hub_core.protocol import (
    MSG_CTRL,
    ROLE_HUB,
    ROLE_SAT1,
    Frame,
    build_ctrl_payload,
    crc16_ibm,
    parse_ack,
    parse_telemetry_entry,
)


class ProtocolTests(unittest.TestCase):
    def test_crc16_matches_known_vector(self):
        self.assertEqual(crc16_ibm(bytes.fromhex('BE010000FF000001')), 0xE72B)

    def test_frame_roundtrip(self):
        frame = Frame(
            msg_type=MSG_CTRL,
            seq=7,
            src_role=ROLE_HUB,
            dst_role=ROLE_SAT1,
            payload=build_ctrl_payload(120, -45, 1, 2, True, ROLE_SAT1),
        )
        unpacked = Frame.unpack(frame.pack())
        self.assertEqual(unpacked.msg_type, MSG_CTRL)
        self.assertEqual(unpacked.seq, 7)
        self.assertEqual(unpacked.dst_role, ROLE_SAT1)
        self.assertEqual(unpacked.payload, frame.payload)

    def test_parse_bool_telemetry(self):
        payload = b'P2PActive\x00\x00\x00\x00\x00\x00\x00' + bytes([2]) + bytes([1]) + b'\x00' * 7 + (1234).to_bytes(4, 'little')
        parsed = parse_telemetry_entry(payload)
        self.assertEqual(parsed.name, 'P2PActive')
        self.assertTrue(parsed.value)
        self.assertEqual(parsed.ts_ms, 1234)

    def test_parse_ack(self):
        ack = parse_ack(bytes([7, 1, 2]))
        self.assertEqual(ack.ack_seq, 7)
        self.assertEqual(ack.status, 1)
        self.assertEqual(ack.msg_type, 2)


if __name__ == '__main__':
    unittest.main()
