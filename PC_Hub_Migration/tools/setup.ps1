<#
.SYNOPSIS
    BCWS PC-Hub — Windows Bootstrap Script
    Sets up the Python virtual environment, installs all dependencies,
    creates default configuration, and validates the environment.

.DESCRIPTION
    Idempotent: safe to run multiple times. Skips steps already completed.
    Designed for easy first-time setup on a fresh Windows machine.

    Prerequisites (auto-checked):
        - Python 3.11+ in PATH
        - Git (for cloning if needed)
        - USB bridge device connected (optional at setup time)

    What this script does:
        1.  Checks Python version (>= 3.11)
        2.  Creates Python virtual environment in .venv/
        3.  Installs required pip packages
        4.  Creates config/hub_config.yaml (if not present)
        5.  Creates data/ and logs/ directories
        6.  Validates COM port list (informational)
        7.  Optionally installs NSSM Windows service

.PARAMETER InstallService
    If specified, installs the hub as a Windows background service using NSSM.
    Requires elevated privileges (Run as Administrator).

.PARAMETER ComPort
    Override the COM port written into hub_config.yaml (e.g. COM4).
    Defaults to 'auto'.

.PARAMETER ForceRecreateVenv
    Delete and recreate the virtual environment even if it already exists.

.EXAMPLE
    .\setup.ps1
    .\setup.ps1 -ComPort COM4
    .\setup.ps1 -InstallService
#>

[CmdletBinding()]
param(
    [switch]$InstallService,
    [string]$ComPort = "auto",
    [switch]$ForceRecreateVenv
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ─────────────────────────────────────────────────────────────────────────────
# Colour helpers
# ─────────────────────────────────────────────────────────────────────────────

function Write-Step {
    param([string]$Message)
    Write-Host "`n[*] $Message" -ForegroundColor Cyan
}

function Write-Ok {
    param([string]$Message)
    Write-Host "    [OK] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "    [!]  $Message" -ForegroundColor Yellow
}

function Write-Fail {
    param([string]$Message)
    Write-Host "    [X]  $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "         $Message" -ForegroundColor Gray
}

# ─────────────────────────────────────────────────────────────────────────────
# Resolve paths relative to repo root (two levels up from tools/)
# ─────────────────────────────────────────────────────────────────────────────

$ScriptDir    = $PSScriptRoot
$MigrationDir = Split-Path $ScriptDir -Parent           # PC_Hub_Migration/
$RepoRoot     = Split-Path $MigrationDir -Parent         # repo root
$HubDir       = $MigrationDir                            # working dir for hub
$VenvDir      = Join-Path $HubDir ".venv"
$ConfigDir    = Join-Path $HubDir "config"
$DataDir      = Join-Path $HubDir "data"
$LogsDir      = Join-Path $HubDir "logs"
$RequirementsFile = Join-Path $HubDir "requirements.txt"
$ConfigFile   = Join-Path $ConfigDir "hub_config.yaml"

# ─────────────────────────────────────────────────────────────────────────────
# Banner
# ─────────────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Magenta
Write-Host "║   BCWS PC-Hub — Windows Setup Script                        ║" -ForegroundColor Magenta
Write-Host "║   BotConnectingWifiSystem hub migration bootstrap            ║" -ForegroundColor Magenta
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Magenta
Write-Host ""
Write-Info "Migration dir : $MigrationDir"
Write-Info "Repo root     : $RepoRoot"
Write-Info "Venv          : $VenvDir"
Write-Host ""

# ─────────────────────────────────────────────────────────────────────────────
# Step 1: Check Python version
# ─────────────────────────────────────────────────────────────────────────────

Write-Step "Checking Python version..."

$pythonCmd = $null
foreach ($candidate in @("python", "python3", "py")) {
    try {
        $ver = & $candidate --version 2>&1
        if ($ver -match "Python (\d+)\.(\d+)") {
            $major = [int]$Matches[1]
            $minor = [int]$Matches[2]
            if ($major -ge 3 -and $minor -ge 11) {
                $pythonCmd = $candidate
                Write-Ok "Found $ver (>= 3.11 required)"
                break
            }
            else {
                Write-Warn "Found $ver — need 3.11+, trying next..."
            }
        }
    }
    catch { }
}

if (-not $pythonCmd) {
    Write-Fail "Python 3.11+ not found in PATH."
    Write-Info "Download from: https://www.python.org/downloads/"
    Write-Info "Make sure to check 'Add Python to PATH' during install."
    exit 1
}

# ─────────────────────────────────────────────────────────────────────────────
# Step 2: Create / validate virtual environment
# ─────────────────────────────────────────────────────────────────────────────

Write-Step "Setting up Python virtual environment..."

if ($ForceRecreateVenv -and (Test-Path $VenvDir)) {
    Write-Warn "ForceRecreateVenv: removing existing venv at $VenvDir"
    Remove-Item $VenvDir -Recurse -Force
}

if (Test-Path (Join-Path $VenvDir "Scripts\python.exe")) {
    Write-Ok "Virtual environment already exists — skipping creation"
}
else {
    Write-Info "Creating venv at $VenvDir ..."
    & $pythonCmd -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "Failed to create virtual environment."
        exit 1
    }
    Write-Ok "Virtual environment created"
}

