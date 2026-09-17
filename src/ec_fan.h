/* ec_fan.h - EC fan control common module (Wujie 16 Pro register map) */
#ifndef EC_FAN_H
#define EC_FAN_H

#include <windows.h>

/* presets: name, display label, EC target (0-127); -1 = auto */
#define FAN_PRESET_MAX 16
typedef struct {
    char name[32];
    char desc[64];
    int  percent;   /* -1 = auto (EC control), else EC target 0-127 */
} fan_preset;

extern fan_preset fan_presets[FAN_PRESET_MAX];
extern int        fan_preset_count;

/* runtime configuration (loaded from fanctl.conf next to the exe) */
typedef struct {
    int  poll_interval_ms;
    int  fan_max_target;
    int  fan_max_rpm;
    char winio_service[32];
    char state_file[MAX_PATH];
    int  max_min;
    int  max_max;
    int  max_ramp_step;
    int  boot_delay_ms;
    char boot_preset[32];
} fan_config;
extern fan_config g_config;

/* load config defaults + fanctl.conf. 0 if file parsed, nonzero if missing/fallback */
int  config_load(void);

/* load WinIo dll + init driver. returns 0 ok, nonzero fail */
int  ec_init(void);
void ec_shutdown(void);

/* enable/disable manual fan control (0xd130). en=1 manual, 0 auto */
void ec_set_manual(int en);
int  ec_get_manual(void);

/* target: 0-127. fan: 1 or 2 */
void ec_set_target(int fan, int target);
int  ec_read_target(int fan);   /* current EC target 0-127 */
int  ec_read_rpm(int fan);      /* returns RPM, or -1 */
int  ec_read_temp(int which);   /* 0=cpu(0xd118) 1=env(0xd115), or -1 */

/* preset helpers. returns 0 ok */
int ec_apply_preset(const char *name);            /* by name, both fans */
int ec_apply_preset_fan(const char *name, int fan); /* single fan; auto -> EC auto */
int ec_apply_percent(int pct);                    /* both fans, 1-123 */
int ec_apply_custom(int pct1, int pct2);          /* per-fan */
const char *fan_name_suffix(int fan);             /* " (fan1)" / " (fan2)" */

/* state file for tray/cli sharing: current mode name + custom targets */
const char *fan_state_path(void);
int  fan_state_load(char *mode, int *p1, int *p2);          /* 0 ok */
int  fan_state_save(const char *mode, int p1, int p2);      /* 0 ok */

#endif
