// Moonlight XR Native (lane deploy-res-fix): rate limit for common-c's "Unrecoverable frame"
// log lines (log_throttle.h). GPLv3.
#include "log_throttle.h"

#include <stdio.h>
#include <string.h>

void mlxr_log_throttle_init(MlxrLogThrottle* t, uint64_t windowMs) {
    memset(t, 0, sizeof(*t));
    t->windowMs = windowMs;
}

int mlxr_log_throttle_matches(const char* line) {
    // common-c (RtpVideoQueue.c): "Unrecoverable frame %d: %d+%d=%d received < %d needed", and
    // the "(block %d of %d)" and "lost FEC blocks" variants
    return strstr(line, "Unrecoverable frame") != NULL;
}

int mlxr_log_throttle_line(MlxrLogThrottle* t, const char* line, uint64_t nowMs, char* out, size_t outSize) {
    if (outSize == 0) return 0;
    if (!mlxr_log_throttle_matches(line) || t->windowMs == 0) {
        snprintf(out, outSize, "%s", line);
        return 1;
    }
    t->seen++;
    if (t->wrote && nowMs < t->lastWriteMs + t->windowMs) {
        t->suppressed++;
        return 0;
    }
    if (t->suppressed)
        snprintf(out, outSize, "%s ... (+%llu more)", line, (unsigned long long)t->suppressed);
    else
        snprintf(out, outSize, "%s", line);
    t->suppressed = 0;
    t->wrote = 1;
    t->lastWriteMs = nowMs;
    return 1;
}

uint64_t mlxr_log_throttle_flush(MlxrLogThrottle* t, uint64_t nowMs) {
    if (!t->suppressed || (t->wrote && nowMs < t->lastWriteMs + t->windowMs)) return 0;
    uint64_t n = t->suppressed;
    t->suppressed = 0;
    return n;
}

uint64_t mlxr_log_throttle_window(MlxrLogThrottle* t) {
    uint64_t n = t->seen - t->seenPrev;
    t->seenPrev = t->seen;
    return n;
}
