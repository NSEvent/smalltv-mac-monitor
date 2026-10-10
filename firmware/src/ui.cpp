#include "app.h"
#include "panel.h"
#include <stdio.h>
#include <string.h>

static char gPrev[12][20];
static uint16_t gPrevC[12];
static int gPrevBar[3] = {-1, -1, -1};
static LinkState gPrevLink = LINK_SETUP;
static bool gForce = true;

static void fmt1(char *o, size_t n, float v) {
    if (v < 0) v = 0;
    int s = (int)(v * 10.0f + 0.5f);
    snprintf(o, n, "%d.%d", s / 10, s % 10);
}

static void fmtPct(char *o, size_t n, float v) {
    int p = (int)(v + 0.5f);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    snprintf(o, n, "%d%%", p);
}

static int pctOf(float v) {
    int p = (int)(v + 0.5f);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

static uint16_t barColor(int p) {
    if (p < 50) return kGreen;
    if (p < 80) return kYellow;
    return kRed;
}

// Apple silicon idles warm. Colors stay on the Celsius ranges
// (under 60, 60–74, 75–89, 90+), which is 140°F, 167°F, and 194°F.
static uint16_t gDot[2] = {0xFFFF, 0xFFFF};

static int cToF(float c) {
    return (int)(c * 9.0f / 5.0f + 32.0f + 0.5f);
}

// The Mac sends Celsius. Show it as-is or converted, per the cube's setting.
static void fmtTemp(char *out, size_t n, float c, bool celsius) {
    if (celsius) snprintf(out, n, "%dC", (int)(c + (c < 0 ? -0.5f : 0.5f)));
    else snprintf(out, n, "%dF", cToF(c));
}

static uint16_t tempColor(float c) {
    if (c < 60.0f) return kGreen;
    if (c < 75.0f) return kYellow;
    if (c < 90.0f) return kOrange;
    return kRed;
}

static void tempMark(int id, int x, int y, bool known, float c) {
    uint16_t col = known ? tempColor(c) : kBlack;
    if (!gForce && gDot[id] == col) return;
    panelFill(x, y, 36, 36, col);
    gDot[id] = col;
}

static void field(int id, int x, int y, int w, int h, const char *text, uint16_t color, uint8_t size) {
    if (!gForce && strcmp(gPrev[id], text) == 0 && gPrevC[id] == color) return;
    panelField(x, y, w, h, text, color, size);
    strncpy(gPrev[id], text, sizeof gPrev[id] - 1);
    gPrev[id][sizeof gPrev[id] - 1] = 0;
    gPrevC[id] = color;
}

static void bar(int id, int x, int y, int w, int h, int p, bool known) {
    if (!known) p = 0;
    if (!gForce && gPrevBar[id] == (known ? p : -1)) return;
    gPrevBar[id] = known ? p : -1;
    int fw = 0;
    if (known && p > 0) {
        fw = (w - 2) * p / 100;
        if (fw < 1) fw = 1;
    }
    panelBar(x, y, w, h, fw, barColor(p));
}

static void titleCase(char *dst, size_t n, const char *src) {
    size_t i = 0;
    for (; src && src[i] && i + 1 < n; i++) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        dst[i] = c;
    }
    dst[i] = 0;
}

