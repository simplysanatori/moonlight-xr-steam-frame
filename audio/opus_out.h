// Audio renderer for moonlight-common-c: Opus multistream decode -> (stereo
// passthrough | 5.1/7.1 through the virtual surround stage or a plain
// downmix) -> a low-latency PulseAudio stream.
//
// Usage (see docs/dev/reports/audio.md for the full sequence):
//
//   AUDIO_RENDERER_CALLBACKS ar;
//   mlxr_opus_out_callbacks(&ar);
//   MlxrOpusOutConfig cfg; mlxr_opus_out_config_default(&cfg);
//   cfg.hrtfDir = "<assets>/hrtf/kemar";
//   LiStartConnection(..., &ar, ..., &cfg /* audioContext */, 0 /* arFlags */);
//   ...frame loop:  mlxr_opus_out_set_pose(yaw, pitch); mlxr_opus_out_stats(&s);
//   headset off:    mlxr_opus_out_set_muted(true);
//
// The module is a singleton (the callbacks carry no instance pointer).
//
// Threading
//   init / start / stop / cleanup: called by moonlight-common-c from the thread
//     running LiStartConnection (init, start) and LiStopConnection (stop,
//     cleanup). stop() and cleanup() are called after the audio receive threads
//     have been told to stop and (for cleanup) joined, so no decode is in flight.
//   decodeAndPlaySample: advertised CAPABILITY_DIRECT_SUBMIT, so it runs on
//     common-c's audio receive thread, one packet at a time, ~every 5 ms. It
//     never waits for the sound server (it takes the PulseAudio mainloop lock for
//     a few microseconds) and does the Opus decode and the HRTF convolution on
//     that thread. data == NULL means the packet was lost: packet loss
//     concealment is run for one frame.
//   set_pose / set_surround_enabled / set_muted / stats: any thread, lock-free.
//     Mute is applied by the audio thread on its next packet (cork + flush the
//     stream, drop packets); unmute restarts the decoder so there is no stale
//     state. stats() after cleanup() returns the totals of the stream that ended.
#ifndef MLXR_OPUS_OUT_H
#define MLXR_OPUS_OUT_H

#include <stdbool.h>
#include <stdint.h>

#include "Limelight.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Directory with the 37 KEMAR WAVs (assets/hrtf/kemar). NULL: no HRTFs, 5.1/7.1
    // are downmixed to stereo plainly.
    const char* hrtfDir;
    // Render 5.1/7.1 with the HRTF virtual surround (initial state; runtime
    // toggle: mlxr_opus_out_set_surround_enabled).
    bool surround;
    // PulseAudio sink name; NULL = default sink.
    const char* sink;
    // SteamOS runs every output stream through Valve's VR spatializer (WirePlumber
    // script) while the built-in speakers are the default sink, which would process
    // our own HRTF rendering a second time and adds ~30 ms. Streams whose node name
    // contains both "virtual" and "sink" are exempt. false (default): use such a name,
    // i.e. bypass it; true: plain name "Moonlight XR" (the system spatializes).
    // See docs/dev/reports/audio.md.
    bool systemSpatializer;
    // Explicit stream (PipeWire node) name; overrides the choice above. NULL = automatic.
    const char* streamName;
    // Server buffer target / request size / start threshold / our own queue cap.
    // 0 = defaults (20 / 5 / tlength/2 / 10 ms).
    int tlengthMs, minreqMs, prebufMs, queueCapMs;
    // Adaptive jitter cushion on top of the server buffer (jitter.h): range in ms.
    // 0/0 = defaults (0 and 40). fixedBuffer = true turns it off (the old fixed
    // 20 ms + 10 ms queue). Environment MLXR_AUDIO_JITTER="min,max" overrides.
    int jitterMinMs, jitterMaxMs;
    bool fixedBuffer;
    // Priority of common-c's audio receive thread, which decodes and feeds the
    // sink (thread_prio.h): "off", "nice" (default) or "rt". NULL = default;
    // environment MLXR_AUDIO_PRIORITY overrides.
    const char* threadPriority;

    // Test hooks. decodedTap sees the Opus decoder's output in Moonlight's
    // channel order (FL FR FC LFE BL BR SL SR) before any surround/downmix.
    // If capture is set, no PulseAudio is used: the final stereo float frames are
    // handed to it instead (on the audio thread).
    void (*decodedTap)(const float* pcm, int frames, int channels, void* user);
    void (*capture)(const float* stereo, int frames, void* user);
    void* hookUser;
} MlxrOpusOutConfig;

