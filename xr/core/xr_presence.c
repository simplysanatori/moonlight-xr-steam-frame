// See xr_presence.h. GPLv3.
#include "xr_presence.h"

#include <stdatomic.h>
#include <time.h>

#define DOFF_SECONDS_MAX 3600
#define IDLE_MINUTES_MAX 1440

static int clampInt(int v, int hi) {
    return v < 0 ? 0 : (v > hi ? hi : v);
}

void presenceSetLimits(PresenceState* p, int doffSeconds, int idleMinutes) {
    p->doffSeconds = clampInt(doffSeconds, DOFF_SECONDS_MAX);
    p->idleMinutes = clampInt(idleMinutes, IDLE_MINUTES_MAX);
}

void presenceInit(PresenceState* p, int doffSeconds, int idleMinutes, int64_t nowNs) {
    p->present = 1;
    p->offSinceNs = 0;
    p->lastInputNs = nowNs;
    p->doffFired = 0;
    p->idleFired = 0;
    presenceSetLimits(p, doffSeconds, idleMinutes);
}

void presenceReset(PresenceState* p, int64_t nowNs) {
    p->lastInputNs = nowNs;
    p->doffFired = 0;
    p->idleFired = 0;
    if (!p->present) {
        // A stream that begins with the headset off counts its time off from now
        p->offSinceNs = nowNs;
    }
}

int presenceEvent(PresenceState* p, int present, int64_t nowNs) {
    present = present != 0;
    if (present == p->present) {
        return PRESENCE_NONE;
    }
    p->present = present;
    if (present) {
        p->doffFired = 0;
        presenceInput(p, nowNs);
        return PRESENCE_DON;
    }
    p->offSinceNs = nowNs;
    p->doffFired = 0;
    return PRESENCE_DOFF;
}

void presenceInput(PresenceState* p, int64_t nowNs) {
    if (nowNs > p->lastInputNs) {
        p->lastInputNs = nowNs;
    }
    p->idleFired = 0;
}

int presenceTick(PresenceState* p, int64_t nowNs, int streaming) {
    if (!streaming) {
        return PRESENCE_NONE;
    }
    if (!p->present && p->doffSeconds > 0 && !p->doffFired
            && nowNs - p->offSinceNs >= (int64_t)p->doffSeconds * 1000000000LL) {
        p->doffFired = 1;
        return PRESENCE_DISCONNECT_DOFF;
    }
    if (p->idleMinutes > 0 && !p->idleFired
            && nowNs - p->lastInputNs >= (int64_t)p->idleMinutes * 60LL * 1000000000LL) {
        p->idleFired = 1;
        return PRESENCE_DISCONNECT_IDLE;
    }
    return PRESENCE_NONE;
}

double presenceOffSeconds(const PresenceState* p, int64_t nowNs) {
    if (p->present || nowNs < p->offSinceNs) {
        return 0.0;
    }
    return (double)(nowNs - p->offSinceNs) / 1e9;
}

static _Atomic int64_t g_activityNs;

int64_t presenceNowNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void presenceNoteActivity(void) {
    atomic_store_explicit(&g_activityNs, presenceNowNs(), memory_order_relaxed);
}

int64_t presenceActivityNs(void) {
    return atomic_load_explicit(&g_activityNs, memory_order_relaxed);
}
