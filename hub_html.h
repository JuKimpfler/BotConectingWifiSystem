#pragma once

// ══════════════════════════════════════════════════════
//  Webinterface HTML – im Flash gespeichert (PROGMEM)
//  Terminal-Ästhetik, vollständig offline-fähig
// ══════════════════════════════════════════════════════
const char HUB_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP Mesh Hub</title>
<style>
  :root {
    --bg:        #0c0e0f;
    --surface:   #131618;
    --border:    #252a2d;
    --border-hi: #2e363b;
    --green:     #00e676;
    --green-dim: #00c853;
    --amber:     #ffab00;
    --amber-dim: #ff8f00;
    --blue:      #40c4ff;
    --red:       #ff5252;
    --text:      #cdd6dc;
    --text-dim:  #5a6a72;
    --text-hi:   #eaf2f5;
    --font-mono: 'Courier New', 'Lucida Console', monospace;
  }

  * { box-sizing: border-box; margin: 0; padding: 0; }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font-mono);
    font-size: 13px;
    min-height: 100vh;
    display: flex;
    flex-direction: column;
    overflow-x: hidden;
  }

  /* ── Scanlines ── */
  body::before {
    content: '';
    position: fixed;
    inset: 0;
    background: repeating-linear-gradient(
      0deg,
      transparent,
      transparent 2px,
      rgba(0,0,0,0.04) 2px,
      rgba(0,0,0,0.04) 4px
    );
    pointer-events: none;
    z-index: 1000;
  }

  /* ── Header ── */
  header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 12px 20px;
    border-bottom: 1px solid var(--border);
    background: var(--surface);
  }

  .logo {
    display: flex;
    align-items: center;
    gap: 10px;
  }

  .logo-icon {
    width: 28px; height: 28px;
    border: 2px solid var(--green);
    border-radius: 4px;
    display: flex; align-items: center; justify-content: center;
    position: relative;
  }
  .logo-icon::before {
    content: '';
    width: 10px; height: 10px;
    background: var(--green);
    border-radius: 50%;
    animation: pulse 2s ease-in-out infinite;
  }
  @keyframes pulse {
    0%,100% { opacity: 1; transform: scale(1); }
    50%      { opacity: 0.4; transform: scale(0.8); }
  }

  .logo-text {
    font-size: 14px;
    font-weight: bold;
    color: var(--text-hi);
    letter-spacing: 2px;
    text-transform: uppercase;
  }
  .logo-sub {
    font-size: 10px;
    color: var(--text-dim);
    letter-spacing: 1px;
  }

  .header-status {
    display: flex;
    gap: 16px;
    align-items: center;
  }

  .status-dot {
    display: flex;
    align-items: center;
    gap: 5px;
    font-size: 11px;
    color: var(--text-dim);
  }
  .dot {
    width: 7px; height: 7px;
    border-radius: 50%;
    background: var(--text-dim);
  }
  .dot.on  { background: var(--green); box-shadow: 0 0 6px var(--green); }
  .dot.off { background: var(--red); }

  #conn-status { font-size: 11px; }

  /* ── Main Layout ── */
  .main {
    flex: 1;
    display: grid;
    grid-template-rows: 1fr auto;
    gap: 0;
    padding: 16px;
    max-width: 1200px;
    width: 100%;
    margin: 0 auto;
  }

  .panels {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    margin-bottom: 12px;
  }

  /* ── Panel ── */
  .panel {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 4px;
    display: flex;
    flex-direction: column;
    height: 340px;
    position: relative;
    overflow: hidden;
  }

  .panel::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
  }
  .panel-esp1::before { background: var(--green); }
  .panel-esp3::before { background: var(--amber); }

  .panel-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 12px;
    border-bottom: 1px solid var(--border);
    flex-shrink: 0;
  }

  .panel-title {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 11px;
    font-weight: bold;
    letter-spacing: 1.5px;
    text-transform: uppercase;
  }
  .panel-esp1 .panel-title { color: var(--green); }
  .panel-esp3 .panel-title { color: var(--amber); }

  .panel-badge {
    font-size: 10px;
    padding: 1px 6px;
    border-radius: 2px;
    color: var(--bg);
    font-weight: bold;
  }
  .panel-esp1 .panel-badge { background: var(--green); }
  .panel-esp3 .panel-badge { background: var(--amber); }

  .msg-count {
    font-size: 10px;
    color: var(--text-dim);
  }

  .panel-body {
    flex: 1;
    overflow-y: auto;
    padding: 8px 12px;
    font-size: 12px;
    line-height: 1.7;
    scrollbar-width: thin;
    scrollbar-color: var(--border-hi) transparent;
  }

  .panel-body::-webkit-scrollbar { width: 4px; }
  .panel-body::-webkit-scrollbar-track { background: transparent; }
  .panel-body::-webkit-scrollbar-thumb { background: var(--border-hi); }

  /* ── Log Zeilen ── */
  .log-line {
    display: flex;
    gap: 8px;
    padding: 2px 0;
    border-bottom: 1px solid rgba(255,255,255,0.02);
    animation: fadeIn 0.15s ease;
  }
  @keyframes fadeIn {
    from { opacity: 0; transform: translateX(-4px); }
    to   { opacity: 1; transform: translateX(0); }
  }

  .log-time {
    color: var(--text-dim);
    flex-shrink: 0;
    font-size: 11px;
    padding-top: 1px;
  }

  .log-id {
    flex-shrink: 0;
    font-size: 10px;
    padding: 1px 5px;
    border-radius: 2px;
    align-self: flex-start;
    margin-top: 2px;
    letter-spacing: 0.5px;
  }
  .panel-esp1 .log-id { background: rgba(0,230,118,0.12); color: var(--green-dim); }
  .panel-esp3 .log-id { background: rgba(255,171,0,0.12);  color: var(--amber-dim); }

  .log-payload {
    color: var(--text-hi);
    word-break: break-all;
    flex: 1;
  }

  /* ── Input Panel ── */
  .input-panel {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 4px;
    overflow: hidden;
    position: relative;
  }
  .input-panel::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    background: var(--blue);
  }

  .input-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 12px;
    border-bottom: 1px solid var(--border);
    font-size: 11px;
    letter-spacing: 1.5px;
    text-transform: uppercase;
    color: var(--blue);
    font-weight: bold;
  }

  .target-badges {
    display: flex;
    gap: 6px;
  }
  .target-badge {
    font-size: 10px;
    padding: 1px 8px;
    border-radius: 2px;
    color: var(--bg);
    font-weight: bold;
  }
  .tb-esp1 { background: var(--green); }
  .tb-esp3 { background: var(--amber); }

  .input-row {
    display: flex;
    align-items: stretch;
    gap: 0;
  }

  .prompt {
    display: flex;
    align-items: center;
    padding: 0 10px 0 14px;
    color: var(--blue);
    font-size: 14px;
    flex-shrink: 0;
    user-select: none;
  }

  #msg-input {
    flex: 1;
    background: transparent;
    border: none;
    outline: none;
    color: var(--text-hi);
    font-family: var(--font-mono);
    font-size: 13px;
    padding: 12px 0;
    caret-color: var(--blue);
  }
  #msg-input::placeholder { color: var(--text-dim); }

  #send-btn {
    background: transparent;
    border: none;
    border-left: 1px solid var(--border);
    color: var(--blue);
    font-family: var(--font-mono);
    font-size: 11px;
    font-weight: bold;
    letter-spacing: 1px;
    padding: 0 20px;
    cursor: pointer;
    text-transform: uppercase;
    transition: background 0.15s, color 0.15s;
    flex-shrink: 0;
  }
  #send-btn:hover {
    background: rgba(64,196,255,0.12);
    color: var(--text-hi);
  }
  #send-btn:active { background: rgba(64,196,255,0.25); }

  .char-count {
    padding: 4px 14px;
    font-size: 10px;
    color: var(--text-dim);
    display: flex;
    justify-content: space-between;
  }
  .char-count.warn { color: var(--red); }

  /* ── Footer ── */
  footer {
    border-top: 1px solid var(--border);
    background: var(--surface);
    padding: 5px 20px;
    display: flex;
    gap: 20px;
    font-size: 10px;
    color: var(--text-dim);
    letter-spacing: 0.5px;
  }
  footer span { display: flex; gap: 5px; }
  footer b { color: var(--text); }

  /* ── Responsive ── */
  @media (max-width: 680px) {
    .panels { grid-template-columns: 1fr; }
    .header-status { display: none; }
  }
