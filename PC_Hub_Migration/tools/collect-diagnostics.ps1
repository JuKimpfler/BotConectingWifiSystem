<#
.SYNOPSIS
    BCWS PC-Hub — Diagnostics Collection Script
    Collects all diagnostic information for issue reporting.

.DESCRIPTION
    Collects:
    - Last 200 lines of hub log file
    - /metrics JSON snapshot (if hub is running)
    - /api/devices JSON snapshot (if hub is running)
    - Python version and installed packages
    - COM port list
    - DB stats (row counts per table)
    - System info (OS, RAM, CPU)

    Output: diagnostics_YYYYMMDD_HHmmss.txt in current directory.

.PARAMETER HubUrl
    Hub base URL. Defaults to http://127.0.0.1:8765

.EXAMPLE
    .\collect-diagnostics.ps1
    .\collect-diagnostics.ps1 -HubUrl http://192.168.1.50:8765
#>

[CmdletBinding()]
param(
    [string]$HubUrl = "http://127.0.0.1:8765"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "SilentlyContinue"

$ScriptDir    = $PSScriptRoot
$MigrationDir = Split-Path $ScriptDir -Parent
$PythonVenv   = Join-Path $MigrationDir ".venv\Scripts\python.exe"
$LogsDir      = Join-Path $MigrationDir "logs"
$DataDir      = Join-Path $MigrationDir "data"
$DbPath       = Join-Path $DataDir "telemetry.db"

$Timestamp    = (Get-Date -Format "yyyyMMdd_HHmmss")
$OutFile      = Join-Path (Get-Location) "diagnostics_$Timestamp.txt"

function Section {
    param([string]$Title)
    $sep = "=" * 60
    "`n$sep`n$Title`n$sep"
}

$Lines = @()

$Lines += "BCWS PC-Hub Diagnostics"
$Lines += "Collected: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
$Lines += "Hub URL: $HubUrl"
$Lines += ""

# ── System Info ───────────────────────────────────────────────
$Lines += Section "System Information"
$Lines += "OS: $([System.Environment]::OSVersion)"
$Lines += "Machine: $([System.Environment]::MachineName)"
$cpu = (Get-WmiObject Win32_Processor | Select-Object -First 1)
$Lines += "CPU: $($cpu.Name)"
$ram = (Get-WmiObject Win32_OperatingSystem)
$Lines += "RAM (total): $([Math]::Round($ram.TotalVisibleMemorySize / 1MB, 1)) GB"
$Lines += "RAM (free):  $([Math]::Round($ram.FreePhysicalMemory / 1MB, 1)) GB"

# ── Python info ───────────────────────────────────────────────
$Lines += Section "Python Environment"
if (Test-Path $PythonVenv) {
    $Lines += & $PythonVenv --version 2>&1
    $Lines += ""
    $Lines += "Installed packages:"
    $Lines += & $PythonVenv -m pip list 2>&1
} else {
    $Lines += "Virtual environment not found at: $PythonVenv"
    $Lines += "Run .\tools\setup.ps1 to create it."
}

# ── COM ports ─────────────────────────────────────────────────
$Lines += Section "COM Ports"
try {
    $Ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($Ports.Count -eq 0) {
        $Lines += "(no COM ports detected)"
    } else {
        foreach ($p in $Ports) {
            $wmiPort = Get-WmiObject Win32_SerialPort | Where-Object { $_.DeviceID -eq $p }
            if ($wmiPort) {
                $Lines += "$p — $($wmiPort.Description)"
            } else {
                $Lines += "$p"
            }
        }
    }
} catch {
    $Lines += "Error enumerating ports: $_"
}

# ── Hub REST endpoints ────────────────────────────────────────
$Lines += Section "Hub /metrics (live)"
try {
    $resp = Invoke-RestMethod -Uri "$HubUrl/metrics" -TimeoutSec 3
    $Lines += ($resp | ConvertTo-Json -Depth 5)
} catch {
    $Lines += "Hub not reachable or /metrics failed: $_"
}

$Lines += Section "Hub /api/devices (live)"
try {
    $resp = Invoke-RestMethod -Uri "$HubUrl/api/devices" -TimeoutSec 3
    $Lines += ($resp | ConvertTo-Json -Depth 5)
} catch {
    $Lines += "Hub not reachable or /api/devices failed: $_"
}

$Lines += Section "Hub /ready (live)"
try {
    $resp = Invoke-RestMethod -Uri "$HubUrl/ready" -TimeoutSec 3
    $Lines += ($resp | ConvertTo-Json)
} catch {
    $Lines += "Hub not reachable: $_"
}

# ── Log files ─────────────────────────────────────────────────
$Lines += Section "Recent Log Lines (last 200)"
if (Test-Path $LogsDir) {
    $logFiles = Get-ChildItem $LogsDir -Filter "*.log" | Sort-Object LastWriteTime -Descending
    if ($logFiles.Count -eq 0) {
        $Lines += "(no log files found in $LogsDir)"
    } else {
        $latestLog = $logFiles[0].FullName
        $Lines += "Log file: $latestLog"
        $Lines += ""
        $Lines += (Get-Content $latestLog -Tail 200 | ForEach-Object { $_ })
    }
} else {
    $Lines += "Logs directory not found: $LogsDir"
}

# ── SQLite DB stats ───────────────────────────────────────────
$Lines += Section "Database Stats"
if (Test-Path $DbPath) {
    $Lines += "DB path: $DbPath"
    $Lines += "DB size: $([Math]::Round((Get-Item $DbPath).Length / 1KB, 1)) KB"
    if (Test-Path $PythonVenv) {
        $sqlScript = @"
import sqlite3, sys
db = sqlite3.connect(sys.argv[1])
for t in ['samples','events','commands','peer_status']:
    try:
        c = db.execute(f'SELECT COUNT(*) FROM {t}')
        print(f'{t}: {c.fetchone()[0]} rows')
    except Exception as e:
        print(f'{t}: error — {e}')
"@
        $tempScript = Join-Path $env:TEMP "bcws_dbstats_$Timestamp.py"
        $sqlScript | Set-Content $tempScript -Encoding UTF8
        $Lines += (& $PythonVenv $tempScript $DbPath 2>&1)
        Remove-Item $tempScript -Force -ErrorAction SilentlyContinue
    }
} else {
    $Lines += "DB not found at: $DbPath"
}

# ── Config file ───────────────────────────────────────────────
$Lines += Section "Hub Config (hub_config.yaml)"
$ConfigFile = Join-Path $MigrationDir "config\hub_config.yaml"
if (Test-Path $ConfigFile) {
    $Lines += Get-Content $ConfigFile
} else {
    $Lines += "Config file not found: $ConfigFile"
}

# ── Write output ──────────────────────────────────────────────
$Lines | Out-File -FilePath $OutFile -Encoding UTF8

Write-Host ""
Write-Host "Diagnostics collected: $OutFile" -ForegroundColor Green
Write-Host ""
Write-Host "Lines: $($Lines.Count)    Size: $([Math]::Round((Get-Item $OutFile).Length / 1KB, 1)) KB"
Write-Host ""
Write-Host "Attach this file when reporting issues." -ForegroundColor Cyan
