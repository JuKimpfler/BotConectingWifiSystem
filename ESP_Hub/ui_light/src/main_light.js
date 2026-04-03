// ============================================================
//  ESP-NOW Hub Light UI – Main
//  Telemetry-only display: live plotter + data table.
//
//  Design goals:
//  • Smooth 50 Hz plotter via a continuous requestAnimationFrame loop.
//    The loop only redraws when new data has actually arrived (tracked
//    via _dataSeq counter), avoiding unnecessary GPU work.
//  • All incoming streams are automatically added to the plotter –
//    no checkbox selection needed.
//  • Table refreshes on a 200 ms timer, keeping DOM writes minimal
//    and well away from the animation path.
// ============================================================

import './style_light.css'
import { wsConnect, wsSend, wsOn } from './ws_light.js'

// ── Constants ─────────────────────────────────────────────────
const MAX_PLOT_POINTS  = 200   // samples retained per stream
const MAX_PLOT_BUFFER  = 20    // hysteresis before trimming
const TABLE_REFRESH_MS = 200   // table DOM update interval

// ── State ─────────────────────────────────────────────────────
// plotSeries: key → Float32Array-backed circular buffer (implemented
// with a plain Array for simplicity; sufficient at these rates)
const plotSeries    = new Map()  // key → number[]
const currentValues = new Map()  // key → { role, name, current, min, max }

let _dataSeq      = 0   // incremented on every telemetry message
let _lastDrawSeq  = -1  // last _dataSeq value drawn by RAF loop

// ── DOM references ─────────────────────────────────────────────
const wsBadge    = document.getElementById('ws-status')
const sat1Badge  = document.getElementById('badge-sat1')
const sat2Badge  = document.getElementById('badge-sat2')
const plotCanvas = document.getElementById('telem-plot')
const plotLegend = document.getElementById('plot-legend')
const plotEmpty  = document.getElementById('plot-empty')
const telemBody  = document.getElementById('telem-body')

// ── Tab switching ─────────────────────────────────────────────
const tabButtons = Array.from(document.querySelectorAll('.tab'))
const tabPanels  = Array.from(document.querySelectorAll('.tab-panel'))
let activeTab = 'plotter'

tabButtons.forEach(btn => {
  btn.addEventListener('click', () => {
    if (btn.dataset.tab === activeTab) return
    tabButtons.forEach(t => t.classList.remove('active'))
    tabPanels.forEach(p => p.classList.remove('active'))
    btn.classList.add('active')
    document.getElementById(`tab-${btn.dataset.tab}`).classList.add('active')
    activeTab = btn.dataset.tab
    // Force an immediate redraw when switching to the plotter
    if (activeTab === 'plotter') _lastDrawSeq = -1
  })
})

// ── WebSocket status ──────────────────────────────────────────
wsOn('ws_open', () => {
  wsBadge.textContent = 'WS ●'
  wsBadge.className   = 'badge online'
})

wsOn('ws_close', () => {
  wsBadge.textContent = 'WS'
  wsBadge.className   = 'badge offline'
  sat1Badge.className = 'badge offline'
  sat2Badge.className = 'badge offline'
  sat1Badge.textContent = 'SAT1 –'
  sat2Badge.textContent = 'SAT2 –'
  plotSeries.clear()
  currentValues.clear()
  _dataSeq++
})

// ── Peer status ────────────────────────────────────────────────
wsOn('peer_status', (msg) => {
  const peers = msg.peers || []
  peers.forEach(p => {
    const badge = p.role === 'SAT1' ? sat1Badge : sat2Badge
    if (p.online) {
      badge.textContent = `${p.name || p.role} ●`
      badge.className   = 'badge online'
    } else {
      badge.textContent = `${p.name || p.role} ○`
      badge.className   = 'badge offline'
    }
  })
})

// ── Telemetry ──────────────────────────────────────────────────
wsOn('telemetry', (msg) => {
  const streamsArr = msg.streams || []
  streamsArr.forEach(s => {
    if (!s.name) return
    const key = `${s.role}:${s.name}`
    const val = Number(s.current)

    // Update current/min/max snapshot for table
    currentValues.set(key, {
      role:    s.role,
      name:    s.name,
      current: s.current,
      min:     s.min,
      max:     s.max,
    })

    // Append to plot series (auto-enable all streams)
    if (Number.isFinite(val)) {
      let pts = plotSeries.get(key)
      if (!pts) {
        pts = []
        plotSeries.set(key, pts)
      }
      pts.push(val)
      if (pts.length > MAX_PLOT_POINTS + MAX_PLOT_BUFFER) {
        pts.splice(0, pts.length - MAX_PLOT_POINTS)
      }
    }
  })

  // Signal the RAF loop that fresh data is available
  _dataSeq++
})

// ── Colour palette ─────────────────────────────────────────────
function _colorForKey(key) {
  let hash = 2166136261
  for (let i = 0; i < key.length; i++) {
    hash ^= key.charCodeAt(i)
    hash  = Math.imul(hash, 16777619) >>> 0
  }
  return `hsl(${hash % 360} 85% 58%)`
}

// ── Canvas sizing ──────────────────────────────────────────────
function _resizeCanvas() {
  if (!plotCanvas) return
  const w = plotCanvas.clientWidth
  const h = plotCanvas.clientHeight
  if (plotCanvas.width !== w)  plotCanvas.width  = w
  if (plotCanvas.height !== h) plotCanvas.height = h
}

