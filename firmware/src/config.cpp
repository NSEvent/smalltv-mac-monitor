#include "app.h"
#include <LittleFS.h>

// Preconfiguration slot. scripts/configure-bin.py finds the magic in a built
// firmware.bin and fills these fields, so a prebuilt image can carry Wi-Fi and
// metrics settings without a compiler. It lives in flash (PROGMEM), which the
// ESP8266 ROM loader does not checksum. rev 0 means "not preconfigured".
struct PatchCfg {
    char magic[16];
    uint32_t rev;
    char ssid[33];
    char pass[65];
    char url[160];
    char fallback[160];
    char host[25];
    uint8_t rot;
    uint8_t bright;
    uint8_t units; // 0 = °F, 1 = °C
    uint8_t pad[46];
};
static_assert(sizeof(PatchCfg) == 512, "configure-bin.py expects a 512-byte slot");
static const PatchCfg kPatch PROGMEM __attribute__((used, aligned(4))) = {
    {'S','M','A','L','L','T','V','-','M','A','C','-','C','F','G','1'}, 0,
    {0}, {0}, {0}, {0}, {0}, 0, 0, 0, {0}};
static PatchCfg gPatch;

static bool loadPatch() {
    const void *src = &kPatch;
    asm volatile("" : "+r"(src)); // read the flash bytes, not the compile-time initializer
    memcpy_P(&gPatch, src, sizeof gPatch);
    gPatch.ssid[sizeof gPatch.ssid - 1] = 0;
    gPatch.pass[sizeof gPatch.pass - 1] = 0;
    gPatch.url[sizeof gPatch.url - 1] = 0;
    gPatch.fallback[sizeof gPatch.fallback - 1] = 0;
    gPatch.host[sizeof gPatch.host - 1] = 0;
    return gPatch.rev != 0 && gPatch.ssid[0];
}

static void applyPatch(Config *c) {
    strncpy(c->ssid, gPatch.ssid, sizeof c->ssid - 1);
    strncpy(c->pass, gPatch.pass, sizeof c->pass - 1);
    strncpy(c->url, gPatch.url, sizeof c->url - 1);
    strncpy(c->fallbackUrl, gPatch.fallback, sizeof c->fallbackUrl - 1);
    if (gPatch.host[0]) strncpy(c->host, gPatch.host, sizeof c->host - 1);
    c->rot = gPatch.rot & 3;
    if (gPatch.bright) c->bright = (uint8_t)constrain(gPatch.bright, 5, 100);
    c->celsius = gPatch.units ? 1 : 0;
}

static const char *kPath = "/mac.cfg";

void configDefaults(Config *c) {
    memset(c, 0, sizeof *c);
    c->bright = 30;
    strncpy(c->host, "smalltv-mac", sizeof c->host - 1);
}

static void copyField(char *dst, size_t n, const char *src) {
    size_t i = 0;
    if (!src) src = "";
    while (src[i] && i + 1 < n && src[i] != '\n' && src[i] != '\r') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int readStored(Config *c) {
    File f = LittleFS.open(kPath, "r");
    if (!f) return -1;
    int rev = -1;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (eq <= 0) continue;
        String key = line.substring(0, eq);
        String val = line.substring(eq + 1);
        if (key == "ssid") copyField(c->ssid, sizeof c->ssid, val.c_str());
        else if (key == "pass") copyField(c->pass, sizeof c->pass, val.c_str());
        else if (key == "url") copyField(c->url, sizeof c->url, val.c_str());
        else if (key == "fallback") copyField(c->fallbackUrl, sizeof c->fallbackUrl, val.c_str());
        else if (key == "host") copyField(c->host, sizeof c->host, val.c_str());
        else if (key == "rot") c->rot = (uint8_t)val.toInt() & 3;
        else if (key == "bright") c->bright = (uint8_t)constrain(val.toInt(), 5, 100);
        else if (key == "bgr") c->bgr = val.toInt() ? 1 : 0;
        else if (key == "units") c->celsius = (val == "C" || val == "c") ? 1 : 0;
        else if (key == "rev") rev = val.toInt();
    }
    f.close();
    if (!c->host[0]) strncpy(c->host, "smalltv-mac", sizeof c->host - 1);
    return rev;
}

void configLoad(Config *c) {
    if (!LittleFS.begin()) {
        LittleFS.format();
        LittleFS.begin();
    }
    configDefaults(c);
    int rev = readStored(c);
    // A newly preconfigured image carries a new rev. One boot replaces the
    // saved setup, then later edits from the web page stay until the next image.
    if (loadPatch() && rev != (int)gPatch.rev) {
        configDefaults(c);
        applyPatch(c);
        configSave(c);
    }
}

void configSave(const Config *c) {
    File f = LittleFS.open(kPath, "w");
    if (!f) return;
    f.printf("ssid=%s\npass=%s\nurl=%s\nfallback=%s\nhost=%s\nrot=%u\nbright=%u\nbgr=%u\nunits=%c\n",
             c->ssid, c->pass, c->url, c->fallbackUrl, c->host, c->rot, c->bright, c->bgr,
             c->celsius ? 'C' : 'F');
    if (gPatch.rev) f.printf("rev=%u\n", (unsigned)gPatch.rev);
    f.close();
}
