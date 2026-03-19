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
const streams = new Map()

// ── Load saved config into settings form (Bug 2) ──────────────
function _loadConfig() {
  fetch('/api/config')
    .then(r => { if (r.ok) return r.json(); throw new Error('no config') })
    .then(cfg => {
      if (cfg.channel != null)
        document.getElementById('cfg-channel').value = cfg.channel
      if (cfg.telemetry && cfg.telemetry.max_rate_hz != null)
        document.getElementById('cfg-telem-hz').value = cfg.telemetry.max_rate_hz
    })
    .catch(() => { /* first boot – use form defaults */ })
}

// ── UI references ─────────────────────────────────────────────
const wsBadge    = document.getElementById('ws-status')
const sat1Badge  = document.getElementById('badge-sat1')
const sat2Badge  = document.getElementById('badge-sat2')
const telemBody  = document.getElementById('telem-body')
const rawMonitor = document.getElementById('raw-monitor')
const monitorPause = document.getElementById('monitor-pause')
const modeFeedback = document.getElementById('mode-feedback')
const calFeedback  = document.getElementById('cal-feedback')
const settingsFeedback = document.getElementById('settings-feedback')
const peerList   = document.getElementById('peer-list')

// ── Tab switching ─────────────────────────────────────────────
document.querySelectorAll('.tab').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'))
    document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'))
    btn.classList.add('active')
    document.getElementById(`tab-${btn.dataset.tab}`).classList.add('active')
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
  telemBody.innerHTML = ''
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
    streams.set(s.name, { current: s.current, min: s.min, max: s.max })
  })
  _renderTelemetry()
})

function _renderTelemetry() {
  const rows = []
  streams.forEach((v, name) => {
    const fmt = n => (typeof n === 'number' ? n.toFixed(3) : n)
    rows.push(`<tr>
      <td>${name}</td>
      <td style="color:var(--green)">${fmt(v.current)}</td>
      <td style="color:var(--text-dim)">${fmt(v.min)}</td>
      <td style="color:var(--text-dim)">${fmt(v.max)}</td>
    </tr>`)
  })
  telemBody.innerHTML = rows.join('')
}

// ── Raw monitor ────────────────────────────────────────────────
wsOn('raw', (data) => {
  if (monitorPause.checked) return
  const line = document.createElement('div')
  line.textContent = data
  rawMonitor.appendChild(line)
  // Keep at most 200 lines
  while (rawMonitor.children.length > 200) {
    rawMonitor.removeChild(rawMonitor.firstChild)
  }
  rawMonitor.scrollTop = rawMonitor.scrollHeight
})

document.getElementById('monitor-clear').addEventListener('click', () => {
  rawMonitor.innerHTML = ''
})

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
document.querySelectorAll('.btn-target').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.btn-target').forEach(b => b.classList.remove('active'))
    btn.classList.add('active')
    targetRole = parseInt(btn.dataset.role, 10)
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
document.querySelectorAll('.btn-action').forEach(btn => {
  btn.addEventListener('click', () => {
    const b = 1 << (parseInt(btn.dataset.btn, 10) - 1)
    _sendCtrl({ btn: b }, true)
  })
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

// ── Settings ──────────────────────────────────────────────────
let _settingsAckTimer = null

document.getElementById('btn-save-cfg').addEventListener('click', () => {
  const channel = parseInt(document.getElementById('cfg-channel').value, 10)
  const pmk     = document.getElementById('cfg-pmk').value.trim()
  const hz      = parseInt(document.getElementById('cfg-telem-hz').value, 10)

  if (!wsSend({
    type: 'settings',
    data: JSON.stringify({ channel, pmk, telemetry_max_hz: hz })
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
wsConnect()
