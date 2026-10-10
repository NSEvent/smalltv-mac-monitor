#include "../firmware/src/endpoints.h"
#include <assert.h>
#include <vector>
#include <string>
#include <stdio.h>

int main() {
    EndpointState state;
    bool primaryOk = true, fallbackOk = true;
    std::vector<std::string> calls;
    auto fetch = [&](const char *url) {
        calls.push_back(url);
        return strcmp(url, "primary") == 0 ? primaryOk : fallbackOk;
    };
    assert(pollEndpoints("primary", "fallback", 1000, state, fetch));
    assert(!state.fallback && calls == std::vector<std::string>{"primary"});
    primaryOk = false; calls.clear();
    assert(pollEndpoints("primary", "fallback", 2000, state, fetch));
    assert(state.fallback && (calls == std::vector<std::string>{"primary", "fallback"}));
    calls.clear();
    assert(pollEndpoints("primary", "fallback", 3000, state, fetch));
    assert(calls == std::vector<std::string>{"fallback"});
    calls.clear();
    assert(pollEndpoints("primary", "fallback", 32000, state, fetch));
    assert(state.fallback && (calls == std::vector<std::string>{"primary", "fallback"}));
    primaryOk = true; calls.clear();
    assert(pollEndpoints("primary", "fallback", 62000, state, fetch));
    assert(!state.fallback && calls == std::vector<std::string>{"primary"});
    primaryOk = false; calls.clear();
    assert(pollEndpoints("primary", "fallback", 63000, state, fetch));
    primaryOk = true; fallbackOk = false; calls.clear();
    assert(pollEndpoints("primary", "fallback", 64000, state, fetch));
    assert(!state.fallback && (calls == std::vector<std::string>{"fallback", "primary"}));
    primaryOk = false; calls.clear();
    assert(!pollEndpoints("primary", "fallback", 65000, state, fetch));
    assert((calls == std::vector<std::string>{"primary", "fallback"}));
    calls.clear();
    assert(!pollEndpoints("primary", "", 66000, state, fetch));
    assert(calls == std::vector<std::string>{"primary"});
    calls.clear();
    assert(!pollEndpoints("primary", "primary", 67000, state, fetch));
    assert(calls == std::vector<std::string>{"primary"});
    // Unsigned elapsed time remains correct across millis() rollover.
    state.fallback = true; state.lastPrimaryProbe = UINT32_MAX - 10000;
    primaryOk = true; calls.clear();
    assert(pollEndpoints("primary", "fallback", 20000, state, fetch));
    assert(!state.fallback && calls == std::vector<std::string>{"primary"});
    puts("endpoint failover regression tests passed");
}
