// The in-headset latency / network HUD's arithmetic and words: the figures of
// one window from two readings of the stream's running totals, the one line
// of the Lite stats mode and the few lines of the HUD, the rule that raises
// the "slow connection" badge, and the compositor timing the runtime reports.
// Plain numbers and text, no OpenXR calls, so the host tests reach all of it.
//
// Native (lane wave3-hud). PLAN 3.4, 3.14, 4b.15. GPLv3.
#ifndef XR_HUD_H
#define XR_HUD_H

#include <stddef.h>
#include <stdint.h>

// What the Stats overlay shows (Settings.stats_style)
#define STATS_STYLE_FULL 0   // every line the session writes (the overlay as it was)
#define STATS_STYLE_HUD  1   // latency, network, decode, rate in a few lines
#define STATS_STYLE_LITE 2   // speed / latency / decode / loss / fps on one line

// The stream's running totals, read at one moment. Every counter only goes
// up within a stream; the window is the difference of two readings.
typedef struct MlxrHudCum {
    int valid;                 // a stream is up and these are its numbers
    uint64_t frames;           // frames received (decode units)
    uint64_t lost;             // frames lost on the network
    uint64_t bytes;            // video payload received
    uint64_t hostN;            // frames the host timed its own capture and encode of
    double hostMsSum;
    double recvMsSum;          // first packet to frame assembled
    double queueMsSum;         // assembled to handed to the decoder
    uint64_t decodeN;          // frames the decoder timed
    double decodeMsSum;
    uint32_t rttMs;            // the control stream's estimate now
    uint32_t rttVarMs;
    uint32_t fecRecovered;     // video packets saved by the host's error correction
    uint32_t fecFailed;        // and the times it could not save them
    uint64_t udpDrops;         // datagrams the kernel dropped for a full receive buffer (whole system)
    int poor;                  // the connection status common-c last reported is "poor"
    int setKbps;               // the bitrate the stream was started with (0 unknown); wave4b-abr
} MlxrHudCum;

// One window of it
typedef struct MlxrHudFigures {
    int valid;                 // there were frames in the window
    double mbps;               // video received
    double hostMs;             // the host's capture and encode
    double recvMs;             // a frame reaching us over the network
    double queueMs;            // waiting for our decoder
    double decodeMs;
    double rttMs;
    double latencyMs;          // host + half the round trip + receive + queue + decode
    double lossPct;            // frames lost over frames sent
    double fps;                // pictures drawn a second
    uint32_t fecRecovered, fecFailed;   // in the window
    uint64_t udpDrops;                  // in the window
    int poor;
    int setKbps;               // the bitrate the stream was started with (0 unknown); the HUD shows it beside the rate
    // Native (lane beam-streak): figures nothing in the window told, shown as "n/a" rather than as a
    // 0 that reads as a real one: the host's capture and encode (a host that sends no timing with its
    // frames) and our decode (no frame the decoder timed: the software decoder, or none taken)
    int hostUnknown;
    int decodeUnknown;
} MlxrHudFigures;

// The window from prev (NULL or invalid: from the start) to cur, secs long;
// fps is the drawn picture rate the caller counted. 0 when cur is not valid.
// A counter that went down means a new stream: that window is from zero.
int mlxr_hud_window(const MlxrHudCum* prev, const MlxrHudCum* cur, double secs, double fps,
                    MlxrHudFigures* out);

// "54 Mb/s | 12 ms | dec 3.1 | loss 0.2% | 90 fps": the Lite stats mode. A
// leading "! " when the connection is slow. "" when there are no figures.
void mlxr_hud_lite(const MlxrHudFigures* f, int slow, char* buf, size_t cap);

// Native (lane beam-streak): the Full stats' decoder line, from whichever decoder is decoding (the
// hardware one's counters, or the software one's frame slot). "Decoded 1234, dropped 5, decode 3.1
// ms"; the decode time "n/a" before the first frame. It read the software decoder's slot alone, which
// the hardware decoder (every stream on the Frame) never fills, so it said 0 all through a stream.
typedef struct MlxrDecodeTally {
    uint64_t decoded;          // pictures out of the decoder
    uint64_t dropped;          // of them, never shown (the loop fell behind, or a newer one came)
    double decodeMs;           // the decoder's time for a frame, recent average
} MlxrDecodeTally;
void mlxr_stats_decode_text(const MlxrDecodeTally* t, char* buf, size_t cap);

// Native (lane beam-streak): the Full stats' audio line, "Audio 2ch, 40 ms buffered, 2 underruns, 0
// lost", or "No audio from host" while no packet has come (the host playing its sound itself, or a
// silent stream), where it said "audio 0ch 0 ms buffered"
void mlxr_stats_audio_text(uint64_t packets, int channels, double bufferedMs, uint32_t underruns, uint32_t lost,
                           char* buf, size_t cap);

// Compositor figures from the runtime (XR_META_performance_metrics)
typedef struct MlxrCompositorMetrics {
    int valid;                 // the runtime answered at least one counter
    int haveCpu, haveGpu, haveDropped, haveApp;
    float cpuMs, gpuMs;        // the compositor's frame time on each
    float appCpuMs, appGpuMs;  // and ours
    uint32_t dropped;          // frames the compositor dropped since the session began
} MlxrCompositorMetrics;

// The HUD, lines split by '\n': latency with its parts, the network, the rate
// and, when the runtime reports them, the compositor. m may be NULL.
void mlxr_hud_panel(const MlxrHudFigures* f, int slow, const MlxrCompositorMetrics* m, double displayHz,
                    char* buf, size_t cap);

// The compositor part alone, for the log line and the full overlay: "Compositor CPU 1.2 ms, GPU 3.4 ms,
// 0 dropped"; "" when m has nothing.
void mlxr_compositor_line(const MlxrCompositorMetrics* m, char* buf, size_t cap);

// Sets one named counter of the runtime's (the path after "/perfmetrics_meta/", such as
// "compositor/cpu_frametime") in m. Unknown names are left alone; 1 when it was one we use.
int mlxr_metric_set(MlxrCompositorMetrics* m, const char* name, float value);

// ---- the slow connection badge

// Raised when the connection has been poor (common-c's status, or loss over
// SLOW_LOSS_PCT) for SLOW_ON_MS without a break, and lowered after SLOW_OFF_MS
// without any
#define SLOW_LOSS_PCT 1.5
#define SLOW_ON_MS 5000
#define SLOW_OFF_MS 5000

typedef struct MlxrSlowConn {
    int on;
    int64_t badSinceMs, goodSinceMs;   // -1 when not in that state
} MlxrSlowConn;

void mlxr_slow_init(MlxrSlowConn* s);
// One reading a second or so. f NULL (no stream): the badge goes down at once.
// Returns whether the badge is up.
int mlxr_slow_update(MlxrSlowConn* s, int64_t nowMs, const MlxrHudFigures* f);

#endif
