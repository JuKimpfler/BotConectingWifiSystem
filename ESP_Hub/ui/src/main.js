// ============================================================
//  ESP-NOW Hub UI – Main entry point
//  Tab routing, peer status, telemetry, command dispatch
// ============================================================

import './style.css'
import { wsConnect, wsSend, wsOn, isConnected } from './ws.js'

// ── State ─────────────────────────────────────────────────────
let targetRole = 1   // 1 = SAT1, 2 = SAT2
let startActive = false
let cmdRateLimitMs = 20
let lastCmdTs = 0

// Joystick state
let joystickSpeed = 0
let joystickAngle = 0
let joystickActive = false
let joystickInterval = null
const JOYSTICK_SEND_MS = 50

// Telemetry stream map: name -> { current, min, max }
let telemFilter = '1'
const streams = new Map()
const plotSelection = new Map() // key -> enabled (default false)
const plotSeries = new Map()    // key -> number[]
const MAX_PLOT_POINTS = 200
// Hysteresis buffer: trim only after points exceed MAX_PLOT_POINTS + MAX_PLOT_BUFFER.
const MAX_PLOT_BUFFER = 24
const TELEMETRY_RENDER_MIN_INTERVAL_MS = 100
const LED_STREAM_REGEX = /^Led([1-4])$/
// Treat numeric telemetry >= 0.5 as "on" (supports 0/1 as int or float).
const LED_ON_THRESHOLD = 0.5
const PLOT_EMPTY_MESSAGE = 'Keine Streams aktiviert. Im Tabellen-Tab die Plot-Checkbox setzen.'
const ledStateByRole = {
  1: [false, false, false, false],
  2: [false, false, false, false],
}
const MODE_CHANNELS = 5
const CAL_CHANNELS = 5
let modeLabels = Array.from({ length: MODE_CHANNELS }, (_, i) => `Mode ${i + 1}`)
let calLabels = Array.from({ length: CAL_CHANNELS }, (_, i) => `Calib ${i + 1}`)
let _plotDrawScheduled = false
let _plotDrawPending = false
let _telemRenderScheduled = false
let _telemRenderPending = false
let _telemRenderTimer = null
let _lastTelemetryRenderTs = 0
const telemRows = new Map() // key -> { row, roleTd, nameTd, curTd, minTd, maxTd, chk }

function _renderModeCalLabels() {
  document.querySelectorAll('.btn-mode').forEach(btn => {
    const idx = Math.max(0, Math.min(MODE_CHANNELS - 1, (parseInt(btn.dataset.mode, 10) || 1) - 1))
    btn.textContent = `${idx + 1} – ${modeLabels[idx] || `Mode ${idx + 1}`}`
  })
  document.querySelectorAll('.btn-cal').forEach(btn => {
    const idx = Math.max(0, Math.min(CAL_CHANNELS - 1, (parseInt(btn.dataset.cal, 10) || 1) - 1))
    btn.textContent = `${idx + 1} – ${calLabels[idx] || `Calib ${idx + 1}`}`
  })
}

// ── Load saved config into settings form (Bug 2) ──────────────
function _loadConfig() {
  fetch('/api/config')
    .then(r => { if (r.ok) return r.json(); throw new Error('no config') })
    .then(cfg => {
      if (cfg.channel != null)
        document.getElementById('cfg-channel').value = cfg.channel
      if (cfg.network_id != null)
        document.getElementById('cfg-network-id').value = cfg.network_id
      if (cfg.telemetry && cfg.telemetry.max_rate_hz != null)
        document.getElementById('cfg-telem-hz').value = cfg.telemetry.max_rate_hz
      if (cfg.ui && Array.isArray(cfg.ui.mode_labels)) {
        cfg.ui.mode_labels.slice(0, MODE_CHANNELS).forEach((name, i) => {
          if (typeof name === 'string') modeLabels[i] = name
        })
      }
      if (cfg.ui && Array.isArray(cfg.ui.cal_labels)) {
        cfg.ui.cal_labels.slice(0, CAL_CHANNELS).forEach((name, i) => {
          if (typeof name === 'string') calLabels[i] = name
        })
      }
      _renderModeCalLabels()
    })
    .catch(() => { /* first boot – use form defaults */ })
}

// ── UI references ─────────────────────────────────────────────
const wsBadge    = document.getElementById('ws-status')
const sat1Badge  = document.getElementById('badge-sat1')
const sat2Badge  = document.getElementById('badge-sat2')
const telemBody  = document.getElementById('telem-body')
const telemFilterBtns = document.querySelectorAll('.btn-telem-filter')
const plotCanvas = document.getElementById('telem-plot')
const plotLegend = document.getElementById('plot-legend')
const ledElems = [
  document.getElementById('dbg-led-1'),
  document.getElementById('dbg-led-2'),
  document.getElementById('dbg-led-3'),
  document.getElementById('dbg-led-4'),
]
const modeFeedback = document.getElementById('mode-feedback')
const calFeedback  = document.getElementById('cal-feedback')
const settingsFeedback = document.getElementById('settings-feedback')
const peerList   = document.getElementById('peer-list')

