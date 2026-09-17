/* ec_fan.c - EC fan control common module (Wujie 16 Pro register map)
 * Port of jpy794/wujie-fan-control via WinIo (Super I/O 0x4E/0x4F indirect EC SRAM) */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ec_fan.h"

/* ---- EC register map (wujie_fan.c v0) ---- */
#define FAN1_RPM_LSB    0x181e
#define FAN1_RPM_MSB    0x181f
#define FAN2_RPM_LSB    0x1820
#define FAN2_RPM_MSB    0x1821
#define EXT_FAN_CTRL_EN 0xd130
#define EXT_FAN1_TARGET 0xd16f
#define EXT_FAN2_TARGET 0xd133
#define EXT_CPU_TEMP    0xd118
#define EXT_ENV_TEMP    0xd115

#define PNP_ADDR   0x4e
#define PNP_DATA   0x4f
#define SIO_ADDR   0x2e
#define I2EC_DATA_IDX 0x2f   /* Super I/O data index byte for writes */
#define SIO_DATA   0x2f
#define I2EC_ADDR_L 0x10
#define I2EC_ADDR_H 0x11
#define I2EC_DATA   0x12

/* ---- runtime config + presets (loaded from fanctl.conf) ---- */
fan_config g_config;
fan_preset fan_presets[FAN_PRESET_MAX];
int       fan_preset_count = 0;
static int g_config_loaded = 0;

static void add_preset(const char *name, int target) {
    fan_preset *p;
    if (fan_preset_count >= FAN_PRESET_MAX) return;
    p = &fan_presets[fan_preset_count++];
    lstrcpyA(p->name, name);
    if (target < 0) lstrcpyA(p->desc, name);
    else snprintf(p->desc, sizeof p->desc, "%s %d", name, target);
    p->percent = target;
}

static void config_defaults(void) {
    char pd[MAX_PATH];
    g_config.poll_interval_ms = 2000;
    g_config.fan_max_target  = 123;
    g_config.fan_max_rpm     = 5674;
    lstrcpyA(g_config.winio_service, "WINIO");
    pd[0] = 0;
    GetEnvironmentVariableA("PROGRAMDATA", pd, MAX_PATH);
    snprintf(g_config.state_file, MAX_PATH, "%s\\wujie-fan-state.txt", pd);
    g_config.max_min       = 80;
    g_config.max_max       = 123;
    g_config.max_ramp_step = 3;
    g_config.boot_delay_ms = 15000;
    lstrcpyA(g_config.boot_preset, "silent");
    fan_preset_count = 0;
    add_preset("auto", -1);
    add_preset("silent", 40);
    add_preset("office", 50);
    add_preset("balanced", 60);
    add_preset("task", 70);
    add_preset("game", 80);
    add_preset("max", 123);
}

static char *trim(char *s) {
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == '\r' || e[-1] == '\n' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
    return s;
}

int config_load(void) {
    char exe[MAX_PATH], conf[MAX_PATH], line[256], section[32], *p, *eq, *k, *v;
    FILE *f;
    config_defaults();
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) { g_config_loaded = 1; return 1; }
    p = strrchr(exe, '\\');
    if (!p) { g_config_loaded = 1; return 1; }
    p[1] = 0;
    snprintf(conf, MAX_PATH, "%sfanctl.conf", exe);
    f = fopen(conf, "r");
    if (!f) { g_config_loaded = 1; return 1; }
    section[0] = 0;
    while (fgets(line, sizeof line, f)) {
        k = trim(line);
        if (*k == 0 || *k == '#' || *k == ';') continue;
        if (*k == '[') {
            sscanf(k, "[%31[^]]", section);
            if (strcmp(section, "preset") == 0) fan_preset_count = 0;
            continue;
        }
        eq = strchr(k, '=');
        if (!eq) continue;
        *eq = 0;
        v = trim(eq + 1);
        k = trim(k);
        if (strcmp(section, "preset") == 0) {
            add_preset(k, atoi(v));
        } else if (strcmp(k, "poll_interval_ms") == 0) g_config.poll_interval_ms = atoi(v);
        else if (strcmp(k, "fan_max_target") == 0)     g_config.fan_max_target  = atoi(v);
        else if (strcmp(k, "fan_max_rpm") == 0)        g_config.fan_max_rpm     = atoi(v);
        else if (strcmp(k, "winio_service") == 0)      lstrcpynA(g_config.winio_service, v, sizeof g_config.winio_service);
        else if (strcmp(k, "state_file") == 0)         ExpandEnvironmentStringsA(v, g_config.state_file, MAX_PATH);
        else if (strcmp(k, "max_min") == 0)            g_config.max_min       = atoi(v);
        else if (strcmp(k, "max_max") == 0)            g_config.max_max       = atoi(v);
        else if (strcmp(k, "max_ramp_step") == 0)      g_config.max_ramp_step = atoi(v);
        else if (strcmp(k, "boot_delay_ms") == 0)      g_config.boot_delay_ms = atoi(v);
        else if (strcmp(k, "boot_preset") == 0)        lstrcpynA(g_config.boot_preset, v, sizeof g_config.boot_preset);
    }
    fclose(f);
    g_config_loaded = 1;
    return 0;
}

