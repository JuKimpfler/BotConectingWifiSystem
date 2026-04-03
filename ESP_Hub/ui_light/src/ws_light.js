// ============================================================
//  ESP-NOW Hub Light UI – WebSocket client
//  No telemetry dispatch throttle: messages are emitted immediately
//  so the RAF-driven plotter gets each update within its render frame.
// ============================================================

const WS_URL = (() => {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${location.host}/ws`
})()

const RECONNECT_DELAY_MS = 3000

let socket = null
let listeners = {}
let connected = false
let _reconnectTimer = null

export function wsConnect() {
  if (_reconnectTimer !== null) {
    clearTimeout(_reconnectTimer)
    _reconnectTimer = null
  }
  if (socket) {
    try { socket.close() } catch (_) { /* ignore */ }
    socket = null
  }

  socket = new WebSocket(WS_URL)

  socket.onopen = () => {
    connected = true
    _emit('ws_open')
    // Ask hub for current peer status
    wsSend({ type: 'get_status' })
  }

  socket.onclose = () => {
    connected = false
    _emit('ws_close')
    _reconnectTimer = setTimeout(wsConnect, RECONNECT_DELAY_MS)
  }

  socket.onerror = (e) => {
    console.warn('[WS] error', e)
  }

  socket.onmessage = (evt) => {
    try {
      const msg = JSON.parse(evt.data)
      // Dispatch immediately – no throttle; RAF loop handles render rate.
      _emit(msg.type, msg)
    } catch {
      // ignore non-JSON frames
    }
  }
}

export function wsSend(obj) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(obj))
    return true
  }
  return false
}

export function wsOn(event, cb) {
  if (!listeners[event]) listeners[event] = []
  listeners[event].push(cb)
}

export function isConnected() {
  return connected
}

function _emit(event, data) {
  if (listeners[event]) {
    listeners[event].forEach(cb => cb(data))
  }
}
