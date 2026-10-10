#include <Arduino.h>
#include <SPI.h>
#include "panel.h"
#include "glcdfont.h"
#include "raster.h"

// GeekMagic SmallTV / SmallTV-Ultra ESP-12F wiring (same map smalltv-mod and
// the ESPHome/Tasmota teardowns use). The ST7789 only clocks SPI mode 3;
// mode 2 leaves the panel black. Backlight is active-low.
static const int kSclk = 14;
static const int kMosi = 13;
static const int kDc = 0;
static const int kRst = 2;
static const int kCs = 15;
static const int kBl = 5;

static int gXOff = 0;
static int gYOff = 0;

static void cmd(uint8_t c) {
    digitalWrite(kDc, LOW);
    digitalWrite(kCs, LOW);
    SPI.transfer(c);
    digitalWrite(kCs, HIGH);
}

static void dat(uint8_t d) {
    digitalWrite(kDc, HIGH);
    digitalWrite(kCs, LOW);
    SPI.transfer(d);
    digitalWrite(kCs, HIGH);
}

static void dat16(uint16_t v) {
    dat((uint8_t)(v >> 8));
    dat((uint8_t)v);
}

static void datN(uint8_t c, const uint8_t *bytes, int n) {
    cmd(c);
    for (int i = 0; i < n; i++) dat(bytes[i]);
}

static void setWindow(int x, int y, int w, int h) {
    int x0 = x + gXOff;
    int y0 = y + gYOff;
    int x1 = x0 + w - 1;
    int y1 = y0 + h - 1;
    cmd(0x2A);
    dat16((uint16_t)x0);
    dat16((uint16_t)x1);
    cmd(0x2B);
    dat16((uint16_t)y0);
    dat16((uint16_t)y1);
    cmd(0x2C);
}

static void blast(uint16_t color, uint32_t count) {
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;
    uint8_t buf[128];
    for (int i = 0; i < 128; i += 2) {
        buf[i] = hi;
        buf[i + 1] = lo;
    }
    digitalWrite(kDc, HIGH);
    digitalWrite(kCs, LOW);
    while (count) {
        uint32_t n = count > 64 ? 64 : count;
        SPI.writeBytes(buf, n * 2);
        count -= n;
        if ((count & 0x3ff) == 0) yield();
    }
    digitalWrite(kCs, HIGH);
}

void panelSetBright(uint8_t pct) {
    if (pct > 100) pct = 100;
    // Active-low LED in a sealed cube. Slider 80 is about 15% on-time.
    // Slider 100 is about 18%. The panel still looks bright below that.
    int on = 8 + (int)pct * 38 / 100;
    analogWrite(kBl, 255 - on);
}

static void applyRotation(uint8_t r, bool bgr) {
    r &= 3;
    uint8_t mad = 0;
    gXOff = 0;
    gYOff = 0;
    switch (r) {
    case 1:
        mad = 0x60;  // MX | MV
        break;
    case 2:
        mad = 0xC0;  // MX | MY
        gYOff = 80;  // 240x320 controller, 240 glass
        break;
    case 3:
        mad = 0xA0;  // MY | MV
        gXOff = 80;
        break;
    default:
        mad = 0x00;
        break;
    }
    if (bgr) mad |= 0x08;
    cmd(0x36);
    dat(mad);
}

void panelBegin(uint8_t rotation, bool bgr, uint8_t brightPct) {
    pinMode(kBl, OUTPUT);
    analogWriteRange(255);
    analogWriteFreq(2000);
    panelSetBright(brightPct);

    pinMode(kDc, OUTPUT);
    pinMode(kCs, OUTPUT);
    digitalWrite(kCs, HIGH);
    pinMode(kRst, OUTPUT);
    digitalWrite(kRst, HIGH);

    SPI.begin();
    SPI.setDataMode(SPI_MODE3);
    SPI.setBitOrder(MSBFIRST);
    SPI.setFrequency(40000000);
    (void)kSclk;
    (void)kMosi;

    digitalWrite(kRst, LOW);
    delay(20);
    digitalWrite(kRst, HIGH);
    delay(120);
    cmd(0x01);  // SWRESET
    delay(120);

    cmd(0x11);  // SLPOUT
    delay(120);
    cmd(0x3A);
    dat(0x55);  // 16-bit
    static const uint8_t ramctrl[] = {0x00, 0xF0};
    datN(0xB0, ramctrl, 2);
    static const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    datN(0xB2, porch, 5);
    cmd(0xB7); dat(0x35);
    cmd(0xBB); dat(0x19);
    cmd(0xC0); dat(0x2C);
    cmd(0xC2); dat(0x01);
    cmd(0xC3); dat(0x12);
    cmd(0xC4); dat(0x20);
    cmd(0xC6); dat(0x0F);
    static const uint8_t pw[] = {0xA4, 0xA1};
    datN(0xD0, pw, 2);
    static const uint8_t gammaP[] = {
        0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C, 0x44, 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21};
    static const uint8_t gammaN[] = {
        0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B, 0x44, 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21};
    datN(0xE0, gammaP, 14);
    datN(0xE1, gammaN, 14);
    cmd(0x13);  // NORON
    delay(10);
    cmd(0x29);  // DISPON
    cmd(0x21);  // INVON — IPS panel, same as smalltv-mod ips=true
    applyRotation(rotation, bgr);
    panelFill(0, 0, kScreen, kScreen, kBlack);
}

