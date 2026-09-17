# install-tasks.ps1 - register FanTray + FanBoot scheduled tasks (logon trigger, elevated)
#requires -version 5
$root = Split-Path -Parent $PSScriptRoot
$tray = "$root\build\fantray.exe"
$boot = "$root\build\fanboot.exe"

# self-elevate
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$pr = New-Object Security.Principal.WindowsPrincipal($id)
if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "re-launching elevated..."
    Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Wait
    exit
}

if (-not (Test-Path $tray)) { Write-Host "build first (scripts\build.ps1)" -ForegroundColor Red; exit 1 }

schtasks /create /tn FanTray /tr ('"' + $tray + '"') /sc onlogon /rl highest /f
schtasks /create /tn FanBoot /tr ('"' + $boot + '"') /sc onstart /ru SYSTEM /f
Write-Host "tasks installed: FanTray (logon, elevated) + FanBoot (boot, SYSTEM) — fanboot silences fans before logon" -ForegroundColor Green