$PythonVenv = Join-Path $VenvDir "Scripts\python.exe"
$PipVenv    = Join-Path $VenvDir "Scripts\pip.exe"

# ─────────────────────────────────────────────────────────────────────────────
# Step 3: Create requirements.txt if missing, then install
# ─────────────────────────────────────────────────────────────────────────────

Write-Step "Installing Python dependencies..."

if (-not (Test-Path $RequirementsFile)) {
    Write-Info "requirements.txt not found — creating default..."
    $DefaultRequirements = @"
# BCWS PC-Hub Python dependencies
# Generated by setup.ps1

# Async serial communication with USB bridge
pyserial==3.5
pyserial-asyncio==0.6

# WebSocket server for browser/UI clients
websockets==12.0

# HTTP REST API
aiohttp==3.9.5

# YAML configuration parsing
PyYAML==6.0.1

# Structured logging
python-json-logger==2.0.7

# Rich terminal output (coloured console)
rich==13.7.1
"@
    Set-Content -Path $RequirementsFile -Value $DefaultRequirements -Encoding UTF8
    Write-Ok "Created requirements.txt"
}
else {
    Write-Ok "requirements.txt exists"
}

Write-Info "Running pip install..."
& $PipVenv install -r $RequirementsFile --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Fail "pip install failed. Check network connection or requirements.txt."
    exit 1
}
Write-Ok "All Python dependencies installed"

# ─────────────────────────────────────────────────────────────────────────────
# Step 4: Create directory structure
# ─────────────────────────────────────────────────────────────────────────────

Write-Step "Creating runtime directories..."

foreach ($dir in @($ConfigDir, $DataDir, $LogsDir)) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        Write-Ok "Created $dir"
    }
    else {
        Write-Info "Already exists: $dir"
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Step 5: Create default hub_config.yaml
# ─────────────────────────────────────────────────────────────────────────────

Write-Step "Writing default configuration..."

if (Test-Path $ConfigFile) {
    Write-Ok "hub_config.yaml already exists — not overwriting"
    Write-Info "Delete $ConfigFile to regenerate defaults."
}
else {
    $YamlContent = @"
# ============================================================
# BCWS PC-Hub Configuration
# Generated by setup.ps1 — edit as needed
# See PC_Hub_Migration/docs/hub_migration_summary.md Appendix B
# ============================================================

hub:
  bind_host: "127.0.0.1"     # Use "0.0.0.0" to expose on LAN
  ws_port: 8765               # WebSocket port (ws://localhost:8765/ws)
  rest_port: 8766             # REST API port
  log_level: "INFO"           # DEBUG / INFO / WARNING / ERROR
  log_dir: "logs"

bridge:
  com_port: "$ComPort"        # USB ESP-NOW bridge COM port, or "auto"
  baud_rate: 921600
  network_id: 0x01            # Must match ESPNOW_NETWORK_ID in sat firmware
  channel: 6                  # ESP-NOW channel (default 6)

telemetry:
  max_rate_hz: 50             # Max WebSocket push rate per stream to UI
  batch_flush_ms: 200         # SQLite batch write interval (ms)

heartbeat:
  interval_ms: 1000           # How often hub sends heartbeat to satellites
  timeout_ms: 4000            # Mark satellite offline after this silence (ms)

ack:
  timeout_ms: 500             # Wait this long for ACK before retry
  max_retries: 3              # Max ACK retries before marking command failed

storage:
  db_path: "data/telemetry.db"
  retention_hours: 24         # Auto-purge samples older than this
  vacuum_interval_hours: 6    # SQLite VACUUM interval

satellites:
  SAT1:
    role: 1
    mac: ""                   # Leave empty for auto-discovery via MSG_DISCOVERY
  SAT2:
    role: 2
    mac: ""
"@
    Set-Content -Path $ConfigFile -Value $YamlContent -Encoding UTF8
    Write-Ok "hub_config.yaml written to $ConfigFile"
}

# ─────────────────────────────────────────────────────────────────────────────
# Step 6: Detect available COM ports (informational)
# ─────────────────────────────────────────────────────────────────────────────

Write-Step "Scanning for available COM ports..."

try {
    $Ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($Ports.Count -eq 0) {
        Write-Warn "No COM ports detected. Connect USB bridge device and re-run."
    }
    else {
        Write-Ok "Found COM ports:"
        foreach ($p in $Ports) {
            # Try to get friendly name via WMI
            try {
                $wmiPort = Get-WmiObject Win32_SerialPort | Where-Object { $_.DeviceID -eq $p }
                if ($wmiPort) {
                    Write-Info "  $p — $($wmiPort.Description)"
                }
                else {
                    Write-Info "  $p"
                }
            }
            catch {
                Write-Info "  $p"
            }
        }
        if ($ComPort -eq "auto" -and $Ports.Count -eq 1) {
            Write-Warn "Only one COM port found ($($Ports[0])). Consider running:"
            Write-Info "  .\setup.ps1 -ComPort $($Ports[0])"
            Write-Info "to set it explicitly in hub_config.yaml."
        }
    }
}
catch {
    Write-Warn "Could not enumerate COM ports: $_"
}

# ─────────────────────────────────────────────────────────────────────────────
# Step 7: Validate Python packages installed correctly
# ─────────────────────────────────────────────────────────────────────────────

Write-Step "Validating installed packages..."

$RequiredPackages = @("serial", "websockets", "aiohttp", "yaml", "rich")
$AllOk = $true
foreach ($pkg in $RequiredPackages) {
    $result = & $PythonVenv -c "import $pkg" 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Ok "  import $pkg — OK"
    }
    else {
        Write-Fail "  import $pkg — FAILED: $result"
        $AllOk = $false
    }
}