void panelFill(int x, int y, int w, int h, uint16_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= kScreen || y >= kScreen) return;
    if (x + w > kScreen) w = kScreen - x;
    if (y + h > kScreen) h = kScreen - y;
    if (w <= 0 || h <= 0) return;
    setWindow(x, y, w, h);
    blast(color, (uint32_t)w * (uint32_t)h);
}

static void drawGlyph(int x, int y, char ch, uint16_t color, uint8_t size) {
    if (ch < 32 || ch > 126) ch = '?';
    if (size < 1 || size > 8) return;
    int gw = 6 * size;
    int gh = 8 * size;
    if (x < 0 || y < 0 || x + gw > kScreen || y + gh > kScreen) return;
    const uint8_t *base = glcdFont + (int)(uint8_t)ch * 5;
    uint8_t bits[5];
    for (int i = 0; i < 5; i++) bits[i] = pgm_read_byte(base + i);
    setWindow(x, y, gw, gh);
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;
    uint8_t row[96];
    int nbytes = gw * 2;
    digitalWrite(kDc, HIGH);
    digitalWrite(kCs, LOW);
    for (int srcRow = 0; srcRow < 8; srcRow++) {
        int n = 0;
        for (int col = 0; col < 6; col++) {
            bool on = col < 5 && srcRow < 7 && (bits[col] & (1 << srcRow));
            uint8_t bhi = on ? hi : 0;
            uint8_t blo = on ? lo : 0;
            for (uint8_t s = 0; s < size; s++) {
                row[n++] = bhi;
                row[n++] = blo;
            }
        }
        for (uint8_t s = 0; s < size; s++) SPI.writeBytes(row, nbytes);
    }
    digitalWrite(kCs, HIGH);
}

void panelText(int x, int y, const char *s, uint16_t color, uint8_t size) {
    if (!s || size < 1) return;
    while (*s) {
        drawGlyph(x, y, *s, color, size);
        x += 6 * size;
        s++;
    }
}

int panelTextW(const char *s, uint8_t size) {
    if (!s) return 0;
    return (int)strlen(s) * 6 * size;
}

// One uninterrupted window transfer per field. No visible erase phase and
// no full framebuffer (240x240 RGB565 would consume 115200 bytes).
template <typename Compose>
static void streamRows(int x, int y, int w, int h, Compose compose) {
    if (w <= 0 || h <= 0 || x < 0 || y < 0 ||
        x + w > kScreen || y + h > kScreen) return;
    uint8_t row[kScreen * 2];
    setWindow(x, y, w, h);
    digitalWrite(kDc, HIGH);
    digitalWrite(kCs, LOW);
    for (int i = 0; i < h; ++i) {
        compose(row, i);
        SPI.writeBytes(row, w * 2);
    }
    digitalWrite(kCs, HIGH);
    yield();
}

void panelField(int x, int y, int w, int h, const char *text, uint16_t color,
                uint8_t size, int tx, int ty, uint16_t background) {
    if (!text || size < 1 || size > 8) return;
    streamRows(x, y, w, h, [&](uint8_t *row, int i) {
        rasterTextRow(row, w, i, tx, ty, text, color, size,
                      [](int offset) { return pgm_read_byte(glcdFont + offset); }, background);
    });
}

void panelBar(int x, int y, int w, int h, int fill, uint16_t color) {
    streamRows(x, y, w, h, [&](uint8_t *row, int i) {
        rasterBarRow(row, w, h, i, fill, color, kTrack);
    });
}

void panelSegments(int x, int y, int w, int h, int percent,
                   uint16_t color, uint16_t track, uint16_t background) {
    streamRows(x, y, w, h, [&](uint8_t *row, int) {
        rasterSegmentsRow(row, w, percent, color, track, background);
    });
}

void panelTrace(int x, int y, int w, int h, const uint8_t *values, int count,
                uint16_t color, uint16_t grid, uint16_t background) {
    streamRows(x, y, w, h, [&](uint8_t *row, int i) {
        rasterTraceRow(row, w, h, i, values, count, color, grid, background);
    });
}