</style>
</head>
<body>

<header>
  <div class="logo">
    <div class="logo-icon"></div>
    <div>
      <div class="logo-text">ESP Mesh Hub</div>
      <div class="logo-sub">ESP32-C3 // ESP-NOW v2</div>
    </div>
  </div>
  <div class="header-status">
    <div class="status-dot">
      <div class="dot on" id="dot-esp1"></div>
      ESP_1
    </div>
    <div class="status-dot">
      <div class="dot on" id="dot-esp3"></div>
      ESP_3
    </div>
    <div class="status-dot">
      <div class="dot off" id="dot-ws"></div>
      <span id="conn-status">VERBINDE...</span>
    </div>
  </div>
</header>

<div class="main">
  <div class="panels">

    <!-- ESP_1 Panel -->
    <div class="panel panel-esp1">
      <div class="panel-header">
        <div class="panel-title">
          <svg width="12" height="12" viewBox="0 0 12 12" fill="none">
            <rect x="1" y="1" width="10" height="10" rx="1"
                  stroke="currentColor" stroke-width="1.5"/>
            <path d="M4 6h4M6 4v4" stroke="currentColor" stroke-width="1.5"
                  stroke-linecap="round"/>
          </svg>
          Empfang von ESP_1
        </div>
        <div style="display:flex;gap:8px;align-items:center">
          <span class="msg-count" id="count-esp1">0 Msg</span>
          <span class="panel-badge">NODE 1</span>
        </div>
      </div>
      <div class="panel-body" id="log-esp1">
        <div class="log-line">
          <span class="log-time">--:--:--</span>
          <span class="log-id">SYS</span>
          <span class="log-payload" style="color:var(--text-dim)">
            Warte auf Nachrichten von ESP_1...
          </span>
        </div>
      </div>
    </div>

    <!-- ESP_3 Panel -->
    <div class="panel panel-esp3">
      <div class="panel-header">
        <div class="panel-title">
          <svg width="12" height="12" viewBox="0 0 12 12" fill="none">
            <circle cx="6" cy="6" r="4.5" stroke="currentColor" stroke-width="1.5"/>
            <path d="M6 3.5v2.5l1.5 1.5" stroke="currentColor" stroke-width="1.5"
                  stroke-linecap="round"/>
          </svg>
          Empfang von ESP_3
        </div>
        <div style="display:flex;gap:8px;align-items:center">
          <span class="msg-count" id="count-esp3">0 Msg</span>
          <span class="panel-badge">NODE 3</span>
        </div>
      </div>
      <div class="panel-body" id="log-esp3">
        <div class="log-line">
          <span class="log-time">--:--:--</span>
          <span class="log-id">SYS</span>
          <span class="log-payload" style="color:var(--text-dim)">
            Warte auf Nachrichten von ESP_3...
          </span>
        </div>
      </div>
    </div>

  </div>

  <!-- Senden Panel -->
  <div class="input-panel">
    <div class="input-header">
      <span>&#9658; Nachricht senden</span>
      <div class="target-badges">
        <span class="target-badge tb-esp1">→ ESP_1</span>
        <span class="target-badge tb-esp3">→ ESP_3</span>
      </div>
    </div>
    <div class="input-row">
      <span class="prompt">&#62;_</span>
      <input type="text" id="msg-input"
             placeholder="Text eingeben und Enter drücken (max. 9 Zeichen)..."
             maxlength="9" autocomplete="off" spellcheck="false">
      <button id="send-btn" onclick="sendMsg()">SENDEN</button>
    </div>
    <div class="char-count" id="char-info">
      <span>Drücke <b>Enter</b> zum Senden</span>
      <span id="char-count">0 / 9</span>
    </div>
  </div>
