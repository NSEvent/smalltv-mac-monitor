#include "../firmware/src/raster.h"
#include "../firmware/src/glcdfont.c"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <initializer_list>

static uint16_t pixel(const uint8_t *row, int x) {
    return (uint16_t)(row[2 * x] << 8) | row[2 * x + 1];
}

// Independent reference: draw glyphs onto a cleared field, as the old renderer
// did on the glass. The new stream must produce exactly the final image.
static void checkText(const char *s, int w, int h, int size, int tx, int ty,
                      uint16_t color) {
    uint16_t expected[240 * 48] = {};
    for (size_t i = 0; i < strlen(s); ++i) {
        unsigned char ch = s[i];
        if (ch < 32 || ch > 126) ch = '?';
        for (int col = 0; col < 5; ++col) {
            for (int bit = 0; bit < 7; ++bit) {
                if (!(glcdFont[ch * 5 + col] & (1 << bit))) continue;
                for (int sy = 0; sy < size; ++sy) {
                    for (int sx = 0; sx < size; ++sx) {
                        int x = tx + (i * 6 + col) * size + sx;
                        int y = ty + bit * size + sy;
                        if (x >= 0 && x < w && y >= 0 && y < h)
                            expected[y * w + x] = color;
                    }
                }
            }
        }
    }
    uint8_t row[480];
    for (int y = 0; y < h; ++y) {
        memset(row, 255, sizeof row);
        rasterTextRow(row, w, y, tx, ty, s, color, size,
                      [](int offset) { return glcdFont[offset]; });
        for (int x = 0; x < w; ++x) assert(pixel(row, x) == expected[y * w + x]);
    }
}

int main() {
    checkText("100%", 150, 48, 6, 0, 0, 0xFFFF);
    checkText("9%", 150, 48, 6, 0, 0, 0x7BEF);
    checkText("--", 150, 48, 6, 0, 0, 0xFFFF);
    checkText("MAC STUDIO", 240, 18, 2, 60, 2, 0xFFFF);
    checkText("STALE", 240, 18, 2, 90, 2, 0xFD20);
    checkText("JOIN SmallTV-Mac", 232, 16, 2, 0, 0, 0xFFE0);
    checkText("12.3W", 116, 16, 2, 0, 0, 0xFFFF);
    checkText("", 116, 16, 2, 0, 0, 0xFFFF);
    checkText("long clipped text", 72, 16, 2, 0, 0, 0xFFFF);
    checkText("\1", 72, 16, 2, 0, 0, 0xFFFF);
    uint8_t row[480];
    for (int fill : {222, 1, 0, 100}) {
        for (int y = 0; y < 12; ++y) {
            memset(row, 255, sizeof row);
            rasterBarRow(row, 224, 12, y, fill, 0x07E0, 0x2104);
            for (int x = 0; x < 224; ++x) {
                bool filled = y >= 1 && y <= 10 && x >= 1 && x < 1 + fill;
                assert(pixel(row, x) == (filled ? 0x07E0 : 0x2104));
            }
        }
    }
    puts("raster regression tests passed");
    for (int p : {-10, 0, 1, 5, 6, 99, 100, 110}) {
        int clamped = p < 0 ? 0 : p > 100 ? 100 : p;
        rasterSegmentsRow(row,216,p,0xEDCA,0x3183,0x18A1);
        for (int x=0;x<216;++x) {
            uint16_t expected=x%11>=8 ? 0x18A1 : x/11*5<clamped ? 0xEDCA : 0x3183;
            assert(pixel(row,x)==expected);
        }
    }
    rasterTextRow(row,216,0,0,0,"",0xEDCA,2,[](int n){return glcdFont[n];},0x18A1);
    for(int x=0;x<216;++x) assert(pixel(row,x)==0x18A1);
}