/* ---- WinIo runtime binding ---- */
static int  (__stdcall *pInitializeWinIo)(void);
static void (__stdcall *pShutdownWinIo)(void);
static int  (__stdcall *pGetPortVal)(unsigned short, unsigned long *, unsigned char);
static int  (__stdcall *pSetPortVal)(unsigned short, unsigned long, unsigned char);
static HMODULE g_winio = NULL;
static HANDLE g_mutex = NULL;   /* cross-process EC access lock (tray vs CLI) */

static void ec_lock(void) {
    if (!g_mutex) g_mutex = CreateMutexA(NULL, FALSE, "Global\\WujieFanECLock");
    WaitForSingleObject(g_mutex, 1000);
}
static void ec_unlock(void) {
    if (g_mutex) ReleaseMutex(g_mutex);
}

static unsigned char superio_read(unsigned char addr) {
    unsigned long v = 0;
    pSetPortVal(PNP_ADDR, SIO_ADDR, 1);
    pSetPortVal(PNP_DATA, addr, 1);
    pSetPortVal(PNP_ADDR, SIO_DATA, 1);
    pGetPortVal(PNP_DATA, &v, 1);
    return (unsigned char)v;
}

static void superio_write(unsigned char addr, unsigned char data) {
    pSetPortVal(PNP_ADDR, SIO_ADDR, 1);
    pSetPortVal(PNP_DATA, addr, 1);
    pSetPortVal(PNP_ADDR, I2EC_DATA_IDX, 1);   /* data index 0x2f, NOT 0x4f */
    pSetPortVal(PNP_DATA, data, 1);
}

/* i2ec indirect EC SRAM access */
static void i2ec_write(unsigned short addr, unsigned char data) {
    superio_write(I2EC_ADDR_L, (unsigned char)(addr & 0xff));
    superio_write(I2EC_ADDR_H, (unsigned char)(addr >> 8));
    superio_write(I2EC_DATA, data);
}

static unsigned char i2ec_read(unsigned short addr) {
    superio_write(I2EC_ADDR_L, (unsigned char)(addr & 0xff));
    superio_write(I2EC_ADDR_H, (unsigned char)(addr >> 8));
    return superio_read(I2EC_DATA);
}

