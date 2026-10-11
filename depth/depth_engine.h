// The depth model's thread and the hand-off around it, ported from Moonlight
// XR's depth path (XrRenderer.java's depth and stage threads, DepthPairs,
// xr_depth.c's capture finish and upload) for a Vulkan app with no GL
// contexts to juggle.
//
//   frame loop   mlxr_depth_due() at each new video frame; when it says so,
//                record mlxr_pass_downscale into a model-sized RGBA8 image and
//                copy it into a host-visible buffer (vkCmdCopyImageToBuffer)
//   stage thread once that copy's fence or timeline value lands,
//                mlxr_depth_submit() the mapped pixels with the frame's id and
//                capture time: converted to model input and run through the
//                scene cut detector there, as nativeFinishDepthCapture did
//   depth thread (owned here) runs the model on the newest submitted capture,
//                no faster than the rate asked for, then normalises the output
//                exactly as nativeUploadDepth did (per texel average over real
//                time, robust range smoothed on its own, cut resets, the
//                global/local remap) into the RGBA8 map the upsample and warp
//                sample: guide colour rgb, depth a, 1 = near
//   frame loop   mlxr_depth_acquire() the newest map, upload it into the next
//                depth texture of the rotation, mlxr_depth_release() it
//
// A capture submitted while another is still waiting for the model replaces
// it (its cut resets carry over to the new one), so the model never runs on a
// stale frame. All calls are thread safe; submit is meant for one thread.
// Rows are top first throughout, as the Vulkan passes are (docs/dev/reports/shaders.md),
// so nothing is flipped. GPLv3.

#ifndef MLXR_DEPTH_ENGINE_H
#define MLXR_DEPTH_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#include "litert_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

// The stage thread's knobs, upstream's XrCtx fields of the same names
typedef struct MlxrDepthParams {
    int tauMs;            // per texel average, DEPTH_TAU_DEFAULT_MS (30); 0 off
    int rangeTauMs;       // range average, DEPTH_RANGE_TAU_DEFAULT_MS (150); 0 off
    int cutLevel;         // scene cut detector: 0 off, 1 on (upstream's default)
    float global;         // depthGlobal, 1.0
    float local;          // depthLocal, the depth scale preference / 100
    float convergence;    // convergence preference / 100
} MlxrDepthParams;

void mlxr_depth_params_default(MlxrDepthParams* p);

typedef struct MlxrDepthConfig {
    const char* modelPath;
    int threads;          // XNNPACK threads including the depth thread (2 if <= 0)
    unsigned flags;       // MLXR_DEPTH_FP16 ...
    int perSecond;        // starting rate, DEPTH_RATE_MIN..DEPTH_RATE_MAX; 0 unlimited
    uint64_t cpuMask;     // CPUs for the depth thread and its workers; 0 leaves it
    int warmupRuns;       // runs on a grey image before ready; < 0 none, 0 means 3
    int keepRaw;          // also hand out the model's raw output
    MlxrDepthParams params;
} MlxrDepthConfig;

typedef enum MlxrDepthState {
    MLXR_DEPTH_LOADING = 0,
    MLXR_DEPTH_READY = 1,
    MLXR_DEPTH_FAILED = 2,  // the model will make no map: stereo stays flat
} MlxrDepthState;

typedef struct MlxrDepthResult {
    int width, height;
    const uint8_t* rgba;  // width*height*4, guide rgb + depth a: the map to upload
    const float* depth;   // the same depth as floats 0..1, 1 = near
    const float* raw;     // the model output (larger nearer), NULL without keepRaw
    uint64_t frameId;     // as submitted
    int64_t captureNs;    // as submitted (the frame's time)
    int64_t submitNs;     // CLOCK_MONOTONIC: submit returned
    int64_t startNs;      // the model started on it
    int64_t doneNs;       // the map was published
    float convertMs;      // RGBA8 to model input + cut check (stage thread)
    float modelMs;        // the model run
    float normMs;         // averages, range, remap, RGBA8 pack
    int resets;           // DEPTH_RESET_TEXEL | DEPTH_RESET_RANGE applied
    uint64_t seq;         // 1 for the first map of the session
} MlxrDepthResult;

