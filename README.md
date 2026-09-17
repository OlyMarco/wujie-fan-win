# wujie-fan-win

Fan control for the **Wujie 16 Pro** laptop on Windows — a tiny systray app plus CLI that drives the embedded controller (EC) fan targets directly, bypassing the vendor tooling.

Tray icon, live RPM/temperature readout, preset switching, a per-fan custom slider, and a `max` mode that rides CPU load between configurable bounds. No installer, no runtime, no telemetry — just C and a kernel I/O driver.

> Ported from the Linux [jpy794/wujie-fan-control](https://github.com/jpy794/wujie-fan-control) register map to Windows via [WinIo](https://github.com/yfdyh000/WinIo) (Super I/O `0x4E/0x4F` indirect EC SRAM access on the IT5570).

## Features

- **Systray UI** — fan icon, right-click menu with live `FAN1`/`FAN2` RPM, CPU temp, current mode, and one-click presets.
- **Presets** — `auto` (EC curve), `silent 40`, `office 50`, `balanced 60`, `task 70`, `game 80`, `max`. All editable in `fanctl.conf`.
- **Custom mode** — dual trackbar popup (1–123 per fan); opens at the current target, applies on drag, auto-hides when you click away.
- **Dynamic `max`** — target smoothly tracks CPU load between `max_min` and `max_max` (default 80–123).
- **Boot silence** — `fanboot` runs at system boot (before logon) as `SYSTEM`, waits `boot_delay_ms` (default 1s) then applies `boot_preset` (default silent), quieting fans ASAP. `fantray` also applies `boot_preset` on startup, so the fan always enters silent after boot regardless of the last mode used.
- **Config-driven** — every tunable (presets, intervals, RPM cap, max bounds, boot behaviour, state-file path) lives in `fanctl.conf`. Defaults are baked in if the file is missing.
- **Self-healing driver** — `ec_init` starts the WinIo kernel service itself and retries, so logon races don't break startup.

## Repository layout

```
src/              C sources — ec_fan.c/h (EC control), fantray.c (systray), fanctl.c (CLI), fanboot.c (boot silence)
res/              tray.rc + tray.manifest (ComCtl32 v6 visual styles)
config/           fanctl.conf — all tunables
scripts/          build.ps1, install-tasks.ps1, start-fantray.{bat,ps1}
third_party/winio/ WinIo64.dll/.sys/.def + libwinio64.a
build/            compiler output
```

## Prerequisites

- **Windows 10/11 x64** on a Wujie 16 Pro (IT5570 EC).
- **Administrator rights** to run (raw port I/O).

### 1. MinGW-w64 (WinLibs GCC)

`build.ps1` needs `gcc` + `windres`. Grab a WinLibs release:

1. Download the latest **x86_64 POSIX SEH** archive from <https://winlibs.com/>.
2. Extract the inner `mingw64` folder anywhere — e.g. `C:\Temp\mingw64` (which `build.ps1` checks first), or any path.
3. Either leave it at `C:\Temp\mingw64`, or add its `bin` subfolder to your `PATH` so `gcc` is discoverable.

### 2. WinIo kernel driver

The driver binaries (`WinIo64.dll`, `WinIo64.sys`, import lib) are bundled in `third_party/winio/`. The kernel service must be registered once, and because the driver is self-signed, test-signing must be enabled:

```powershell
# enable loading of self-signed kernel drivers (reboot required)
bcdedit /set testsigning on

# after reboot, register the service (run elevated, from the repo root)
sc create WINIO type= kernel start= demand binPath= "$PWD\third_party\winio\WinIo64.sys"
```

`ec_init` starts the `WINIO` service on demand at runtime, so you don't need to keep it running. If you move the repo, update the service path with `sc config WINIO binPath= "<new path>\WinIo64.sys"`.

> **Test-mode watermark** — enabling test-signing puts a "Test Mode" watermark in the bottom-right corner of the desktop. To hide it without disabling test-signing, use [wesmar/Watermark_Remover](https://github.com/wesmar/Watermark_Remover).

## Build

```powershell
PS> scripts\build.ps1
```

Outputs `fantray.exe`, `fanctl.exe`, `fanboot.exe` plus `WinIo64.dll/.sys` and `fanctl.conf` into `build\`.

## Install (autostart)

```powershell
PS> scripts\install-tasks.ps1
```

Registers two scheduled tasks: `FanTray` (logon trigger, elevated — shows the tray icon) and `FanBoot` (boot trigger, `SYSTEM` — silences fans before logon). Re-run after moving the project to update paths.

## Usage

**Tray** — right-click the fan icon:

```
FAN1 3782/5674
FAN2 3857/5674
TEMP 52
MODE silent
─────────────
Custom...
─────────────
auto
silent 40
office 50
balanced 60
task 70
game 80
max
─────────────
Exit
```

**CLI**

```
fanctl status                 show RPM / temp / mode
fanctl list                   show presets
fanctl silent                 apply a preset (both fans)
fanctl silent 1               apply to fan 1 only
fanctl custom 55 60           per-fan targets (1–123)
```

**Quick wake-up** — double-click `scripts\start-fantray.bat` to (re)launch the tray elevated if it isn't running.

## Configuration

`build\fanctl.conf` (copied from `config\fanctl.conf` by `build.ps1`) is read next to the exe at startup. Edit and restart the tray to apply.

```ini
poll_interval_ms=2000
fan_max_target=123
fan_max_rpm=5674
winio_service=WINIO
state_file=%PROGRAMDATA%\wujie-fan-state.txt

max_min=80
max_max=123
max_ramp_step=3

boot_delay_ms=1000
boot_preset=silent

[preset]
auto=-1        # -1 = EC automatic, 0-127 = manual EC duty target
silent=40
office=50
balanced=60
task=70
game=80
max=123
```

Missing keys fall back to compiled defaults, so deleting the file keeps everything working.

## Troubleshooting

- **Fans at full speed after a cold boot** — if the EC is left in manual mode when the machine powers off, the BIOS enters a "recovery" state on the next cold boot and runs the fan at full speed. `fantray` prevents this by restoring the EC to automatic mode (`auto`) on real power-off shutdown (`WM_ENDSESSION`), distinguished from restart via the `Reliability\ShutdownType` registry value. Restart, sleep, hibernate and logoff preserve the current mode. As long as `fantray` was running before a power-off shutdown, the next cold boot starts in `auto` mode and `fanboot` applies `silent` within 1s of Windows starting.
- **No tray icon after logon** — `Shell_NotifyIcon` can race explorer; the tray retries every poll interval, the icon appears within a couple of seconds.
- **Fans blast during POST (before Windows)** — the BIOS POST phase runs before Windows and is not controllable from the OS. With the EC in `auto` mode (ensured by graceful shutdown), the BIOS manages fan speed automatically based on temperature — typically not full speed. A warm reboot preserves EC state; only a cold boot (power off → power on) resets the EC.
- **Rebuild says "Permission denied" on fantray.exe** — the running tray holds the file. Stop it first: `sudo taskkill /F /IM fantray.exe` (or use the elevated rebuild flow).
## Credits

- Linux original & EC register map: [jpy794/wujie-fan-control](https://github.com/jpy794/wujie-fan-control)
- Port I/O: [WinIo](https://github.com/yfdyh000/WinIo)

## License

MIT — see [LICENSE](LICENSE). Third-party WinIo retains its own license (see `third_party/winio/`).
