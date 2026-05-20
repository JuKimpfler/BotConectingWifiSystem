from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import Any

FRAME_MAGIC = 0xBE
FRAME_MAX_PAYLOAD = 180
FRAME_HEADER_SIZE = 8

ROLE_HUB = 0x00
ROLE_SAT1 = 0x01
ROLE_SAT2 = 0x02
ROLE_BROADCAST = 0xFF

MSG_DBG = 0x01
MSG_CTRL = 0x02
MSG_MODE = 0x03
MSG_CAL = 0x04
MSG_HEARTBEAT = 0x06
MSG_ACK = 0x07
MSG_DISCOVERY = 0x0A
MSG_UART_RAW = 0x0B
MSG_TELEM_DICT = 0x0C
MSG_TELEM_BATCH = 0x0D

FLAG_ACK_REQ = 0x01
FLAG_IS_RESPONSE = 0x02
FLAG_PRIORITY = 0x04

CAL_IR_MAX = 0x01
CAL_IR_MIN = 0x02
CAL_LINE_MAX = 0x03
CAL_LINE_MIN = 0x04
CAL_BNO = 0x05

ROLE_NAMES = {
    ROLE_HUB: "HUB",
    ROLE_SAT1: "SAT1",
    ROLE_SAT2: "SAT2",
    ROLE_BROADCAST: "ALL",
}


class ProtocolError(ValueError):
    pass


@dataclass(slots=True)
class Frame:
    msg_type: int
    seq: int
    src_role: int
    dst_role: int
    flags: int = 0
    network_id: int = 0x01
    payload: bytes = b""

    def pack(self) -> bytes:
        if len(self.payload) > FRAME_MAX_PAYLOAD:
            raise ProtocolError(f"payload too large: {len(self.payload)}")
        header = struct.pack(
            "<BBBBBBBB",
            FRAME_MAGIC,
            self.msg_type,
            self.seq & 0xFF,
            self.src_role & 0xFF,
            self.dst_role & 0xFF,
            self.flags & 0xFF,
            len(self.payload) & 0xFF,
            self.network_id & 0xFF,
        )
        crc = crc16_ibm(header + self.payload)
        return header + self.payload + struct.pack("<H", crc)

    @classmethod
    def unpack(cls, data: bytes) -> "Frame":
        if len(data) < FRAME_HEADER_SIZE + 2:
            raise ProtocolError("frame too short")
        magic, msg_type, seq, src_role, dst_role, flags, payload_len, network_id = struct.unpack(
            "<BBBBBBBB", data[:FRAME_HEADER_SIZE]
        )
        if magic != FRAME_MAGIC:
            raise ProtocolError(f"unexpected magic: {magic:#x}")
        if payload_len > FRAME_MAX_PAYLOAD:
            raise ProtocolError(f"invalid payload length: {payload_len}")
        expected = FRAME_HEADER_SIZE + payload_len + 2
        if len(data) < expected:
            raise ProtocolError("truncated frame")
        payload = data[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + payload_len]
        received_crc = struct.unpack("<H", data[FRAME_HEADER_SIZE + payload_len : expected])[0]
        computed_crc = crc16_ibm(data[: FRAME_HEADER_SIZE + payload_len])
        if received_crc != computed_crc:
            raise ProtocolError("CRC mismatch")
        return cls(
            msg_type=msg_type,
            seq=seq,
            src_role=src_role,
            dst_role=dst_role,
            flags=flags,
            network_id=network_id,
            payload=payload,
        )


@dataclass(slots=True)
class TelemetryValue:
    name: str
    vtype: int
    value: Any
    ts_ms: int


@dataclass(slots=True)
class Heartbeat:
    uptime_ms: int
    rssi: int
    queue_len: int


@dataclass(slots=True)
class Ack:
    ack_seq: int
    status: int
    msg_type: int


def crc16_ibm(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x01:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build_ctrl_payload(speed: int, angle: int, switches: int, buttons: int, start: bool, target_role: int) -> bytes:
    return struct.pack("<hhBBBB", speed, angle, switches & 0xFF, buttons & 0xFF, 1 if start else 0, target_role & 0xFF)


def build_mode_payload(mode_id: int, target_role: int) -> bytes:
    return struct.pack("<BB", mode_id & 0xFF, target_role & 0xFF)


def build_cal_payload(cal_cmd: int, target_role: int) -> bytes:
    return struct.pack("<BB", cal_cmd & 0xFF, target_role & 0xFF)


def build_heartbeat_payload(uptime_ms: int, rssi: int = 0, queue_len: int = 0) -> bytes:
    return struct.pack("<IbB", uptime_ms & 0xFFFFFFFF, int(rssi), queue_len & 0xFF)


def parse_heartbeat(payload: bytes) -> Heartbeat:
    if len(payload) < 6:
        raise ProtocolError("heartbeat payload too short")
    uptime_ms, rssi, queue_len = struct.unpack("<IbB", payload[:6])
    return Heartbeat(uptime_ms=uptime_ms, rssi=rssi, queue_len=queue_len)


def parse_telemetry_entry(payload: bytes) -> TelemetryValue:
    if len(payload) < 29:
        raise ProtocolError("telemetry payload too short")
    name_bytes = payload[:16]
    name = name_bytes.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
    vtype = payload[16]
    raw = payload[17:25]
    ts_ms = struct.unpack("<I", payload[25:29])[0]
    if vtype == 0:
        value = struct.unpack("<i", raw[:4])[0]
    elif vtype == 1:
        value = round(struct.unpack("<f", raw[:4])[0], 5)
    elif vtype == 2:
        value = bool(raw[0])
    else:
        value = raw.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
    return TelemetryValue(name=name, vtype=vtype, value=value, ts_ms=ts_ms)


def parse_ack(payload: bytes) -> Ack:
    if len(payload) < 3:
        raise ProtocolError("ack payload too short")
    ack_seq, status, msg_type = struct.unpack("<BBB", payload[:3])
    return Ack(ack_seq=ack_seq, status=status, msg_type=msg_type)


def role_name(role: int) -> str:
    return ROLE_NAMES.get(role, f"ROLE_{role}")