typedef struct MlxrDepthStats {
    MlxrDepthState state;
    int width, height, threads;
    float loadMs;         // read, build, allocate
    float warmupMs;       // all warmup runs
    uint64_t submitted;   // captures submitted
    uint64_t replaced;    // dropped for a newer one before the model got to them
    uint64_t maps;        // maps published
    uint64_t failed;      // runs that failed
    uint64_t noSlot;      // maps dropped as every result slot was held
    float mapsPerSecond;  // over the last 32 maps
    float modelMsAvg, modelMsP95;   // over the last 64 runs
    float latencyMsAvg;   // submit to publish, last 64
    int perSecond;        // the rate in force
} MlxrDepthStats;

typedef struct MlxrDepthEngine MlxrDepthEngine;

// Starts the depth thread, which loads the model (its XNNPACK workers inherit
// cpuMask) and warms it up. Does not wait: see mlxr_depth_state and
// mlxr_depth_wait_ready. NULL only when the thread cannot start.
MlxrDepthEngine* mlxr_depth_start(const MlxrDepthConfig* cfg);
// Ends the thread, waiting out a run in progress, and frees everything. Any
// acquired result must have been released.
void mlxr_depth_stop(MlxrDepthEngine* e);

MlxrDepthState mlxr_depth_state(MlxrDepthEngine* e);
// Waits up to timeoutMs (< 0 forever) for READY or FAILED and returns it.
// The reason for a failure goes in err.
MlxrDepthState mlxr_depth_wait_ready(MlxrDepthEngine* e, int timeoutMs, char* err,
                                     size_t errLen);
// The model's input size, the size the downscale target must be (0 before
// READY)
void mlxr_depth_size(MlxrDepthEngine* e, int* w, int* h);

// The rate the depth thread holds to, maps a second (the governor's target,
// xr_rate.h depthGovernor*), 0 for as fast as it goes
void mlxr_depth_set_rate(MlxrDepthEngine* e, int perSecond);
void mlxr_depth_set_params(MlxrDepthEngine* e, const MlxrDepthParams* p);
// Forget the averages and the cut detector, as when the 3D comes back on
void mlxr_depth_reset(MlxrDepthEngine* e);

// Frame loop, once per new video frame: whether to capture this one. Runs
// the rate gate (xr_rate.h depthGateDue) and says no while the engine is not
// ready or a capture is still waiting for the model (upstream's forCapture).
int mlxr_depth_due(MlxrDepthEngine* e, int64_t nowNs);

// Stage thread: a model-sized RGBA8 capture (stride bytes a row; flip for
// bottom-first rows). Copied before it returns. 1 queued, 0 queued in place of
// a waiting capture, -1 not ready or wrong size.
int mlxr_depth_submit(MlxrDepthEngine* e, const uint8_t* rgba, size_t stride, int w, int h,
                      int flip, uint64_t frameId, int64_t captureNs);

// Frame loop: the newest map if its seq is above haveSeq, else NULL. Held
// until released; hold at most one at a time.
const MlxrDepthResult* mlxr_depth_acquire(MlxrDepthEngine* e, uint64_t haveSeq);
void mlxr_depth_release(MlxrDepthEngine* e, const MlxrDepthResult* r);

void mlxr_depth_stats(MlxrDepthEngine* e, MlxrDepthStats* s);

// CLOCK_MONOTONIC in ns, the clock every time above is on
int64_t mlxr_depth_now_ns(void);

// The normalisation on its own (what the depth thread does after each run),
// for tests and tools: state across calls in *n. Not thread safe.
typedef struct MlxrDepthNorm MlxrDepthNorm;
MlxrDepthNorm* mlxr_depth_norm_create(int w, int h);
void mlxr_depth_norm_free(MlxrDepthNorm* n);
// raw: the model output; guide: the model input (rgb 0..1) for the colour;
// outRgba and outDepth may be NULL
void mlxr_depth_norm_run(MlxrDepthNorm* n, const MlxrDepthParams* p, const float* raw,
                         const float* guide, int resets, int64_t nowNs, uint8_t* outRgba,
                         float* outDepth);

#ifdef __cplusplus
}
#endif

#endif
