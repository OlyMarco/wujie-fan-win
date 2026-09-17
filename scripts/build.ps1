# build.ps1 - compile fanctl, fantray, fanboot into build/
#requires -version 5
$ErrorActionPreference = 'Stop'
$root    = Split-Path -Parent $PSScriptRoot
$src     = "$root\src"
$res     = "$root\res"
$winio   = "$root\third_party\winio"
$conf    = "$root\config"
$out     = "$root\build"

# locate gcc: prefer PATH, fall back to common WinLibs location
$gcc = $null
if (Test-Path 'C:\Temp\mingw64\bin\gcc.exe') { $gcc = 'C:\Temp\mingw64\bin\gcc.exe' }
if (-not $gcc) { $gcc = (Get-Command gcc.exe -ErrorAction SilentlyContinue).Source }
if (-not $gcc) { throw 'gcc not found. Install MinGW-w64 (WinLibs) or add it to PATH.' }
$windres = Join-Path (Split-Path $gcc) 'windres.exe'
Write-Host "using gcc: $gcc"

New-Item -ItemType Directory -Path $out -Force | Out-Null

# resource (manifest for visual styles)
Push-Location $res
& $windres 'tray.rc' -O coff -o "$out\tray_res.o"
Pop-Location

# fanctl (CLI)
& $gcc -O2 "$src\fanctl.c" "$src\ec_fan.c" -o "$out\fanctl.exe" -L"$winio" -lwinio64 -ladvapi32
# fantray (GUI + manifest)
& $gcc -O2 -mwindows "$src\fantray.c" "$src\ec_fan.c" "$out\tray_res.o" -o "$out\fantray.exe" -L"$winio" -lwinio64 -lcomctl32 -lshell32 -lgdi32 -luser32 -ladvapi32
# fanboot (GUI)
& $gcc -O2 -mwindows "$src\fanboot.c" "$src\ec_fan.c" -o "$out\fanboot.exe" -L"$winio" -lwinio64 -ladvapi32

# runtime deps next to the exe
Copy-Item "$winio\WinIo64.dll" "$out\" -Force
Copy-Item "$winio\WinIo64.sys" "$out\" -Force
Copy-Item "$conf\fanctl.conf"  "$out\" -Force

Write-Host "build complete -> $out" -ForegroundColor Green