// ── Tab switching ─────────────────────────────────────────────
const tabButtons = Array.from(document.querySelectorAll('.tab'))
const tabPanels = Array.from(document.querySelectorAll('.tab-panel'))
function _resolveInitialActiveTab() {
  const activeBtnTab = tabButtons.find(t => t.classList.contains('active'))?.dataset.tab
  if (activeBtnTab && document.getElementById(`tab-${activeBtnTab}`)) return activeBtnTab
  const firstValidBtnTab = tabButtons.find(t => t.dataset.tab && document.getElementById(`tab-${t.dataset.tab}`))?.dataset.tab
  return firstValidBtnTab || 'table'
}
let activeTab = _resolveInitialActiveTab()
tabButtons.forEach(btn => {
  btn.addEventListener('click', () => {
    if (btn.dataset.tab === activeTab) return
    tabButtons.forEach(t => t.classList.remove('active'))
    tabPanels.forEach(p => p.classList.remove('active'))
    btn.classList.add('active')
    document.getElementById(`tab-${btn.dataset.tab}`).classList.add('active')
    activeTab = btn.dataset.tab
    if (activeTab === 'table' && _telemRenderPending) _scheduleTelemetryRender(true)
    if (activeTab === 'plotter' && _plotDrawPending) _schedulePlotDraw(true)
  })
})

// ── Telemetry filter selector ───────────────────────────────
const defaultTelemBtn = document.querySelector('.btn-telem-filter.active')
if (defaultTelemBtn) telemFilter = defaultTelemBtn.dataset.filter || telemFilter
telemFilterBtns.forEach(btn => {
  btn.addEventListener('click', () => {
    telemFilterBtns.forEach(b => b.classList.remove('active'))
    btn.classList.add('active')
    telemFilter = btn.dataset.filter || 'both'
    _scheduleTelemetryRender(true)
    _updateLedIndicators()
    _schedulePlotDraw()
  })
})

// ── WebSocket status ──────────────────────────────────────────
wsOn('ws_open', () => {
  wsBadge.textContent = 'WS ●'
  wsBadge.className = 'badge online'
  // Load current config into settings form (Bug 2)
  _loadConfig()
})

wsOn('ws_close', () => {
  wsBadge.textContent = 'WS'
  wsBadge.className = 'badge offline'
  sat1Badge.className = 'badge offline'
  sat2Badge.className = 'badge offline'
  // Clear stale telemetry so the table shows fresh data after reconnect
  streams.clear()
  plotSeries.clear()
  plotSelection.clear()
  telemRows.clear()
  _telemRenderScheduled = false
  _telemRenderPending = false
  if (_telemRenderTimer) {
    clearTimeout(_telemRenderTimer)
    _telemRenderTimer = null
  }
  telemBody.innerHTML = ''
  _updateLedIndicators()
  _schedulePlotDraw()
})

