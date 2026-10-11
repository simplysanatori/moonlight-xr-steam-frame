// Moonlight XR Native (lane perf): a watchdog on reference frame
// invalidation (RFI). GPLv3.
//
// Why: with RFI on (e439ea2), moonlight-common-c drops every frame after a
// loss until Sunshine sends the frame that re-references a good one. In the
// owner's 23:46 run (80 Mbps over the Frame's hotspot) 69 recoveries came
// back as such P-frames, all within 89 ms and after at most 3 dropped
// frames; the other 40 came back only as an IDR frame Sunshine fell back to,
// after 40 ms to 12 s (p90 0.6 s; 43 s of the 91 s run spent waiting, 1055
// complete frames thrown away). A recovery that has not come within ~100 ms
// is not coming as a P-frame: asking for an IDR frame then is what common-c
// does without RFI, so the long waits end in about a round trip.
//
// What: fed common-c's log lines (the only place it says this) and a clock,
// it says when to call LiRequestIdrFrame: once a wait is older than
// timeoutMs AND at least minDrops complete frames were dropped in it (frames
// flow again, so the request is not lost in the same outage), then again
// every repeatMs while the wait goes on. Pure logic, no common-c calls.
#pragma once
#include <stdint.h>

typedef struct MlxrRfiWatch {
    int timeoutMs, minDrops, repeatMs; // timeoutMs 0: off
    int waiting;                       // an RFI wait is open
    uint64_t startMs, lastIdrMs, lastLineMs;
    int drops;                         // complete frames dropped in this wait
    // counters (the log takes deltas)
    uint64_t waits, closed, recovered, idrAsked;
    uint64_t waitMsSum, waitMsMax;     // closed waits
} MlxrRfiWatch;

void mlxr_rfi_watch_init(MlxrRfiWatch* w, int timeoutMs, int minDrops, int repeatMs);
/** One common-c log line at nowMs. Returns 1 when an IDR frame is to be requested now. */
int mlxr_rfi_watch_line(MlxrRfiWatch* w, const char* line, uint64_t nowMs);
