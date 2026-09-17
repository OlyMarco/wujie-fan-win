/* ecloop.c - decisive test of i2ec read/write path */
#include <windows.h>
#include <stdio.h>

static int  (__stdcall *pInitializeWinIo)(void);
static int  (__stdcall *pGetPortVal)(unsigned short, unsigned long *, unsigned char);
static int  (__stdcall *pSetPortVal)(unsigned short, unsigned long, unsigned char);

static void sio_write(unsigned char addr, unsigned char data) {
    unsigned long v;
    v = 0x2e; pSetPortVal(0x4e, v, 1);
    v = addr; pSetPortVal(0x4f, v, 1);
    v = 0x2f; pSetPortVal(0x4e, v, 1);
    v = data; pSetPortVal(0x4f, v, 1);
}
static unsigned char sio_read(unsigned char addr) {
    unsigned long v;
    v = 0x2e; pSetPortVal(0x4e, v, 1);
    v = addr; pSetPortVal(0x4f, v, 1);
    v = 0x2f; pSetPortVal(0x4e, v, 1);
    pGetPortVal(0x4f, &v, 1);
    return (unsigned char)v;
}
static unsigned char i2ec_read(unsigned short a) {
    sio_write(0x10, a & 0xff);
    sio_write(0x11, a >> 8);
    return sio_read(0x12);
}
static void i2ec_write(unsigned short a, unsigned char d) {
    sio_write(0x10, a & 0xff);
    sio_write(0x11, a >> 8);
    sio_write(0x12, d);
}

int main(void) {
    HMODULE h = LoadLibraryA("WinIo64.dll");
    unsigned char en, t1, t2, m1, m2;
    if (!h) { printf("no dll\n"); return 1; }
    pGetPortVal = (int (__stdcall *)(unsigned short, unsigned long *, unsigned char))GetProcAddress(h, "GetPortVal");
    pSetPortVal = (int (__stdcall *)(unsigned short, unsigned long, unsigned char))GetProcAddress(h, "SetPortVal");
    pInitializeWinIo = (int (__stdcall *)(void))GetProcAddress(h, "InitializeWinIo");
    if (!pGetPortVal || !pSetPortVal || !pInitializeWinIo) { printf("no exports\n"); return 1; }
    if (!pInitializeWinIo()) { printf("InitializeWinIo failed (admin? driver?)\n"); return 1; }

    printf("0xd130 en: %02x\n", i2ec_read(0xd130));
    printf("0xd16f t1: %02x  0xd133 t2: %02x\n", i2ec_read(0xd16f), i2ec_read(0xd133));
    m1 = i2ec_read(0x181f); m2 = i2ec_read(0x181e);
    printf("rpm1 raw: %02x%02x\n", m1, m2);

    /* write test: target1 = 0x7F, read back */
    i2ec_write(0xd16f, 0x7F);
    t1 = i2ec_read(0xd16f);
    printf("write 7F -> read back 0xd16f: %02x (%s)\n", t1, t1 == 0x7F ? "OK" : "FAIL");

    /* enable manual + max both fans */
    i2ec_write(0xd130, 0x01);
    i2ec_write(0xd16f, 0x7F);
    i2ec_write(0xd133, 0x7F);
    Sleep(1500);
    m1 = i2ec_read(0x181f); m2 = i2ec_read(0x181e);
    printf("after max: en=%02x rpm1 raw %02x%02x", i2ec_read(0xd130), m1, m2);
    if (m1 || m2) {
        unsigned short sp = ((unsigned short)m1 << 8) | m2;
        if (sp >= 0x80 && sp < 0x4000) printf(" = %d RPM", 2156250 / sp);
    }
    printf("\n");
    FreeLibrary(h);
    return 0;
}
