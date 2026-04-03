"""
hub_core/diagnostics.py — Per-satellite error counters and hub metrics

All counters are in-memory per session. Accessible via /metrics REST endpoint.
"""

import time
from dataclasses import dataclass, field
from typing import Dict


@dataclass
class SatelliteCounters:
    """Error and traffic counters for one satellite."""
    sat_id: str = ""
    # RX error counters
    rx_crc_errors:        int = 0
    rx_magic_errors:      int = 0
    rx_network_id_errors: int = 0
    rx_unknown_type:      int = 0
    rx_len_overflow:      int = 0
    # TX reliability counters
    tx_ack_timeouts:      int = 0
    tx_ack_retries:       int = 0
    tx_command_failures:  int = 0
    # Traffic counters
    rx_frames:            int = 0
    tx_frames:            int = 0

    def to_dict(self) -> dict:
        return {
            "rx_crc_errors":        self.rx_crc_errors,
            "rx_magic_errors":      self.rx_magic_errors,
            "rx_network_id_errors": self.rx_network_id_errors,
            "rx_unknown_type":      self.rx_unknown_type,
            "rx_len_overflow":      self.rx_len_overflow,
            "tx_ack_timeouts":      self.tx_ack_timeouts,
            "tx_ack_retries":       self.tx_ack_retries,
            "tx_command_failures":  self.tx_command_failures,
            "rx_frames":            self.rx_frames,
            "tx_frames":            self.tx_frames,
        }


class Diagnostics:
    """
    Hub-wide diagnostics: per-satellite counters, global frame counters,
    and uptime tracking.
    """

    def __init__(self) -> None:
        self._start_time: float = time.monotonic()
        self._rx_frames_total: int = 0
        self._tx_frames_total: int = 0
        self._rx_parse_errors: int = 0
        self._counters: Dict[str, SatelliteCounters] = {}

    def _get_or_create(self, sat_id: str) -> SatelliteCounters:
        if sat_id not in self._counters:
            self._counters[sat_id] = SatelliteCounters(sat_id=sat_id)
        return self._counters[sat_id]

    # ── RX error recording ────────────────────────────────────

    def rx_crc_error(self, sat_id: str) -> None:
        self._get_or_create(sat_id).rx_crc_errors += 1

    def rx_magic_error(self, sat_id: str = "UNKNOWN") -> None:
        self._get_or_create(sat_id).rx_magic_errors += 1

    def rx_network_id_error(self, sat_id: str) -> None:
        self._get_or_create(sat_id).rx_network_id_errors += 1

    def rx_unknown_type(self, sat_id: str) -> None:
        self._get_or_create(sat_id).rx_unknown_type += 1

    def rx_len_overflow(self, sat_id: str) -> None:
        self._get_or_create(sat_id).rx_len_overflow += 1

    def rx_frame_ok(self, sat_id: str) -> None:
        self._get_or_create(sat_id).rx_frames += 1
        self._rx_frames_total += 1

    def rx_parse_error(self) -> None:
        self._rx_parse_errors += 1

    # ── TX error recording ────────────────────────────────────

    def tx_ack_timeout(self, sat_id: str) -> None:
        self._get_or_create(sat_id).tx_ack_timeouts += 1

    def tx_ack_retry(self, sat_id: str) -> None:
        self._get_or_create(sat_id).tx_ack_retries += 1

    def tx_command_failure(self, sat_id: str) -> None:
        self._get_or_create(sat_id).tx_command_failures += 1

    def tx_frame(self, sat_id: str) -> None:
        self._get_or_create(sat_id).tx_frames += 1
        self._tx_frames_total += 1

    # ── Snapshot ─────────────────────────────────────────────

    def snapshot(self) -> dict:
        """Return full metrics snapshot as a dict (JSON-serializable)."""
        uptime_s = time.monotonic() - self._start_time
        return {
            "uptime_s":         round(uptime_s, 1),
            "rx_frames":        self._rx_frames_total,
            "tx_frames":        self._tx_frames_total,
            "rx_parse_errors":  self._rx_parse_errors,
            "errors": {
                sat_id: c.to_dict()
                for sat_id, c in self._counters.items()
            },
        }