// ── Peer status ────────────────────────────────────────────────
wsOn('peer_status', (msg) => {
  const peers = msg.peers || []
  peerList.innerHTML = ''
  peers.forEach(p => {
    // Header badge: show data path health when online, presence-only when offline
    const badge = p.role === 'SAT1' ? sat1Badge : sat2Badge
    const dataOk = p.online && p.data_path_ok
    let statusSymbol, badgeClass, statusTitle
    if (dataOk) {
      statusSymbol = '●'
      badgeClass = 'online'
      statusTitle = 'Online – data path OK'
    } else if (p.online) {
      statusSymbol = '◑'
      badgeClass = 'online-no-data'
      statusTitle = 'Online – data path STALE (no recent data)'
    } else {
      statusSymbol = '○'
      badgeClass = 'offline'
      statusTitle = 'Offline'
    }
    badge.textContent = `${p.name || p.role} ${statusSymbol}`
    badge.className = `badge ${badgeClass}`
    badge.title = statusTitle

    const div = document.createElement('div')
    div.className = 'peer-item'
    const roleTag = document.createElement('span')
    roleTag.className = `badge ${badgeClass}`
    roleTag.textContent = p.role || 'SAT'
    roleTag.title = statusTitle
    div.appendChild(roleTag)

    // Data-path indicator
    const dataTag = document.createElement('span')
    dataTag.className = `badge ${dataOk ? 'online' : 'offline'}`
    dataTag.style.fontSize = '10px'
    dataTag.textContent = dataOk ? 'DATA ●' : 'DATA ○'
    dataTag.title = dataOk ? 'Data path confirmed OK' : 'No recent data delivery'
    div.appendChild(dataTag)

    const nameInput = document.createElement('input')
    nameInput.className = 'peer-name-input'
    nameInput.type = 'text'
    nameInput.maxLength = 16
    nameInput.value = p.name || ''
    div.appendChild(nameInput)

    const macTag = document.createElement('span')
    macTag.style.color = 'var(--text-dim)'
    macTag.style.fontSize = '11px'
    macTag.textContent = p.mac || ''
    div.appendChild(macTag)

    const renameBtn = document.createElement('button')
    renameBtn.className = 'btn-peer-rename'
    renameBtn.textContent = 'Rename'
    div.appendChild(renameBtn)

    const deleteBtn = document.createElement('button')
    deleteBtn.className = 'btn-peer-delete btn-danger'
    deleteBtn.textContent = 'Delete'
    div.appendChild(deleteBtn)

    renameBtn.addEventListener('click', () => {
      const name = nameInput.value.trim()
      if (!name) {
        settingsFeedback.textContent = 'Peer name must not be empty'
        settingsFeedback.className = 'feedback error'
        return
      }
      if (!wsSend({ type: 'rename_peer', data: JSON.stringify({ mac: p.mac, name }) })) {
        settingsFeedback.textContent = 'Error: WebSocket not connected'
        settingsFeedback.className = 'feedback error'
        return
      }
      settingsFeedback.textContent = `Renamed peer ${p.mac}`
      settingsFeedback.className = 'feedback'
    })
    deleteBtn.addEventListener('click', () => {
      if (!confirm(`Delete peer ${p.name || p.role}?`)) return
      if (!wsSend({ type: 'delete_peer', data: JSON.stringify({ mac: p.mac }) })) {
        settingsFeedback.textContent = 'Error: WebSocket not connected'
        settingsFeedback.className = 'feedback error'
        return
      }
      settingsFeedback.textContent = `Deleted peer ${p.mac}`
      settingsFeedback.className = 'feedback'
    })
    peerList.appendChild(div)
  })
})

// ── Telemetry ──────────────────────────────────────────────────
wsOn('telemetry', (msg) => {
  const streamsArr = msg.streams || []
  streamsArr.forEach(s => {
    if (!s.name) return
    const role = parseInt(s.role, 10) || 0
    const key = `${role}:${s.name}`
    const numCurrent = Number(s.current)

    if (!plotSelection.has(key)) plotSelection.set(key, false)
    streams.set(key, {
      name: s.name,
      role,
      current: s.current,
      min: s.min,
      max: s.max,
    })

    const ledMatch = s.name.match(LED_STREAM_REGEX)
    if (ledMatch && (role === 1 || role === 2)) {
      const ledIdx = parseInt(ledMatch[1], 10) - 1
      ledStateByRole[role][ledIdx] = numCurrent >= LED_ON_THRESHOLD
    }

    if (Number.isFinite(numCurrent)) {
      let points = plotSeries.get(key)
      if (!points) {
        points = []
        plotSeries.set(key, points)
      }
      points.push(numCurrent)
      if (points.length > (MAX_PLOT_POINTS + MAX_PLOT_BUFFER)) {
        points.splice(0, points.length - MAX_PLOT_POINTS)
      }
    }
  })
  _scheduleTelemetryRender()
  _updateLedIndicators()
  _schedulePlotDraw()
})

function _scheduleTelemetryRender(force = false) {
  if (_telemRenderScheduled) {
    if (!force) return
    if (_telemRenderTimer) {
      clearTimeout(_telemRenderTimer)
      _telemRenderTimer = null
    }
  }
  if (!force && activeTab !== 'table') {
    _telemRenderPending = true
    return
  }
  _telemRenderScheduled = true
  const now = performance.now()
  const delay = force ? 0 : Math.max(0, TELEMETRY_RENDER_MIN_INTERVAL_MS - (now - _lastTelemetryRenderTs))
  _telemRenderTimer = setTimeout(() => {
    _telemRenderTimer = null
    requestAnimationFrame(() => {
      _telemRenderScheduled = false
      _telemRenderPending = false
      _lastTelemetryRenderTs = performance.now()
      _renderTelemetry()
    })
  }, delay)
}

