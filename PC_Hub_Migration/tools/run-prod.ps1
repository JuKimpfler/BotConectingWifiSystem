<#
.SYNOPSIS
    BCWS PC-Hub — Production Start Script
    Starts the hub with production settings (persistent, auto-restart).

.DESCRIPTION
    Launches hub_core/main.py in the virtual environment with production
    log level (WARNING), restarts on crash up to 3 times.

    Differences from run-dev.ps1:
      - Log level: WARNING (not DEBUG)
      - Auto-restart loop on non-zero exit
      - No simulator companion

.PARAMETER ComPort
    Override bridge COM port (e.g. COM4). Defaults to config value.

.PARAMETER ConfigPath
    Path to hub_config.yaml. Defaults to config/hub_config.yaml.

.EXAMPLE
    .\run-prod.ps1
    .\run-prod.ps1 -ComPort COM4
#>

[CmdletBinding()]
param(
    [string]$ComPort     = "",
    [string]$ConfigPath  = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir    = $PSScriptRoot
$MigrationDir = Split-Path $ScriptDir -Parent
$PythonVenv   = Join-Path $MigrationDir ".venv\Scripts\python.exe"
$HubMain      = Join-Path $MigrationDir "hub_core\main.py"

function Write-Info { param([string]$M) Write-Host "  [HUB] $M" -ForegroundColor Cyan }
function Write-Warn { param([string]$M) Write-Host "  [!]  $M" -ForegroundColor Yellow }
function Write-Fail { param([string]$M) Write-Host "  [X]  $M" -ForegroundColor Red }

if (-not (Test-Path $PythonVenv)) {
    Write-Fail "Virtual environment not found. Run .\tools\setup.ps1 first."
    exit 1
}

# Apply overrides via environment variables
if ($ComPort)     { $env:BCWS_BRIDGE_COM_PORT = $ComPort }
if ($ConfigPath)  { $env:BCWS_CONFIG_PATH     = $ConfigPath }
$env:BCWS_HUB_LOG_LEVEL = "WARNING"

Write-Host ""
Write-Host "╔══════════════════════════════════════════════╗" -ForegroundColor Magenta
Write-Host "║   BCWS PC-Hub [PROD]                         ║" -ForegroundColor Magenta
Write-Host "╚══════════════════════════════════════════════╝" -ForegroundColor Magenta
Write-Host ""
Write-Info "Working dir : $MigrationDir"
Write-Info "Hub script  : $HubMain"
Write-Info "Log level   : WARNING"
Write-Host ""

Set-Location $MigrationDir

$maxRestarts  = 3
$restartCount = 0
$restartDelay = 5

while ($restartCount -le $maxRestarts) {
    Write-Info "Starting hub (attempt $($restartCount + 1) of $($maxRestarts + 1))..."
    & $PythonVenv $HubMain
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
        Write-Info "Hub exited cleanly (code 0). Done."
        break
    }
    elseif ($restartCount -ge $maxRestarts) {
        Write-Fail "Hub crashed $($restartCount + 1) times. Giving up."
        Write-Fail "Exit code: $exitCode"
        Write-Warn "Check logs\ for details."
        exit $exitCode
    }
    else {
        $restartCount++
        Write-Warn "Hub exited with code $exitCode. Restarting in ${restartDelay}s (attempt $restartCount of $maxRestarts)..."
        Start-Sleep -Seconds $restartDelay
        $restartDelay = [Math]::Min($restartDelay * 2, 30)
    }
}