</div>

<footer>
  <span>TX: <b id="f-sent">0</b></span>
  <span>RX: <b id="f-recv">0</b></span>
  <span>FAIL: <b id="f-fail">0</b></span>
  <span>DROP: <b id="f-drop">0</b></span>
  <span style="margin-left:auto">KANAL: <b id="f-ch">6</b></span>
  <span>IP: <b>192.168.4.1</b></span>
</footer>

<script>
// ── Zähler ──────────────────────────────────────────
const counts = { esp1: 0, esp3: 0 };
const MAX_LOG_LINES = 60;

// ── WebSocket ───────────────────────────────────────
let ws, reconnTimer;

function connect() {
  ws = new WebSocket('ws://' + location.hostname + '/ws');

  ws.onopen = () => {
    document.getElementById('dot-ws').className = 'dot on';
    document.getElementById('conn-status').textContent = 'VERBUNDEN';
    clearTimeout(reconnTimer);
  };

  ws.onclose = () => {
    document.getElementById('dot-ws').className = 'dot off';
    document.getElementById('conn-status').textContent = 'GETRENNT';
    reconnTimer = setTimeout(connect, 2000);
  };

  ws.onerror = () => ws.close();

  ws.onmessage = (evt) => {
    try {
      const d = JSON.parse(evt.data);
      switch (d.type) {
        case 'msg':
          appendLog(d.from, d.id, d.payload, d.ts);
          break;
        case 'stats':
          document.getElementById('f-sent').textContent = d.sent;
          document.getElementById('f-recv').textContent = d.recv;
          document.getElementById('f-fail').textContent = d.fail;
          document.getElementById('f-drop').textContent = d.drop;
          break;
      }
    } catch(e) {}
  };
}

