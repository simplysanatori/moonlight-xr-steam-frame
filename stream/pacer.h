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
#pragma once
#include <stdint.h>

#define MLXR_PACER_MAX_QUEUED 3
#define MLXR_PACER_MAX_HISTORY 256

typedef struct MlxrPacer {
    int historyLen;                      // refreshes in half a second
    uint8_t hist[MLXR_PACER_MAX_HISTORY];
    int histCount, histHead;
    int emptyRun;                        // refreshes since a frame was last shown

    // counters (the log takes deltas)
    uint64_t refreshes;     // refreshes with the stream up
    uint64_t shown;         // refreshes that showed a new frame
    uint64_t holes;         // single refreshes without a new frame between two that had one (a repeat)
    uint64_t catchUpDrops;  // frames dropped because the queue stayed long
    uint64_t queuedSum;     // frames waiting at each refresh, for the average depth
} MlxrPacer;

void mlxr_pacer_init(MlxrPacer* p, int displayHz);
/**
 * One display refresh with `queued` decoded frames waiting. Sets *drop to the
 * number of OLDEST frames to drop first; returns 1 when the oldest of the rest
 * is to be shown now (0: nothing to show, the last picture stays).
 */
int mlxr_pacer_refresh(MlxrPacer* p, int queued, int* drop);
/** The same accounting for the newest-frame mode (shown or not), for comparison. */
void mlxr_pacer_note(MlxrPacer* p, int queued, int shown);
