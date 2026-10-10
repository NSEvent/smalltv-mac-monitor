#include "app.h"
#include "panel.h"
#include "metrics.h"
#include "endpoints.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <strings.h>

static Config *gCfg = nullptr;
static bool gAp = false;
static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;
static DNSServer dns;
static IPAddress gResolved;
static char gResolvedHost[64];
static uint32_t gResolvedAt = 0;
static uint32_t gFailSince = 0;
static uint32_t gLastRejoin = 0;
static uint32_t gLastOk = 0;
static EndpointState gEndpoint;
static const char *kFirmwareVersion = "2026-10-10-temp-squares-7";

static void forgetHost() {
    gResolvedHost[0] = 0;
    gResolvedAt = 0;
}

static void noteFail() {
    forgetHost();
    if (!gFailSince) gFailSince = millis();
}

static void noteOk() { gFailSince = 0; gLastOk = millis(); }

static const char kPage[] PROGMEM = R"HTML(<!doctype html>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>SmallTV Mac</title>
<style>
body{margin:0;background:#121410;color:#e7e2d4;font:16px/1.4 ui-monospace,Menlo,monospace}
main{max-width:28rem;margin:0 auto;padding:1.25rem}
h1{font-size:1.15rem;letter-spacing:.08em;margin:0 0 1rem}
label{display:block;margin:.8rem 0 .2rem;color:#b7b09e}
input{width:100%;box-sizing:border-box;background:#1c1f18;color:#f4f1e8;border:1px solid #3c4234;padding:.55rem;font:inherit}
button{margin-top:1rem;background:#d6ff4a;color:#14160f;border:0;padding:.7rem 1rem;font:inherit;font-weight:700}
.note{color:#8d8878;font-size:.85rem}
</style>
<main>
<h1>SMALLTV MAC</h1>
<p class=note>Point this screen at the Mac running macmetrics. Plain HTTP on the LAN. The same firmware image is used on every cube; only this page changes.</p>
<form method=POST action=/save>
<label>Wi-Fi name</label><input name=ssid maxlength=32>
<label>Wi-Fi password</label><input name=pass type=password maxlength=64 placeholder="leave blank to keep">
<label>Metrics URL</label><input name=url maxlength=150 placeholder="http://mac-studio.local:9123/metrics">
<label>Fallback metrics URL</label><input name=fallback maxlength=150 placeholder="http://192.168.1.50:9123/metrics">
<label>This cube's name</label><input name=host maxlength=24>
<label>Rotation 0-3</label><input name=rot maxlength=1>
<label>Brightness 5-100</label><input name=bright maxlength=3>
<label>BGR 0 or 1</label><input name=bgr maxlength=1>
<button type=submit>Save and reboot</button>
</form>
<p class=note>Later firmware uploads go to <a href=/update>/update</a>.</p>
</main>)HTML";

bool netConnected() { return WiFi.status() == WL_CONNECTED; }
bool netApMode() { return gAp; }

static int gRadio = -1;

static void applyRadio() {
    int mode = 1;
    if (gAp) mode = 0;
    else if (WiFi.status() == WL_CONNECTED) mode = 2;
    if (mode == gRadio) return;
    gRadio = mode;
    if (mode == 0) {
        // Setup AP has to keep beaconing or phones cannot join.
        WiFi.setSleepMode(WIFI_NONE_SLEEP);
        WiFi.setOutputPower(12.0f);
    } else if (mode == 2) {
        // Light sleep left the STA reporting connected while packets had
        // stopped, and the cube sat on STALE until a power cycle. Modem
        // sleep still rests the radio between beacons. 8 dBm is enough here.
        WiFi.setSleepMode(WIFI_MODEM_SLEEP);
        WiFi.setOutputPower(8.0f);
    } else {
        WiFi.setSleepMode(WIFI_MODEM_SLEEP);
        WiFi.setOutputPower(14.0f);
    }
}

static void startAp() {
    // STA scans (especially for a 5 GHz name the radio cannot see) blank the
    // setup AP every few seconds, and phones fail to join. Drop the station
    // and hold a steady open AP until the next reboot.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    WiFi.softAP("SmallTV-Mac", "", 6);
    dns.start(53, "*", WiFi.softAPIP());
    gAp = true;
    gRadio = -1;
    applyRadio();
}

static void stopAp() {
    if (!gAp) return;
    dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    gAp = false;
    gRadio = -1;
}

// ESP8266 Arduino 3.1 has no MDNS.queryHost. Ask for a unicast mDNS A reply (QU bit).
static bool mdnsLookup(const char *fqdn, IPAddress *ip) {
    uint8_t pkt[160];
    size_t n = 0;
    pkt[n++] = 0x12;
    pkt[n++] = 0x34;
    pkt[n++] = 0x00;
    pkt[n++] = 0x00;
    pkt[n++] = 0x00;
    pkt[n++] = 0x01;
    pkt[n++] = 0x00;
    pkt[n++] = 0x00;
    pkt[n++] = 0x00;
    pkt[n++] = 0x00;
    pkt[n++] = 0x00;
    pkt[n++] = 0x00;
    const char *p = fqdn;
    while (*p && n < sizeof pkt - 6) {
        const char *dot = strchr(p, '.');
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        if (len == 0 || len > 63 || n + len + 1 >= sizeof pkt) return false;
        pkt[n++] = (uint8_t)len;
        memcpy(pkt + n, p, len);
        n += len;
        if (!dot) break;
        p = dot + 1;
    }
    pkt[n++] = 0;
    pkt[n++] = 0x00;
    pkt[n++] = 0x01;  // A
    pkt[n++] = 0x80;
    pkt[n++] = 0x01;  // IN + QU

    WiFiUDP udp;
    if (!udp.begin(5354)) return false;
    udp.beginPacket(IPAddress(224, 0, 0, 251), 5353);
    udp.write(pkt, n);
    udp.endPacket();

    uint32_t start = millis();
    while (millis() - start < 1200) {
        int got = udp.parsePacket();
        if (got <= 0) {
            delay(10);
            yield();
            continue;
        }
        uint8_t buf[512];
        int rn = udp.read(buf, sizeof buf);
        if (rn < 12) continue;
        int an = (buf[6] << 8) | buf[7];
        int i = 12;
        while (i < rn && buf[i] != 0) {
            if ((buf[i] & 0xC0) == 0xC0) { i += 2; break; }
            i += 1 + buf[i];
        }
        if (i < rn && buf[i] == 0) i++;
        i += 4;
        for (int a = 0; a < an && i + 12 < rn; a++) {
            if ((buf[i] & 0xC0) == 0xC0) i += 2;
            else {
                while (i < rn && buf[i] != 0) i += 1 + buf[i];
                i++;
            }
            if (i + 10 > rn) break;
            int type = (buf[i] << 8) | buf[i + 1];
            int rdlen = (buf[i + 8] << 8) | buf[i + 9];
            i += 10;
            if (i + rdlen > rn) break;
            if (type == 1 && rdlen == 4) {
                *ip = IPAddress(buf[i], buf[i + 1], buf[i + 2], buf[i + 3]);
                udp.stop();
                return true;
            }
            i += rdlen;
        }
    }
    udp.stop();
    return false;
}

static bool resolveHost(const char *host, IPAddress *ip) {
    uint32_t now = millis();
    if (gResolvedHost[0] && strcmp(gResolvedHost, host) == 0 && (uint32_t)gResolved != 0 &&
        now - gResolvedAt < 30000) {
        *ip = gResolved;
        return true;
    }
    int nlen = (int)strlen(host);
    IPAddress found;
    bool ok = false;
    if (nlen > 6 && strcasecmp(host + nlen - 6, ".local") == 0) {
        ok = mdnsLookup(host, &found);
    } else {
        ok = WiFi.hostByName(host, found) == 1;
    }
    if (!ok) return false;
    gResolved = found;
    strncpy(gResolvedHost, host, sizeof gResolvedHost - 1);
    gResolvedAt = now;
    *ip = found;
    return true;
}

bool netPoll(Reading *r) {
    if (!netConnected() || !gCfg || !gCfg->url[0]) return false;
    bool ok = pollEndpoints(gCfg->url, gCfg->fallbackUrl, millis(), gEndpoint,
        [&](const char *url) { return fetchReading(url, r, resolveHost); });
    if (ok) noteOk(); else noteFail();
    return ok;
}

static void handleRoot() { server.send_P(200, PSTR("text/html"), kPage); }

static void handleSave() {
    Config next = *gCfg;
    if (server.hasArg("ssid")) {
        strncpy(next.ssid, server.arg("ssid").c_str(), sizeof next.ssid - 1);
        next.ssid[sizeof next.ssid - 1] = 0;
    }
    if (server.hasArg("pass") && server.arg("pass").length()) {
        strncpy(next.pass, server.arg("pass").c_str(), sizeof next.pass - 1);
        next.pass[sizeof next.pass - 1] = 0;
    }
    if (server.hasArg("url")) {
        String u = server.arg("url");
        u.trim();
        if (u.length() && !u.startsWith("http://") && !u.startsWith("https://")) u = "http://" + u;
        strncpy(next.url, u.c_str(), sizeof next.url - 1);
        next.url[sizeof next.url - 1] = 0;
    }
    if (server.hasArg("fallback")) {
        String u = server.arg("fallback");
        u.trim();
        if (u.length() && !u.startsWith("http://")) u = "http://" + u;
        strncpy(next.fallbackUrl, u.c_str(), sizeof next.fallbackUrl - 1);
        next.fallbackUrl[sizeof next.fallbackUrl - 1] = 0;
    }
    if (server.hasArg("host") && server.arg("host").length()) {
        strncpy(next.host, server.arg("host").c_str(), sizeof next.host - 1);
        next.host[sizeof next.host - 1] = 0;
    }
    if (server.hasArg("rot") && server.arg("rot").length()) next.rot = (uint8_t)server.arg("rot").toInt() & 3;
    if (server.hasArg("bright") && server.arg("bright").length()) {
        next.bright = (uint8_t)constrain(server.arg("bright").toInt(), 5, 100);
    }
    if (server.hasArg("bgr") && server.arg("bgr").length()) next.bgr = server.arg("bgr").toInt() ? 1 : 0;
    configSave(&next);
    server.send(200, "text/html", "<meta http-equiv=refresh content='8;url=/'><p>Saved. Rebooting.</p>");
    delay(400);
    ESP.restart();
}

static void handleStatus() {
    char body[832];
    snprintf(body, sizeof body,
             "{\"wifi\":%s,\"ap\":%s,\"ip\":\"%s\",\"rssi\":%d,\"url\":\"%s\",\"host\":\"%s\","
             "\"firmware\":\"%s\",\"sample_age_ms\":%ld,\"rotation\":%u,\"brightness\":%u,\"fallback_url\":\"%s\",\"active_url\":\"%s\"}",
             netConnected() ? "true" : "false", gAp ? "true" : "false",
             netConnected() ? WiFi.localIP().toString().c_str() : WiFi.softAPIP().toString().c_str(),
             WiFi.RSSI(), gCfg->url, gCfg->host, kFirmwareVersion,
             gLastOk ? (long)(millis() - gLastOk) : -1L,
             (unsigned)gCfg->rot, (unsigned)gCfg->bright, gCfg->fallbackUrl,
             gLastOk ? (gEndpoint.fallback ? gCfg->fallbackUrl : gCfg->url) : "");
    server.send(200, "application/json", body);
}

static void handleNotFound() {
    if (gAp) {
        server.sendHeader("Location", "http://192.168.4.1/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(404, "text/plain", "no");
}

void netBegin(Config *c) {
    gCfg = c;
    WiFi.persistent(false);
    // Modem sleep drops the radio between beacons. NONE_SLEEP kept it
    // transmitting full time and heated the sealed cube.
    WiFi.setAutoReconnect(true);
    WiFi.hostname(c->host);
    if (c->ssid[0]) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(c->ssid, c->pass);
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
            delay(200);
            yield();
        }
    }
    if (WiFi.status() != WL_CONNECTED) startAp();
    if (WiFi.status() == WL_CONNECTED) MDNS.begin(c->host);
    applyRadio();

    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/api/status", HTTP_GET, handleStatus);
    server.onNotFound(handleNotFound);
    updater.setup(&server);
    server.begin();
}

static void rejoinSta() {
    if (!gCfg || !gCfg->ssid[0]) return;
    forgetHost();
    WiFi.disconnect();
    delay(50);
    WiFi.mode(WIFI_STA);
    WiFi.hostname(gCfg->host);
    WiFi.begin(gCfg->ssid, gCfg->pass);
    gRadio = -1;
}

void netLoop() {
    static uint32_t lastTry = 0;
    static uint32_t downSince = 0;
    server.handleClient();
    applyRadio();
    if (gAp) dns.processNextRequest();
    // Status can stay WL_CONNECTED after the link is dead (the STALE wedge).
    // Rejoin the saved network, then reboot if samples still do not return.
    if (gFailSince && gCfg && gCfg->url[0] && gCfg->ssid[0] && !gAp) {
        uint32_t staleFor = millis() - gFailSince;
        if (staleFor > 60000) ESP.restart();
        if (staleFor > 12000 && millis() - gLastRejoin > 20000) {
            gLastRejoin = millis();
            rejoinSta();
            return;
        }
    }
    if (netConnected()) {
        downSince = 0;
        MDNS.update();
        if (gAp && gCfg) {
            stopAp();
            MDNS.begin(gCfg->host);
        }
        return;
    }
    // Recovery AP must stay up. A station reconnect scans and kicks clients off.
    if (gAp || !gCfg || !gCfg->ssid[0]) return;
    uint32_t now = millis();
    if (!downSince) downSince = now;
    if (now - lastTry > 5000) {
        lastTry = now;
        WiFi.reconnect();
    }
    if (now - downSince > 20000) startAp();
}
