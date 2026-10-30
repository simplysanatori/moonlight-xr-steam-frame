// perf-<stamp>.csv, next to vrtiming.log: one row every 5 s while the stats
// are on, each with the configuration it was measured in (stream size and
// rate, bitrate asked for, codec, display rate, 3D, room, sharpening) beside
// the figures, so runs in different configurations can be compared offline.
// Only numbers and fixed words go in: nothing in it names a host or a network.
//
// Native (lane wave3-hud). PLAN 4b.15. GPLv3.
#ifndef XR_PERFLOG_H
#define XR_PERFLOG_H

#include <stddef.h>

#include "xr_hud.h"

typedef struct MlxrPerfRow {
    double tSec;               // seconds since the stream's first row
    int width, height;         // the picture the stream sends
    int streamFps;             // the rate asked of the host
    int bitrateKbps;           // asked of the host
    int codec;                 // Settings.codec: 0 auto, 1 av1, 2 hevc, 3 h264
    double displayHz;          // what the display is on
    int stereo;                // 3D on
    int environment;           // Settings.environment (0 auto, 1 passthrough, 2 void, 3.. rooms)
    int sharpen, supersample;  // 0 off, 1 normal, 2 quality
    int slow;                  // the slow connection badge was up
    MlxrHudFigures fig;
    MlxrCompositorMetrics comp;
} MlxrPerfRow;

// The first line of the file
size_t mlxr_perf_header(char* buf, size_t cap);
// One row, newline ended. Returns the length written, 0 when it did not fit.
size_t mlxr_perf_row(const MlxrPerfRow* r, char* buf, size_t cap);

// <dir>/perf-<stamp>.csv for a stream that began at the local time given
// (year as 4 digits, month 1..12): 1 when it fitted
int mlxr_perf_path(const char* dir, int year, int mon, int day, int hour, int min, int sec,
                   char* out, size_t cap);

#endif