// ── Log-Zeile hinzufügen ────────────────────────────
function appendLog(from, id, payload, ts) {
  const panelId  = (from === 1) ? 'log-esp1' : 'log-esp3';
  const countId  = (from === 1) ? 'count-esp1' : 'count-esp3';
  const dotId    = (from === 1) ? 'dot-esp1' : 'dot-esp3';
  const key      = (from === 1) ? 'esp1' : 'esp3';

  const log = document.getElementById(panelId);

  // Ersten Platzhalter entfernen
  const placeholder = log.querySelector('.log-payload[style*="text-dim"]');
  if (placeholder) placeholder.closest('.log-line').remove();

  counts[key]++;
  document.getElementById(countId).textContent = counts[key] + ' Msg';

  // Dot blinken
  const dot = document.getElementById(dotId);
  dot.style.background = '#fff';
  setTimeout(() => dot.className = 'dot on', 120);

  // Zeitstempel
  const now = new Date();
  const time = now.toTimeString().slice(0,8);

  const line = document.createElement('div');
  line.className = 'log-line';
  line.innerHTML =
    '<span class="log-time">' + time + '</span>' +
    '<span class="log-id">#' + String(id).padStart(4,'0') + '</span>' +
    '<span class="log-payload">' + escHtml(payload) + '</span>';

  log.appendChild(line);

  // Max Zeilen begrenzen
  while (log.children.length > MAX_LOG_LINES) {
    log.removeChild(log.firstChild);
  }

  // Auto-Scroll
  log.scrollTop = log.scrollHeight;
}

// ── Nachricht senden ────────────────────────────────
function sendMsg() {
  const input = document.getElementById('msg-input');
  const text  = input.value.trim();
  if (!text || !ws || ws.readyState !== WebSocket.OPEN) return;

  ws.send(JSON.stringify({ type: 'send', payload: text }));
  input.value = '';
  updateCharCount();
}

// ── Char-Counter ────────────────────────────────────
function updateCharCount() {
  const input = document.getElementById('msg-input');
  const len   = input.value.length;
  const el    = document.getElementById('char-count');
  const info  = document.getElementById('char-info');
  el.textContent = len + ' / 9';
  info.className = 'char-count' + (len >= 9 ? ' warn' : '');
}

document.getElementById('msg-input').addEventListener('input', updateCharCount);
document.getElementById('msg-input').addEventListener('keydown', (e) => {
  if (e.key === 'Enter') sendMsg();
});

// ── HTML escapen ────────────────────────────────────
function escHtml(s) {
  return String(s)
    .replace(/&/g,'&amp;')
    .replace(/</g,'&lt;')
    .replace(/>/g,'&gt;');
}

// ── Start ───────────────────────────────────────────
connect();
</script>
</body>
</html>
)rawhtml";
