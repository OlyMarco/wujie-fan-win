# start-fantray.ps1 - launch fantray with admin rights (quick wake-up)
$root = Split-Path -Parent $PSScriptRoot
$exe  = "$root\build\fantray.exe"

if (Get-Process -Name fantray -ErrorAction SilentlyContinue) {
    Write-Host "fantray is already running." -ForegroundColor Yellow
    exit 0
}
if (-not (Test-Path $exe)) {
    Write-Host "fantray.exe not found at $exe" -ForegroundColor Red
    Write-Host "Run scripts\build.ps1 first."
    exit 1
}
Start-Process $exe -Verb RunAs -WorkingDirectory (Split-Path $exe)
Write-Host "fantray launched (elevated)." -ForegroundColor Green
