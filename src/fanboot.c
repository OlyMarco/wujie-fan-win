/* fanboot.c - boot smooth ramp: wait 15s after logon, then apply silent preset.
 * Tray app is started immediately; this runs alongside.
 */
#include <windows.h>
#include <stdio.h>
#include "ec_fan.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmd, int show) {
    (void)hInst; (void)prev; (void)cmd; (void)show;
    config_load();
    Sleep(g_config.boot_delay_ms);      /* delay after logon, then boot preset */
    if (ec_init()) return 1;
    ec_apply_preset(g_config.boot_preset);
    ec_shutdown();
    return 0;
}
