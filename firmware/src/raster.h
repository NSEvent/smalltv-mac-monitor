#pragma once
#include <stdint.h>
#include <stddef.h>

// Interpolate a 3-pixel trace on a fixed 0..100 scale. 255 denotes a gap.
inline void rasterTraceRow(uint8_t *row, int width, int height, int y,
                           const uint8_t *values, int count, uint16_t color,
                           uint16_t grid, uint16_t background) {
    for (int x = 0; x < width; ++x) {
        uint16_t pixel = y == 0 || y == height - 1 ? grid : background;
        int scaled = x * (count - 1);
        int a = scaled / (width - 1), fraction = scaled % (width - 1);
        int b = a < count - 1 ? a + 1 : a;
        if (values[a] <= 100 && (fraction == 0 || values[b] <= 100)) {
            int p = fraction == 0 ? values[a] :
                    (values[a] * (width - 1 - fraction) + values[b] * fraction) / (width - 1);
            int py = height - 1 - p * (height - 1) / 100;
            // Keep the full stroke visible at both scale limits.
            int top = py - 1;
            if (top < 0) top = 0;
            if (top > height - 3) top = height - 3;
            if (y >= top && y < top + 3) pixel = color;
        }
        row[2*x] = pixel >> 8;
        row[2*x+1] = pixel;
    }
}

// Compose final pixels, including background, before touching display RAM.
// Reader abstracts the flash-resident font for host-side regression tests.
template <typename Reader>
void rasterTextRow(uint8_t *row, int width, int y, int tx, int ty,
                   const char *text, uint16_t color, uint8_t size, Reader read,
                   uint16_t background = 0) {
    size_t length = 0;
    while (text[length]) ++length;
    for (int x = 0; x < width; ++x) {
        bool on = false;
        int dx = x - tx, dy = y - ty;
        if (size && dx >= 0 && dy >= 0 && dy < 7 * size) {
            int index = dx / (6 * size);
            int col = (dx / size) % 6;
            if ((size_t)index < length && col < 5) {
                uint8_t ch = (uint8_t)text[index];
                if (ch < 32 || ch > 126) ch = '?';
                on = read(ch * 5 + col) & (1 << (dy / size));
            }
        }
        uint16_t pixel = on ? color : background;
        row[x * 2] = pixel >> 8;
        row[x * 2 + 1] = pixel;
    }
}

inline void rasterSegmentsRow(uint8_t *row, int width, int percent,
                              uint16_t color, uint16_t track, uint16_t background) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    int lit = (percent + 4) / 5;
    for (int x = 0; x < width; ++x) {
        int segment = x / 11;
        uint16_t pixel = segment < 20 && x % 11 < 8
                         ? (segment < lit ? color : track) : background;
        row[2 * x] = pixel >> 8;
        row[2 * x + 1] = pixel;
    }
}

inline void rasterBarRow(uint8_t *row, int width, int height, int y,
                         int fill, uint16_t color, uint16_t track) {
    for (int x = 0; x < width; ++x) {
        uint16_t pixel = (y > 0 && y < height - 1 && x > 0 && x <= fill)
                         ? color : track;
        row[x * 2] = pixel >> 8;
        row[x * 2 + 1] = pixel;
    }
}