typedef struct {
    // counters since init
    uint64_t packets;        // packets handed to decodeAndPlaySample (incl. loss notices)
    uint32_t lostPackets;    // NULL / concealed
    uint32_t decodeErrors;
    uint32_t mutedPackets;   // dropped while muted
    uint32_t underruns;      // sink ran dry while playing
    uint32_t lateUnderruns;  // ... and the data came back late (network burst or scheduling)
    uint32_t silenceUnderruns; // ... because the host stopped sending (inaudible)
    uint32_t overruns;       // pushes that had to drop old audio (queue cap)
    uint64_t droppedFrames;
    uint64_t framesOut;      // frames delivered to the sink
    uint64_t trimmedFrames;  // excess latency spliced out after a calm spell (crossfaded)
    uint32_t jitterGrows, jitterShrinks;
    uint32_t outages;        // late underruns too long for any cushion (the host stalled)
    uint64_t sinkFrames;     // frames handed to the sound server
    // host clock: packets since the first one against this machine's clock
    int64_t firstPacketNs;   // CLOCK_MONOTONIC of the first decoded packet (0 none)
    int64_t lastPacketNs;
    int samplesPerFrame;
    // the audio receive thread (0 until its first packet)
    uint64_t threadCpuNs;    // CPU time it used
    uint64_t threadWaitNs;   // time it was runnable but waited for a CPU (starvation)
    // levels now
    float queueMs;           // our queue
    float latencyMs;         // PulseAudio-reported stream latency (buffer + device); 0 until sampled
    float bufferedMs;        // queueMs + latencyMs
    float tlengthMs;         // what the server granted
    float processUs;         // decode + surround per packet, running average
    float processMaxUs;
    float jitterTargetMs;    // cushion kept in our queue now (adaptive)
    int gapMaxMs;            // longest gap between packets since the last take_gap_max
    char threadPriority[96]; // what the priority raise reached ("nice -10 (rtkit)", ...)
    int channels;            // 2, 6 or 8
    int surroundActive;      // 1 when HRTF rendering is in use right now
    int muted;
    int running;             // init done and sink alive (0 once the PulseAudio server is gone)
    int sinkDead;            // the PulseAudio server went away
    int taps;                // HRTF taps (0 without surround)
    char kernel[8];          // "FFT" / "direct" / ""
} MlxrOpusOutStats;

void mlxr_opus_out_config_default(MlxrOpusOutConfig* cfg);
/** Sets the config used by the next init (also taken from init's context pointer if non-NULL). */
void mlxr_opus_out_configure(const MlxrOpusOutConfig* cfg);

/** Fills the callbacks (init/start/stop/cleanup/decodeAndPlaySample, DIRECT_SUBMIT). */
void mlxr_opus_out_callbacks(AUDIO_RENDERER_CALLBACKS* cb);

/** Head pose against the screen, radians; yaw positive = head turned left. Any thread. */
void mlxr_opus_out_set_pose(float yawRadians, float pitchRadians);
void mlxr_opus_out_set_surround_enabled(bool enabled);
void mlxr_opus_out_set_muted(bool muted);
void mlxr_opus_out_stats(MlxrOpusOutStats* out);
/** Longest gap between packets since the last call (resets; one caller: the 5 s log). */
int mlxr_opus_out_take_gap_max(void);

#ifdef __cplusplus
}
#endif
#endif
