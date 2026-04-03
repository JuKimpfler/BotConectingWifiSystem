"""
hub_core/ingress.py — Async serial bridge reader

Reads 194-byte framed packets from the USB-ESP32 bridge:
  [0xAA] [0x55] [len_LE 2 bytes] [Frame_t 190 bytes]

Features:
- Byte-by-byte SOF sync recovery (re-syncs after any gap/corruption)
- CRC validation with per-satellite error counting
- Pushes validated Frame objects into an asyncio.Queue
- Handles serial port disconnect/reconnect
"""

import asyncio
import logging
import struct
import time
from typing import Optional

import serial
import serial.tools.list_ports

from .protocol import (
    Frame, FRAME_SIZE, SOF_BYTE1, SOF_BYTE2, WIRE_HEADER,
    ROLE_NAME, MSG_NAMES, NETWORK_ID,
)
from .diagnostics import Diagnostics

logger = logging.getLogger(__name__)

# SOF sync state machine states
_S_WAIT_SOF1 = 0
_S_WAIT_SOF2 = 1
_S_READ_LEN  = 2
_S_READ_FRAME = 3


def _find_bridge_port() -> Optional[str]:
    """Scan COM ports for a likely USB CDC bridge device (ESP32)."""
    candidates = []
    for port in serial.tools.list_ports.comports():
        desc = (port.description or "").lower()
        mfr  = (port.manufacturer or "").lower()
        if any(kw in desc or kw in mfr for kw in
               ("cp210", "ch340", "ftdi", "usb serial", "esp", "uart")):
            candidates.append(port.device)
    if candidates:
        logger.info("Auto-detected bridge candidates: %s — using first: %s",
                    candidates, candidates[0])
        return candidates[0]
    # Fallback: first available COM port
    all_ports = list(serial.tools.list_ports.comports())
    if all_ports:
        logger.warning("No known ESP COM port found; trying first available: %s",
                       all_ports[0].device)
        return all_ports[0].device
    return None


