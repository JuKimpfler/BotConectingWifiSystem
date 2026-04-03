"""
hub_core/logging_setup.py — Structured JSON + human-readable console logging

Provides:
- JSON Lines file logger (logs/hub_YYYY-MM-DD.log), daily rotation, 7-day retention
- Coloured human-readable console logger
"""

import logging
import logging.handlers
import json
import time
import os
import sys
from pathlib import Path


# ANSI colour codes (disabled on non-TTY)
_COLOURS = {
    logging.DEBUG:    "\033[36m",    # cyan
    logging.INFO:     "\033[32m",    # green
    logging.WARNING:  "\033[33m",    # yellow
    logging.ERROR:    "\033[31m",    # red
    logging.CRITICAL: "\033[35m",    # magenta
}
_RESET = "\033[0m"


class JsonLineFormatter(logging.Formatter):
    """Format log records as JSON Lines for file output."""

    def format(self, record: logging.LogRecord) -> str:
        entry = {
            "ts":      time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(record.created))
                       + f".{int(record.msecs):03d}Z",
            "level":   record.levelname,
            "logger":  record.name,
            "msg":     record.getMessage(),
        }
        if record.exc_info:
            entry["exc"] = self.formatException(record.exc_info)
        # Include any extra fields attached to the record
        for key, val in record.__dict__.items():
            if key not in ("args", "exc_info", "exc_text", "stack_info",
                           "created", "msecs", "relativeCreated", "thread",
                           "threadName", "processName", "process",
                           "pathname", "filename", "module", "lineno",
                           "funcName", "levelname", "levelno", "name",
                           "msg", "message"):
                try:
                    json.dumps(val)  # Check serialisability
                    entry[key] = val
                except (TypeError, ValueError):
                    entry[key] = str(val)
        return json.dumps(entry, ensure_ascii=False)


class ColouredConsoleFormatter(logging.Formatter):
    """Human-readable coloured formatter for console output."""

    _use_colour: bool = sys.stdout.isatty() if hasattr(sys.stdout, "isatty") else False

    def format(self, record: logging.LogRecord) -> str:
        ts = time.strftime("%H:%M:%S", time.localtime(record.created))
        ms = int(record.msecs)
        level = f"{record.levelname:<8}"
        msg = record.getMessage()
        if record.exc_info:
            msg += "\n" + self.formatException(record.exc_info)
        if self._use_colour:
            colour = _COLOURS.get(record.levelno, "")
            return f"{colour}{ts}.{ms:03d}  {level}  [{record.name}]  {msg}{_RESET}"
        return f"{ts}.{ms:03d}  {level}  [{record.name}]  {msg}"


def setup_logging(log_level: str = "INFO", log_dir: str = "logs") -> None:
    """
    Configure root logger with:
    - Coloured console handler at specified level
    - JSON Lines file handler (daily rotation, 7-day retention)
    """
    level = getattr(logging, log_level.upper(), logging.INFO)

    root = logging.getLogger()
    root.setLevel(logging.DEBUG)  # Capture everything; handlers filter

    # Avoid adding duplicate handlers on reload
    root.handlers.clear()

    # Console handler
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(level)
    console_handler.setFormatter(ColouredConsoleFormatter())
    root.addHandler(console_handler)

    # JSON file handler (daily rotation)
    try:
        Path(log_dir).mkdir(parents=True, exist_ok=True)
        log_path = os.path.join(log_dir, "hub.log")
        file_handler = logging.handlers.TimedRotatingFileHandler(
            log_path,
            when="midnight",
            backupCount=7,
            encoding="utf-8",
        )
        file_handler.setLevel(logging.DEBUG)
        file_handler.setFormatter(JsonLineFormatter())
        file_handler.suffix = "%Y-%m-%d"
        root.addHandler(file_handler)
    except Exception as exc:  # noqa: BLE001
        logging.getLogger(__name__).warning("Could not set up file logging: %s", exc)
