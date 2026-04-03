<#
.SYNOPSIS
    BCWS PC-Hub — Developer Run Script
    Starts the PC hub in development mode with coloured live logging.
    Optionally launches the satellite simulator in a separate window.

.DESCRIPTION
    Activates the Python virtual environment and starts hub_core/main.py
    with development-friendly settings:
        - DEBUG log level
        - Coloured rich terminal output
        - Automatic reload on code changes (if -Watch specified)
        - Optional simulator launch

    Prerequisites:
        - setup.ps1 must have been run first
        - USB bridge connected OR -Simulate flag used

.PARAMETER Simulate
    Launch simulator.ps1 in a separate window automatically.
    The hub will listen for simulator traffic on the loopback.

.PARAMETER LogLevel
    Override log level: DEBUG / INFO / WARNING / ERROR.
    Defaults to DEBUG for dev mode.

.PARAMETER Watch
    Restart hub automatically when Python source files change.
    Requires 'watchdog' pip package (auto-installed if missing).

.PARAMETER ComPort
    Override COM port for this run only (does not modify config file).

.PARAMETER NoColor
    Disable rich coloured output (useful when redirecting logs).

.EXAMPLE
    .\run-dev.ps1
    .\run-dev.ps1 -Simulate
    .\run-dev.ps1 -Simulate -LogLevel INFO
    .\run-dev.ps1 -ComPort COM5
    .\run-dev.ps1 -Watch
#>

