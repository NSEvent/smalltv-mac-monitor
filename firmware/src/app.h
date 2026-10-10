#pragma once
#include <Arduino.h>

struct Config {
    char ssid[33];
    char pass[65];
    char url[160];
    char fallbackUrl[160];
    char host[25];
    uint8_t rot;
    uint8_t bright;
    uint8_t bgr;
    uint8_t celsius; // 1 = show °C, 0 = °F
};

struct Reading {
    bool have;
    uint32_t okAt;
    char name[20];
    float cpu, gpu, memU, memT, cpuT, gpuT, power;
    int fan;
    bool hasCpu, hasGpu, hasMem, hasCpuT, hasGpuT, hasPower, hasFan;
};

enum LinkState { LINK_SETUP, LINK_WAIT, LINK_LIVE, LINK_STALE, LINK_OFFLINE };

void configLoad(Config *c);
void configSave(const Config *c);
void configDefaults(Config *c);

void netBegin(Config *c);
void netLoop();
bool netConnected();
bool netApMode();
bool netPoll(Reading *r);

void uiDraw(const Config *c, const Reading *r, LinkState link);
