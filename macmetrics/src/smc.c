#include "smc.h"

#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <string.h>

typedef struct {
    char major, minor, build, reserved;
    uint16_t release;
} SMCVers;

typedef struct {
    uint16_t version, length;
    uint32_t cpuPLimit, gpuPLimit, memPLimit;
} SMCPLimit;

typedef struct {
    uint32_t dataSize;
    uint32_t dataType;
    uint8_t dataAttributes;
} SMCKeyInfo;

typedef struct {
    uint32_t key;
    SMCVers vers;
    SMCPLimit pLimit;
    SMCKeyInfo keyInfo;
    uint8_t result, status, data8;
    uint32_t data32;
    uint8_t bytes[32];
} SMCKeyData;

static io_connect_t g_conn = 0;

static uint32_t keycode(const char *s) {
    return ((uint32_t)(uint8_t)s[0] << 24) | ((uint32_t)(uint8_t)s[1] << 16) |
           ((uint32_t)(uint8_t)s[2] << 8) | (uint32_t)(uint8_t)s[3];
}

static int smc_call(SMCKeyData *in, SMCKeyData *out) {
    size_t out_size = sizeof(*out);
    return IOConnectCallStructMethod(g_conn, 2, in, sizeof(*in), out, &out_size) == 0;
}

int smc_open(void) {
    if (g_conn) return 0;
    io_service_t dev = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
    if (!dev) return -1;
    kern_return_t kr = IOServiceOpen(dev, mach_task_self(), 0, &g_conn);
    IOObjectRelease(dev);
    return kr == 0 ? 0 : -1;
}

void smc_close(void) {
    if (!g_conn) return;
    IOServiceClose(g_conn);
    g_conn = 0;
}

static float decode(uint32_t type, uint32_t size, const uint8_t *b, int *ok) {
    char t[5] = {(char)(type >> 24), (char)(type >> 16), (char)(type >> 8), (char)type, 0};
    *ok = 1;
    if (size == 4 && memcmp(t, "flt ", 4) == 0) {
        float f;
        memcpy(&f, b, 4);
        return f;
    }
    if (size == 2 && memcmp(t, "sp78", 4) == 0) {
        int16_t v = (int16_t)((b[0] << 8) | b[1]);
        return (float)v / 256.0f;
    }
    if (size == 2 && memcmp(t, "fpe2", 4) == 0) {
        uint16_t v = (uint16_t)((b[0] << 8) | b[1]);
        return (float)v / 4.0f;
    }
    if (size == 1 && memcmp(t, "ui8 ", 4) == 0) return (float)b[0];
    if (size == 2 && memcmp(t, "ui16", 4) == 0) return (float)((b[0] << 8) | b[1]);
    if (size == 4 && memcmp(t, "ui32", 4) == 0) {
        return (float)(((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3]);
    }
    *ok = 0;
    return 0;
}

int smc_read_f(const char *key, float *out) {
    if (!g_conn || !key || !out || strlen(key) != 4) return -1;
    SMCKeyData in, info;
    memset(&in, 0, sizeof in);
    memset(&info, 0, sizeof info);
    in.key = keycode(key);
    in.data8 = 9; /* read key info */
    if (!smc_call(&in, &info) || info.keyInfo.dataSize == 0 || info.keyInfo.dataSize > 32) return -1;

    SMCKeyData val;
    memset(&in, 0, sizeof in);
    memset(&val, 0, sizeof val);
    in.key = keycode(key);
    in.keyInfo.dataSize = info.keyInfo.dataSize;
    in.data8 = 5; /* read bytes */
    if (!smc_call(&in, &val)) return -1;

    int ok = 0;
    float f = decode(info.keyInfo.dataType, info.keyInfo.dataSize, val.bytes, &ok);
    if (!ok) return -1;
    *out = f;
    return 0;
}