[CmdletBinding()]
param(
    [switch]$Simulate,
    [ValidateSet("DEBUG","INFO","WARNING","ERROR")]
    [string]$LogLevel = "DEBUG",
    [switch]$Watch,
    [string]$ComPort = "",
    [switch]$NoColor
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ─────────────────────────────────────────────────────────────────────────────
# Colour helpers
# ─────────────────────────────────────────────────────────────────────────────

function Write-Step  { param([string]$m) Write-Host "`n[*] $m" -ForegroundColor Cyan }
function Write-Ok    { param([string]$m) Write-Host "    [OK] $m" -ForegroundColor Green }
function Write-Warn  { param([string]$m) Write-Host "    [!]  $m" -ForegroundColor Yellow }
function Write-Fail  { param([string]$m) Write-Host "    [X]  $m" -ForegroundColor Red }
function Write-Info  { param([string]$m) Write-Host "         $m" -ForegroundColor Gray }

# ─────────────────────────────────────────────────────────────────────────────
# Paths
# ─────────────────────────────────────────────────────────────────────────────

$ScriptDir      = $PSScriptRoot
$MigrationDir   = Split-Path $ScriptDir -Parent
$VenvPython     = Join-Path $MigrationDir ".venv\Scripts\python.exe"
$HubMain        = Join-Path $MigrationDir "hub_core\main.py"
$ConfigFile     = Join-Path $MigrationDir "config\hub_config.yaml"
$SimulatorScript= Join-Path $ScriptDir "simulator.ps1"

# ─────────────────────────────────────────────────────────────────────────────
# Pre-flight checks
# ─────────────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║   BCWS PC-Hub — Dev Mode                                    ║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""

Write-Step "Pre-flight checks..."

# Check venv exists
if (-not (Test-Path $VenvPython)) {
    Write-Fail "Virtual environment not found at $VenvPython"
    Write-Info "Run setup.ps1 first: .\tools\setup.ps1"
    exit 1
}
Write-Ok "Python venv: $VenvPython"

# Check config
if (-not (Test-Path $ConfigFile)) {
    Write-Warn "hub_config.yaml not found at $ConfigFile"
    Write-Info "Run setup.ps1 first to generate defaults."
    exit 1
}
Write-Ok "Config: $ConfigFile"

# Check hub_core/main.py
if (-not (Test-Path $HubMain)) {
    Write-Warn "hub_core\main.py not found at $HubMain"
    Write-Info "Hub implementation not yet created."
    Write-Info "See PC_Hub_Migration/docs/hub_migration_summary.md Phase 1."
    Write-Info ""
    Write-Info "For now you can:"
    Write-Info "  1. Run .\tools\simulator.ps1 to test the simulator standalone"
    Write-Info "  2. Implement hub_core/main.py as described in the migration docs"
    Write-Warn "Exiting — hub_core/main.py is required to start the hub."
    exit 1
}
Write-Ok "Hub main: $HubMain"

# Optional: install watchdog if -Watch requested
if ($Watch) {
    Write-Step "Checking watchdog package for auto-reload..."
    $result = & $VenvPython -c "import watchdog" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Info "Installing watchdog..."
        & (Join-Path $MigrationDir ".venv\Scripts\pip.exe") install watchdog --quiet
        if ($LASTEXITCODE -ne 0) {
            Write-Warn "Could not install watchdog. -Watch disabled."
            $Watch = $false
        }
        else {
            Write-Ok "watchdog installed"
        }
    }
    else {
        Write-Ok "watchdog already available"
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Build argument list for hub_core/main.py
# ─────────────────────────────────────────────────────────────────────────────

$HubArgs = @(
    "--config", $ConfigFile,
    "--log-level", $LogLevel
)

if ($NoColor) {
    $HubArgs += "--no-color"
}

if ($ComPort -ne "") {
    $HubArgs += "--com-port"
    $HubArgs += $ComPort
    Write-Warn "COM port override: $ComPort (not saved to config)"
}

# ─────────────────────────────────────────────────────────────────────────────
# Launch simulator in a new window if requested
# ─────────────────────────────────────────────────────────────────────────────

if ($Simulate) {
    Write-Step "Launching simulator in separate window..."

    if (-not (Test-Path $SimulatorScript)) {
        Write-Warn "simulator.ps1 not found at $SimulatorScript — skipping simulator launch"
    }
    else {
        $SimArgs = "-NoExit -File `"$SimulatorScript`" -Mode loopback"
        Start-Process powershell -ArgumentList $SimArgs -WindowStyle Normal
        Write-Ok "Simulator launched (separate window)"
        Write-Info "Waiting 2s for simulator to initialize..."
        Start-Sleep -Seconds 2
    }
}

# ─────────────────────────────────────────────────────────────────────────────
# Display startup info
# ─────────────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "  Hub Endpoints:" -ForegroundColor White
Write-Host "    WebSocket  : ws://localhost:8765/ws" -ForegroundColor Green
Write-Host "    REST API   : http://localhost:8766" -ForegroundColor Green
Write-Host "    Health     : http://localhost:8766/health" -ForegroundColor Green
Write-Host "    Metrics    : http://localhost:8766/metrics" -ForegroundColor Green
Write-Host ""
Write-Host "  Log level    : $LogLevel" -ForegroundColor Gray
Write-Host "  Config       : $ConfigFile" -ForegroundColor Gray
Write-Host "  Auto-reload  : $Watch" -ForegroundColor Gray
Write-Host "  Simulator    : $Simulate" -ForegroundColor Gray
Write-Host ""
Write-Host "  Press Ctrl+C to stop the hub." -ForegroundColor Yellow
Write-Host ""

# ─────────────────────────────────────────────────────────────────────────────
# Start hub — with or without file-watch auto-reload
# ─────────────────────────────────────────────────────────────────────────────

if ($Watch) {
    # Use watchmedo to auto-restart on file change
    $WatchmedoPath = Join-Path $MigrationDir ".venv\Scripts\watchmedo.exe"
    if (-not (Test-Path $WatchmedoPath)) {
        Write-Warn "watchmedo.exe not found, falling back to direct run"
        & $VenvPython $HubMain @HubArgs
    }
    else {
        Write-Step "Starting hub with auto-reload (watchmedo)..."
        $WatchArgs = @(
            "auto-restart",
            "--directory", (Join-Path $MigrationDir "hub_core"),
            "--pattern", "*.py",
            "--recursive",
            "--",
            $VenvPython,
            $HubMain
        ) + $HubArgs
        & $WatchmedoPath @WatchArgs
    }
}
else {
    # Direct run
    Write-Step "Starting hub..."
    & $VenvPython $HubMain @HubArgs
}

# ─────────────────────────────────────────────────────────────────────────────
# Post-exit
# ─────────────────────────────────────────────────────────────────────────────

Write-Host ""
if ($LASTEXITCODE -eq 0) {
    Write-Ok "Hub exited cleanly (exit code 0)."
}
else {
    Write-Warn "Hub exited with code $LASTEXITCODE."
    Write-Info "Check logs in: $(Join-Path $MigrationDir 'logs')"
}
Write-Host ""
