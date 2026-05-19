(() => {
  const params = new URLSearchParams(window.location.search)
  const role = (params.get('role') || 'SAT1').toUpperCase()
  const targetInfo = document.getElementById('target-info')
  const statusBadge = document.getElementById('status')
  const speedValue = document.getElementById('speed-value')
  const angleValue = document.getElementById('angle-value')
  const startBtn = document.getElementById('start-btn')
  const canvas = document.getElementById('joystick')
  const ctx = canvas.getContext('2d')
  const center = canvas.width / 2
  const radius = center - 18
  const knobRadius = 34
  let knobX = center
  let knobY = center
  let dragging = false
  let startActive = false
  let currentSpeed = 0
  let currentAngle = 0
  let ws
  let sendTimer = null

  targetInfo.textContent = `Target: ${role}`

  const wsUrl = `${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`

  function draw() {
    ctx.clearRect(0, 0, canvas.width, canvas.height)
    ctx.strokeStyle = '#252a2d'
    ctx.lineWidth = 2
    ctx.beginPath()
    ctx.arc(center, center, radius, 0, Math.PI * 2)
    ctx.stroke()
    ctx.beginPath()
    ctx.moveTo(center, 16)
    ctx.lineTo(center, canvas.height - 16)
    ctx.moveTo(16, center)
    ctx.lineTo(canvas.width - 16, center)
    ctx.stroke()
    ctx.fillStyle = 'rgba(0,230,118,0.18)'
    ctx.beginPath()
    ctx.arc(center, center, radius - 2, 0, Math.PI * 2)
    ctx.fill()
    ctx.fillStyle = '#00e676'
    ctx.beginPath()
    ctx.arc(knobX, knobY, knobRadius, 0, Math.PI * 2)
    ctx.fill()
  }

  function normalize(pointerX, pointerY) {
    let dx = pointerX - center
    let dy = pointerY - center
    const distance = Math.hypot(dx, dy)
    if (distance > radius - knobRadius) {
      const factor = (radius - knobRadius) / distance
      dx *= factor
      dy *= factor
    }
    knobX = center + dx
    knobY = center + dy
    currentAngle = Math.round((dx / (radius - knobRadius)) * 255)
    currentSpeed = Math.round((-dy / (radius - knobRadius)) * 255)
    speedValue.textContent = String(currentSpeed)
    angleValue.textContent = String(currentAngle)
    draw()
  }

  function resetStick() {
    knobX = center
    knobY = center
    currentSpeed = 0
    currentAngle = 0
    speedValue.textContent = '0'
    angleValue.textContent = '0'
    draw()
    sendControl()
  }

  function sendControl() {
    if (!ws || ws.readyState !== WebSocket.OPEN) return
    ws.send(JSON.stringify({
      type: 'control',
      role,
      speed: currentSpeed,
      angle: currentAngle,
      start: startActive,
    }))
  }

  function ensureTimer() {
    if (sendTimer) return
    sendTimer = setInterval(sendControl, 70)
  }

  function connect() {
    ws = new WebSocket(wsUrl)
    ws.addEventListener('open', () => {
      statusBadge.textContent = 'WS ●'
      statusBadge.className = 'badge online'
      ensureTimer()
      sendControl()
    })
    ws.addEventListener('close', () => {
      statusBadge.textContent = 'WS'
      statusBadge.className = 'badge offline'
      window.setTimeout(connect, 1500)
    })
  }

  function pointerPos(evt) {
    const rect = canvas.getBoundingClientRect()
    const point = evt.touches ? evt.touches[0] : evt
    return {
      x: (point.clientX - rect.left) * (canvas.width / rect.width),
      y: (point.clientY - rect.top) * (canvas.height / rect.height),
    }
  }

  canvas.addEventListener('mousedown', (evt) => {
    dragging = true
    const pos = pointerPos(evt)
    normalize(pos.x, pos.y)
  })
  canvas.addEventListener('mousemove', (evt) => {
    if (!dragging) return
    const pos = pointerPos(evt)
    normalize(pos.x, pos.y)
  })
  window.addEventListener('mouseup', () => {
    if (!dragging) return
    dragging = false
    resetStick()
  })
  canvas.addEventListener('touchstart', (evt) => {
    dragging = true
    const pos = pointerPos(evt)
    normalize(pos.x, pos.y)
    evt.preventDefault()
  }, { passive: false })
  canvas.addEventListener('touchmove', (evt) => {
    if (!dragging) return
    const pos = pointerPos(evt)
    normalize(pos.x, pos.y)
    evt.preventDefault()
  }, { passive: false })
  window.addEventListener('touchend', () => {
    if (!dragging) return
    dragging = false
    resetStick()
  })

  startBtn.addEventListener('click', () => {
    startActive = !startActive
    startBtn.classList.toggle('active', startActive)
    startBtn.textContent = startActive ? 'START EIN' : 'START AUS'
    sendControl()
  })

  draw()
  connect()
})()