function _createTelemetryRow() {
  const row = document.createElement('tr')
  const roleTd = document.createElement('td')
  const nameTd = document.createElement('td')
  const curTd = document.createElement('td')
  const minTd = document.createElement('td')
  const maxTd = document.createElement('td')
  const plotTd = document.createElement('td')
  const chk = document.createElement('input')

  curTd.className = 'value-current'
  minTd.className = 'value-dim'
  maxTd.className = 'value-dim'
  chk.type = 'checkbox'
  chk.className = 'plot-toggle'
  plotTd.appendChild(chk)

  row.appendChild(roleTd)
  row.appendChild(nameTd)
  row.appendChild(curTd)
  row.appendChild(minTd)
  row.appendChild(maxTd)
  row.appendChild(plotTd)

  return { row, roleTd, nameTd, curTd, minTd, maxTd, chk }
}

function _renderTelemetry() {
  const fmt = n => (typeof n === 'number' ? n.toFixed(3) : n)
  const selectedRole = telemFilter === 'both' ? null : parseInt(telemFilter, 10)
  const sorted = Array.from(streams.entries())
    .filter(([, s]) => selectedRole == null || s.role === selectedRole)
    .sort((a, b) => {
      const ra = a[1].role || 99
      const rb = b[1].role || 99
      if (ra !== rb) return ra - rb
      return a[1].name.localeCompare(b[1].name)
    })

  const frag = document.createDocumentFragment()
  sorted.forEach(([key, s]) => {
    const roleLabel = s.role ? `SAT${s.role}` : 'SAT'
    const checked = plotSelection.get(key) === true
    let rowRefs = telemRows.get(key)
    if (!rowRefs) {
      rowRefs = _createTelemetryRow()
      telemRows.set(key, rowRefs)
    }

    rowRefs.row.className = `telem-row role-${s.role || 0}`
    rowRefs.roleTd.className = `role-chip role-${s.role || 0}`
    rowRefs.roleTd.textContent = roleLabel
    rowRefs.nameTd.textContent = s.name
    rowRefs.curTd.textContent = fmt(s.current)
    rowRefs.minTd.textContent = fmt(s.min)
    rowRefs.maxTd.textContent = fmt(s.max)
    rowRefs.chk.dataset.streamKey = key
    if (rowRefs.chk.checked !== checked) rowRefs.chk.checked = checked
    frag.appendChild(rowRefs.row)
  })
  telemBody.replaceChildren(frag)
}

telemBody.addEventListener('change', (evt) => {
  const target = evt.target
  if (!(target instanceof HTMLInputElement)) return
  if (!target.classList.contains('plot-toggle')) return
  const key = target.dataset.streamKey
  if (!key) return
  plotSelection.set(key, target.checked)
  _schedulePlotDraw()
})

function _getLedDisplayRole() {
  if (telemFilter === '1' || telemFilter === '2') return parseInt(telemFilter, 10)
  return targetRole === 2 ? 2 : 1
}

function _updateLedIndicators() {
  const role = _getLedDisplayRole()
  const states = ledStateByRole[role] || [false, false, false, false]
  ledElems.forEach((el, i) => {
    if (!el) return
    el.classList.toggle('on', !!states[i])
  })
}

function _colorForSeriesKey(key) {
  let hash = 2166136261 // FNV-1a 32-bit offset basis
  for (let i = 0; i < key.length; i++) {
    hash ^= key.charCodeAt(i)
    hash = Math.imul(hash, 16777619) >>> 0 // FNV-1a 32-bit prime
  }
  const hue = hash % 360
  return `hsl(${hue} 85% 58%)`
}

function _schedulePlotDraw(force = false) {
  if (_plotDrawScheduled || !plotCanvas) return
  if (!force && activeTab !== 'plotter') {
    _plotDrawPending = true
    return
  }
  _plotDrawScheduled = true
  requestAnimationFrame(() => {
    _plotDrawScheduled = false
    _plotDrawPending = false
    _drawPlot()
  })
}

