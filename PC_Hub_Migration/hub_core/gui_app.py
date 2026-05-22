from __future__ import annotations

import math
import queue
import time
import tkinter as tk
from dataclasses import dataclass
from tkinter import ttk
from typing import Iterable

from debug_ingest import DebugIngest, DebugSample


@dataclass(slots=True)
class SensorState:
    analog: int = 0
    active: bool = False


class DebugGuiApp:
    def __init__(self, ingest: DebugIngest) -> None:
        self.ingest = ingest
        self.root = tk.Tk()
        self.root.title("BCWS PC Hub - V3 Debug GUI")
        self.root.geometry("1360x900")
        self.root.minsize(1180, 760)
        self.root.configure(bg="#0b1220")

        self.sensors = {i: SensorState() for i in range(1, 41)}
        self.lw_angle = 0.0
        self.last_update_ts = 0.0

        self._setup_style()
        self._build_layout()
        self._schedule_updates()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def run(self) -> None:
        self.root.mainloop()

    def _setup_style(self) -> None:
        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        style.configure(".", background="#0b1220", foreground="#e5e7eb")
        style.configure("TFrame", background="#0b1220")
        style.configure("Card.TFrame", background="#111827")
        style.configure("TLabel", background="#0b1220", foreground="#d1d5db", font=("Segoe UI", 10))
        style.configure("Title.TLabel", font=("Segoe UI Semibold", 16), foreground="#f9fafb")
        style.configure("SubTitle.TLabel", font=("Segoe UI Semibold", 11), foreground="#93c5fd")
        style.configure("CardTitle.TLabel", background="#111827", font=("Segoe UI Semibold", 11), foreground="#93c5fd")
        style.configure("CardValue.TLabel", background="#111827", font=("Consolas", 11), foreground="#f9fafb")
        style.configure("TNotebook", background="#0b1220", borderwidth=0)
        style.configure("TNotebook.Tab", font=("Segoe UI", 10), padding=(14, 7), background="#1f2937", foreground="#d1d5db")
        style.map("TNotebook.Tab", background=[("selected", "#111827")], foreground=[("selected", "#ffffff")])

    def _build_layout(self) -> None:
        root_frame = ttk.Frame(self.root, padding=12)
        root_frame.pack(fill=tk.BOTH, expand=True)

        title = ttk.Label(root_frame, text="BCWS V3 - PC Hub GUI", style="Title.TLabel")
        title.pack(anchor=tk.W)
        subtitle = ttk.Label(root_frame, text="Debug-Visualisierung mit Platinenansicht und Live-Daten", style="SubTitle.TLabel")
        subtitle.pack(anchor=tk.W, pady=(0, 10))

        self.top_notebook = ttk.Notebook(root_frame)
        self.top_notebook.pack(fill=tk.BOTH, expand=True)

        overview_tab = ttk.Frame(self.top_notebook)
        self.top_notebook.add(overview_tab, text="Übersicht")
        self._build_overview_tab(overview_tab)

        debug_tab = ttk.Frame(self.top_notebook)
        self.top_notebook.add(debug_tab, text="Debug")
        self._build_debug_tab(debug_tab)

    def _build_overview_tab(self, parent: ttk.Frame) -> None:
        wrap = ttk.Frame(parent, style="Card.TFrame", padding=16)
        wrap.pack(fill=tk.BOTH, expand=True, padx=2, pady=2)

        ttk.Label(wrap, text="Systemstatus", style="CardTitle.TLabel").grid(row=0, column=0, sticky="w")
        self.status_value = ttk.Label(wrap, text="Warte auf Debug-Daten ...", style="CardValue.TLabel")
        self.status_value.grid(row=1, column=0, sticky="w", pady=(4, 14))

        metrics = ttk.Frame(wrap, style="Card.TFrame")
        metrics.grid(row=2, column=0, sticky="ew")

        self.metric_packets = self._metric_cell(metrics, 0, "UDP Pakete")
        self.metric_samples = self._metric_cell(metrics, 1, "Samples")
        self.metric_errors = self._metric_cell(metrics, 2, "Decode Errors")
        self.metric_drops = self._metric_cell(metrics, 3, "Queue Drops")

    def _metric_cell(self, parent: ttk.Frame, col: int, title: str) -> ttk.Label:
        card = ttk.Frame(parent, style="Card.TFrame", padding=12)
        card.grid(row=0, column=col, sticky="nsew", padx=(0, 8))
        parent.columnconfigure(col, weight=1)
        ttk.Label(card, text=title, style="CardTitle.TLabel").pack(anchor=tk.W)
        value = ttk.Label(card, text="0", style="CardValue.TLabel")
        value.pack(anchor=tk.W, pady=(4, 0))
        return value

    def _build_debug_tab(self, parent: ttk.Frame) -> None:
        shell = ttk.Frame(parent)
        shell.pack(fill=tk.BOTH, expand=True)

        self.debug_notebook = ttk.Notebook(shell)
        self.debug_notebook.pack(fill=tk.BOTH, expand=True)

        board_tab = ttk.Frame(self.debug_notebook)
        bars_tab = ttk.Frame(self.debug_notebook)
        state_tab = ttk.Frame(self.debug_notebook)

        self.debug_notebook.add(board_tab, text="Platine")
        self.debug_notebook.add(bars_tab, text="Analog-Balken")
        self.debug_notebook.add(state_tab, text="Live-Status")

        self.board_canvas = tk.Canvas(board_tab, bg="#020617", highlightthickness=0)
        self.board_canvas.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)
        self.board_canvas.bind("<Configure>", lambda _e: self._draw_board())

        self.bars_canvas = tk.Canvas(bars_tab, bg="#020617", highlightthickness=0)
        self.bars_canvas.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)
        self.bars_canvas.bind("<Configure>", lambda _e: self._draw_bars())

        status_wrap = ttk.Frame(state_tab, style="Card.TFrame", padding=12)
        status_wrap.pack(fill=tk.BOTH, expand=True, padx=8, pady=8)
        self.sensor_tree = ttk.Treeview(status_wrap, columns=("sensor", "analog", "active"), show="headings", height=20)
        self.sensor_tree.heading("sensor", text="Sensor")
        self.sensor_tree.heading("analog", text="LS")
        self.sensor_tree.heading("active", text="LB")
        self.sensor_tree.column("sensor", width=110, anchor=tk.CENTER)
        self.sensor_tree.column("analog", width=110, anchor=tk.CENTER)
        self.sensor_tree.column("active", width=110, anchor=tk.CENTER)
        self.sensor_tree.pack(fill=tk.BOTH, expand=True)
        for idx in range(1, 41):
            self.sensor_tree.insert("", tk.END, iid=f"s{idx}", values=(f"S{idx}", 0, "false"))

    def _schedule_updates(self) -> None:
        self._drain_queue()
        self._refresh_metrics()
        self._draw_board()
        self._draw_bars()
        self._refresh_sensor_table()
        self.root.after(50, self._schedule_updates)

    def _drain_queue(self) -> None:
        while True:
            try:
                sample = self.ingest.queue.get_nowait()
            except queue.Empty:
                return
            self._apply_sample(sample)

    def _apply_sample(self, sample: DebugSample) -> None:
        name = sample.name
        value = sample.value
        self.last_update_ts = sample.rx_ts
        if name == "LW":
            self.lw_angle = self._clamp(float(value), -180.0, 180.0)
            return

        try:
            sensor_index = int(name[2:])
        except ValueError:
            return
        if sensor_index < 1 or sensor_index > 40:
            return
        sensor = self.sensors[sensor_index]
        if name.startswith("LS"):
            sensor.analog = int(self._clamp(float(value), 0, 255))
        elif name.startswith("LB"):
            if isinstance(value, str):
                sensor.active = value.lower() in {"1", "true", "on", "yes"}
            else:
                sensor.active = bool(value)

    def _refresh_metrics(self) -> None:
        stats = self.ingest.stats
        self.metric_packets.configure(text=str(stats["packets"]))
        self.metric_samples.configure(text=str(stats["samples"]))
        self.metric_errors.configure(text=str(stats["decode_errors"]))
        self.metric_drops.configure(text=str(stats["dropped_samples"]))

        if self.last_update_ts <= 0:
            self.status_value.configure(text="Warte auf Debug-Daten ...")
            return
        ago = max(0.0, time.time() - self.last_update_ts)
        self.status_value.configure(text=f"Live-Daten aktiv · letztes Update vor {ago:.2f}s")

    def _draw_board(self) -> None:
        c = self.board_canvas
        w = c.winfo_width()
        h = c.winfo_height()
        if w < 50 or h < 50:
            return
        c.delete("all")

        cx, cy = w * 0.5, h * 0.5
        outer = min(w, h) * 0.40
        inner = outer * 0.58

        c.create_rectangle(0, 0, w, h, fill="#020617", outline="")
        c.create_oval(cx - outer, cy - outer, cx + outer, cy + outer, fill="#3d4c3f", outline="#4b5f4f", width=2)
        c.create_oval(cx - inner, cy - inner, cx + inner, cy + inner, fill="#020617", outline="#111827", width=2)

        for lug_deg in (0, 45, 90, 135, 180, 225, 270, 315):
            rad = math.radians(lug_deg)
            lx = cx + math.cos(rad) * (outer * 0.95)
            ly = cy + math.sin(rad) * (outer * 0.95)
            lug = outer * 0.24
            c.create_oval(lx - lug * 0.35, ly - lug * 0.35, lx + lug * 0.35, ly + lug * 0.35, fill="#3d4c3f", outline="#4b5f4f")

        for idx, x, y in self._sensor_positions(cx, cy, inner * 1.25):
            sensor = self.sensors[idx]
            base_color = "#5d0000"
            active_color = "#ff4d4d"
            glow_color = "#ff8a8a"
            color = active_color if sensor.active else base_color

            if sensor.active:
                c.create_oval(x - 10, y - 10, x + 10, y + 10, fill=glow_color, outline="", stipple="gray50")
            c.create_oval(x - 6, y - 6, x + 6, y + 6, fill=color, outline="#2b0a0a", width=1)

            off_x = 16 if x >= cx else -16
            anchor = tk.W if x >= cx else tk.E
            c.create_text(x + off_x, y - 7, text=str(sensor.analog), fill="#fca5a5", font=("Consolas", 9, "bold"), anchor=anchor)
            c.create_text(x + off_x, y + 7, text=f"S{idx}", fill="#9ca3af", font=("Segoe UI", 8), anchor=anchor)

        self._draw_angle_line(c, cx, cy, inner * 0.95, self.lw_angle)
        c.create_text(12, 12, anchor=tk.NW, text=f"LW: {self.lw_angle:.1f}°", fill="#93c5fd", font=("Consolas", 12, "bold"))
        c.create_text(12, 32, anchor=tk.NW, text="0° = oben, +Winkel im Uhrzeigersinn", fill="#64748b", font=("Segoe UI", 9))

    def _draw_angle_line(self, canvas: tk.Canvas, cx: float, cy: float, length: float, angle: float) -> None:
        rad = math.radians(angle)
        x = cx + length * math.sin(rad)
        y = cy - length * math.cos(rad)
        canvas.create_line(cx, cy, x, y, fill="#38bdf8", width=4, arrow=tk.LAST, arrowshape=(14, 16, 6))
        canvas.create_oval(cx - 5, cy - 5, cx + 5, cy + 5, fill="#38bdf8", outline="")

    def _draw_bars(self) -> None:
        c = self.bars_canvas
        w = c.winfo_width()
        h = c.winfo_height()
        if w < 100 or h < 80:
            return
        c.delete("all")
        c.create_rectangle(0, 0, w, h, fill="#020617", outline="")

        padding_x = 20
        top = 40
        bottom = h - 35
        usable_h = max(10, bottom - top)
        bar_w = max(2, (w - 2 * padding_x) / 40.0)

        for i in range(1, 41):
            sensor = self.sensors[i]
            x0 = padding_x + (i - 1) * bar_w + 1
            x1 = padding_x + i * bar_w - 1
            ratio = sensor.analog / 255.0
            y1 = bottom
            y0 = bottom - ratio * usable_h
            fill = "#ef4444" if sensor.active else "#7f1d1d"
            c.create_rectangle(x0, y0, x1, y1, fill=fill, outline="")
            if i % 5 == 0:
                c.create_text((x0 + x1) * 0.5, bottom + 10, text=str(i), fill="#94a3b8", font=("Segoe UI", 8))

        c.create_line(padding_x, bottom, w - padding_x, bottom, fill="#334155")
        c.create_line(padding_x, top, padding_x, bottom, fill="#334155")
        c.create_text(padding_x, 14, text="LS1..LS40 Analogwerte (0-255)", anchor=tk.W, fill="#93c5fd", font=("Segoe UI Semibold", 11))

    def _refresh_sensor_table(self) -> None:
        for idx in range(1, 41):
            sensor = self.sensors[idx]
            self.sensor_tree.item(f"s{idx}", values=(f"S{idx}", sensor.analog, "true" if sensor.active else "false"))

    @staticmethod
    def _sensor_positions(cx: float, cy: float, radius: float) -> Iterable[tuple[int, float, float]]:
        start = 100.0
        step = 360.0 / 40.0
        for idx in range(1, 41):
            deg = start - (idx - 1) * step
            rad = math.radians(deg)
            x = cx + math.cos(rad) * radius
            y = cy - math.sin(rad) * radius
            yield idx, x, y

    @staticmethod
    def _clamp(value: float, low: float, high: float) -> float:
        return max(low, min(high, value))

    def _on_close(self) -> None:
        self.ingest.stop()
        self.root.destroy()
