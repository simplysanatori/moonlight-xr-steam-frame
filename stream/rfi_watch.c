// Moonlight XR Native (lane perf): RFI watchdog (rfi_watch.h). GPLv3.
#include "rfi_watch.h"

#include <string.h>

void mlxr_rfi_watch_init(MlxrRfiWatch* w, int timeoutMs, int minDrops, int repeatMs) {
    memset(w, 0, sizeof(*w));
    w->timeoutMs = timeoutMs;
    w->minDrops = minDrops;
    w->repeatMs = repeatMs > 0 ? repeatMs : 300;
}

static void closeWait(MlxrRfiWatch* w, uint64_t nowMs, int recovered) {
    if (!w->waiting) return;
    uint64_t took = nowMs > w->startMs ? nowMs - w->startMs : 0;
    w->waitMsSum += took;
    if (took > w->waitMsMax) w->waitMsMax = took;
    w->closed++;
    if (recovered) w->recovered++;
    w->waiting = 0;
}

int mlxr_rfi_watch_line(MlxrRfiWatch* w, const char* line, uint64_t nowMs) {
    // common-c (VideoDepacketizer.c, ControlStream.c): "Sending (speculative)
    // RFI request ...", "Invalidate reference frame request sent (a to b)",
    // "Waiting for RFI frame" (a complete frame dropped), "Next
    // post-invalidation frame is: n (P-frame|I-frame)" (the wait is over)
    if (strstr(line, "post-invalidation frame")) {
        closeWait(w, nowMs, 1);
        return 0;
    }
    // common-c went over to waiting for an IDR frame itself (an IDR frame
    // ends both waits, but it then says nothing about the RFI one)
    if (strstr(line, "Waiting for IDR frame")) {
        closeWait(w, nowMs, 0);
        return 0;
    }
    // A wait that saw no line for a second was closed without a word
    if (w->waiting && nowMs > w->lastLineMs + 1000) closeWait(w, w->lastLineMs, 0);
    if (strstr(line, "RFI request") || strstr(line, "Invalidate reference frame request")) {
        if (!w->waiting) {
            w->waiting = 1;
            w->startMs = nowMs;
            w->lastIdrMs = 0;
            w->drops = 0;
            w->waits++;
        }
        w->lastLineMs = nowMs;
        return 0;
    }
    if (!strstr(line, "Waiting for RFI frame")) return 0;
    w->lastLineMs = nowMs;
    if (!w->waiting) { // the request line came first in another thread's log
        w->waiting = 1;
        w->startMs = nowMs;
        w->lastIdrMs = 0;
        w->drops = 0;
        w->waits++;
    }
    w->drops++;
    if (w->timeoutMs <= 0 || w->drops < w->minDrops || nowMs < w->startMs + (uint64_t)w->timeoutMs) return 0;
    if (w->lastIdrMs != 0 && nowMs < w->lastIdrMs + (uint64_t)w->repeatMs) return 0;
    w->lastIdrMs = nowMs;
    w->idrAsked++;
    return 1;
}
