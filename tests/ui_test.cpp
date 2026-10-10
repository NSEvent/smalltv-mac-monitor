#include "../firmware/src/app.h"
#include "../firmware/src/panel.h"
#include "../firmware/src/raster.h"
#include "../firmware/src/glcdfont.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint16_t screen[240 * 240];
static int fullClears = 0;
static int writes = 0;
bool netApMode() { return false; }
int panelTextW(const char *s, uint8_t size) { return strlen(s) * 6 * size; }
static void bounds(int x, int y, int w, int h) {
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && x + w <= 240 && y + h <= 240);
    ++writes;
}
void panelFill(int x, int y, int w, int h, uint16_t color) {
    bounds(x, y, w, h);
    if (w == 240 && h == 240) ++fullClears;
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) screen[j * 240 + i] = color;
}
static void copyRow(int x, int y, int w, const uint8_t *row) {
    for (int i = 0; i < w; ++i) screen[y * 240 + x + i] = row[2*i] * 256 + row[2*i+1];
}
void panelField(int x, int y, int w, int h, const char *s, uint16_t color,
                uint8_t size, int tx, int ty, uint16_t background) {
    bounds(x,y,w,h);
    assert(tx >= 0 && tx + panelTextW(s, size) <= w); // no truncated metric
    uint8_t row[480];
    for (int j = 0; j < h; ++j) {
        rasterTextRow(row,w,j,tx,ty,s,color,size,[](int n){return glcdFont[n];},background);
        copyRow(x,y+j,w,row);
    }
}
void panelBar(int x, int y, int w, int h, int fill, uint16_t color) {
    bounds(x, y, w, h);
    assert(fill >= 0 && fill <= w);
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i) screen[j * 240 + i] = i < x + fill ? color : 0;
}
void panelSegments(int x, int y, int w, int h, int p, uint16_t color,
                   uint16_t track, uint16_t background) {
    bounds(x,y,w,h);
    uint8_t row[480];
    rasterSegmentsRow(row,w,p,color,track,background);
    for (int j=0;j<h;++j) copyRow(x,y+j,w,row);
}
void panelTrace(int x, int y, int w, int h, const uint8_t *values, int count,
                uint16_t color, uint16_t grid, uint16_t background) {
    bounds(x,y,w,h);
    uint8_t row[480];
    for (int j=0;j<h;++j) {
        rasterTraceRow(row,w,h,j,values,count,color,grid,background);
        copyRow(x,y+j,w,row);
    }
}
static uint16_t at(int x, int y) { return screen[y * 240 + x]; }
int main() {
    Config c = {}; strcpy(c.ssid,"test");
    Reading r = {}; r.have=true; strcpy(r.name,"Mac Studio");
    r.hasCpu=r.hasGpu=r.hasMem=r.hasCpuT=r.hasGpuT=r.hasPower=r.hasFan=true;
    r.cpu=100; r.gpu=87; r.memU=206.4f; r.memT=512; r.cpuT=58; r.gpuT=47;
    r.power=123.4f; r.fan=1999;
    uiDraw(&c,&r,LINK_LIVE);
    assert(fullClears==0); // fields stream in place; no full-screen clear
    // Temperature squares: 36x36 at x=196, CPU y=40, GPU y=124.
    uint16_t cool=at(214,58);
    assert(cool!=0 && at(214,142)==cool);
    int before=writes; uiDraw(&c,&r,LINK_LIVE); assert(writes==before);
    r.cpuT=95; uiDraw(&c,&r,LINK_LIVE);
    assert(at(214,58)!=cool && at(214,142)==cool);
    for(int i=1;i<=24;++i) { r.okAt=i*1000; r.cpu=i*4; r.gpu=100-i*3; uiDraw(&c,&r,LINK_LIVE); }
    before=writes; uiDraw(&c,&r,LINK_LIVE); assert(writes==before);
    uiDraw(&c,&r,LINK_STALE); uiDraw(&c,&r,LINK_WAIT); uiDraw(&c,&r,LINK_SETUP);
    uiDraw(&c,&r,LINK_LIVE); assert(fullClears==0); // fields stream in place; no full-screen clear
    FILE *file=fopen("/tmp/smalltv-temp-squares.ppm","wb"); assert(file);
    fprintf(file,"P6\n240 240\n255\n");
    for(uint16_t p:screen) { unsigned char rgb[]={(unsigned char)(((p>>11)&31)*255/31),
        (unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
        fwrite(rgb,1,3,file); }
    fclose(file);
    puts("Temperature-squares UI bounds, caching, square colors, state transitions and render passed");
}
