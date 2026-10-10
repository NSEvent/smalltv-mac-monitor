#include "metrics.h"
#include <WiFiClient.h>

static bool parseUrl(const char *url, char *host, size_t hostN, uint16_t *port, char *path, size_t pathN) {
    if (strncmp(url, "http://", 7) != 0) return false;
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    if (colon && slash && colon > slash) colon = nullptr;
    size_t hlen = 0;
    if (colon && (!slash || colon < slash)) {
        hlen = (size_t)(colon - p);
        *port = (uint16_t)atoi(colon + 1);
    } else if (slash) {
        hlen = (size_t)(slash - p);
        *port = 80;
    } else {
        hlen = strlen(p);
        *port = 80;
    }
    if (hlen == 0 || hlen + 1 > hostN) return false;
    memcpy(host, p, hlen);
    host[hlen] = 0;
    if (!slash) {
        strncpy(path, "/", pathN);
    } else {
        strncpy(path, slash, pathN - 1);
        path[pathN - 1] = 0;
    }
    return true;
}

static bool jsonNum(const char *body, const char *key, float *out) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(body, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == 'n' || *p == '}' || *p == 0) return false;
    *out = strtof(p, nullptr);
    return true;
}

static void jsonStr(const char *body, const char *key, char *dst, size_t n) {
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":\"", key);
    const char *p = strstr(body, pat);
    dst[0] = 0;
    if (!p) return;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        dst[i++] = *p++;
    }
    dst[i] = 0;
}

bool fetchReading(const char *url, Reading *r,
                  bool (*resolve)(const char *, IPAddress *)) {
    if (!url || !url[0]) return false;
    char host[64], path[96];
    uint16_t port = 80;
    if (!parseUrl(url, host, sizeof host, &port, path, sizeof path)) {
        return false;
    }
    IPAddress ip;
    if (!resolve(host, &ip)) {
        return false;
    }

    WiFiClient client;
    client.setTimeout(1200);
    if (!client.connect(ip, port)) {
        client.stop();
        return false;
    }
    char req[220];
    int rn = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (rn <= 0 || rn >= (int)sizeof req) {
        client.stop();
        return false;
    }
    client.write((const uint8_t *)req, (size_t)rn);

    static char buf[640];
    size_t n = 0;
    uint32_t start = millis();
    while (client.connected() && n + 1 < sizeof buf && millis() - start < 900) {
        while (client.available() && n + 1 < sizeof buf) buf[n++] = (char)client.read();
        delay(1);
        yield();
    }
    while (client.available() && n + 1 < sizeof buf) buf[n++] = (char)client.read();
    buf[n] = 0;
    client.stop();

    char *body = strstr(buf, "\r\n\r\n");
    if (strncmp(buf, "HTTP/1.0 200 ", 13) != 0 &&
        strncmp(buf, "HTTP/1.1 200 ", 13) != 0) return false;
    if (!body || body[4] != '{') {
        return false;
    }
    body += 4;

    Reading next = *r;
    next.have = true;
    next.okAt = millis();
    jsonStr(body, "name", next.name, sizeof next.name);
    next.hasCpu = jsonNum(body, "cpu", &next.cpu);
    next.hasGpu = jsonNum(body, "gpu", &next.gpu);
    next.hasMem = jsonNum(body, "memory_used_gb", &next.memU) && jsonNum(body, "memory_total_gb", &next.memT);
    next.hasCpuT = jsonNum(body, "cpu_temp_c", &next.cpuT);
    next.hasGpuT = jsonNum(body, "gpu_temp_c", &next.gpuT);
    float fan = 0;
    next.hasFan = jsonNum(body, "fan_rpm", &fan);
    next.fan = (int)(fan + 0.5f);
    next.hasPower = jsonNum(body, "power_w", &next.power);
    if (!next.name[0] || !(next.hasCpu || next.hasGpu || next.hasMem)) return false;
    *r = next;
    return true;
}

