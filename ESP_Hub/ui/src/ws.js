// ============================================================
//  ESP-NOW Hub UI – WebSocket client
//  Manages connection, reconnect, message dispatch
// ============================================================

const WS_URL = (() => {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws'
  return `${proto}://${location.host}/ws`
})()

const RECONNECT_DELAY = 3000

let socket = null
let listeners = {}
let connected = false
let _reconnectTimer = null

export function wsConnect() {
  // Cancel any pending reconnect timer before creating a new connection
  if (_reconnectTimer !== null) {
    clearTimeout(_reconnectTimer)
    _reconnectTimer = null
  }

  // Close stale socket so the server doesn't count it as an extra client (Bug 2)
  if (socket) {
    try { socket.close() } catch (_) { /* ignore */ }
    socket = null
  }

  socket = new WebSocket(WS_URL)

  socket.onopen = () => {
    connected = true
    _emit('ws_open')
    // Request initial peer status
    wsSend({ type: 'get_status' })
  }

  socket.onclose = () => {
    connected = false
    _emit('ws_close')
    _reconnectTimer = setTimeout(wsConnect, RECONNECT_DELAY)
  }

  socket.onerror = (e) => {
    console.warn('[WS] error', e)
  }

  socket.onmessage = (evt) => {
    try {
      const msg = JSON.parse(evt.data)
      _emit(msg.type, msg)
      _emit('raw', evt.data)
    } catch {
      _emit('raw', evt.data)
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
