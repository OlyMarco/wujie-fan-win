/* fantray.c - Wujie 16 Pro fan control systray app
 *
 * Menu layout:
 *   FAN1 rpm/maxRPM
 *   FAN2 rpm/maxRPM
 *   TEMP cpu
 *   MODE <mode>
 *   ---
 *   Custom...            (slider popup, 1-123 per fan)
 *   ---
 *   auto / silent 40 / office 50 / balanced 60 / task 70 / game 80 / max
 *   ---
 *   Exit
 *
 * max mode: target dynamically tracks CPU load in 80-123.
 */
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ec_fan.h"

#define WM_TRAYICON   (WM_USER + 1)
#define IDC_STATUS_F1 100
#define IDC_STATUS_F2 101
#define IDC_STATUS_T  102
#define IDC_STATUS_M  103
#define IDC_CUSTOM    110
#define IDC_SLIDER1   121
#define IDC_SLIDER2   122
#define IDC_PCT1      123
#define IDC_PCT2      124
#define IDC_PRESET0   200   /* +0..6 */
#define IDC_EXIT      300
#define TIMER_POLL    1

static HINSTANCE g_hInst;
static HWND      g_hPopup;
static NOTIFYICONDATAA g_nid;
static HMENU     g_menu;
static HFONT     g_hFont;
static char      g_mode[32] = "auto";
static int       g_p1 = -1, g_p2 = -1;
static int       g_max_target = 0;          /* max-mode dynamic target */
static BOOL      g_iconOk = FALSE;          /* tray icon added successfully */
static ULONGLONG g_prev_total = 0, g_prev_idle = 0;

