// Adaptive jitter cushion for the stream's audio (pure policy, no I/O, so it
// is unit-tested on its own; pulse_out.c drives it under the mainloop lock).
//
// Why: packets come off Wi-Fi in bursts. The sound server holds ~16 ms and
// our queue used to hold nothing in steady state, so any gap in arrivals
// longer than ~16-25 ms ran the sink dry (a crackle), and the burst that
// followed the gap was cut back to the queue cap, so the next gap ran dry
// again. moonlight-common-c's FEC queue alone can hold packets for up to
// 30 ms (4 x 5 ms + 10 ms) while it waits for a lost one.
//
// What: a cushion `target` of audio is kept in our queue on top of the
// server's buffer. It starts at minMs (0 by default: no added latency on a
// clean link). When the sink runs dry and data then comes back within
// silenceMs (a late burst, not the host going quiet), the cushion grows to
// cover the gap that was seen (at least stepMs more), up to maxMs. After an
// underrun, playback waits until server buffer + cushion are queued again,
// so the next gap meets a full buffer instead of a trickle. After calmMs
// without such an underrun, the cushion shrinks by shrinkMs, and trim()
// says how many frames of excess to skip (the caller splices them out with
// a crossfade), so latency comes back down once the link is clean.
//
// Stock moonlight-qt for comparison: 15 ms device buffer plus up to 50 ms
// in SDL's queue plus up to 30 ms in common-c's queue. maxMs 40 keeps ours
// (server ~16 ms + cushion + one quantum of slack) inside that.
#ifndef MLXR_JITTER_H
#define MLXR_JITTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // tunables (init sets the defaults)
    int minMs, maxMs;       // cushion range
    int stepMs;             // least growth per late underrun
    int shrinkMs, calmMs;   // shrink by shrinkMs after calmMs without one
    int silenceMs;          // a gap at least this long is the host going quiet
    int outageMarginMs;     // a late gap over server + maxMs + this is an outage: no growth
    int trimEveryMs;        // how often trim() looks at the queue's low point
    int trimSlackMs;        // excess the low point may carry before it is cut

    // state
    int targetMs;
    int refilling;          // waiting for server buffer + cushion before playing
    int pendingUnderrun;    // the sink ran dry; classified when data comes back
    int64_t lastPacketMs;   // -1 before the first
    int64_t lastLateMs;     // last late underrun (or start)
    int64_t lastShrinkMs;
    int64_t trimWindowMs;   // start of the current trim window
    int lowFrames;          // lowest queue level seen in it (-1 none)

    // counters since init
    uint32_t lateUnderruns;    // the sink ran dry and the data came back late
    uint32_t silenceUnderruns; // ... because the host stopped sending
    uint32_t outages;          // late underruns too long for any cushion (counted in lateUnderruns too)
    uint32_t grows, shrinks;
    uint64_t trimmedFrames;
    int maxGapMs;              // longest gap between packets since stats_reset_gap
} MlxrJitter;

void mlxr_jitter_init(MlxrJitter* j, int minMs, int maxMs);
/** The sink ran dry (any thread holding the caller's lock). */
void mlxr_jitter_underrun(MlxrJitter* j);
/** Playback starts over (stream start, uncork): wait for the cushion first. */
void mlxr_jitter_restart(MlxrJitter* j);
/**
 * A packet arrived at nowMs. serverMs is what the server buffer holds when full
 * (tlength granted). Classifies a pending underrun, grows or shrinks the cushion.
 * Returns 1 when targetMs changed.
 */
int mlxr_jitter_packet(MlxrJitter* j, int64_t nowMs, float serverMs);
/**
 * May the queue feed the server now? While refilling, only once queuedFrames
 * reaches server buffer + cushion; that ends the refill.
 */
int mlxr_jitter_may_play(MlxrJitter* j, int queuedFrames, int serverFrames, int rate);
/**
 * Called after each push+pump with what is left in our queue. Returns how many
 * frames of excess to skip now (0 mostly): once per trimEveryMs, the low point of
 * the window above cushion + trimSlackMs.
 */
int mlxr_jitter_trim(MlxrJitter* j, int64_t nowMs, int queuedFrames, int rate);
/** Queue cap that leaves room for the cushion plus the refill (frames). */
int mlxr_jitter_cap_frames(const MlxrJitter* j, int serverFrames, int slackFrames, int rate);

#ifdef __cplusplus
}
#endif
#endif
