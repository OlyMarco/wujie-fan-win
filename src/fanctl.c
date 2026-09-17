/* fanctl.c - Wujie 16 Pro fan control CLI (preset-based, English output)
 *
 * Usage:
 *   fanctl status              show RPM/temp/mode
 *   fanctl list                show presets
 *   fanctl auto                back to EC automatic control
 *   fanctl <preset> [fan]      apply preset; optional fan = 1 or 2 (default both)
 *   fanctl custom <p1> <p2>    per-fan percent 1-100
 *
 * Presets: auto, silent(40), office(50), balanced(60), task(70), game(80), max(80-123)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ec_fan.h"

static int find_preset(const char *name) {
    int i;
    for (i = 0; i < fan_preset_count; i++)
        if (strcmp(fan_presets[i].name, name) == 0) return i;
    return -1;
}

int main(int argc, char **argv) {
    int pi, fan = 0, i;

    config_load();
    if (argc < 2) {
        printf("usage: fanctl status|list|<preset> [fan 1|2]|custom <p1> <p2>\n");
        printf("presets:");
        for (i = 0; i < fan_preset_count; i++) printf(" %s", fan_presets[i].name);
        printf("\n");
        return 1;
    }

    if (strcmp(argv[1], "list") == 0) {
        int i;
        for (i = 0; i < fan_preset_count; i++)
            printf("%-8s %s\n", fan_presets[i].name, fan_presets[i].desc);
        return 0;
    }

    if (ec_init()) {
        printf("EC init failed (need admin, WINIO driver running)\n");
        return 1;
    }

    if (strcmp(argv[1], "status") == 0) {
        char mode[32]; int p1, p2;
        printf("fan1: %d RPM  fan2: %d RPM\n", ec_read_rpm(1), ec_read_rpm(2));
        printf("cpu temp: %d  env temp: %d\n", ec_read_temp(0), ec_read_temp(1));
        if (fan_state_load(mode, &p1, &p2) == 0)
            printf("mode: %s (%d/%d)\n", mode, p1, p2);
        else
            printf("mode: unknown\n");
        ec_shutdown();
        return 0;
    }

    if (strcmp(argv[1], "custom") == 0 && argc >= 4) {
        int a = atoi(argv[2]), b = atoi(argv[3]);
        if (a < 1 || a > g_config.fan_max_target || b < 1 || b > g_config.fan_max_target) {
            printf("custom: targets must be 1-%d\n", g_config.fan_max_target);
            ec_shutdown();
            return 1;
        }
        ec_apply_custom(a, b);
        printf("custom: fan1=%d fan2=%d\n", a, b);
        ec_shutdown();
        return 0;
    }

    pi = find_preset(argv[1]);
    if (pi < 0) {
        printf("unknown command: %s\n", argv[1]);
        ec_shutdown();
        return 1;
    }

    /* optional per-fan argument: fanctl silent 1 */
    if (argc >= 3) {
        fan = atoi(argv[2]);
        if (fan != 1 && fan != 2) {
            printf("fan argument must be 1 or 2\n");
            ec_shutdown();
            return 1;
        }
    }

    if (fan == 0)
        ec_apply_preset(fan_presets[pi].name);
    else
        ec_apply_preset_fan(fan_presets[pi].name, fan);
    printf("preset: %s%s\n", fan_presets[pi].desc, fan ? fan_name_suffix(fan) : "");

    ec_shutdown();
    return 0;
}
