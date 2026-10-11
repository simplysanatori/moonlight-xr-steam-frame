// Moonlight XR Native (lane av-latency): video frame pacing policy. GPLv3.
#include "pacer.h"

#include <string.h>

void mlxr_pacer_init(MlxrPacer* p, int displayHz) {
    memset(p, 0, sizeof(*p));
    int len = displayHz / 2;
    p->historyLen = len < 1 ? 1 : (len > MLXR_PACER_MAX_HISTORY ? MLXR_PACER_MAX_HISTORY : len);
    p->emptyRun = -1; // nothing shown yet
}

static void account(MlxrPacer* p, int queued, int shown) {
    p->refreshes++;
    p->queuedSum += (uint64_t)(queued > 0 ? queued : 0);
    if (shown) {
        p->shown++;
        if (p->emptyRun == 1) p->holes++;
        p->emptyRun = 0;
    }
    else if (p->emptyRun >= 0) {
        p->emptyRun++;
    }
}

int mlxr_pacer_refresh(MlxrPacer* p, int queued, int* drop) {
    // moonlight-qt: strict (1) unless the queue was short at some refresh in
    // the last half second, then lenient (3) so a jittery but on-rate stream
    // keeps its cushion
    int target = 1;
    for (int i = 0; i < p->histCount; i++) {
        if (p->hist[i] <= 1) {
            target = 3;
            break;
        }
    }
    int slot = (p->histHead + p->histCount) % p->historyLen;
    if (p->histCount == p->historyLen) {
        slot = p->histHead;
        p->histHead = (p->histHead + 1) % p->historyLen;
    }
    else {
        p->histCount++;
    }
    p->hist[slot] = (uint8_t)(queued > 255 ? 255 : (queued < 0 ? 0 : queued));

    int d = queued > target ? queued - target : 0;
    *drop = d;
    p->catchUpDrops += (uint64_t)d;
    int show = queued - d > 0;
    account(p, queued, show);
    return show;
}

void mlxr_pacer_note(MlxrPacer* p, int queued, int shown) { account(p, queued, shown); }
