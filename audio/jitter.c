#include "jitter.h"

#include <string.h>

void mlxr_jitter_init(MlxrJitter* j, int minMs, int maxMs) {
    memset(j, 0, sizeof *j);
    j->minMs = minMs < 0 ? 0 : minMs;
    j->maxMs = maxMs < j->minMs ? j->minMs : maxMs;
    j->stepMs = 10;
    j->shrinkMs = 5;
    j->calmMs = 30000;
    j->silenceMs = 400;
    j->outageMarginMs = 20;
    j->trimEveryMs = 1000;
    j->trimSlackMs = 4;
    j->targetMs = j->minMs;
    j->refilling = 1;
    j->lastPacketMs = -1;
    j->lastLateMs = -1;
    j->lastShrinkMs = -1;
    j->trimWindowMs = -1;
    j->lowFrames = -1;
}

void mlxr_jitter_underrun(MlxrJitter* j) {
    j->pendingUnderrun = 1;
    j->refilling = 1;
}

void mlxr_jitter_restart(MlxrJitter* j) {
    j->refilling = 1;
    j->pendingUnderrun = 0;
    j->lastPacketMs = -1;
    j->trimWindowMs = -1;
    j->lowFrames = -1;
}

static int roundUp5(int ms) {
    return (ms + 4) / 5 * 5;
}

int mlxr_jitter_packet(MlxrJitter* j, int64_t nowMs, float serverMs) {
    int before = j->targetMs;
    int gap = j->lastPacketMs < 0 ? 0 : (int) (nowMs - j->lastPacketMs);
    j->lastPacketMs = nowMs;
    if (j->lastLateMs < 0) {
        j->lastLateMs = nowMs;
        j->lastShrinkMs = nowMs;
    }
    if (gap < j->silenceMs && gap > j->maxGapMs) {
        j->maxGapMs = gap;
    }
    if (j->pendingUnderrun) {
        j->pendingUnderrun = 0;
        if (gap > 0 && gap < j->silenceMs && gap > (int) serverMs + j->maxMs + j->outageMarginMs) {
            // Late, but by more than any cushion we allow could cover (the
            // host stalled for a few hundred ms): growing would only add
            // latency for the next ordinary gap
            j->lateUnderruns++;
            j->outages++;
        }
        else if (gap > 0 && gap < j->silenceMs) {
            // Late data: enough cushion to have ridden out this gap, and at
            // least one step more than before
            j->lateUnderruns++;
            int want = roundUp5(gap - (int) serverMs);
            int grown = j->targetMs + j->stepMs;
            int t = want > grown ? want : grown;
            j->targetMs = t > j->maxMs ? j->maxMs : t;
            j->lastLateMs = nowMs;
            j->lastShrinkMs = nowMs;
        }
        else {
            j->silenceUnderruns++;
        }
    }
    else if (j->targetMs > j->minMs && nowMs - j->lastLateMs >= j->calmMs
             && nowMs - j->lastShrinkMs >= j->calmMs) {
        int t = j->targetMs - j->shrinkMs;
        j->targetMs = t < j->minMs ? j->minMs : t;
        j->lastShrinkMs = nowMs;
    }
    if (j->targetMs > before) {
        j->grows++;
    }
    else if (j->targetMs < before) {
        j->shrinks++;
    }
    return j->targetMs != before;
}

int mlxr_jitter_may_play(MlxrJitter* j, int queuedFrames, int serverFrames, int rate) {
    if (!j->refilling) {
        return 1;
    }
    int need = serverFrames + (int) ((int64_t) j->targetMs * rate / 1000);
    if (queuedFrames < need) {
        return 0;
    }
    j->refilling = 0;
    // The level right after a refill is not a low point to trim from
    j->trimWindowMs = -1;
    j->lowFrames = -1;
    return 1;
}

int mlxr_jitter_trim(MlxrJitter* j, int64_t nowMs, int queuedFrames, int rate) {
    if (j->refilling) {
        return 0;
    }
    if (j->trimWindowMs < 0) {
        j->trimWindowMs = nowMs;
        j->lowFrames = queuedFrames;
        return 0;
    }
    if (j->lowFrames < 0 || queuedFrames < j->lowFrames) {
        j->lowFrames = queuedFrames;
    }
    if (nowMs - j->trimWindowMs < j->trimEveryMs) {
        return 0;
    }
    int cushion = (int) ((int64_t) j->targetMs * rate / 1000);
    int slack = (int) ((int64_t) j->trimSlackMs * rate / 1000);
    int low = j->lowFrames;
    j->trimWindowMs = nowMs;
    j->lowFrames = -1;
    if (low <= cushion + slack) {
        return 0;
    }
    // The queue never went below `low` in the window: that much of it is
    // latency nothing needed. Cut down to the cushion itself.
    int cut = low - cushion;
    if (cut > queuedFrames) {
        cut = queuedFrames;
    }
    j->trimmedFrames += (uint64_t) cut;
    return cut;
}

int mlxr_jitter_cap_frames(const MlxrJitter* j, int serverFrames, int slackFrames, int rate) {
    int cushion = (int) ((int64_t) j->targetMs * rate / 1000);
    // The refill has to fit: server buffer + cushion, and a quantum over it
    int room = serverFrames > slackFrames ? serverFrames : slackFrames;
    return cushion + room + slackFrames / 2;
}