// ── Plotter draw ───────────────────────────────────────────────
function _drawPlot() {
  if (!plotCanvas) return
  _resizeCanvas()
  const ctx = plotCanvas.getContext('2d')
  if (!ctx) return

  const width  = plotCanvas.width
  const height = plotCanvas.height

  // Collect visible series (all non-empty series)
  const keys = []
  plotSeries.forEach((pts, key) => { if (pts.length >= 2) keys.push(key) })

  // Hide empty-state message
  if (plotEmpty) plotEmpty.style.display = keys.length === 0 ? 'block' : 'none'

  // Clear
  ctx.clearRect(0, 0, width, height)
  ctx.fillStyle = '#080a0b'
  ctx.fillRect(0, 0, width, height)

  if (keys.length === 0) return

  // Determine global Y range across all series
  let minY =  Infinity
  let maxY = -Infinity
  keys.forEach(key => {
    const pts = plotSeries.get(key) || []
    for (let i = 0; i < pts.length; i++) {
      if (pts[i] < minY) minY = pts[i]
      if (pts[i] > maxY) maxY = pts[i]
    }
  })
  if (!isFinite(minY) || !isFinite(maxY)) return
  if (minY === maxY) { minY -= 1; maxY += 1 }

  const padL = 42, padR = 12, padT = 12, padB = 22
  const plotW = width  - padL - padR
  const plotH = height - padT - padB
  const xStep = plotW / (MAX_PLOT_POINTS - 1)
  const scaleY = plotH / (maxY - minY)

  // Grid lines
  ctx.strokeStyle = '#1a1f22'
  ctx.lineWidth   = 1
  ctx.beginPath()
  for (let i = 0; i <= 4; i++) {
    const y = padT + plotH * i / 4
    ctx.moveTo(padL, y)
    ctx.lineTo(padL + plotW, y)
  }
  ctx.stroke()

  // Y-axis labels
  ctx.fillStyle = '#5a6a72'
  ctx.font      = '11px Courier New, monospace'
  ctx.textAlign = 'right'
  for (let i = 0; i <= 4; i++) {
    const v = maxY - (maxY - minY) * i / 4
    const y = padT + plotH * i / 4
    ctx.fillText(v.toPrecision(4), padL - 4, y + 4)
  }
  ctx.textAlign = 'left'

  // Series lines
  const legendFrag = document.createDocumentFragment()
  keys.forEach(key => {
    const pts   = plotSeries.get(key) || []
    const color = _colorForKey(key)

    ctx.strokeStyle = color
    ctx.lineWidth   = 1.8
    ctx.beginPath()
    const start = Math.max(0, pts.length - MAX_PLOT_POINTS)
    for (let i = start; i < pts.length; i++) {
      const x = padL + (i - start) * xStep
      const y = padT + (maxY - pts[i]) * scaleY
      if (i === start) ctx.moveTo(x, y)
      else             ctx.lineTo(x, y)
    }
    ctx.stroke()

    // Legend entry
    if (plotLegend) {
      const item = document.createElement('span')
      item.className = 'plot-series'
      const chip = document.createElement('span')
      chip.className        = 'plot-chip'
      chip.style.background = color
      const txt = document.createElement('span')
      txt.textContent = key.replace(':', ' / ')
      item.appendChild(chip)
      item.appendChild(txt)
      legendFrag.appendChild(item)
    }
  })
  if (plotLegend) plotLegend.replaceChildren(legendFrag)
}

// ── Continuous RAF loop ────────────────────────────────────────
// Only calls _drawPlot() when new telemetry data has arrived since the
// last frame.  This gives smooth 60 fps rendering at the browser's
// native frame rate without burning GPU/CPU when there is no new data.
function _rafLoop() {
  if (activeTab === 'plotter' && _dataSeq !== _lastDrawSeq) {
    _lastDrawSeq = _dataSeq
    _drawPlot()
  }
  requestAnimationFrame(_rafLoop)
}
requestAnimationFrame(_rafLoop)

// ── Table update (timer-driven, 200 ms) ────────────────────────
// Decoupled from the plotter RAF loop to keep DOM work out of the
// animation path.
const tableRows = new Map() // key → { row, roleTd, nameTd, curTd, minTd, maxTd }

function _createTableRow() {
  const row   = document.createElement('tr')
  const roleTd = document.createElement('td')
  const nameTd = document.createElement('td')
  const curTd  = document.createElement('td')
  const minTd  = document.createElement('td')
  const maxTd  = document.createElement('td')
  curTd.className = 'value-current'
  minTd.className = 'value-dim'
  maxTd.className = 'value-dim'
  row.appendChild(roleTd)
  row.appendChild(nameTd)
  row.appendChild(curTd)
  row.appendChild(minTd)
  row.appendChild(maxTd)
  return { row, roleTd, nameTd, curTd, minTd, maxTd }
}

function _renderTable() {
  if (activeTab !== 'table') return
  const fmt = n => (typeof n === 'number' ? n.toFixed(4) : String(n))
  const frag = document.createDocumentFragment()

  const sorted = Array.from(currentValues.entries()).sort((a, b) => {
    const ra = Number(a[1].role) || 0
    const rb = Number(b[1].role) || 0
    if (ra !== rb) return ra - rb
    return a[1].name.localeCompare(b[1].name)
  })

  sorted.forEach(([key, s]) => {
    let refs = tableRows.get(key)
    if (!refs) {
      refs = _createTableRow()
      tableRows.set(key, refs)
    }
    const roleNum = Number(s.role) || 0
    refs.roleTd.textContent = roleNum > 0 ? `SAT${roleNum}` : 'SAT'
    refs.nameTd.textContent = s.name
    refs.curTd.textContent  = fmt(s.current)
    refs.minTd.textContent  = fmt(s.min)
    refs.maxTd.textContent  = fmt(s.max)
    frag.appendChild(refs.row)
  })
  telemBody.replaceChildren(frag)
}

setInterval(_renderTable, TABLE_REFRESH_MS)

// ── Boot ──────────────────────────────────────────────────────
wsConnect()
