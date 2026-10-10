#include "app.h"
#include "panel.h"

static Config gCfg;
static Reading gRead;
static uint32_t gLastPoll = 0;
static LinkState gShown = LINK_SETUP;

static LinkState linkNow() {
    if (!gCfg.ssid[0] || !gCfg.url[0]) return LINK_SETUP;
    if (!netConnected()) return gRead.have ? LINK_STALE : LINK_OFFLINE;
    if (!gRead.have) return LINK_WAIT;
    if (millis() - gRead.okAt > 2500) return LINK_STALE;
    return LINK_LIVE;
}

void setup() {
    Serial.begin(115200);
    Serial.println(F("smalltv-mac"));
    configLoad(&gCfg);
    panelBegin(gCfg.rot, gCfg.bgr != 0, gCfg.bright);
    memset(&gRead, 0, sizeof gRead);
    uiDraw(&gCfg, &gRead, gCfg.ssid[0] ? LINK_WAIT : LINK_SETUP);
    netBegin(&gCfg);
}

void loop() {
    netLoop();
    uint32_t now = millis();
    LinkState link = linkNow();
    bool due = (uint32_t)(now - gLastPoll) >= 1000;
    if (due) {
        gLastPoll = now;
        netPoll(&gRead);
    }
    if (due || link != gShown) {
        uiDraw(&gCfg, &gRead, link);
        gShown = link;
    }
    // A tight loop holds the CPU at 100% and blocks Wi-Fi modem sleep.
    delay(25);
}