function _drawPlot() {
  if (!plotCanvas) return
  const ctx = plotCanvas.getContext('2d')
  if (!ctx) return

  const width = plotCanvas.clientWidth || plotCanvas.width
  const height = plotCanvas.clientHeight || plotCanvas.height
  if (plotCanvas.width !== width) plotCanvas.width = width
  if (plotCanvas.height !== height) plotCanvas.height = height

  ctx.clearRect(0, 0, width, height)
  ctx.fillStyle = '#080a0b'
  ctx.fillRect(0, 0, width, height)

  const selectedRole = telemFilter === 'both' ? null : parseInt(telemFilter, 10)
  const visibleKeys = []
  plotSelection.forEach((enabled, key) => {
    if (!enabled) return
    const role = parseInt((key.split(':')[0] || '0'), 10)
    if (selectedRole != null && role !== selectedRole) return
    const points = plotSeries.get(key)
    if (!points || points.length < 2) return
    visibleKeys.push(key)
  })

  if (visibleKeys.length === 0) {
    if (plotLegend) plotLegend.textContent = PLOT_EMPTY_MESSAGE
    return
  }

  let minY = Number.POSITIVE_INFINITY
  let maxY = Number.NEGATIVE_INFINITY
  visibleKeys.forEach(key => {
    const points = plotSeries.get(key) || []
    for (let i = 0; i < points.length; i++) {
      const p = points[i]
      if (p < minY) minY = p
      if (p > maxY) maxY = p
    }
  })
  if (!Number.isFinite(minY) || !Number.isFinite(maxY)) return
  if (minY === maxY) {
    minY -= 1
    maxY += 1
  }

  const padL = 36
  const padR = 12
  const padT = 12
  const padB = 20
  const plotW = width - padL - padR
  const plotH = height - padT - padB
  const xStep = plotW / (MAX_PLOT_POINTS - 1)
  const scaleY = plotH / (maxY - minY)

  ctx.strokeStyle = '#1a1f22'
  ctx.lineWidth = 1
  ctx.beginPath()
  for (let i = 0; i <= 4; i++) {
    const y = padT + (plotH * i / 4)
    ctx.moveTo(padL, y)
    ctx.lineTo(padL + plotW, y)
  }
  ctx.stroke()

  ctx.fillStyle = '#5a6a72'
  ctx.font = '11px Courier New, monospace'
  ctx.fillText(maxY.toFixed(2), 2, padT + 8)
  ctx.fillText(minY.toFixed(2), 2, padT + plotH)

  const legendFrag = document.createDocumentFragment()
  visibleKeys.forEach(key => {
    const points = plotSeries.get(key) || []
    const color = _colorForSeriesKey(key)
    ctx.strokeStyle = color
    ctx.lineWidth = 1.6
    ctx.beginPath()
    const start = Math.max(0, points.length - MAX_PLOT_POINTS)
    for (let i = start; i < points.length; i++) {
      const x = padL + (i - start) * xStep
      const y = padT + (maxY - points[i]) * scaleY
      if (i === start) ctx.moveTo(x, y)
      else ctx.lineTo(x, y)
    }
    ctx.stroke()
    if (plotLegend) {
      const item = document.createElement('span')
      item.className = 'plot-series'
      const chip = document.createElement('span')
      chip.className = 'plot-chip'
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

// ── ACK feedback ──────────────────────────────────────────────
wsOn('ack', (msg) => {
  const ok = msg.status === 0
  const label = `ACK seq=${msg.seq} ${ok ? '✓ OK' : '✗ ERR ' + msg.status}`
  ;[modeFeedback, calFeedback, settingsFeedback].forEach(el => {
    if (el.textContent.startsWith('Sent')) {
      el.textContent = label
      el.className = `feedback ${ok ? '' : 'error'}`
    }
  })
})

wsOn('error', (msg) => {
  settingsFeedback.textContent = msg.msg || 'Unknown error'
  settingsFeedback.className = 'feedback error'
})

// ── Global target selector (Bug 6) ────────────────────────────
const targetButtons = document.querySelectorAll('.target-sel-global .btn-target')
targetButtons.forEach(btn => {
  btn.addEventListener('click', () => {
    targetButtons.forEach(b => b.classList.remove('active'))
    btn.classList.add('active')
    targetRole = parseInt(btn.dataset.role, 10)
    _updateLedIndicators()
  })
})

// ── Control send helper ────────────────────────────────────────
function _sendCtrl(overrides = {}, force = false, includeSwitches = true) {
  const now = Date.now()
  if (!force && now - lastCmdTs < cmdRateLimitMs) return
  lastCmdTs = now

  let sw = 0
  if (includeSwitches) {
    const sw1 = document.getElementById('sw1').checked ? 1 : 0
    const sw2 = document.getElementById('sw2').checked ? 2 : 0
    const sw3 = document.getElementById('sw3').checked ? 4 : 0
    sw = sw1 | sw2 | sw3
  }

  wsSend({
    type: 'ctrl',
    data: JSON.stringify({
      speed: 0, angle: 0,
      sw,
      btn: 0,
      start: startActive ? 1 : 0,
      target: targetRole,
      ...overrides,
    })
  })
}

// ── Joystick (Bug 4 – replaces D-Pad) ─────────────────────────
const joystickCanvas = document.getElementById('joystick')
if (joystickCanvas) {
  const ctx = joystickCanvas.getContext('2d')
  const W = joystickCanvas.width
  const H = joystickCanvas.height
  const cx = W / 2
  const cy = H / 2
  const outerR = W / 2 - 4
  const thumbR = 18
  let thumbX = cx
  let thumbY = cy

  function _drawJoystick() {
    ctx.clearRect(0, 0, W, H)
    // Outer ring
    ctx.beginPath()
    ctx.arc(cx, cy, outerR, 0, Math.PI * 2)
    ctx.strokeStyle = '#2e363b'
    ctx.lineWidth = 2
    ctx.stroke()
    // Cross-hair lines
    ctx.beginPath()
    ctx.moveTo(cx, cy - outerR + 10)
    ctx.lineTo(cx, cy + outerR - 10)
    ctx.moveTo(cx - outerR + 10, cy)
    ctx.lineTo(cx + outerR - 10, cy)
    ctx.strokeStyle = '#1a1f22'
    ctx.lineWidth = 1
    ctx.stroke()
    // Thumb
    ctx.beginPath()
    ctx.arc(thumbX, thumbY, thumbR, 0, Math.PI * 2)
    ctx.fillStyle = joystickActive ? '#00e676' : '#5a6a72'
    ctx.fill()
    ctx.strokeStyle = joystickActive ? '#00c853' : '#2e363b'
    ctx.lineWidth = 2
    ctx.stroke()
  }

  function _getPointerPos(e) {
    const rect = joystickCanvas.getBoundingClientRect()
    const touch = e.touches ? e.touches[0] : e
    return {
      x: touch.clientX - rect.left,
      y: touch.clientY - rect.top
    }
  }

  function _updateJoystick(px, py) {
    let dx = px - cx
    let dy = py - cy
    const dist = Math.sqrt(dx * dx + dy * dy)
    const maxDist = outerR - thumbR

    // Clamp to outer circle
    if (dist > maxDist) {
      dx = (dx / dist) * maxDist
      dy = (dy / dist) * maxDist
    }

    thumbX = cx + dx
    thumbY = cy + dy

    // Map to speed and angle
    const maxSpeed = parseInt(document.getElementById('inp-speed').value, 10) || 100
    const normDist = Math.min(dist, maxDist) / maxDist
    joystickSpeed = Math.round(normDist * maxSpeed)

    // Angle: 0=up, 90=right, 180/-180=down, -90=left (mathematical convention)
    joystickAngle = Math.round(Math.atan2(dx, -dy) * (180 / Math.PI))

    // Update display
    document.getElementById('js-speed').textContent = joystickSpeed
    document.getElementById('js-angle').textContent = joystickAngle

    _drawJoystick()
  }

  function _startJoystick(e) {
    e.preventDefault()
    joystickActive = true
    const pos = _getPointerPos(e)
    _updateJoystick(pos.x, pos.y)
    _startCyclicSend()
  }

  function _moveJoystick(e) {
    if (!joystickActive) return
    e.preventDefault()
    const pos = _getPointerPos(e)
    _updateJoystick(pos.x, pos.y)
  }

  function _stopJoystick() {
    if (!joystickActive) return
    joystickActive = false
    joystickSpeed = 0
    joystickAngle = 0
    thumbX = cx
    thumbY = cy

    document.getElementById('js-speed').textContent = '0'
    document.getElementById('js-angle').textContent = '0'

    _drawJoystick()
    _stopCyclicSend()
    // Send stop command
    _sendCtrl({ speed: 0, angle: 0 })
  }

  function _startCyclicSend() {
    if (joystickInterval) return
    joystickInterval = setInterval(() => {
      if (joystickActive) {
        _sendCtrl({ speed: joystickSpeed, angle: joystickAngle }, false, false)
      }
    }, JOYSTICK_SEND_MS)
  }

  function _stopCyclicSend() {
    if (joystickInterval) {
      clearInterval(joystickInterval)
      joystickInterval = null
    }
  }

  // Mouse events
  joystickCanvas.addEventListener('mousedown', _startJoystick)
  document.addEventListener('mousemove', _moveJoystick)
  document.addEventListener('mouseup', _stopJoystick)

  // Touch events
  joystickCanvas.addEventListener('touchstart', _startJoystick)
  document.addEventListener('touchmove', _moveJoystick, { passive: false })
  document.addEventListener('touchend', _stopJoystick)

  // Initial draw
  _drawJoystick()
}

// ── Switch change listeners (Bug 3) ────────────────────────────
;['sw1', 'sw2', 'sw3'].forEach(id => {
  document.getElementById(id).addEventListener('change', () => {
    _sendCtrl({ speed: joystickSpeed, angle: joystickAngle }, true)
  })
})

// ── Speed input change listener (Bug 3) ────────────────────────
document.getElementById('inp-speed').addEventListener('change', () => {
  _sendCtrl({ speed: joystickSpeed, angle: joystickAngle }, true)
})

// ── Action buttons ─────────────────────────────────────────────
// Send button-pressed on mousedown/touchstart and button-released
// (btn: 0) on mouseup/touchend so every state change triggers a message.
document.querySelectorAll('.btn-action').forEach(btn => {
  const _press = () => {
    const b = 1 << (parseInt(btn.dataset.btn, 10) - 1)
    _sendCtrl({ btn: b }, true)
  }
  const _release = () => {
    _sendCtrl({ btn: 0 }, true)
  }
  btn.addEventListener('mousedown', _press)
  btn.addEventListener('touchstart', _press, { passive: true })
  btn.addEventListener('mouseup', _release)
  btn.addEventListener('touchend', _release)
})

// ── Start toggle ──────────────────────────────────────────────
document.getElementById('btn-start').addEventListener('click', function () {
  startActive = !startActive
  this.classList.toggle('active', startActive)
  this.textContent = startActive ? '■ STOP' : 'START'
  _sendCtrl({}, true)
})

// ── Mode buttons ──────────────────────────────────────────────
document.querySelectorAll('.btn-mode').forEach(btn => {
  btn.addEventListener('click', () => {
    const modeId = parseInt(btn.dataset.mode, 10)
    if (!wsSend({
      type: 'mode',
      data: JSON.stringify({ mode_id: modeId, target: targetRole })
    })) {
      modeFeedback.textContent = 'Error: WebSocket not connected'
      modeFeedback.className = 'feedback error'
      return
    }
    modeFeedback.textContent = `Sent mode ${modeId}…`
    modeFeedback.className = 'feedback'
  })
})

// ── Calibrate buttons ─────────────────────────────────────────
document.querySelectorAll('.btn-cal').forEach(btn => {
  btn.addEventListener('click', () => {
    const calCmd = parseInt(btn.dataset.cal, 10)
    if (!wsSend({
      type: 'cal',
      data: JSON.stringify({ cal_cmd: calCmd, target: targetRole })
    })) {
      calFeedback.textContent = 'Error: WebSocket not connected'
      calFeedback.className = 'feedback error'
      return
    }
    calFeedback.textContent = `Sent cal cmd ${calCmd}…`
    calFeedback.className = 'feedback'
  })
})

function _saveUiLabels() {
  const channel   = parseInt(document.getElementById('cfg-channel').value, 10)
  const pmk       = document.getElementById('cfg-pmk').value.trim()
  const hz        = parseInt(document.getElementById('cfg-telem-hz').value, 10)
  const networkId = parseInt(document.getElementById('cfg-network-id').value, 10)
  return wsSend({
    type: 'settings',
    data: JSON.stringify({
      channel, pmk, telemetry_max_hz: hz, network_id: networkId,
      mode_labels: modeLabels,
      cal_labels: calLabels
    })
  })
}

document.getElementById('btn-mode-label-add').addEventListener('click', () => {
  const channel = Math.max(1, Math.min(MODE_CHANNELS, parseInt(document.getElementById('mode-label-channel').value, 10) || 1))
  const name = document.getElementById('mode-label-text').value.trim()
  if (!name) {
    modeFeedback.textContent = 'Name required'
    modeFeedback.className = 'feedback error'
    return
  }
  modeLabels[channel - 1] = name
  _renderModeCalLabels()
  if (!_saveUiLabels()) {
    modeFeedback.textContent = 'Error: WebSocket not connected'
    modeFeedback.className = 'feedback error'
    return
  }
  modeFeedback.textContent = `Saved mode label for channel ${channel}`
  modeFeedback.className = 'feedback'
})

document.getElementById('btn-cal-label-add').addEventListener('click', () => {
  const channel = Math.max(1, Math.min(CAL_CHANNELS, parseInt(document.getElementById('cal-label-channel').value, 10) || 1))
  const name = document.getElementById('cal-label-text').value.trim()
  if (!name) {
    calFeedback.textContent = 'Name required'
    calFeedback.className = 'feedback error'
    return
  }
  calLabels[channel - 1] = name
  _renderModeCalLabels()
  if (!_saveUiLabels()) {
    calFeedback.textContent = 'Error: WebSocket not connected'
    calFeedback.className = 'feedback error'
    return
  }
  calFeedback.textContent = `Saved cal label for channel ${channel}`
  calFeedback.className = 'feedback'
})

// ── Settings ──────────────────────────────────────────────────
let _settingsAckTimer = null

document.getElementById('btn-save-cfg').addEventListener('click', () => {
  const channel   = parseInt(document.getElementById('cfg-channel').value, 10)
  const pmk       = document.getElementById('cfg-pmk').value.trim()
  const hz        = parseInt(document.getElementById('cfg-telem-hz').value, 10)
  const networkId = parseInt(document.getElementById('cfg-network-id').value, 10)

  if (!wsSend({
    type: 'settings',
    data: JSON.stringify({
      channel, pmk, telemetry_max_hz: hz, network_id: networkId,
      mode_labels: modeLabels,
      cal_labels: calLabels
    })
  })) {
    settingsFeedback.textContent = 'Error: WebSocket not connected'
    settingsFeedback.className = 'feedback error'
    return
  }

  settingsFeedback.textContent = 'Sent settings…'
  settingsFeedback.className = 'feedback'

  // Timeout: show error if no ACK arrives within 5 s
  if (_settingsAckTimer) clearTimeout(_settingsAckTimer)
  _settingsAckTimer = setTimeout(() => {
    if (settingsFeedback.textContent === 'Sent settings…') {
      settingsFeedback.textContent = 'Timeout – no ACK received'
      settingsFeedback.className = 'feedback error'
    }
  }, 5000)
})

document.getElementById('btn-download-cfg').addEventListener('click', () => {
  fetch('/api/config_export')
    .then(r => {
      if (!r.ok) throw new Error('download failed')
      return r.text()
    })
    .then(text => {
      const blob = new Blob([text], { type: 'application/json' })
      const url = URL.createObjectURL(blob)
      const a = document.createElement('a')
      a.href = url
      a.download = 'hub-config-export.json'
      document.body.appendChild(a)
      a.click()
      document.body.removeChild(a)
      URL.revokeObjectURL(url)
      settingsFeedback.textContent = 'Config JSON downloaded'
      settingsFeedback.className = 'feedback'
    })
    .catch(() => {
      settingsFeedback.textContent = 'Download failed'
      settingsFeedback.className = 'feedback error'
    })
})

// Clear ACK timeout when a settings ACK arrives
wsOn('ack', (msg) => {
  if (_settingsAckTimer && msg.msg_type === 9) {
    clearTimeout(_settingsAckTimer)
    _settingsAckTimer = null
  }
})

// ── Scan for peers ────────────────────────────────────────────
const scanResults = document.getElementById('scan-results')

document.getElementById('btn-scan').addEventListener('click', () => {
  scanResults.innerHTML = 'Scanning…'
  wsSend({ type: 'pair', data: JSON.stringify({ action: 0, role: 0, name: '' }) })
})

wsOn('scan_result', (msg) => {
  // Remove placeholder text on first result
  if (scanResults.textContent === 'Scanning…') {
    scanResults.innerHTML = ''
  }
  const item = document.createElement('div')
  item.className = 'scan-item'
  item.innerHTML = `
    <span>${msg.name || '—'}</span>
    <span style="color:var(--text-dim);font-size:11px">${msg.mac}</span>
    <span style="color:var(--text-dim);font-size:11px">ch${msg.channel}</span>
    <button class="btn-use-peer">Use</button>
  `
  item.querySelector('.btn-use-peer').addEventListener('click', () => {
    wsSend({
      type: 'add_peer',
      data: JSON.stringify({ mac: msg.mac, name: msg.name, role: msg.role })
    })
  })
  scanResults.appendChild(item)
})

// ── Manual peer add ───────────────────────────────────────────
document.getElementById('btn-add-peer').addEventListener('click', () => {
  const mac  = document.getElementById('cfg-peer-mac').value.trim().toUpperCase()
  const name = document.getElementById('cfg-peer-name').value.trim() || 'SAT'
  const role = parseInt(document.getElementById('cfg-peer-role').value, 10)
  if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(mac)) {
    settingsFeedback.textContent = 'Invalid MAC (use XX:XX:XX:XX:XX:XX)'
    settingsFeedback.className = 'feedback error'
    return
  }
  wsSend({ type: 'add_peer', data: JSON.stringify({ mac, name, role }) })
  settingsFeedback.textContent = `Peer ${name} (${mac}) added`
  settingsFeedback.className = 'feedback'
})

document.getElementById('btn-factory-reset').addEventListener('click', () => {
  if (!confirm('Factory reset? All settings will be lost.')) return
  fetch('/api/factory_reset', { method: 'POST' })
    .then(() => { settingsFeedback.textContent = 'Factory reset – rebooting…' })
    .catch(() => { settingsFeedback.textContent = 'Reset failed' })
})

document.getElementById('btn-reset-peers').addEventListener('click', () => {
  if (!confirm('Delete all connected satellites from this hub?')) return
  if (!wsSend({ type: 'clear_peers', data: '{}' })) {
    settingsFeedback.textContent = 'Error: WebSocket not connected'
    settingsFeedback.className = 'feedback error'
    return
  }
  settingsFeedback.textContent = 'All peers removed'
  settingsFeedback.className = 'feedback'
})

// ── Boot ──────────────────────────────────────────────────────
_renderModeCalLabels()
wsConnect()
