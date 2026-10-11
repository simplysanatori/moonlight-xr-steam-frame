// Moonlight XR Native (lane perf): the host's frame cadence, from the 90 kHz
// RTP timestamp Sunshine/Apollo stamps on each frame at capture. GPLv3.
//
// Why: the owner's 00:47 run "wasn't stable 90": 69 fps decoded over 200 s,
// and even in windows with no loss and a 2 ms RTT the host sent only 67-85
// fps with 16-39 one-refresh repeats. Whether that is the host's capture
// (Apollo's virtual display had been at 360 Hz: frames then land on a 2.78 ms
// grid and the gaps vary when resampled to 90) or the game's own frame rate
// needs the intervals between frames as the host saw them, which no log had.
//
// What: intervals between consecutive frames (frame numbers n, n+1) in
// buckets around the 90 Hz period, the host's frame rate over the window
// (frames over the timestamp span), how many intervals sit on a 2.78 ms
// (360 Hz) grid but off the 11.1 ms one, and the arrival jitter at the
// client for the same frames (receive time minus capture time, spread).
// Native (lane refresh): "the 11.1 ms one" is the stream's own period
// (mlxr_cadence_set_fps; 90 fps when not set), so a 120 fps stream's 8.3 ms
// frames, which sit on the 2.78 ms grid too, are not counted as off it.
#pragma once
#include <stdint.h>

#define MLXR_CADENCE_BUCKETS 7

typedef struct MlxrCadence {
    int have;
    uint32_t lastFrame, lastRtp;
    uint64_t lastRecvUs;
    uint64_t intervals, spanTicks;
    uint64_t bucket[MLXR_CADENCE_BUCKETS]; // <6, 6-9.7, 9.7-12.5, 12.5-16, 16-25, 25-50, >=50 ms
    uint64_t grid360;                      // on the 2.78 ms grid, off the stream period's grid
    uint32_t periodTicks;                  // Native (lane refresh): the stream's period, 90 kHz ticks (0: 1000)
    double transitMin, transitSum, transitSq; // receive minus capture (offset unknown), ms
    uint64_t transitN;
} MlxrCadence;

void mlxr_cadence_add(MlxrCadence* c, uint32_t frameNumber, uint32_t rtp90k, uint64_t recvUs);
/** Native (lane refresh): the stream's frame rate, for the grid count (0 or less: 90). */
void mlxr_cadence_set_fps(MlxrCadence* c, int fps);
/** The window's line; starts the next window (keeps the last frame for continuity). */
void mlxr_cadence_line(MlxrCadence* c, char* buf, int len);
