"""
hub_core/protocol.py — BCWS Protocol constants, frame dataclass, CRC-16/IBM

Ported from shared/messages.h and shared/crc16.h.
DO NOT modify frame layout or CRC algorithm — frozen as protocol_v1.
"""

import struct
from dataclasses import dataclass, field
from typing import Optional

# ── Protocol version ─────────────────────────────────────────
PROTO_VERSION = 0x01

# ── Frame layout constants ────────────────────────────────────
FRAME_MAGIC       = 0xBE
FRAME_SIZE        = 190
FRAME_HEADER_SIZE = 8
PAYLOAD_MAX       = 180

# SOF framing on USB serial link
SOF_BYTE1   = 0xAA
SOF_BYTE2   = 0x55
WIRE_HEADER = 4     # SOF(2) + len_LE(2)
WIRE_SIZE   = WIRE_HEADER + FRAME_SIZE  # 194

# ── Roles ─────────────────────────────────────────────────────
ROLE_HUB       = 0x00
ROLE_SAT1      = 0x01
ROLE_SAT2      = 0x02
ROLE_BROADCAST = 0xFF

ROLE_NAME = {
    ROLE_HUB:       "HUB",
    ROLE_SAT1:      "SAT1",
    ROLE_SAT2:      "SAT2",
    ROLE_BROADCAST: "BROADCAST",
}

# ── Message types ─────────────────────────────────────────────
MSG_DBG         = 0x01
MSG_CTRL        = 0x02
MSG_MODE        = 0x03
MSG_CAL         = 0x04
MSG_PAIR        = 0x05
MSG_HEARTBEAT   = 0x06
MSG_ACK         = 0x07
MSG_ERROR       = 0x08
MSG_SETTINGS    = 0x09
MSG_DISCOVERY   = 0x0A
MSG_UART_RAW    = 0x0B
MSG_TELEM_DICT  = 0x0C
MSG_TELEM_BATCH = 0x0D

MSG_NAMES = {
    MSG_DBG:         "DBG",
    MSG_CTRL:        "CTRL",
    MSG_MODE:        "MODE",
    MSG_CAL:         "CAL",
    MSG_PAIR:        "PAIR",
    MSG_HEARTBEAT:   "HEARTBEAT",
    MSG_ACK:         "ACK",
    MSG_ERROR:       "ERROR",
    MSG_SETTINGS:    "SETTINGS",
    MSG_DISCOVERY:   "DISCOVERY",
    MSG_UART_RAW:    "UART_RAW",
    MSG_TELEM_DICT:  "TELEM_DICT",
    MSG_TELEM_BATCH: "TELEM_BATCH",
}

KNOWN_MSG_TYPES = set(MSG_NAMES.keys())
KNOWN_ROLES     = {ROLE_HUB, ROLE_SAT1, ROLE_SAT2, ROLE_BROADCAST}

# ── Flags ─────────────────────────────────────────────────────
FLAG_ACK_REQ     = 0x01
FLAG_IS_RESPONSE = 0x02
FLAG_PRIORITY    = 0x04
FLAG_ENCRYPTED   = 0x08

# ── ACK status ────────────────────────────────────────────────
ACK_OK          = 0x00
ACK_ERR_UNKNOWN = 0x01
ACK_ERR_BUSY    = 0x02
ACK_ERR_TIMEOUT = 0x03
ACK_ERR_INVALID = 0x04

ACK_STATUS_NAME = {
    ACK_OK:          "ok",
    ACK_ERR_UNKNOWN: "rejected",
    ACK_ERR_BUSY:    "busy",
    ACK_ERR_TIMEOUT: "timeout",
    ACK_ERR_INVALID: "invalid",
}

# ── Network ID ────────────────────────────────────────────────
NETWORK_ID = 0x01

# ── Timing ────────────────────────────────────────────────────
ACK_TIMEOUT_MS        = 500
ACK_MAX_RETRIES       = 3
HEARTBEAT_INTERVAL_MS = 1000
HEARTBEAT_TIMEOUT_MS  = 4000

# ── Cal commands ──────────────────────────────────────────────
CAL_IR_MAX   = 0x01
CAL_IR_MIN   = 0x02
CAL_LINE_MAX = 0x03
CAL_LINE_MIN = 0x04
CAL_BNO      = 0x05