if (-not $AllOk) {
    Write-Fail "Some packages could not be imported. Try re-running setup.ps1 -ForceRecreateVenv"
    exit 1
}

# ─────────────────────────────────────────────────────────────────────────────
# Step 8 (optional): Install Windows service with NSSM
# ─────────────────────────────────────────────────────────────────────────────

if ($InstallService) {
    Write-Step "Installing Windows service (NSSM)..."

    # Check for elevation
    $IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $IsAdmin) {
        Write-Fail "Service installation requires Administrator privileges."
        Write-Info "Re-run PowerShell as Administrator and use: .\setup.ps1 -InstallService"
        exit 1
    }

    $NssmPath = (Get-Command nssm -ErrorAction SilentlyContinue)?.Source
    if (-not $NssmPath) {
        Write-Warn "NSSM not found in PATH."
        Write-Info "Install NSSM from https://nssm.cc/download"
        Write-Info "Or add it to PATH, then re-run .\setup.ps1 -InstallService"
        exit 1
    }

    $ServiceName = "BCWSHub"
    $HubMain     = Join-Path $HubDir "hub_core\main.py"
    $LogFile     = Join-Path $LogsDir "service-stdout.log"
    $ErrFile     = Join-Path $LogsDir "service-stderr.log"

    # Check if service already exists
    $existing = & nssm status $ServiceName 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Warn "Service '$ServiceName' already exists. Stopping and reconfiguring..."
        & nssm stop $ServiceName 2>&1 | Out-Null
    }

    Write-Info "Registering service '$ServiceName'..."
    & nssm install $ServiceName $PythonVenv $HubMain
    & nssm set $ServiceName AppDirectory $HubDir
    & nssm set $ServiceName AppStdout $LogFile
    & nssm set $ServiceName AppStderr $ErrFile
    & nssm set $ServiceName Start SERVICE_AUTO_START
    & nssm set $ServiceName AppRestartDelay 5000

    Write-Ok "Service '$ServiceName' installed."
    Write-Info "Start:   Start-Service BCWSHub  (or: nssm start BCWSHub)"
    Write-Info "Stop:    Stop-Service BCWSHub"
    Write-Info "Logs:    $LogFile"
}

# ─────────────────────────────────────────────────────────────────────────────
# Summary
# ─────────────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║   Setup Complete!                                            ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor White
Write-Host ""
Write-Host "  1. Connect USB ESP-NOW bridge to PC (or run simulator first)" -ForegroundColor Gray
Write-Host "  2. Edit config: $ConfigFile" -ForegroundColor Gray
Write-Host "     Set 'bridge.com_port' to your bridge COM port (e.g. COM3)" -ForegroundColor Gray
Write-Host "  3. Start hub in dev mode:" -ForegroundColor Gray
Write-Host "     .\tools\run-dev.ps1" -ForegroundColor Cyan
Write-Host "  4. Or run simulator (no hardware needed):" -ForegroundColor Gray
Write-Host "     .\tools\simulator.ps1" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Docs: $MigrationDir\docs\" -ForegroundColor Gray
Write-Host ""
