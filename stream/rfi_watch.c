// Moonlight XR Native (lane perf): RFI watchdog (rfi_watch.h). GPLv3.
// Native (lane wave1-burst): the adaptive wait, the IDR request cap and the burst tracker.
#include "rfi_watch.h"

#include <stdio.h>
#include <string.h>

// ---- what a common-c line is (VideoDepacketizer.c, RtpVideoQueue.c, ControlStream.c; the
// fragments are in tests/stream/test_commonc_log.c's contract)

static const struct {
    const char* fragment;
    MlxrBurstLine kind;
} LINE_KINDS[] = {
    {"Network dropped ", MLXR_BL_NET_DROPPED},
    {"RFI request for", MLXR_BL_RFI_SEND}, // "Sending (speculative) RFI request for ..."; before "Unrecoverable"
    {"Unrecoverable frame", MLXR_BL_UNRECOVERABLE},
    {"Invalidate reference frame request sent", MLXR_BL_RFI_SENT},
    {"Waiting for RFI frame", MLXR_BL_WAIT_RFI},
    {"Waiting for IDR frame", MLXR_BL_WAIT_IDR},
    {"IDR frame request sent", MLXR_BL_IDR_SENT},
    {"Requesting IDR frame on behalf of DR", MLXR_BL_IDR_FOR_DR},
    {"post-invalidation frame", MLXR_BL_REC_P}, // (I-frame) told apart below
};

MlxrBurstLine mlxr_burst_classify(const char* line, int* count) {
    if (count) *count = 0;
    if (!line) return MLXR_BL_NONE;
    for (size_t i = 0; i < sizeof(LINE_KINDS) / sizeof(LINE_KINDS[0]); i++) {
        const char* at = strstr(line, LINE_KINDS[i].fragment);
        if (!at) continue;
        MlxrBurstLine k = LINE_KINDS[i].kind;
        if (k == MLXR_BL_NET_DROPPED) {
            int n = 0;
            if (!strstr(at, " frame") || sscanf(at, "Network dropped %d frame", &n) != 1) continue;
            if (count) *count = n > 0 ? n : 0;
        }
        if (k == MLXR_BL_REC_P && strstr(at, "(I-frame)")) k = MLXR_BL_REC_I;
        return k;
    }
    return MLXR_BL_NONE;
}

static int isLoss(MlxrBurstLine k) {
    return k == MLXR_BL_NET_DROPPED || k == MLXR_BL_UNRECOVERABLE || k == MLXR_BL_RFI_SEND;
}

// ---- the watchdog

void mlxr_rfi_watch_init(MlxrRfiWatch* w, int timeoutMs, int minDrops, int repeatMs) {
    memset(w, 0, sizeof(*w));
    w->timeoutMs = timeoutMs;
    w->minDrops = minDrops;
    w->repeatMs = repeatMs > 0 ? repeatMs : 300;
    mlxr_rfi_watch_set_ceiling(w, 1000);
}

void mlxr_rfi_watch_set_ceiling(MlxrRfiWatch* w, int ceilMs) {
    w->ceilMs = ceilMs < w->timeoutMs ? w->timeoutMs : ceilMs;
}

void mlxr_rfi_watch_set_rtt(MlxrRfiWatch* w, uint32_t rttMs) { w->rttMs = rttMs; }

int mlxr_rfi_watch_base_ms(const MlxrRfiWatch* w) {
    if (w->timeoutMs <= 0) return 0;
    uint64_t b = (uint64_t)w->rttMs * 3u;
    if (b < (uint64_t)w->timeoutMs) b = (uint64_t)w->timeoutMs;
    if (b > (uint64_t)w->ceilMs) b = (uint64_t)w->ceilMs;
    return (int)b;
}

// When the next ask is due: `minGap` after the wait opened (base) or after the last ask (repeatMs),
// and `base` after the last network loss, but no later than the ceiling after that same anchor
static uint64_t askDueMs(const MlxrRfiWatch* w) {
    uint64_t base = (uint64_t)mlxr_rfi_watch_base_ms(w);
    uint64_t anchor = w->lastIdrMs ? w->lastIdrMs : w->startMs;
    uint64_t minGap = w->lastIdrMs ? (uint64_t)w->repeatMs : base;
    uint64_t due = anchor + minGap;
    if (w->anyLoss && w->lastLossMs + base > due) due = w->lastLossMs + base;
    uint64_t ceil = (uint64_t)w->ceilMs > minGap ? (uint64_t)w->ceilMs : minGap;
    if (due > anchor + ceil) due = anchor + ceil;
    return due;
}

