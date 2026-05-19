<#
.SYNOPSIS
    BCWS PC-Hub — Windows Setup Script

.DESCRIPTION
    Creates a Python virtual environment, installs dependencies, writes the
    default V3.0 UDP configuration, and creates a Start-Hub launcher plus
    desktop shortcut for the native Windows GUI.
#>

[CmdletBinding()]
param(
    [switch]$InstallService,
    [switch]$ForceRecreateVenv
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Step { param([string]$Message) Write-Host "`n[*] $Message" -ForegroundColor Cyan }
function Write-Ok   { param([string]$Message) Write-Host "    [OK] $Message" -ForegroundColor Green }
function Write-Warn { param([string]$Message) Write-Host "    [!]  $Message" -ForegroundColor Yellow }
function Write-Fail { param([string]$Message) Write-Host "    [X]  $Message" -ForegroundColor Red }
function Write-Info { param([string]$Message) Write-Host "         $Message" -ForegroundColor Gray }

$ScriptDir    = $PSScriptRoot
$MigrationDir = Split-Path $ScriptDir -Parent
$RepoRoot     = Split-Path $MigrationDir -Parent
$HubDir       = $MigrationDir
$VenvDir      = Join-Path $HubDir ".venv"
$ConfigDir    = Join-Path $HubDir "config"
$DataDir      = Join-Path $HubDir "data"
$LogsDir      = Join-Path $HubDir "logs"
$RequirementsFile = Join-Path $HubDir "requirements.txt"
$ConfigFile   = Join-Path $ConfigDir "hub_config.yaml"
$StartScript  = Join-Path $MigrationDir "Start-Hub.bat"

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Magenta
Write-Host "║   BCWS PC-Hub — Windows Setup Script                        ║" -ForegroundColor Magenta
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Magenta
Write-Host ""
Write-Info "Migration dir : $MigrationDir"
Write-Info "Repo root     : $RepoRoot"
Write-Info "Venv          : $VenvDir"
Write-Host ""

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
                Write-Ok "Found $ver"
                break
            }
        }
    }
    catch { }
}
if (-not $pythonCmd) {
    Write-Fail "Python 3.11+ not found in PATH."
    exit 1
}

Write-Step "Setting up Python virtual environment..."
if ($ForceRecreateVenv -and (Test-Path $VenvDir)) {
    Remove-Item $VenvDir -Recurse -Force
    Write-Warn "Existing venv removed"
}
if (-not (Test-Path (Join-Path $VenvDir "Scripts\python.exe"))) {
    & $pythonCmd -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "Failed to create virtual environment."
        exit 1
    }
    Write-Ok "Virtual environment created"
}
else {
    Write-Ok "Virtual environment already exists"
}
$PythonVenv = Join-Path $VenvDir "Scripts\python.exe"
$PipVenv    = Join-Path $VenvDir "Scripts\pip.exe"

Write-Step "Installing Python dependencies..."
if (-not (Test-Path $RequirementsFile)) {
    @"
pyserial==3.5
pyserial-asyncio==0.6
websockets==12.0
aiohttp==3.9.5
PyYAML==6.0.1
python-json-logger==2.0.7
rich==13.7.1
"@ | Set-Content -Path $RequirementsFile -Encoding UTF8
    Write-Ok "requirements.txt created"
}
& $PipVenv install -r $RequirementsFile --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Fail "pip install failed"
    exit 1
}
Write-Ok "Dependencies installed"

Write-Step "Creating runtime directories..."
foreach ($dir in @($ConfigDir, $DataDir, $LogsDir)) {
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        Write-Ok "Created $dir"
    }
}

Write-Step "Writing default configuration..."
if (-not (Test-Path $ConfigFile)) {
@"
hub:
  bind_host: "0.0.0.0"
  web_port: 8080
  log_level: "INFO"

network:
  telemetry_port: 5005

gui:
  refresh_ms: 150

mobile:
  default_role: "SAT1"

heartbeat:
  interval_ms: 1000
  timeout_ms: 4000

satellites:
  SAT1:
    role: 1
    name: "SAT1"
    host: ""
    port: 5006
  SAT2:
    role: 2
    name: "SAT2"
    host: ""
    port: 5006
"@ | Set-Content -Path $ConfigFile -Encoding UTF8
    Write-Ok "hub_config.yaml written"
}
else {
    Write-Ok "hub_config.yaml already exists"
}

Write-Step "Creating Windows launchers..."
@"
@echo off
cd /d "$RepoRoot"
"$PythonVenv" -m PC_Hub_Migration.hub_core.main --config "$ConfigFile" --log-level INFO
"@ | Set-Content -Path $StartScript -Encoding ASCII
Write-Ok "Created launcher: $StartScript"

try {
    $DesktopPath = [Environment]::GetFolderPath("Desktop")
    $ShortcutPath = Join-Path $DesktopPath "BCWS PC Hub.lnk"
    $WshShell = New-Object -ComObject WScript.Shell
    $Shortcut = $WshShell.CreateShortcut($ShortcutPath)
    $Shortcut.TargetPath = $StartScript
    $Shortcut.WorkingDirectory = $MigrationDir
    $Shortcut.IconLocation = "$env:SystemRoot\System32\shell32.dll,13"
    $Shortcut.Save()
    Write-Ok "Desktop shortcut created: $ShortcutPath"
}
catch {
    Write-Warn "Desktop shortcut could not be created automatically: $_"
}

Write-Step "Validating installed packages..."
$RequiredPackages = @("aiohttp", "yaml", "rich")
$AllOk = $true
foreach ($pkg in $RequiredPackages) {
    $result = & $PythonVenv -c "import $pkg" 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Ok "import $pkg — OK"
    }
    else {
        Write-Fail "import $pkg — FAILED: $result"
        $AllOk = $false
    }
}
if (-not $AllOk) {
    exit 1
}

if ($InstallService) {
    Write-Step "Installing Windows service (NSSM)..."
    $IsAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    if (-not $IsAdmin) {
        Write-Fail "Service installation requires Administrator privileges."
        exit 1
    }
    $NssmPath = (Get-Command nssm -ErrorAction SilentlyContinue)?.Source
    if (-not $NssmPath) {
        Write-Fail "NSSM not found in PATH."
        exit 1
    }
    $ServiceName = "BCWSHub"
    $LogFile     = Join-Path $LogsDir "service-stdout.log"
    $ErrFile     = Join-Path $LogsDir "service-stderr.log"
    & nssm install $ServiceName $PythonVenv
    & nssm set $ServiceName AppDirectory $RepoRoot
    & nssm set $ServiceName AppParameters "-m PC_Hub_Migration.hub_core.main --config `"$ConfigFile`" --headless --log-level INFO"
    & nssm set $ServiceName AppStdout $LogFile
    & nssm set $ServiceName AppStderr $ErrFile
    & nssm set $ServiceName Start SERVICE_AUTO_START
    Write-Ok "Service '$ServiceName' installed"
}

Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║   Setup Complete!                                           ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "  Next steps:" -ForegroundColor White
Write-Host "  1. Windows-Hotspot auf festem 2.4-GHz-Kanal starten" -ForegroundColor Gray
Write-Host "  2. Satelliten mit diesem Hotspot verbinden" -ForegroundColor Gray
Write-Host "  3. Hub starten: .\tools\run-dev.ps1 oder Start-Hub.bat" -ForegroundColor Gray
Write-Host "  4. Mobile UI: http://<PC-IP>:8080/mobile?role=SAT1" -ForegroundColor Gray
Write-Host ""
