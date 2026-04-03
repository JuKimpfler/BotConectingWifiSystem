"""
hub_core/storage.py — Async SQLite telemetry storage (WAL mode)

Schema:
  samples(id, ts_real, sat_role, stream_name, vtype, value_i, value_f, value_s, ts_sat_ms)
  events(id, ts_real, sat_role, event_type, detail_json)
  commands(id, ts_real, sat_role, cmd_type, payload_json, status, retries)
  peer_status(id, ts_real, sat_role, online, uptime_ms, rssi, queue_len)

Uses batched writes (flush every batch_flush_ms) to achieve ≥1000 rows/s.
"""

import asyncio
import json
import logging
import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, List, Optional

try:
    import aiosqlite
    HAS_AIOSQLITE = True
except ImportError:
    HAS_AIOSQLITE = False

logger = logging.getLogger(__name__)

_CREATE_SCHEMA = """
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
PRAGMA temp_store=MEMORY;

CREATE TABLE IF NOT EXISTS samples (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL    NOT NULL,
    sat_role    INTEGER NOT NULL,
    stream_name TEXT    NOT NULL,
    vtype       INTEGER NOT NULL,
    value_i     INTEGER,
    value_f     REAL,
    value_s     TEXT,
    ts_sat_ms   INTEGER
);
CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts_real);
CREATE INDEX IF NOT EXISTS idx_samples_stream ON samples(sat_role, stream_name, ts_real);

CREATE TABLE IF NOT EXISTS events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real     REAL    NOT NULL,
    sat_role    INTEGER NOT NULL,
    event_type  TEXT    NOT NULL,
    detail_json TEXT
);
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts_real);

CREATE TABLE IF NOT EXISTS commands (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real      REAL    NOT NULL,
    sat_role     INTEGER NOT NULL,
    cmd_type     TEXT    NOT NULL,
    payload_json TEXT,
    status       TEXT,
    retries      INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_commands_ts ON commands(ts_real);

CREATE TABLE IF NOT EXISTS peer_status (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    ts_real    REAL    NOT NULL,
    sat_role   INTEGER NOT NULL,
    online     INTEGER NOT NULL,
    uptime_ms  INTEGER,
    rssi       INTEGER,
    queue_len  INTEGER
);
CREATE INDEX IF NOT EXISTS idx_peer_status_ts ON peer_status(ts_real);
"""


@dataclass
class SampleRow:
    ts_real:     float
    sat_role:    int
    stream_name: str
    vtype:       int
    value_i:     Optional[int]   = None
    value_f:     Optional[float] = None
    value_s:     Optional[str]   = None
    ts_sat_ms:   Optional[int]   = None


