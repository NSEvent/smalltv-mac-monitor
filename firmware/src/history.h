#pragma once
#include <stdint.h>
#include <string.h>

// Missing samples remain gaps; repeated screen draws never invent history.
struct TraceHistory {
    static const int count = 24;
    uint8_t values[3][count];
    uint32_t lastAt = 0;
    bool started = false;
    TraceHistory() { memset(values, 255, sizeof values); }
    bool append(uint32_t at, const int *percent, const bool *known) {
        if (started && at == lastAt) return false;
        for (int i = 0; i < 3; ++i) {
            memmove(values[i], values[i] + 1, count - 1);
            values[i][count - 1] = known[i] ? percent[i] : 255;
        }
        lastAt = at;
        started = true;
        return true;
    }
};