void uiDraw(const Config *c, const Reading *r, LinkState link) {
    if (link != gPrevLink) gForce = true;
    uint16_t num = (link == LINK_LIVE) ? kWhite : kDim;
    char title[20];
    uint16_t tc = kWhite;
    if (link == LINK_SETUP) {
        strncpy(title, "SETUP", sizeof title);
        tc = kYellow;
    } else if (link == LINK_OFFLINE) {
        strncpy(title, "OFFLINE", sizeof title);
        tc = kRed;
    } else if (link == LINK_WAIT) {
        strncpy(title, "WAIT", sizeof title);
        tc = kYellow;
    } else if (link == LINK_STALE) {
        strncpy(title, "STALE", sizeof title);
        tc = kOrange;
    } else if (r->have && r->name[0]) {
        titleCase(title, sizeof title, r->name);
        tc = kWhite;
    } else {
        strncpy(title, "MAC", sizeof title);
    }
    int tw = panelTextW(title, 2);
    int tx = (kScreen - tw) / 2;
    if (tx < 0) tx = 0;
    if (gForce || strcmp(gPrev[0], title) != 0 || gPrevC[0] != tc) {
        panelField(0, 0, kScreen, 18, title, tc, 2, tx, 2);
        strncpy(gPrev[0], title, sizeof gPrev[0] - 1);
        gPrevC[0] = tc;
    }

    char cpuP[12] = "--";
    char gpuP[12] = "--";
    char cpuC[12] = "--";
    char gpuC[12] = "--";
    char mem[20] = "--";
    char memP[12] = "--";
    char watts[12] = "--";
    char fan[12] = "--";
    int cpuBar = 0, gpuBar = 0, memBar = 0;
    bool cpuKnown = false, gpuKnown = false, memKnown = false;

    if (link == LINK_LIVE || link == LINK_STALE) {
        if (r->hasCpu) { fmtPct(cpuP, sizeof cpuP, r->cpu); cpuBar = pctOf(r->cpu); cpuKnown = true; }
        if (r->hasGpu) { fmtPct(gpuP, sizeof gpuP, r->gpu); gpuBar = pctOf(r->gpu); gpuKnown = true; }
        if (r->hasCpuT) fmtTemp(cpuC, sizeof cpuC, r->cpuT, c->celsius);
        if (r->hasGpuT) fmtTemp(gpuC, sizeof gpuC, r->gpuT, c->celsius);
        if (r->hasMem && r->memT > 0.1f) {
            char u[8], tot[8];
            fmt1(u, sizeof u, r->memU);
            snprintf(tot, sizeof tot, "%d", (int)(r->memT + 0.5f));
            snprintf(mem, sizeof mem, "%s/%sG", u, tot);
            memBar = pctOf(r->memU / r->memT * 100.0f);
            fmtPct(memP, sizeof memP, r->memU / r->memT * 100.0f);
            memKnown = true;
        }
        if (r->hasPower) { char t[8]; fmt1(t, sizeof t, r->power); snprintf(watts, sizeof watts, "%sW", t); }
        if (r->hasFan) snprintf(fan, sizeof fan, "%d", r->fan);
    }

    field(1, 4, 20, 80, 16, "CPU", num, 2);
    {
        int tw2 = panelTextW(cpuC, 2);
        field(2, kScreen - 4 - 72, 20, 72, 16, cpuC, num, 2);
        (void)tw2;
    }
    field(3, 4, 38, 150, 48, cpuP, num, 6);
    tempMark(0, 196, 40, (link == LINK_LIVE || link == LINK_STALE) && r->hasCpuT, r->cpuT);
    bar(0, 8, 88, 224, 12, cpuBar, cpuKnown);

    field(4, 4, 104, 80, 16, "GPU", num, 2);
    field(5, kScreen - 4 - 72, 104, 72, 16, gpuC, num, 2);
    field(6, 4, 122, 150, 48, gpuP, num, 6);
    tempMark(1, 196, 124, (link == LINK_LIVE || link == LINK_STALE) && r->hasGpuT, r->gpuT);
    bar(1, 8, 172, 224, 12, gpuBar, gpuKnown);

    field(7, 4, 188, 150, 16, mem, num, 2);
    field(8, kScreen - 4 - 72, 188, 72, 16, memP, num, 2);
    bar(2, 8, 206, 224, 8, memBar, memKnown);

    if (link == LINK_SETUP || (link == LINK_OFFLINE && netApMode())) {
        const char *hint = (link == LINK_SETUP && c->ssid[0]) ? "SET URL" : "JOIN SmallTV-Mac";
        field(9, 4, 218, 232, 16, hint, kYellow, 2);
        gPrev[10][0] = 0;
    } else {
        field(9, 4, 218, 116, 16, watts, num, 2);
        field(10, 120, 218, 116, 16, fan, num, 2);
    }

    gPrevLink = link;
    gForce = false;
}