class TelemetryStorage:
    """
    Async SQLite writer with batched inserts.

    Usage:
        storage = TelemetryStorage("data/telemetry.db", batch_flush_ms=200)
        await storage.open()
        storage.add_sample(...)
        await storage.close()
    """

    def __init__(
        self,
        db_path: str = "data/telemetry.db",
        batch_flush_ms: int = 200,
        retention_hours: int = 24,
    ) -> None:
        self._db_path        = db_path
        self._flush_interval = batch_flush_ms / 1000.0
        self._retention_s    = retention_hours * 3600.0
        self._db: Optional[Any] = None  # aiosqlite.Connection
        self._sample_batch: List[SampleRow] = []
        self._task: Optional[asyncio.Task] = None
        self._enabled = HAS_AIOSQLITE

    async def open(self) -> None:
        """Open database, create schema, start flush task."""
        if not self._enabled:
            logger.warning("aiosqlite not available — storage disabled")
            return
        # Ensure directory exists
        Path(self._db_path).parent.mkdir(parents=True, exist_ok=True)
        self._db = await aiosqlite.connect(self._db_path)
        self._db.row_factory = aiosqlite.Row
        # Apply schema in one go (each statement separately for compatibility)
        for stmt in _CREATE_SCHEMA.strip().split(";"):
            stmt = stmt.strip()
            if stmt:
                await self._db.execute(stmt)
        await self._db.commit()
        logger.info("Storage opened: %s", self._db_path)
        self._task = asyncio.ensure_future(self._flush_loop())

    async def close(self) -> None:
        if self._task:
            self._task.cancel()
            self._task = None
        await self._flush()
        if self._db:
            await self._db.close()
            self._db = None

    # ── Public write API ─────────────────────────────────────

    def add_sample(
        self,
        ts_real: float,
        sat_role: int,
        stream_name: str,
        vtype: int,
        value: Any,
        ts_sat_ms: int = 0,
    ) -> None:
        """Enqueue a telemetry sample for batch writing."""
        if not self._enabled:
            return
        row = SampleRow(
            ts_real     = ts_real,
            sat_role    = sat_role,
            stream_name = stream_name,
            vtype       = vtype,
            ts_sat_ms   = ts_sat_ms,
        )
        if vtype == 0:
            row.value_i = int(value) if value is not None else None
        elif vtype == 1:
            row.value_f = float(value) if value is not None else None
        elif vtype == 2:
            row.value_i = int(bool(value))
        else:
            row.value_s = str(value) if value is not None else None
        self._sample_batch.append(row)

    async def add_event(
        self,
        ts_real: float,
        sat_role: int,
        event_type: str,
        detail: Optional[dict] = None,
    ) -> None:
        if not self._enabled or not self._db:
            return
        detail_json = json.dumps(detail) if detail else None
        await self._db.execute(
            "INSERT INTO events(ts_real,sat_role,event_type,detail_json) VALUES(?,?,?,?)",
            (ts_real, sat_role, event_type, detail_json),
        )
        await self._db.commit()

    async def add_command(
        self,
        ts_real: float,
        sat_role: int,
        cmd_type: str,
        payload: Optional[dict] = None,
        status: str = "pending",
        retries: int = 0,
    ) -> int:
        """Insert command record, returns row id."""
        if not self._enabled or not self._db:
            return -1
        payload_json = json.dumps(payload) if payload else None
        cursor = await self._db.execute(
            "INSERT INTO commands(ts_real,sat_role,cmd_type,payload_json,status,retries)"
            " VALUES(?,?,?,?,?,?)",
            (ts_real, sat_role, cmd_type, payload_json, status, retries),
        )
        await self._db.commit()
        return cursor.lastrowid or -1

    async def update_command_status(
        self, row_id: int, status: str, retries: int
    ) -> None:
        if not self._enabled or not self._db or row_id < 0:
            return
        await self._db.execute(
            "UPDATE commands SET status=?, retries=? WHERE id=?",
            (status, retries, row_id),
        )
        await self._db.commit()

    async def add_peer_status(
        self,
        ts_real: float,
        sat_role: int,
        online: bool,
        uptime_ms: int = 0,
        rssi: int = 0,
        queue_len: int = 0,
    ) -> None:
        if not self._enabled or not self._db:
            return
        await self._db.execute(
            "INSERT INTO peer_status(ts_real,sat_role,online,uptime_ms,rssi,queue_len)"
            " VALUES(?,?,?,?,?,?)",
            (ts_real, sat_role, int(online), uptime_ms, rssi, queue_len),
        )
        await self._db.commit()

    # ── Query API ────────────────────────────────────────────

    async def query_samples(
        self,
        sat_role: Optional[int] = None,
        stream_name: Optional[str] = None,
        since: Optional[float] = None,
        until: Optional[float] = None,
        limit: int = 1000,
    ) -> list:
        if not self._enabled or not self._db:
            return []
        conditions = []
        params: list = []
        if sat_role is not None:
            conditions.append("sat_role=?")
            params.append(sat_role)
        if stream_name is not None:
            conditions.append("stream_name=?")
            params.append(stream_name)
        if since is not None:
            conditions.append("ts_real>=?")
            params.append(since)
        if until is not None:
            conditions.append("ts_real<=?")
            params.append(until)
        where = ("WHERE " + " AND ".join(conditions)) if conditions else ""
        params.append(limit)
        cursor = await self._db.execute(
            f"SELECT ts_real,sat_role,stream_name,vtype,value_i,value_f,value_s,ts_sat_ms"
            f" FROM samples {where} ORDER BY ts_real DESC LIMIT ?",
            params,
        )
        rows = await cursor.fetchall()
        result = []
        for r in rows:
            v = r["value_f"] if r["vtype"] == 1 else (
                r["value_s"] if r["vtype"] == 3 else r["value_i"]
            )
            result.append({
                "ts_real": r["ts_real"],
                "sat_role": r["sat_role"],
                "stream_name": r["stream_name"],
                "vtype": r["vtype"],
                "value": v,
                "ts_sat_ms": r["ts_sat_ms"],
            })
        return result

    # ── Internal flush ───────────────────────────────────────

    async def _flush_loop(self) -> None:
        while True:
            try:
                await asyncio.sleep(self._flush_interval)
                await self._flush()
                await self._purge_old()
            except asyncio.CancelledError:
                break
            except Exception as exc:  # noqa: BLE001
                logger.error("Storage flush error: %s", exc, exc_info=True)

    async def _flush(self) -> None:
        if not self._enabled or not self._db or not self._sample_batch:
            return
        batch = self._sample_batch
        self._sample_batch = []
        try:
            await self._db.executemany(
                "INSERT INTO samples(ts_real,sat_role,stream_name,vtype,"
                "value_i,value_f,value_s,ts_sat_ms) VALUES(?,?,?,?,?,?,?,?)",
                [
                    (r.ts_real, r.sat_role, r.stream_name, r.vtype,
                     r.value_i, r.value_f, r.value_s, r.ts_sat_ms)
                    for r in batch
                ],
            )
            await self._db.commit()
            logger.debug("Storage flushed %d samples", len(batch))
        except Exception as exc:  # noqa: BLE001
            logger.error("Storage flush failed: %s — %d rows lost", exc, len(batch))

    async def _purge_old(self) -> None:
        if not self._enabled or not self._db or self._retention_s <= 0:
            return
        cutoff = time.time() - self._retention_s
        await self._db.execute("DELETE FROM samples WHERE ts_real<?", (cutoff,))
        await self._db.execute("DELETE FROM events WHERE ts_real<?", (cutoff,))
        await self._db.commit()