/* ---- CPU usage 0-100 via GetSystemTimes ---- */
static int cpu_usage(void) {
    FILETIME idle, kernel, user;
    ULONGLONG t_idle, t_k, t_u, total;
    int load = 0;
    if (!GetSystemTimes(&idle, &kernel, &user)) return 0;
    t_idle = ((ULONGLONG)idle.dwHighDateTime << 32) | idle.dwLowDateTime;
    t_k    = ((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime;
    t_u    = ((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime;
    total  = t_k + t_u;
    if (g_prev_total > 0 && total > g_prev_total) {
        ULONGLONG d_total = total - g_prev_total;
        ULONGLONG d_idle  = (t_idle > g_prev_idle) ? (t_idle - g_prev_idle) : 0;
        if (d_total > 0) load = (int)(100 - 100 * d_idle / d_total);
        if (load < 0) load = 0;
        if (load > 100) load = 100;
    }
    g_prev_total = total;
    g_prev_idle  = t_idle;
    return load;
}

/* ---- self-drawn 32x32 fan icon (blue hub + 3 blades, alpha transparency) ---- */
static HICON create_fan_icon(void) {
    #define ISZ 32
    static unsigned char px[ISZ * ISZ * 4];   /* BGRA */
    static unsigned char mk[ISZ * ISZ / 8];   /* 1-bit AND mask */
    int x, y;
    memset(px, 0, sizeof px);
    memset(mk, 0xFF, sizeof mk);              /* 1 = transparent */
    for (y = 0; y < ISZ; y++) {
        for (x = 0; x < ISZ; x++) {
            double dx = x - 15.5, dy = y - 15.5;
            int draw = 0;
            int b;
            if (dx * dx + dy * dy <= 3.2 * 3.2) draw = 1;        /* hub */
            for (b = 0; b < 3; b++) {                            /* 3 blades */
                double ang = b * 2.0 * 3.14159265358979 / 3.0;
                double ox = 7.0 * sin(ang), oy = -7.0 * cos(ang);
                double pxd = dx - ox, pyd = dy - oy;
                double rx =  pxd * cos(ang) + pyd * sin(ang);
                double ry = -pxd * sin(ang) + pyd * cos(ang);
                if ((rx / 5.5) * (rx / 5.5) + (ry / 2.6) * (ry / 2.6) <= 1.0) draw = 1;
            }
            if (draw) {
                int idx = (y * ISZ + x) * 4;
                px[idx + 0] = 255;  /* B */
                px[idx + 1] = 170;  /* G */
                px[idx + 2] = 80;   /* R */
                px[idx + 3] = 255;  /* A */
                mk[(y * ISZ + x) / 8] &= ~(0x80 >> (x % 8));     /* 0 = opaque */
            }
        }
    }
    {
        BITMAPINFOHEADER bih = {0};
        HBITMAP color, hmask;
        void *bits;
        ICONINFO ii = {0};
        HICON ico;
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = ISZ; bih.biHeight = ISZ;
        bih.biPlanes = 1; bih.biBitCount = 32;
        bih.biCompression = BI_RGB; bih.biSizeImage = ISZ * ISZ * 4;
        color = CreateDIBSection(NULL, (BITMAPINFO *)&bih, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!color) return NULL;
        memcpy(bits, px, ISZ * ISZ * 4);
        hmask = CreateBitmap(ISZ, ISZ, 1, 1, mk);
        ii.fIcon = TRUE;
        ii.hbmMask = hmask;
        ii.hbmColor = color;
        ico = CreateIconIndirect(&ii);
        DeleteObject(color);
        DeleteObject(hmask);
        return ico;
    }
}

/* ---- status line update + tooltip ---- */
static void do_status(HMENU menu, char *out, int cch) {
    int r1 = ec_read_rpm(1), r2 = ec_read_rpm(2);
    int t1 = ec_read_temp(0);
    char mode[32]; int p1, p2;
    MENUITEMINFOA mii;
    char b[64];
    int custom, i;
    if (fan_state_load(mode, &p1, &p2) != 0) lstrcpyA(mode, g_mode);
    lstrcpyA(g_mode, mode);
    if (p1 >= 0) { g_p1 = p1; g_p2 = p2; }
    if (strcmp(mode, "custom") == 0)
        snprintf(out, cch, "FAN1 %d/%d  FAN2 %d/%d  TEMP %d  MODE custom %d/%d",
                 r1, g_config.fan_max_rpm, r2, g_config.fan_max_rpm, t1, p1, p2);
    else
        snprintf(out, cch, "FAN1 %d/%d  FAN2 %d/%d  TEMP %d  MODE %s",
                 r1, g_config.fan_max_rpm, r2, g_config.fan_max_rpm, t1, mode);
    if (!menu) return;
    mii.cbSize = sizeof(MENUITEMINFOA);
    mii.fMask = MIIM_STRING | MIIM_STATE | MIIM_ID;
    mii.fState = MFS_DISABLED;
    mii.dwTypeData = b;
    snprintf(b, sizeof b, "FAN1 %d/%d", r1, g_config.fan_max_rpm);
    mii.wID = IDC_STATUS_F1; SetMenuItemInfoA(menu, IDC_STATUS_F1, FALSE, &mii);
    snprintf(b, sizeof b, "FAN2 %d/%d", r2, g_config.fan_max_rpm);
    mii.wID = IDC_STATUS_F2; SetMenuItemInfoA(menu, IDC_STATUS_F2, FALSE, &mii);
    snprintf(b, sizeof b, "TEMP %d", t1);
    mii.wID = IDC_STATUS_T;  SetMenuItemInfoA(menu, IDC_STATUS_T,  FALSE, &mii);
    if (strcmp(mode, "custom") == 0)
        snprintf(b, sizeof b, "MODE custom %d/%d", p1, p2);
    else
        snprintf(b, sizeof b, "MODE %s", mode);
    mii.wID = IDC_STATUS_M;  SetMenuItemInfoA(menu, IDC_STATUS_M,  FALSE, &mii);
    custom = (strcmp(mode, "custom") == 0);
    CheckMenuItem(menu, IDC_CUSTOM, custom ? MF_CHECKED : MF_UNCHECKED);
    for (i = 0; i < fan_preset_count; i++)
        CheckMenuItem(menu, IDC_PRESET0 + i,
            (!custom && strcmp(mode, fan_presets[i].name) == 0) ? MF_CHECKED : MF_UNCHECKED);
}

/* ---- slider popup ---- */
static void apply_custom_from_sliders(void) {
    int v1 = (int)SendMessage(GetDlgItem(g_hPopup, IDC_SLIDER1), TBM_GETPOS, 0, 0);
    int v2 = (int)SendMessage(GetDlgItem(g_hPopup, IDC_SLIDER2), TBM_GETPOS, 0, 0);
    ec_apply_custom(v1, v2);
    SetDlgItemInt(g_hPopup, IDC_PCT1, v1, FALSE);
    SetDlgItemInt(g_hPopup, IDC_PCT2, v2, FALSE);
}

static LRESULT CALLBACK popup_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_ACTIVATE:
        if (LOWORD(w) == WA_INACTIVE) ShowWindow(h, SW_HIDE);
        return 0;
    case WM_HSCROLL:
        if (LOWORD(w) == TB_ENDTRACK || LOWORD(w) == TB_THUMBPOSITION)
            apply_custom_from_sliders();
        SetDlgItemInt(h, IDC_PCT1, (int)SendMessage(GetDlgItem(h, IDC_SLIDER1), TBM_GETPOS, 0, 0), FALSE);
        SetDlgItemInt(h, IDC_PCT2, (int)SendMessage(GetDlgItem(h, IDC_SLIDER2), TBM_GETPOS, 0, 0), FALSE);
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void create_popup(void) {
    WNDCLASSA wc = {0};
    RECT r;
    HWND s1, s2, c;
    NONCLIENTMETRICSA ncm;
    wc.lpfnWndProc = popup_proc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = "fantray_popup";
    RegisterClassA(&wc);

    ncm.cbSize = sizeof(NONCLIENTMETRICSA);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g_hFont = CreateFontIndirectA(&ncm.lfMessageFont);

    SystemParametersInfoA(SPI_GETWORKAREA, 0, &r, 0);
    g_hPopup = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, "fantray_popup",
        "Fan Control", WS_POPUP | WS_BORDER | WS_CAPTION,
        r.right - 300, r.bottom - 200, 290, 175,
        NULL, NULL, g_hInst, NULL);
    CreateWindowExA(0, "STATIC", "fan1", WS_CHILD | WS_VISIBLE,
        12, 20, 40, 20, g_hPopup, NULL, g_hInst, NULL);
    s1 = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        55, 16, 170, 26, g_hPopup, (HMENU)IDC_SLIDER1, g_hInst, NULL);
    CreateWindowExA(0, "STATIC", "50", WS_CHILD | WS_VISIBLE | SS_CENTER,
        232, 20, 40, 20, g_hPopup, (HMENU)IDC_PCT1, g_hInst, NULL);
    CreateWindowExA(0, "STATIC", "fan2", WS_CHILD | WS_VISIBLE,
        12, 60, 40, 20, g_hPopup, NULL, g_hInst, NULL);
    s2 = CreateWindowExA(0, TRACKBAR_CLASSA, "", WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
        55, 56, 170, 26, g_hPopup, (HMENU)IDC_SLIDER2, g_hInst, NULL);
    CreateWindowExA(0, "STATIC", "50", WS_CHILD | WS_VISIBLE | SS_CENTER,
        232, 60, 40, 20, g_hPopup, (HMENU)IDC_PCT2, g_hInst, NULL);
    CreateWindowExA(0, "STATIC", "drag to set fan target (1-123)",
        WS_CHILD | WS_VISIBLE, 12, 100, 260, 20, g_hPopup, NULL, g_hInst, NULL);
    /* apply GUI font to all children */
    c = GetWindow(g_hPopup, GW_CHILD);
    while (c) { SendMessage(c, WM_SETFONT, (WPARAM)g_hFont, 0); c = GetWindow(c, GW_HWNDNEXT); }
    SendMessage(s1, TBM_SETRANGE, TRUE, MAKELPARAM(1, g_config.fan_max_target));
    SendMessage(s2, TBM_SETRANGE, TRUE, MAKELPARAM(1, g_config.fan_max_target));
    SendMessage(s1, TBM_SETPOS, TRUE, 50);
    SendMessage(s2, TBM_SETPOS, TRUE, 50);
}

/* sync slider positions to current mode/target when opening popup */
static void sync_sliders(void) {
    char mode[32]; int p1, p2;
    int v1 = 50, v2 = 50;
    if (fan_state_load(mode, &p1, &p2) == 0) {
        if (strcmp(mode, "custom") == 0) {
            v1 = p1; v2 = p2;
        } else {
            int i, found = 0;
            for (i = 0; i < fan_preset_count; i++)
                if (strcmp(mode, fan_presets[i].name) == 0) { found = 1; break; }
            if (found && fan_presets[i].percent > 0) {
                v1 = v2 = fan_presets[i].percent;
            } else {
                v1 = ec_read_target(1); v2 = ec_read_target(2);
            }
        }
    } else {
        v1 = ec_read_target(1); v2 = ec_read_target(2);
    }
    if (v1 < 1) v1 = 50; if (v1 > g_config.fan_max_target) v1 = g_config.fan_max_target;
    if (v2 < 1) v2 = 50; if (v2 > g_config.fan_max_target) v2 = g_config.fan_max_target;
    SendMessage(GetDlgItem(g_hPopup, IDC_SLIDER1), TBM_SETPOS, TRUE, v1);
    SendMessage(GetDlgItem(g_hPopup, IDC_SLIDER2), TBM_SETPOS, TRUE, v2);
    SetDlgItemInt(g_hPopup, IDC_PCT1, v1, FALSE);
    SetDlgItemInt(g_hPopup, IDC_PCT2, v2, FALSE);
}

static void build_menu(void) {
    int i;
    g_menu = CreatePopupMenu();
    AppendMenuA(g_menu, MF_STRING | MF_DISABLED, IDC_STATUS_F1, "FAN1 -/-");
    AppendMenuA(g_menu, MF_STRING | MF_DISABLED, IDC_STATUS_F2, "FAN2 -/-");
    AppendMenuA(g_menu, MF_STRING | MF_DISABLED, IDC_STATUS_T,  "TEMP -");
    AppendMenuA(g_menu, MF_STRING | MF_DISABLED, IDC_STATUS_M,  "MODE -");
    AppendMenuA(g_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_menu, MF_STRING, IDC_CUSTOM, "Custom...");
    for (i = 0; i < fan_preset_count; i++)
        AppendMenuA(g_menu, MF_STRING, IDC_PRESET0 + i, fan_presets[i].desc);
    AppendMenuA(g_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_menu, MF_STRING, IDC_EXIT, "Exit");
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    POINT pt;
    char status[192];
    switch (m) {
    case WM_TRAYICON:
        if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) {
            GetCursorPos(&pt);
            SetForegroundWindow(h);
            do_status(g_menu, status, sizeof status);
            TrackPopupMenu(g_menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_CUSTOM:
            sync_sliders();
            ShowWindow(g_hPopup, SW_SHOWNORMAL);
            SetForegroundWindow(g_hPopup);
            return 0;
        case IDC_EXIT:
            Shell_NotifyIconA(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            return 0;
        default:
            if (LOWORD(w) >= IDC_PRESET0 && LOWORD(w) < IDC_PRESET0 + fan_preset_count) {
                int idx = LOWORD(w) - IDC_PRESET0;
                ec_apply_preset(fan_presets[idx].name);
                if (strcmp(fan_presets[idx].name, "max") == 0)
                    g_max_target = 0;   /* let dynamic loop re-seed */
            }
            return 0;
        }
    case WM_TIMER:
        if (w == TIMER_POLL) {
            if (!g_iconOk) g_iconOk = Shell_NotifyIconA(NIM_ADD, &g_nid);
            do_status(NULL, status, sizeof status);
            lstrcpynA(g_nid.szTip, status, sizeof g_nid.szTip);
            if (g_iconOk) Shell_NotifyIconA(NIM_MODIFY, &g_nid);
            if (strcmp(g_mode, "max") == 0) {
                int load = cpu_usage();
                int mn = g_config.max_min, mx = g_config.max_max, stp = g_config.max_ramp_step;
                int target = mn + (mx - mn) * load / 100;
                int diff;
                if (g_max_target == 0) g_max_target = ec_read_target(1);
                if (g_max_target < mn) g_max_target = mn;
                diff = target - g_max_target;
                if (diff > stp) diff = stp;
                if (diff < -stp) diff = -stp;
                g_max_target += diff;
                if (g_max_target < mn) g_max_target = mn;
                if (g_max_target > mx) g_max_target = mx;
                ec_set_manual(1);
                ec_set_target(1, g_max_target);
                ec_set_target(2, g_max_target);
            }
        }
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_POLL);
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmd, int show) {
    WNDCLASSA wc = {0};
    MSG msg;
    HWND h;
    char status[192];
    HICON ico;

    (void)prev; (void)cmd; (void)show;
    g_hInst = hInst;
    InitCommonControls();

    if (ec_init()) {
        MessageBoxA(NULL, "EC init failed (need admin + WINIO driver)", "fan-tray", MB_ICONERROR);
        return 1;
    }

    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.lpszClassName = "fantray_hidden";
    RegisterClassA(&wc);
    h = CreateWindowExA(0, "fantray_hidden", "", 0, 0, 0, 0, 0, NULL, NULL, hInst, NULL);

    create_popup();
    build_menu();

    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = h;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    ico = create_fan_icon();
    g_nid.hIcon = ico ? ico : LoadIcon(NULL, IDI_APPLICATION);
    do_status(NULL, status, sizeof status);
    lstrcpynA(g_nid.szTip, status, sizeof g_nid.szTip);
    g_iconOk = Shell_NotifyIconA(NIM_ADD, &g_nid);

    SetTimer(h, TIMER_POLL, g_config.poll_interval_ms, NULL);
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    ec_shutdown();
    return 0;
}