/* ensure the WINIO kernel driver service is running (start if needed) */
static void ensure_winio_driver(void) {
    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    SC_HANDLE svc;
    SERVICE_STATUS st;
    if (!scm) return;
    svc = OpenServiceA(scm, g_config.winio_service, SERVICE_START | SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return; }
    if (QueryServiceStatus(svc, &st) && st.dwCurrentState != SERVICE_RUNNING)
        StartServiceA(svc, 0, NULL);   /* ignore failure (already running/perms) */
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

int ec_init(void) {
    int tries;
    if (g_winio) return 0;
    if (!g_config_loaded) config_load();
    ensure_winio_driver();
    g_winio = LoadLibraryA("WinIo64.dll");
    if (!g_winio) return 1;
    pInitializeWinIo = (int (__stdcall *)(void))GetProcAddress(g_winio, "InitializeWinIo");
    pShutdownWinIo   = (void (__stdcall *)(void))GetProcAddress(g_winio, "ShutdownWinIo");
    pGetPortVal      = (int (__stdcall *)(unsigned short, unsigned long *, unsigned char))GetProcAddress(g_winio, "GetPortVal");
    pSetPortVal      = (int (__stdcall *)(unsigned short, unsigned long, unsigned char))GetProcAddress(g_winio, "SetPortVal");
    if (!pInitializeWinIo || !pShutdownWinIo || !pGetPortVal || !pSetPortVal) return 2;
    /* driver may still be loading; retry InitializeWinIo for up to ~15s */
    for (tries = 0; tries < 30; tries++) {
        if (pInitializeWinIo()) return 0;
        Sleep(500);
    }
    return 3;
}

void ec_shutdown(void) {
    /* Do NOT call ShutdownWinIo: it stops the kernel driver, breaking the
     * next process. Just unload our DLL reference; the driver stays running. */
    if (g_winio) { FreeLibrary(g_winio); g_winio = NULL; }
}

void ec_set_manual(int en) { ec_lock(); i2ec_write(EXT_FAN_CTRL_EN, en ? 1 : 0); ec_unlock(); }
int  ec_get_manual(void)    { int r; ec_lock(); r = i2ec_read(EXT_FAN_CTRL_EN); ec_unlock(); return r; }

void ec_set_target(int fan, int target) {
    if (target < 0) target = 0;
    if (target > 127) target = 127;
    ec_lock();
    i2ec_write(fan == 1 ? EXT_FAN1_TARGET : EXT_FAN2_TARGET, (unsigned char)target);
    ec_unlock();
}

int ec_read_target(int fan) {
    int r;
    ec_lock();
    r = i2ec_read(fan == 1 ? EXT_FAN1_TARGET : EXT_FAN2_TARGET);
    ec_unlock();
    return r;
}

int ec_read_rpm(int fan) {
    unsigned short lsb_addr, msb_addr, speed;
    unsigned char lsb, msb;
    if (fan == 1) { lsb_addr = FAN1_RPM_LSB; msb_addr = FAN1_RPM_MSB; }
    else          { lsb_addr = FAN2_RPM_LSB; msb_addr = FAN2_RPM_MSB; }
    ec_lock();
    msb = i2ec_read(msb_addr);
    lsb = i2ec_read(lsb_addr);
    ec_unlock();
    speed = ((unsigned short)msb << 8) | lsb;
    if (speed == 0 || speed >= 0x4000) return 0;
    if (speed < 0x80) return 9999;
    return 2156250 / speed;
}

int ec_read_temp(int which) {
    int r;
    ec_lock();
    r = i2ec_read(which == 0 ? EXT_CPU_TEMP : EXT_ENV_TEMP);
    ec_unlock();
    return r;
}

const char *fan_name_suffix(int fan) {
    return fan == 1 ? " (fan1)" : fan == 2 ? " (fan2)" : "";
}

/* Apply preset to a single fan. auto -> that fan follows EC again.
 * State file: keep "custom" semantics with the applied target on that fan. */
int ec_apply_preset_fan(const char *name, int fan) {
    int i;
    for (i = 0; i < fan_preset_count; i++) {
        if (strcmp(fan_presets[i].name, name) != 0) continue;
        {
            int v = fan_presets[i].percent;
            char mode[32]; int p1 = 50, p2 = 50;
            fan_state_load(mode, &p1, &p2);
            if (v < 0) {
                if (strcmp(mode, "custom") != 0) {
                    ec_set_manual(0);
                    fan_state_save("auto", -1, -1);
                } else {
                    ec_set_manual(1);
                    ec_set_target(fan, 50);
                    fan_state_save("custom", fan == 1 ? 50 : p1, fan == 2 ? 50 : p2);
                }
            } else {
                ec_set_manual(1);
                ec_set_target(fan, v);
                fan_state_save("custom", fan == 1 ? v : p1, fan == 2 ? v : p2);
            }
        }
        return 0;
    }
    return 1;
}

int ec_apply_preset(const char *name) {
    int i;
    for (i = 0; i < fan_preset_count; i++) {
        if (strcmp(fan_presets[i].name, name) == 0) {
            int v = fan_presets[i].percent;
            if (v < 0) {
                ec_set_manual(0);
                fan_state_save("auto", -1, -1);
            } else {
                ec_set_manual(1);
                ec_set_target(1, v);
                ec_set_target(2, v);
                fan_state_save(name, v, v);
            }
            return 0;
        }
    }
    return 1;
}

int ec_apply_percent(int pct) { return ec_apply_custom(pct, pct); }

int ec_apply_custom(int pct1, int pct2) {
    int cap = g_config.fan_max_target;
    if (pct1 < 1) pct1 = 1; if (pct1 > cap) pct1 = cap;
    if (pct2 < 1) pct2 = 1; if (pct2 > cap) pct2 = cap;
    ec_set_manual(1);
    ec_set_target(1, pct1);
    ec_set_target(2, pct2);
    fan_state_save("custom", pct1, pct2);
    return 0;
}

/* ---- shared state (tray <-> cli) ---- */
const char *fan_state_path(void) {
    return g_config.state_file;
}

int fan_state_load(char *mode, int *p1, int *p2) {
    FILE *f = fopen(fan_state_path(), "r");
    char buf[64];
    if (!f) return 1;
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return 1; }
    fclose(f);
    buf[strcspn(buf, "\r\n")] = 0;
    if (sscanf(buf, "%31s %d %d", mode, p1, p2) != 3) return 1;
    return 0;
}

int fan_state_save(const char *mode, int p1, int p2) {
    FILE *f = fopen(fan_state_path(), "w");
    if (!f) return 1;
    fprintf(f, "%s %d %d\n", mode, p1, p2);
    fclose(f);
    return 0;
}
