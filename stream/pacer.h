// Moonlight XR Native (lane av-latency): video frame pacing, after
// moonlight-qt's Pacer (app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp,
// handleVsync). Pure policy; hw_video.c keeps the queue of decoded frames.
//
// Why: the frame loop runs once per display refresh (xrWaitFrame) and used
// to show the newest decoded frame. A 90 fps stream on a 90 Hz display with
// arrival jitter then shows one refresh with no new frame (a repeat) and the
// next with two (one thrown away): uneven motion on every wobble, which is
// what testufo shows as micro stutter.
//
// What: decoded frames queue (at most MLXR_PACER_MAX_QUEUED, the oldest
// dropped past that) and each refresh shows the OLDEST one, so a frame that
// arrives a little late still gets its own refresh and the motion stays
// even. Catch-up drops only when the queue stays long: the target is 1
// queued frame, relaxed to 3 while the queue ran down to 1 or less at any
// refresh in the last half second (moonlight-qt's rule), so the extra
// latency is a frame or so and goes away when arrivals settle.
//
// Native (lane perf): the low-latency mode (the default, MLXR_VIDEO_PACING=2
// or unset; 1 is the smooth mode above). Over the owner's Wi-Fi the stream
// stalls often (lost frames, waits for a recovery frame) and then delivers
// a burst; the smooth mode showed every frame of the burst one refresh
// apiece, so the picture ran 2 frames (22 ms) late for the next half second
// (owner's 23:46 run: 1.45-1.66 frames waiting on average at full rate).
// Low latency keeps the one-refresh cushion that hides jitter (a single
// refresh without a new frame) but relaxes only to 2, and after a real
// stall (2 or more refreshes without a new frame) shows the newest frame of
// the burst straight away: frames that old are not worth the delay.
#pragma once
#include <stdint.h>

#define MLXR_PACER_MAX_QUEUED 3
#define MLXR_PACER_MAX_HISTORY 256

typedef enum {
    MLXR_PACE_NEWEST = 0,   // newest frame every refresh (no pacing)
    MLXR_PACE_SMOOTH = 1,   // moonlight-qt's rule: oldest, cushion up to 3
    MLXR_PACE_LOW_LATENCY = 2, // oldest, cushion up to 2, newest after a stall
} MlxrPaceMode;

typedef struct MlxrPacer {
    int historyLen;                      // refreshes in half a second
    uint8_t hist[MLXR_PACER_MAX_HISTORY];
    int histCount, histHead;
    int emptyRun;                        // refreshes since a frame was last shown
    int lowLatency;                      // Native (lane perf): MLXR_PACE_LOW_LATENCY rules

    // counters (the log takes deltas)
    uint64_t refreshes;     // refreshes with the stream up
    uint64_t shown;         // refreshes that showed a new frame
    uint64_t holes;         // single refreshes without a new frame between two that had one (a repeat)
    uint64_t catchUpDrops;  // frames dropped because the queue stayed long
    uint64_t staleDrops;    // Native (lane perf): frames of a burst after a stall skipped for the newest
    uint64_t queuedSum;     // frames waiting at each refresh, for the average depth
} MlxrPacer;

/** "0" newest, "1" smooth, "2" low latency; NULL, empty or unknown: low latency. */
MlxrPaceMode mlxr_pace_mode_parse(const char* s);
const char* mlxr_pace_mode_name(MlxrPaceMode m);

void mlxr_pacer_init(MlxrPacer* p, int displayHz);
/** Native (lane perf): as mlxr_pacer_init, with the low-latency rules on or off. */
void mlxr_pacer_init_mode(MlxrPacer* p, int displayHz, int lowLatency);
/**
 * One display refresh with `queued` decoded frames waiting. Sets *drop to the
 * number of OLDEST frames to drop first; returns 1 when the oldest of the rest
 * is to be shown now (0: nothing to show, the last picture stays).
 */
int mlxr_pacer_refresh(MlxrPacer* p, int queued, int* drop);
/** The same accounting for the newest-frame mode (shown or not), for comparison. */
void mlxr_pacer_note(MlxrPacer* p, int queued, int shown);
