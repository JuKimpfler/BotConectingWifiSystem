from __future__ import annotations

import json
import queue
import re
import socket
import threading
import time
from dataclasses import dataclass
from typing import Any


SENSOR_KEY_PATTERN = re.compile(r"^(LS|LB)([1-9]|[1-3][0-9]|40)$")


@dataclass(slots=True)
class DebugSample:
    name: str
    value: Any
    rx_ts: float


class DebugIngest:
    def __init__(self, bind_host: str = "0.0.0.0", bind_port: int = 9999) -> None:
        self.bind_host = bind_host
        self.bind_port = bind_port
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self.queue: queue.Queue[DebugSample] = queue.Queue(maxsize=5000)
        self.stats = {
            "packets": 0,
            "decode_errors": 0,
            "samples": 0,
            "dropped_samples": 0,
            "last_packet_ts": 0.0,
        }

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._run, name="debug-ingest", daemon=True)
        self._thread.start()

    def stop(self, timeout: float = 1.5) -> None:
        self._stop_event.set()
        if self._thread:
            self._thread.join(timeout=timeout)

    def _run(self) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.bind_host, self.bind_port))
        sock.settimeout(0.3)
        try:
            while not self._stop_event.is_set():
                try:
                    data, _ = sock.recvfrom(8192)
                except socket.timeout:
                    continue
                except OSError:
                    break
                self.stats["packets"] += 1
                self.stats["last_packet_ts"] = time.time()
                self._handle_packet(data)
        finally:
            sock.close()

    def _handle_packet(self, data: bytes) -> None:
        rx_ts = time.time()
        text = data.decode("utf-8", errors="replace").strip()
        parsed: Any
        try:
            parsed = json.loads(text)
            samples = self._parse_json_payload(parsed, rx_ts)
        except json.JSONDecodeError:
            samples = self._parse_text_payload(text, rx_ts)

        if not samples:
            self.stats["decode_errors"] += 1
            return

        for sample in samples:
            try:
                self.queue.put_nowait(sample)
                self.stats["samples"] += 1
            except queue.Full:
                self.stats["dropped_samples"] += 1

    def _parse_json_payload(self, payload: Any, rx_ts: float) -> list[DebugSample]:
        if isinstance(payload, dict):
            if isinstance(payload.get("name"), str) and "value" in payload:
                name = payload["name"].strip()
                if self._is_supported_name(name):
                    return [DebugSample(name=name, value=payload["value"], rx_ts=rx_ts)]

            if payload.get("type") == "telemetry" and isinstance(payload.get("name"), str):
                name = payload["name"].strip()
                if self._is_supported_name(name):
                    return [DebugSample(name=name, value=payload.get("value"), rx_ts=rx_ts)]

            samples: list[DebugSample] = []
            for key, value in payload.items():
                if self._is_supported_name(key):
                    samples.append(DebugSample(name=key, value=value, rx_ts=rx_ts))
            return samples

        if isinstance(payload, list):
            samples: list[DebugSample] = []
            for item in payload:
                samples.extend(self._parse_json_payload(item, rx_ts))
            return samples

        return []

    def _parse_text_payload(self, text: str, rx_ts: float) -> list[DebugSample]:
        samples: list[DebugSample] = []
        if not text:
            return samples
        for token in re.split(r"[,\s;]+", text):
            if not token:
                continue
            if "=" in token:
                name, raw = token.split("=", 1)
            elif ":" in token:
                name, raw = token.split(":", 1)
            else:
                continue
            name = name.strip()
            if not self._is_supported_name(name):
                continue
            samples.append(DebugSample(name=name, value=self._coerce_value(name, raw.strip()), rx_ts=rx_ts))
        return samples

    @staticmethod
    def _is_supported_name(name: str) -> bool:
        return name == "LW" or SENSOR_KEY_PATTERN.match(name) is not None

    @staticmethod
    def _coerce_value(name: str, raw: str) -> Any:
        if name.startswith("LB"):
            return raw.lower() in {"1", "true", "on", "yes"}
        if name == "LW":
            try:
                return float(raw)
            except ValueError:
                return 0.0
        try:
            return int(float(raw))
        except ValueError:
            return 0