# ── CRC-16/IBM (polynomial 0xA001, init 0xFFFF) ──────────────
def crc16(data: bytes) -> int:
    """CRC-16/IBM — matches shared/crc16.h crc16_buf()."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


# ── Frame dataclass ───────────────────────────────────────────
@dataclass
class Frame:
    """Python representation of Frame_t from shared/messages.h."""
    magic:      int
    msg_type:   int
    seq:        int
    src_role:   int
    dst_role:   int
    flags:      int
    len:        int
    network_id: int
    payload:    bytes
    crc16:      int = 0
    # PC-side metadata (not part of wire format)
    rx_ts:      float = 0.0   # wall-clock time received

    @staticmethod
    def from_bytes(raw: bytes) -> "Frame":
        """
        Deserialize 190-byte raw Frame_t.

        CRC is embedded at payload[len:len+2] (firmware convention), not at offset 188-189.
        The crc16 field on the dataclass is set to the value read from that location.
        Raises ValueError on bad input.
        """
        if len(raw) != FRAME_SIZE:
            raise ValueError(f"Expected {FRAME_SIZE} bytes, got {len(raw)}")
        hdr = struct.unpack_from("<BBBBBBBB", raw, 0)
        payload = raw[FRAME_HEADER_SIZE:FRAME_HEADER_SIZE + PAYLOAD_MAX]
        frame_len = hdr[6]
        # Read embedded CRC from payload[frame_len:frame_len+2]
        if frame_len + 2 <= PAYLOAD_MAX:
            crc_val = struct.unpack_from("<H", payload, frame_len)[0]
        else:
            crc_val = 0
        return Frame(
            magic      = hdr[0],
            msg_type   = hdr[1],
            seq        = hdr[2],
            src_role   = hdr[3],
            dst_role   = hdr[4],
            flags      = hdr[5],
            len        = frame_len,
            network_id = hdr[7],
            payload    = bytes(payload),
            crc16      = crc_val,
        )

    def to_bytes(self) -> bytes:
        """
        Serialize to 190-byte wire format matching actual firmware behaviour.

        CRC behaviour (verified against ESP_Satellite/src/EspNowBridge.cpp line 187):
          - CRC computed over bytes [0 .. 8+len-1]  (header + real payload only)
          - CRC stored at payload[len] and payload[len+1]  (inside the payload array)
          - The Frame_t struct's crc16 field at offset 188-189 is left as zero

        This matches the receive-side check:
          calcCrc = crc16_buf(data, FRAME_HEADER_SIZE + f->len)
          rxCrc   read from data[FRAME_HEADER_SIZE + f->len]
        """
        header = struct.pack("<BBBBBBBB", self.magic, self.msg_type, self.seq,
                             self.src_role, self.dst_role, self.flags,
                             self.len, self.network_id)
        # Start with full payload area initialised to zero
        payload_arr = bytearray(PAYLOAD_MAX)
        # Copy actual payload data
        actual = self.payload[:self.len] if self.len <= PAYLOAD_MAX else self.payload[:PAYLOAD_MAX]
        payload_arr[:len(actual)] = actual
        # Compute CRC over header + real payload bytes
        crc_input = bytes(header) + bytes(payload_arr[:self.len])
        computed_crc = crc16(crc_input)
        # Embed CRC at payload[len] and payload[len+1]
        if self.len + 2 <= PAYLOAD_MAX:
            struct.pack_into("<H", payload_arr, self.len, computed_crc)
        # crc16 struct field (offset 188-189) stays zero (unused by firmware)
        return bytes(header) + bytes(payload_arr) + b"\x00\x00"

    def wire_bytes(self) -> bytes:
        """Full 194-byte wire frame: SOF + len_LE + Frame_t."""
        frame_raw = self.to_bytes()
        header = struct.pack("<BBH", SOF_BYTE1, SOF_BYTE2, FRAME_SIZE)
        return header + frame_raw

    def validate(self, expected_network_id: int = NETWORK_ID) -> Optional[str]:
        """
        Return error string if frame fails validation, None if OK.

        CRC is validated using the firmware's convention:
          CRC covers bytes [0 .. 8+len-1]; stored at payload[len:len+2].
        The stored CRC is read from self.crc16 (set by from_bytes()).
        """
        if self.magic != FRAME_MAGIC:
            return f"bad_magic 0x{self.magic:02X}"
        if self.network_id != expected_network_id:
            return f"bad_network_id 0x{self.network_id:02X}"
        if self.len > PAYLOAD_MAX:
            return f"len_overflow {self.len}"
        if self.msg_type not in KNOWN_MSG_TYPES:
            return f"unknown_msg_type 0x{self.msg_type:02X}"
        if self.src_role not in KNOWN_ROLES:
            return f"invalid_src_role 0x{self.src_role:02X}"
        if self.dst_role not in KNOWN_ROLES:
            return f"invalid_dst_role 0x{self.dst_role:02X}"
        # CRC check: compute over header(8) + real payload(len) bytes
        # Use the deserialized field values to reconstruct, same as firmware sender does
        header = struct.pack("<BBBBBBBB", self.magic, self.msg_type, self.seq,
                             self.src_role, self.dst_role, self.flags,
                             self.len, self.network_id)
        real_payload = self.payload[:self.len] if self.len <= len(self.payload) else self.payload
        computed = crc16(header + bytes(real_payload))
        # self.crc16 was populated by from_bytes() from payload[len:len+2]
        if computed != self.crc16:
            return f"crc_mismatch computed=0x{computed:04X} stored=0x{self.crc16:04X}"
        return None

    @property
    def src_name(self) -> str:
        return ROLE_NAME.get(self.src_role, f"ROLE_0x{self.src_role:02X}")

    @property
    def dst_name(self) -> str:
        return ROLE_NAME.get(self.dst_role, f"ROLE_0x{self.dst_role:02X}")

    @property
    def type_name(self) -> str:
        return MSG_NAMES.get(self.msg_type, f"0x{self.msg_type:02X}")

    @property
    def ack_required(self) -> bool:
        return bool(self.flags & FLAG_ACK_REQ)


def _compute_frame_crc(f: "Frame") -> int:
    """Compute the correct CRC for a frame (header + real payload bytes)."""
    header = struct.pack("<BBBBBBBB", f.magic, f.msg_type, f.seq,
                         f.src_role, f.dst_role, f.flags,
                         f.len, f.network_id)
    real_payload = f.payload[:f.len] if f.len <= len(f.payload) else f.payload
    return crc16(header + bytes(real_payload))


# ── Frame builder helpers ─────────────────────────────────────
def build_heartbeat(seq: int, uptime_ms: int, network_id: int = NETWORK_ID) -> Frame:
    """Build a hub heartbeat frame."""
    payload = struct.pack("<IbB", uptime_ms, 0, 0)  # uptime_ms, rssi=0, queue_len=0
    f = Frame(
        magic=FRAME_MAGIC, msg_type=MSG_HEARTBEAT, seq=seq & 0xFF,
        src_role=ROLE_HUB, dst_role=ROLE_BROADCAST,
        flags=0, len=6, network_id=network_id,
        payload=payload + b"\x00" * (PAYLOAD_MAX - len(payload)),
    )
    f.crc16 = _compute_frame_crc(f)
    return f


def build_ctrl(seq: int, speed: int, angle: int, switches: int, buttons: int,
               start: int, target_role: int, network_id: int = NETWORK_ID) -> Frame:
    """Build a MSG_CTRL frame."""
    payload = struct.pack("<hhBBBB", speed, angle, switches, buttons, start, target_role)
    f = Frame(
        magic=FRAME_MAGIC, msg_type=MSG_CTRL, seq=seq & 0xFF,
        src_role=ROLE_HUB, dst_role=target_role,
        flags=FLAG_ACK_REQ, len=len(payload), network_id=network_id,
        payload=payload + b"\x00" * (PAYLOAD_MAX - len(payload)),
    )
    f.crc16 = _compute_frame_crc(f)
    return f


def build_mode(seq: int, mode_id: int, target_role: int,
               network_id: int = NETWORK_ID) -> Frame:
    """Build a MSG_MODE frame."""
    payload = struct.pack("<BB", mode_id, target_role)
    f = Frame(
        magic=FRAME_MAGIC, msg_type=MSG_MODE, seq=seq & 0xFF,
        src_role=ROLE_HUB, dst_role=target_role,
        flags=FLAG_ACK_REQ, len=len(payload), network_id=network_id,
        payload=payload + b"\x00" * (PAYLOAD_MAX - len(payload)),
    )
    f.crc16 = _compute_frame_crc(f)
    return f


def build_cal(seq: int, cal_cmd: int, target_role: int,
              network_id: int = NETWORK_ID) -> Frame:
    """Build a MSG_CAL frame."""
    payload = struct.pack("<BB", cal_cmd, target_role)
    f = Frame(
        magic=FRAME_MAGIC, msg_type=MSG_CAL, seq=seq & 0xFF,
        src_role=ROLE_HUB, dst_role=target_role,
        flags=FLAG_ACK_REQ, len=len(payload), network_id=network_id,
        payload=payload + b"\x00" * (PAYLOAD_MAX - len(payload)),
    )
    f.crc16 = _compute_frame_crc(f)
    return f


def parse_telemetry_entry(payload: bytes) -> dict:
    """Parse MSG_DBG payload into a dict. Returns {} on parse failure."""
    if len(payload) < 25:
        return {}
    name_bytes = payload[0:16]
    name = name_bytes.split(b"\x00")[0].decode("utf-8", errors="replace")
    vtype = payload[16]
    raw_val = payload[17:21]
    ts_ms = struct.unpack_from("<I", payload, 21)[0]
    if vtype == 0:
        value = struct.unpack_from("<i", raw_val)[0]
    elif vtype == 1:
        value = struct.unpack_from("<f", raw_val)[0]
    elif vtype == 2:
        value = bool(raw_val[0])
    elif vtype == 3:
        value = raw_val.split(b"\x00")[0].decode("utf-8", errors="replace")
    else:
        value = None
    return {"name": name, "vtype": vtype, "value": value, "ts_ms": ts_ms}


def parse_heartbeat(payload: bytes) -> dict:
    """Parse MSG_HEARTBEAT payload."""
    if len(payload) < 6:
        return {}
    uptime_ms, rssi, queue_len = struct.unpack_from("<IbB", payload)
    return {"uptime_ms": uptime_ms, "rssi": rssi, "queue_len": queue_len}


def parse_ack(payload: bytes) -> dict:
    """Parse MSG_ACK payload."""
    if len(payload) < 3:
        return {}
    ack_seq, status, msg_type = struct.unpack_from("<BBB", payload)
    return {"acked_seq": ack_seq, "status": status, "msg_type": msg_type}


def parse_discovery(payload: bytes) -> dict:
    """Parse MSG_DISCOVERY payload."""
    if len(payload) < 24:
        return {}
    action = payload[0]
    role = payload[1]
    name = payload[2:18].split(b"\x00")[0].decode("utf-8", errors="replace")
    mac_bytes = payload[18:24]
    mac = ":".join(f"{b:02X}" for b in mac_bytes)
    channel = payload[24] if len(payload) > 24 else 0
    return {"action": action, "role": role, "name": name, "mac": mac, "channel": channel}


def parse_telem_batch(payload: bytes, frame_len: int) -> list:
    """Parse MSG_TELEM_BATCH payload. Returns list of compact value dicts."""
    if frame_len < 1 or len(payload) < 1:
        return []
    count = payload[0]
    entries = []
    offset = 1
    entry_size = 6  # stream_id(1) + vtype(1) + raw(4)
    for _ in range(count):
        if offset + entry_size > frame_len:
            break
        stream_id = payload[offset]
        vtype = payload[offset + 1]
        raw = struct.unpack_from("<i", payload, offset + 2)[0]
        entries.append({"stream_id": stream_id, "vtype": vtype, "raw": raw})
        offset += entry_size
    return entries


def parse_telem_dict(payload: bytes) -> dict:
    """Parse MSG_TELEM_DICT payload."""
    if len(payload) < 17:
        return {}
    stream_id = payload[0]
    name = payload[1:17].split(b"\x00")[0].decode("utf-8", errors="replace")
    return {"stream_id": stream_id, "name": name}
