/* probe.c - Super I/O / EC access diagnostics
 * Checks both 0x2E/0x2F and 0x4E/0x4F entry ports, reads chip ID,
 * and dumps the fan-related i2ec registers.
 */
#include <windows.h>
#include <stdio.h>

static int  (__stdcall *pGetPortVal)(unsigned short, unsigned long *, unsigned char);
static int  (__stdcall *pSetPortVal)(unsigned short, unsigned long, unsigned char);

static void pnp_write(unsigned short base, unsigned char addr, unsigned char data) {
    unsigned long v = addr;
    pSetPortVal(base, v, 1);
    v = data;
    pSetPortVal(base + 1, v, 1);
}

static unsigned char pnp_read(unsigned short base, unsigned char addr) {
    unsigned long v = addr;
    pSetPortVal(base, v, 1);
    pGetPortVal(base + 1, &v, 1);
    return (unsigned char)v;
}

/* enter MB PnP mode: try sequences; verify by reading chip ID */
static void enter_mb(const char *label, unsigned short base) {
    unsigned long v;
    int k;
    const int seqs[][4] = { {0x87,0x87,0x01,0x55}, {0x87,0x01,0x55,0x55}, {0x87,0x01,0x55,0xAA} };
    for (k = 0; k < 3; k++) {
        int s;
        for (s = 0; s < 4; s++) { v = seqs[k][s]; pSetPortVal(base, v, 1); }
        if (pnp_read(base, 0x20) == 0x55 || pnp_read(base, 0x21) == 0x55 ||
            pnp_read(base, 0x20) != 0 || pnp_read(base, 0x21) != 0) {
            /* heuristic: non-echo response */
            if (pnp_read(base, 0x20) != 0x20) {
                printf("%s: entered with seq %d (id %02X %02X)\n", label, k,
                       pnp_read(base, 0x20), pnp_read(base, 0x21));
                return;
            }
        }
    }
    printf("%s: no entry sequence worked\n", label);
}

static void exit_mb(unsigned short base) {
    /* ITE exit: write config reg 0x02 bit1, or 0xAA to 0x2e-based */
    pnp_write(base, 0x02, 0x02);
}

int main(void) {
    HMODULE h = LoadLibraryA("WinIo64.dll");
    unsigned short bases[2] = {0x2e, 0x4e};
    const char *names[2] = {"0x2E/0x2F", "0x4E/0x4F"};
    int i, j;

    if (!h) { printf("no WinIo64.dll\n"); return 1; }
    pGetPortVal = (int (__stdcall *)(unsigned short, unsigned long *, unsigned char))GetProcAddress(h, "GetPortVal");
    pSetPortVal = (int (__stdcall *)(unsigned short, unsigned long, unsigned char))GetProcAddress(h, "SetPortVal");
    if (!pGetPortVal || !pSetPortVal) { printf("no exports\n"); return 1; }

    for (i = 0; i < 2; i++) {
        unsigned char id_hi, id_lo, ver;
        enter_mb(names[i], bases[i]);
        id_hi = pnp_read(bases[i], 0x20);
        id_lo = pnp_read(bases[i], 0x21);
        ver   = pnp_read(bases[i], 0x22);
        printf("%s: chip ID 0x%02X%02X ver 0x%02X\n", names[i], id_hi, id_lo, ver);
        /* try a few ldn reads to see if chip responds */
        for (j = 0x20; j <= 0x2F; j++)
            printf("  reg %02X = %02X%s", j, pnp_read(bases[i], j), (j % 8 == 0xF) ? "\n" : "  ");
        printf("\n");
        exit_mb(bases[i]);
    }
    FreeLibrary(h);
    return 0;
}
