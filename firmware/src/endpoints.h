#pragma once
#include <stdint.h>
#include <string.h>

struct EndpointState {
    bool fallback = false;
    uint32_t lastPrimaryProbe = 0;
};

// Stay on a healthy fallback; retry primary every 30 seconds, or immediately
// if fallback fails. Only switch sources after a successful metrics response.
template <typename Fetch>
bool pollEndpoints(const char *primary, const char *fallback, uint32_t now,
                   EndpointState &state, Fetch fetch) {
    bool hasFallback = fallback[0] && strcmp(primary, fallback) != 0;
    bool triedPrimary = false;
    if (!state.fallback || !hasFallback ||
        (uint32_t)(now - state.lastPrimaryProbe) >= 30000) {
        triedPrimary = true;
        state.lastPrimaryProbe = now;
        if (fetch(primary)) { state.fallback = false; return true; }
    }
    if (hasFallback && fetch(fallback)) { state.fallback = true; return true; }
    if (!triedPrimary) {
        state.lastPrimaryProbe = now;
        if (fetch(primary)) { state.fallback = false; return true; }
    }
    return false;
}
