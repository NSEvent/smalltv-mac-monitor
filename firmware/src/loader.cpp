// One-shot OTA trampoline for SmallTV-Ultra stock firmware.
// Ultra-V9.0.50's updater rejects an image that does not fit its small app
// slot ("Not Enough Space"). This image is built to stay under that slot.
// After it boots it serves /update on the 4m1m layout, which has room for the
// full firmware. Without LOADER_SSID it opens an open AP named SmallTV-Loader.
#ifdef LOADER_BUILD
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#ifndef LOADER_SSID
#define LOADER_SSID ""
#endif
#ifndef LOADER_PASS
#define LOADER_PASS ""
#endif

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;

void setup() {
    Serial.begin(115200);
    WiFi.persistent(false);
    const char *ssid = LOADER_SSID;
    bool joined = false;
    if (ssid[0]) {
        WiFi.mode(WIFI_STA);
        WiFi.hostname("smalltv-loader");
        WiFi.begin(LOADER_SSID, LOADER_PASS);
        for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) {
            delay(250);
            yield();
        }
        joined = WiFi.status() == WL_CONNECTED;
    }
    if (!joined) {
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SmallTV-Loader");
    }
    updater.setup(&server);
    server.begin();
}

void loop() {
    server.handleClient();
    yield();
}
#endif
