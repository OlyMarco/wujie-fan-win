/* ecloop2.c - try ITE entry keys on both port pairs, detect echo */
#include <windows.h>
#include <stdio.h>

static int  (__stdcall *pGetPortVal)(unsigned short, unsigned long *, unsigned char);
static int  (__stdcall *pSetPortVal)(unsigned short, unsigned long, unsigned char);

static void outp8(unsigned short port, unsigned char v) { pSetPortVal(port, v, 1); }
static unsigned char inp8(unsigned short port) { unsigned long v=0; pGetPortVal(port, &v, 1); return (unsigned char)v; }

/* write config index/data on a given address port pair */
static void cfg_write(unsigned short base, unsigned char idx, unsigned char dat) {
    outp8(base, 0x2e); outp8(base+1, idx);   /* switch-to-SIO marker (as linux module) */
    outp8(base, 0x2f); outp8(base+1, dat);
}
static unsigned char cfg_read(unsigned short base, unsigned char idx) {
    outp8(base, 0x2e); outp8(base+1, idx);
    outp8(base, 0x2f);
    return inp8(base+1);
}
static void entry(unsigned short base, const unsigned char *seq, int n) {
    int i; for (i = 0; i < n; i++) outp8(base, seq[i]);
}

int main(void) {
    HMODULE h = LoadLibraryA("WinIo64.dll");
    unsigned short bases[2] = {0x4e, 0x2e};
    int bi, k;
    unsigned char k87_2[2] = {0x87,0x87};
    unsigned char k4e_a[4] = {0x87,0x01,0x55,0x55};
    unsigned char k4e_b[4] = {0x87,0x01,0x55,0xAA};
    unsigned char k87s[4]  = {0x87,0x01,0x55,0x55};

    if (!h) { printf("no dll\n"); return 1; }
    pGetPortVal = (int (__stdcall *)(unsigned short, unsigned long *, unsigned char))GetProcAddress(h, "GetPortVal");
    pSetPortVal = (int (__stdcall *)(unsigned short, unsigned long, unsigned char))GetProcAddress(h, "SetPortVal");

    for (bi = 0; bi < 2; bi++) {
        unsigned short b = bases[bi];
        for (k = 0; k < 3; k++) {
            unsigned char r20, r21, r12;
            entry(b, k==0?k87_2:k==1?k4e_a:k4e_b, k==0?2:4);
            r20 = cfg_read(b, 0x20);
            r21 = cfg_read(b, 0x21);
            r12 = cfg_read(b, 0x12);
            /* echo => r20==0x20 (idx echo) ; alive => differing meaningful bytes */
            printf("base 0x%02X key%d: reg20=%02X reg21=%02X reg12=%02X %s\n",
                   b, k, r20, r21, r12,
                   (r20!=r21 && !(r20==0x20&&r21==0x21) && r12!=0x12) ? "<== plausible" : "");
        }
    }
    FreeLibrary(h);
    return 0;
}