class BridgeIngress:
    """
    Async serial reader for the USB-ESP32 bridge.

    Usage:
        ingress = BridgeIngress(com_port, baud_rate, frame_queue, diag, network_id)
        await ingress.run()   # runs forever; call ingress.stop() to cancel
    """

    RECONNECT_DELAY_S = 3.0

    def __init__(
        self,
        com_port: str,
        baud_rate: int,
        frame_queue: asyncio.Queue,
        diag: Diagnostics,
        network_id: int = NETWORK_ID,
    ) -> None:
        self._com_port   = com_port
        self._baud_rate  = baud_rate
        self._queue      = frame_queue
        self._diag       = diag
        self._network_id = network_id
        self._running    = False
        self._connected  = False
        self._serial: Optional[serial.Serial] = None

    @property
    def connected(self) -> bool:
        return self._connected

    def stop(self) -> None:
        self._running = False

    async def run(self) -> None:
        """Main ingress loop — reconnects on serial errors."""
        self._running = True
        while self._running:
            port = self._com_port
            if port == "auto":
                port = _find_bridge_port()
                if port is None:
                    logger.warning("No USB bridge found; retrying in %ss",
                                   self.RECONNECT_DELAY_S)
                    await asyncio.sleep(self.RECONNECT_DELAY_S)
                    continue

            try:
                logger.info("Opening serial port %s @ %d baud", port, self._baud_rate)
                self._serial = serial.Serial(
                    port=port,
                    baudrate=self._baud_rate,
                    timeout=0,           # non-blocking
                    write_timeout=0.5,
                )
                self._connected = True
                logger.info("Bridge connected: %s", port)
                await self._read_loop()
            except serial.SerialException as exc:
                logger.warning("Serial error on %s: %s — reconnecting in %ss",
                               port, exc, self.RECONNECT_DELAY_S)
            except Exception as exc:  # noqa: BLE001
                logger.error("Unexpected ingress error: %s", exc, exc_info=True)
            finally:
                self._connected = False
                if self._serial and self._serial.is_open:
                    try:
                        self._serial.close()
                    except Exception:  # noqa: BLE001
                        pass
                self._serial = None

            if self._running:
                await asyncio.sleep(self.RECONNECT_DELAY_S)

    async def _read_loop(self) -> None:
        """
        SOF-sync state machine. Reads bytes non-blockingly with asyncio.sleep
        to yield the event loop.
        """
        state       = _S_WAIT_SOF1
        len_buf     = bytearray()
        frame_buf   = bytearray()
        expected_len = 0

        while self._running:
            # Non-blocking read
            try:
                waiting = self._serial.in_waiting
            except Exception as exc:  # noqa: BLE001
                raise serial.SerialException(f"Port read error: {exc}") from exc

            if waiting == 0:
                await asyncio.sleep(0.0005)  # 0.5 ms poll interval
                continue

            chunk = self._serial.read(min(waiting, 256))
            if not chunk:
                await asyncio.sleep(0.001)
                continue

            for byte in chunk:
                if state == _S_WAIT_SOF1:
                    if byte == SOF_BYTE1:
                        state = _S_WAIT_SOF2
                elif state == _S_WAIT_SOF2:
                    if byte == SOF_BYTE2:
                        state    = _S_READ_LEN
                        len_buf  = bytearray()
                    else:
                        state = _S_WAIT_SOF1
                        if byte == SOF_BYTE1:
                            state = _S_WAIT_SOF2
                elif state == _S_READ_LEN:
                    len_buf.append(byte)
                    if len(len_buf) == 2:
                        expected_len = struct.unpack_from("<H", len_buf)[0]
                        if expected_len != FRAME_SIZE:
                            logger.debug("Unexpected frame length %d in SOF header; discarding",
                                         expected_len)
                            self._diag.rx_parse_error()
                            state = _S_WAIT_SOF1
                        else:
                            frame_buf = bytearray()
                            state     = _S_READ_FRAME
                elif state == _S_READ_FRAME:
                    frame_buf.append(byte)
                    if len(frame_buf) == expected_len:
                        self._process_frame(bytes(frame_buf))
                        state = _S_WAIT_SOF1

    def _process_frame(self, raw: bytes) -> None:
        """Validate and enqueue a raw 190-byte frame."""
        if len(raw) != FRAME_SIZE:
            self._diag.rx_parse_error()
            return

        try:
            frame = Frame.from_bytes(raw)
        except (ValueError, struct.error) as exc:
            logger.debug("Frame parse error: %s", exc)
            self._diag.rx_parse_error()
            return

        sat_id = ROLE_NAME.get(frame.src_role, f"ROLE_{frame.src_role:02X}")

        # Validate frame fields
        err = frame.validate(self._network_id)
        if err is not None:
            # Increment the specific error counter
            if "bad_magic" in err:
                self._diag.rx_magic_error(sat_id)
            elif "bad_network_id" in err:
                self._diag.rx_network_id_error(sat_id)
            elif "crc_mismatch" in err:
                self._diag.rx_crc_error(sat_id)
            elif "len_overflow" in err:
                self._diag.rx_len_overflow(sat_id)
            elif "unknown_msg_type" in err:
                self._diag.rx_unknown_type(sat_id)
            elif "invalid_src_role" in err or "invalid_dst_role" in err:
                self._diag.rx_parse_error()
            else:
                self._diag.rx_parse_error()
            logger.debug("Frame validation failed [%s]: %s", sat_id, err)
            return

        frame.rx_ts = time.time()
        self._diag.rx_frame_ok(sat_id)

        logger.debug("RX %s seq=%d src=%s dst=%s flags=0x%02X len=%d",
                     frame.type_name, frame.seq, frame.src_name, frame.dst_name,
                     frame.flags, frame.len)

        try:
            self._queue.put_nowait(frame)
        except asyncio.QueueFull:
            logger.warning("Ingress queue full — dropping frame type=%s seq=%d",
                           frame.type_name, frame.seq)

    async def send(self, frame: Frame) -> bool:
        """
        Write a frame to the serial port.
        Returns True on success, False on error.
        """
        if not self._connected or self._serial is None or not self._serial.is_open:
            logger.warning("Cannot send: bridge not connected")
            return False
        try:
            wire = frame.wire_bytes()
            self._serial.write(wire)
            sat_id = ROLE_NAME.get(frame.dst_role, f"ROLE_{frame.dst_role:02X}")
            self._diag.tx_frame(sat_id)
            logger.debug("TX %s seq=%d dst=%s len=%d",
                         frame.type_name, frame.seq, frame.dst_name, frame.len)
            return True
        except serial.SerialException as exc:
            logger.error("Serial write error: %s", exc)
            self._connected = False
            return False
