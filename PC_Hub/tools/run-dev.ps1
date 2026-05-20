[CmdletBinding()]
param(
    [ValidateSet("DEBUG","INFO","WARNING","ERROR")]
    [string]$LogLevel = "DEBUG",
    [switch]$Headless
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir      = $PSScriptRoot
$MigrationDir   = Split-Path $ScriptDir -Parent
$RepoRoot       = Split-Path $MigrationDir -Parent
$VenvPython     = Join-Path $MigrationDir ".venv\Scripts\python.exe"
$ConfigFile     = Join-Path $MigrationDir "config\hub_config.yaml"

if (-not (Test-Path $VenvPython)) {
    Write-Host "Run .\tools\setup.ps1 first." -ForegroundColor Red
    exit 1
}

$HubArgs = @("--config", $ConfigFile, "--log-level", $LogLevel)
if ($Headless) { $HubArgs += "--headless" }

Write-Host ""
Write-Host "BCWS PC-Hub Dev Mode" -ForegroundColor Cyan
Write-Host "  Mobile UI: http://localhost:8080/mobile" -ForegroundColor Green
Write-Host "  Config   : $ConfigFile" -ForegroundColor Gray
Write-Host ""
Push-Location $RepoRoot
try {
    & $VenvPython -m PC_Hub_Migration.hub_core.main @HubArgs
}
finally {
    Pop-Location
}
