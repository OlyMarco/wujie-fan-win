/* fanboot.c - boot silence: runs at system boot (before logon) as SYSTEM,
 * waits boot_delay_ms, then applies boot_preset (default silent) to quiet
 * fans ASAP. ec_init retries until the WinIo driver is ready.
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
