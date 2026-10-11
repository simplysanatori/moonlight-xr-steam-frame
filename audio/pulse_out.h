// A low-latency PulseAudio playback stream (async API on a threaded
// mainloop). The PulseAudio API is served by PipeWire on SteamOS.
//
// One process-wide context + mainloop thread is shared by every stream
// (refcounted), so the stream session's audio and the UI click use the same
// connection. Each stream is float32, interleaved.
//
// Data path: the producer pushes frames into a small bounded queue
// (pcmq.h) and then pumps whatever the server has asked for into the stream
// (pa_stream_writable_size). The mainloop thread's write callback pumps too.
// The server-side buffer is therefore tlength, and the queue only holds what
// the server has not yet asked for; past queueCapMs the OLDEST frames are
// dropped (counted), so latency is bounded instead of drifting.
//
// Threading: open/close from one control thread. write() from the single
// producer thread (it takes the mainloop lock for a few microseconds and never
// waits for the server). set_corked from the producer thread. stats() from any
// thread and takes no pulse lock.
#ifndef MLXR_PULSE_OUT_H
#define MLXR_PULSE_OUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MlxrPulseOut MlxrPulseOut;

typedef struct {
    const char* streamName;  // shown in pavucontrol; default "Moonlight XR"
    const char* role;        // media.role: "game" (default) or "event"
    const char* sink;        // NULL = the server's default sink
    int channels;            // 1 or 2 (float32)
    int rate;                // 48000
    int tlengthMs;           // server buffer target (default 20)
    int minreqMs;            // server request granularity (default 5)
    int prebufMs;            // start threshold (default tlengthMs / 2; 0 = start at once)
    int queueCapMs;          // frames queued here beyond the server's buffer (default 10)
    bool noFadeAfterUnderrun; // true for short one-shot sounds: do not soften the next attack
    // Adaptive jitter cushion (jitter.h): extra audio kept in our queue on top of
    // the server's buffer, grown when late packets run the sink dry, shrunk when
    // the link is calm. Off by default (the UI click); the stream turns it on.
    bool adaptive;
    int jitterMinMs, jitterMaxMs; // cushion range (defaults 0 and 40)
} MlxrPulseOutParams;

typedef struct {
    uint32_t underruns;        // times the server ran dry while playing
    uint32_t overruns;         // pushes that had to drop old frames
    uint64_t droppedFrames;    // frames dropped by those
    uint64_t writtenFrames;    // frames given to the server
    float queueMs;             // waiting in our queue right now
    float latencyMs;           // server-reported total (buffer + device), sampled ~4x/s
    float tlengthMs;           // what the server granted
    float minreqMs;
    int running;               // stream READY and the server alive
    int dead;                  // the server went away; frames are discarded
    // adaptive cushion (0 when not adaptive)
    int jitterTargetMs;        // cushion kept in our queue now
    uint32_t lateUnderruns;    // underruns where the data came back late (network or scheduling)
    uint32_t silenceUnderruns; // underruns where the host had stopped sending (not audible)
    uint32_t jitterGrows, jitterShrinks;
    uint64_t trimmedFrames;    // excess latency spliced out (crossfaded)
    int gapMaxMs;              // longest gap between writes since the last take_gap_max
} MlxrPulseOutStats;

void mlxr_pulse_out_params_default(MlxrPulseOutParams* p);

/** Connects the shared context if needed and opens the stream. NULL with err on failure. */
MlxrPulseOut* mlxr_pulse_out_open(const MlxrPulseOutParams* p, char* err, int errLen);
void mlxr_pulse_out_close(MlxrPulseOut* po);

/** Queues n frames and pumps. Returns the number of old frames dropped (0 normally). */
int mlxr_pulse_out_write(MlxrPulseOut* po, const float* frames, int n);

/** Pauses (and empties) the stream, or resumes it. While corked write() discards. */
void mlxr_pulse_out_set_corked(MlxrPulseOut* po, bool corked);

/** Any thread, no pulse lock. */
void mlxr_pulse_out_stats(MlxrPulseOut* po, MlxrPulseOutStats* out);
/** The longest gap between writes (under the silence threshold) since the last call; resets it. */
int mlxr_pulse_out_take_gap_max(MlxrPulseOut* po);

/**
 * The shared context, for the tests' helpers (loading a null sink). Returns
 * with a reference held; release with mlxr_pulse_release(). The lock functions
 * are pa_threaded_mainloop_lock/unlock on the returned loop.
 */
struct pa_threaded_mainloop;
struct pa_context;
int mlxr_pulse_acquire(struct pa_threaded_mainloop** loop, struct pa_context** ctx, char* err,
                       int errLen);
void mlxr_pulse_release(void);

#ifdef __cplusplus
}
#endif
#endif