int mlxr_rfi_watch_wait_ms(const MlxrRfiWatch* w, uint64_t nowMs) {
    (void)nowMs;
    if (!w->waiting || w->timeoutMs <= 0) return mlxr_rfi_watch_base_ms(w);
    uint64_t due = askDueMs(w);
    uint64_t anchor = w->lastIdrMs ? w->lastIdrMs : w->startMs;
    return due > anchor ? (int)(due - anchor) : 0;
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

static void openWait(MlxrRfiWatch* w, uint64_t nowMs) {
    w->waiting = 1;
    w->startMs = nowMs;
    w->lastIdrMs = 0;
    w->drops = 0;
    w->heldThisWait = 0;
    w->waits++;
}

int mlxr_rfi_watch_line(MlxrRfiWatch* w, const char* line, uint64_t nowMs) {
    // common-c (VideoDepacketizer.c, ControlStream.c): "Sending (speculative)
    // RFI request ...", "Invalidate reference frame request sent (a to b)",
    // "Waiting for RFI frame" (a complete frame dropped), "Next
    // post-invalidation frame is: n (P-frame|I-frame)" (the wait is over)
    MlxrBurstLine k = mlxr_burst_classify(line, NULL);
    if (k == MLXR_BL_REC_P || k == MLXR_BL_REC_I) {
        closeWait(w, nowMs, 1);
        return 0;
    }
    // common-c went over to waiting for an IDR frame itself (an IDR frame
    // ends both waits, but it then says nothing about the RFI one)
    if (k == MLXR_BL_WAIT_IDR) {
        closeWait(w, nowMs, 0);
        return 0;
    }
    // Native (lane wave1-burst): the link is still losing frames
    if (isLoss(k)) {
        w->anyLoss = 1;
        w->lastLossMs = nowMs;
    }
    // A wait that saw no line for a second was closed without a word
    if (w->waiting && nowMs > w->lastLineMs + 1000) closeWait(w, w->lastLineMs, 0);
    if (k == MLXR_BL_RFI_SEND || k == MLXR_BL_RFI_SENT) {
        if (!w->waiting) openWait(w, nowMs);
        w->lastLineMs = nowMs;
        return 0;
    }
    if (k != MLXR_BL_WAIT_RFI) return 0;
    w->lastLineMs = nowMs;
    if (!w->waiting) openWait(w, nowMs); // the request line came first in another thread's log
    w->drops++;
    if (w->timeoutMs <= 0 || w->drops < w->minDrops) return 0;
    uint64_t due = askDueMs(w);
    if (nowMs < due) {
        // put off by the burst: the plain wait has passed, the loss is what holds it
        uint64_t anchor = w->lastIdrMs ? w->lastIdrMs : w->startMs;
        uint64_t plain = anchor + (w->lastIdrMs ? (uint64_t)w->repeatMs : (uint64_t)mlxr_rfi_watch_base_ms(w));
        if (nowMs >= plain && !w->heldThisWait) {
            w->heldThisWait = 1;
            w->heldByLoss++;
        }
        return 0;
    }
    w->lastAskWaitMs = (int)(nowMs - (w->lastIdrMs ? w->lastIdrMs : w->startMs));
    w->lastIdrMs = nowMs;
    w->idrAsked++;
    return 1;
}

// ---- the IDR request cap

void mlxr_idr_cap_init(MlxrIdrCap* c, int minGapMs) {
    memset(c, 0, sizeof(*c));
    c->minGapMs = minGapMs > 0 ? minGapMs : 0;
}

int mlxr_idr_cap_take(MlxrIdrCap* c, uint64_t nowMs) {
    if (c->minGapMs > 0 && c->any && nowMs < c->lastMs + (uint64_t)c->minGapMs) {
        c->held++;
        return 0;
    }
    c->any = 1;
    c->lastMs = nowMs;
    c->passed++;
    return 1;
}

// ---- bursts

void mlxr_burst_init(MlxrBurst* b, int gapMs) {
    memset(b, 0, sizeof(*b));
    b->gapMs = gapMs > 0 ? gapMs : 1000;
}

// A lost frame, a dropped one, or our decoder refusing one: the picture is broken until a recovery
static int isEvent(MlxrBurstLine k) {
    return isLoss(k) || k == MLXR_BL_WAIT_RFI || k == MLXR_BL_WAIT_IDR || k == MLXR_BL_IDR_FOR_DR;
}

static void closeBurst(MlxrBurst* b, MlxrBurstSummary* out) {
    b->cur.durMs = b->lastEventMs > b->cur.startMs ? b->lastEventMs - b->cur.startMs : 0;
    b->cur.stillBroken = b->broken;
    b->bursts++;
    b->burstMsSum += b->cur.durMs;
    if (b->cur.durMs > b->burstMsMax) b->burstMsMax = b->cur.durMs;
    if (out) *out = b->cur;
    b->active = 0;
    b->broken = 0;
}

int mlxr_burst_tick(MlxrBurst* b, uint64_t nowMs, MlxrBurstSummary* closed) {
    if (!b->active || nowMs < b->lastEventMs + (uint64_t)b->gapMs) return 0;
    closeBurst(b, closed);
    return 1;
}

int mlxr_burst_line(MlxrBurst* b, const char* line, uint64_t nowMs, MlxrBurstSummary* closed, int* didClose) {
    int n = 0;
    MlxrBurstLine k = mlxr_burst_classify(line, &n);
    int c = mlxr_burst_tick(b, nowMs, closed);
    if (didClose) *didClose = c;
    if (k == MLXR_BL_NONE) return 1;
    if (!b->active) {
        if (!isEvent(k)) return 1; // a request or a recovery line outside a burst is written as ever
        memset(&b->cur, 0, sizeof(b->cur));
        memset(b->seen, 0, sizeof(b->seen));
        b->active = 1;
        b->broken = 0;
        b->cur.startMs = nowMs;
    }
    if (isEvent(k)) {
        b->lastEventMs = nowMs;
        b->broken = 1;
        if (k != MLXR_BL_WAIT_RFI && k != MLXR_BL_WAIT_IDR) b->cur.recovery = MLXR_REC_NONE; // a new loss
    }
    switch (k) {
    case MLXR_BL_NET_DROPPED: b->cur.netLost += (uint64_t)n; break;
    case MLXR_BL_UNRECOVERABLE: b->cur.unrecoverable++; break;
    case MLXR_BL_RFI_SEND: b->cur.rfiRequests++; break;
    case MLXR_BL_WAIT_RFI: b->cur.rfiDrops++; break;
    case MLXR_BL_WAIT_IDR: b->cur.idrDrops++; break;
    case MLXR_BL_IDR_SENT: b->cur.idrSent++; break;
    case MLXR_BL_IDR_FOR_DR: b->cur.idrForDecoder++; break;
    case MLXR_BL_REC_P:
        b->broken = 0;
        b->cur.recovery = MLXR_REC_RFI;
        break;
    case MLXR_BL_REC_I:
        b->broken = 0;
        b->cur.recovery = MLXR_REC_IDR;
        break;
    default: break;
    }
    // "Unrecoverable frame" lines keep their own throttle (log_throttle.h); every other per-frame
    // kind is written the first time in a burst and counted after
    if (k == MLXR_BL_UNRECOVERABLE) return 1;
    if (!b->seen[k]) {
        b->seen[k] = 1;
        return 1;
    }
    b->cur.folded++;
    return 0;
}

int mlxr_burst_open(const MlxrBurst* b, MlxrBurstSummary* out) {
    if (!b->active) return 0;
    if (out) {
        *out = b->cur;
        out->durMs = b->lastEventMs > b->cur.startMs ? b->lastEventMs - b->cur.startMs : 0;
        out->stillBroken = b->broken;
    }
    return 1;
}

void mlxr_burst_idr_frame(MlxrBurst* b) {
    if (!b->active || !b->broken) return;
    b->broken = 0;
    b->cur.recovery = MLXR_REC_IDR;
}

void mlxr_burst_idr_ask(MlxrBurst* b, int passed) {
    if (!b->active) return;
    if (passed) b->cur.idrOurs++;
    else b->cur.idrHeld++;
}

void mlxr_burst_text(const MlxrBurstSummary* s, const char* startText, int capMs, char* out, size_t cap) {
    const char* rec = s->stillBroken ? "not recovered (still waiting when it closed)"
                      : s->recovery == MLXR_REC_RFI ? "recovered by RFI"
                      : s->recovery == MLXR_REC_IDR ? "recovered by IDR"
                                                    : "recovered by neither";
    char capText[32];
    if (capMs > 0) snprintf(capText, sizeof(capText), "rfi_idr_ms %d", capMs);
    else snprintf(capText, sizeof(capText), "rfi_idr_ms off");
    snprintf(out, cap,
             "burst at %s for %llu ms: frames lost on the network %llu (unrecoverable %llu), dropped waiting for "
             "RFI %llu / IDR %llu, RFI waits %llu, IDR requests sent %llu (asked here %llu, held back %llu by %s, "
             "for the decoder %llu), %s; %llu per-frame lines folded",
             startText ? startText : "?", (unsigned long long)s->durMs, (unsigned long long)s->netLost,
             (unsigned long long)s->unrecoverable, (unsigned long long)s->rfiDrops, (unsigned long long)s->idrDrops,
             (unsigned long long)s->rfiRequests, (unsigned long long)s->idrSent, (unsigned long long)s->idrOurs,
             (unsigned long long)s->idrHeld, capText, (unsigned long long)s->idrForDecoder, rec,
             (unsigned long long)s->folded);
}
