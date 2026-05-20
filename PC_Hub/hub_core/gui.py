from __future__ import annotations

import asyncio
import tkinter as tk
from tkinter import ttk, messagebox
from typing import Any, Callable

from .protocol import CAL_BNO, CAL_IR_MAX, CAL_IR_MIN, CAL_LINE_MAX, CAL_LINE_MIN

BG = "#0c0e0f"
SURFACE = "#131618"
BORDER = "#252a2d"
TEXT = "#cdd6dc"
TEXT_DIM = "#5a6a72"
GREEN = "#00e676"
AMBER = "#ffab00"
SAT1 = "#40c4ff"
SAT2 = "#ffb74d"
RED = "#ff5252"


class HubGui:
    def __init__(self, root: tk.Tk, controller: Any, submit_async: Callable[[Any], None]) -> None:
        self.root = root
        self.controller = controller
        self.submit_async = submit_async
        self._tree_rows: dict[tuple[str, str], str] = {}
        self.target_role = tk.StringVar(value="SAT1")
        self._build_ui()
        self._refresh()

    def _build_ui(self) -> None:
        self.root.title("BCWS V3.0 PC Hub")
        self.root.geometry("1180x760")
        self.root.configure(bg=BG)
        self.root.option_add("*Font", ("Courier New", 10))

        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("Treeview", background=SURFACE, fieldbackground=SURFACE, foreground=TEXT, bordercolor=BORDER)
        style.configure("Treeview.Heading", background=SURFACE, foreground=AMBER)

        header = tk.Frame(self.root, bg=SURFACE, padx=14, pady=10)
        header.pack(fill="x")
        tk.Label(header, text="BCWS PC HUB", fg=GREEN, bg=SURFACE, font=("Courier New", 18, "bold")).pack(side="left")
        self.mobile_label = tk.Label(header, text="", fg=TEXT_DIM, bg=SURFACE)
        self.mobile_label.pack(side="right")

        status = tk.Frame(self.root, bg=BG, padx=14, pady=10)
        status.pack(fill="x")
        self.sat1_status = self._status_card(status, "SAT1", SAT1)
        self.sat2_status = self._status_card(status, "SAT2", SAT2)

        body = tk.Frame(self.root, bg=BG, padx=14, pady=6)
        body.pack(fill="both", expand=True)
        body.grid_columnconfigure(0, weight=3)
        body.grid_columnconfigure(1, weight=2)
        body.grid_rowconfigure(0, weight=1)

        left = tk.Frame(body, bg=SURFACE, highlightbackground=BORDER, highlightthickness=1)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        tk.Label(left, text="Telemetry Streams", fg=AMBER, bg=SURFACE, anchor="w", padx=10, pady=8).pack(fill="x")
        columns = ("role", "name", "current", "min", "max", "updated")
        tree = ttk.Treeview(left, columns=columns, show="headings", height=24)
        self.tree = tree
        for col, width in (("role", 80), ("name", 180), ("current", 120), ("min", 120), ("max", 120), ("updated", 150)):
            tree.heading(col, text=col.upper())
            tree.column(col, width=width, anchor="center")
        tree.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        right = tk.Frame(body, bg=BG)
        right.grid(row=0, column=1, sticky="nsew")

        control = tk.Frame(right, bg=SURFACE, highlightbackground=BORDER, highlightthickness=1, padx=12, pady=12)
        control.pack(fill="x", pady=(0, 8))
        tk.Label(control, text="Quick Controls", fg=AMBER, bg=SURFACE, anchor="w").pack(fill="x")
        tgt = tk.Frame(control, bg=SURFACE)
        tgt.pack(fill="x", pady=8)
        for role in ("SAT1", "SAT2"):
            tk.Radiobutton(tgt, text=role, variable=self.target_role, value=role, fg=TEXT, bg=SURFACE, selectcolor=BG, activebackground=SURFACE).pack(side="left", padx=(0, 8))

        mode_row = tk.Frame(control, bg=SURFACE)
        mode_row.pack(fill="x", pady=(0, 8))
        for idx in range(1, 6):
            tk.Button(mode_row, text=f"Mode {idx}", command=lambda i=idx: self._send_mode(i), bg=BORDER, fg=TEXT, activebackground=GREEN, relief="flat").pack(side="left", padx=2)

        cal_row = tk.Frame(control, bg=SURFACE)
        cal_row.pack(fill="x")
        for label, value in (("IR Max", CAL_IR_MAX), ("IR Min", CAL_IR_MIN), ("Line Max", CAL_LINE_MAX), ("Line Min", CAL_LINE_MIN), ("BNO", CAL_BNO)):
            tk.Button(cal_row, text=label, command=lambda v=value: self._send_cal(v), bg=BORDER, fg=TEXT, activebackground=GREEN, relief="flat").pack(side="left", padx=2)

        info = tk.Frame(right, bg=SURFACE, highlightbackground=BORDER, highlightthickness=1, padx=12, pady=12)
        info.pack(fill="x", pady=(0, 8))
        tk.Label(info, text="Mobile UI", fg=AMBER, bg=SURFACE, anchor="w").pack(fill="x")
        self.mobile_sat1 = tk.Label(info, text="", fg=SAT1, bg=SURFACE, justify="left", anchor="w")
        self.mobile_sat1.pack(fill="x", pady=(6, 2))
        self.mobile_sat2 = tk.Label(info, text="", fg=SAT2, bg=SURFACE, justify="left", anchor="w")
        self.mobile_sat2.pack(fill="x")
        tk.Label(info, text="Öffne den Link auf Tablet/Handy im gleichen Netzwerk.", fg=TEXT_DIM, bg=SURFACE, anchor="w").pack(fill="x", pady=(8, 0))

        events = tk.Frame(right, bg=SURFACE, highlightbackground=BORDER, highlightthickness=1, padx=12, pady=12)
        events.pack(fill="both", expand=True)
        tk.Label(events, text="Recent Events", fg=AMBER, bg=SURFACE, anchor="w").pack(fill="x")
        self.event_box = tk.Text(events, bg=BG, fg=TEXT, relief="flat", height=20)
        self.event_box.pack(fill="both", expand=True, pady=(8, 0))
        self.event_box.configure(state="disabled")

    def _status_card(self, parent: tk.Widget, name: str, accent: str) -> dict[str, tk.Label]:
        frame = tk.Frame(parent, bg=SURFACE, highlightbackground=BORDER, highlightthickness=1, padx=12, pady=12)
        frame.pack(side="left", fill="x", expand=True, padx=(0, 8) if name == "SAT1" else 0)
        tk.Label(frame, text=name, fg=accent, bg=SURFACE, font=("Courier New", 14, "bold")).pack(anchor="w")
        online = tk.Label(frame, text="Offline", fg=RED, bg=SURFACE)
        online.pack(anchor="w")
        detail = tk.Label(frame, text="no endpoint", fg=TEXT_DIM, bg=SURFACE, justify="left", anchor="w")
        detail.pack(anchor="w", pady=(4, 0))
        return {"online": online, "detail": detail}

    def _send_mode(self, mode_id: int) -> None:
        self.submit_async(self.controller.send_mode(self.target_role.get(), mode_id))

    def _send_cal(self, cal_cmd: int) -> None:
        self.submit_async(self.controller.send_calibration(self.target_role.get(), cal_cmd))

    def _refresh(self) -> None:
        snapshot = self.controller.state.snapshot()
        satellites = snapshot["satellites"]
        self._update_status(self.sat1_status, satellites.get("SAT1", {}), SAT1)
        self._update_status(self.sat2_status, satellites.get("SAT2", {}), SAT2)
        self._update_tree(satellites)
        self._update_events(snapshot.get("events", []))
        self._update_mobile_links()
        self.root.after(self.controller.config.gui_refresh_ms, self._refresh)

    def _update_status(self, widgets: dict[str, tk.Label], sat: dict[str, Any], accent: str) -> None:
        online = bool(sat.get("online"))
        widgets["online"].configure(text="Online" if online else "Offline", fg=GREEN if online else RED)
        host = sat.get("host") or "waiting for telemetry"
        port = sat.get("port") or "-"
        streams = len(sat.get("streams", {}))
        widgets["detail"].configure(text=f"{host}:{port} · Streams {streams} · RSSI {sat.get('rssi', 0)}")

    def _update_tree(self, satellites: dict[str, Any]) -> None:
        seen_keys: set[tuple[str, str]] = set()
        for role, sat in satellites.items():
            for name, stream in sat.get("streams", {}).items():
                key = (role, name)
                seen_keys.add(key)
                values = (role, name, stream.get("current"), stream.get("min"), stream.get("max"), int(stream.get("ts_ms", 0)))
                if key in self._tree_rows:
                    self.tree.item(self._tree_rows[key], values=values)
                else:
                    item = self.tree.insert("", "end", values=values)
                    self._tree_rows[key] = item
        for key, item in list(self._tree_rows.items()):
            if key not in seen_keys:
                self.tree.delete(item)
                del self._tree_rows[key]

    def _update_events(self, events: list[str]) -> None:
        self.event_box.configure(state="normal")
        self.event_box.delete("1.0", "end")
        self.event_box.insert("1.0", "\n".join(events))
        self.event_box.configure(state="disabled")

    def _update_mobile_links(self) -> None:
        base = self.controller.public_base_url()
        self.mobile_label.configure(text=f"Mobile: {base}/mobile")
        self.mobile_sat1.configure(text=f"SAT1 → {base}/mobile?role=SAT1")
        self.mobile_sat2.configure(text=f"SAT2 → {base}/mobile?role=SAT2")


def run_gui(controller: Any, submit_async: Callable[[Any], None]) -> None:
    root = tk.Tk()
    HubGui(root, controller, submit_async)
    root.mainloop()
