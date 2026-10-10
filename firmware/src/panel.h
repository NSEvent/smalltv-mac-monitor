#pragma once
#include <stdint.h>

void panelBegin(uint8_t rotation, bool bgr, uint8_t brightPct);
void panelSetBright(uint8_t pct);
void panelFill(int x, int y, int w, int h, uint16_t color);
void panelText(int x, int y, const char *s, uint16_t color, uint8_t size);
int panelTextW(const char *s, uint8_t size);
void panelField(int x, int y, int w, int h, const char *text, uint16_t color,
                uint8_t size, int tx = 0, int ty = 0, uint16_t background = 0);
void panelBar(int x, int y, int w, int h, int fill, uint16_t color);
void panelSegments(int x, int y, int w, int h, int percent,
                   uint16_t color, uint16_t track, uint16_t background);
void panelTrace(int x, int y, int w, int h, const uint8_t *values, int count,
                uint16_t color, uint16_t grid, uint16_t background);

static const int kScreen = 240;
static const uint16_t kBlack = 0x0000;
static const uint16_t kWhite = 0xFFFF;
static const uint16_t kDim = 0x7BEF;
static const uint16_t kTrack = 0x2104;
static const uint16_t kGreen = 0x07E0;
static const uint16_t kYellow = 0xFFE0;
static const uint16_t kRed = 0xF800;
static const uint16_t kOrange = 0xFD20;
