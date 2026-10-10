// Moonlight XR Native: realtime 2D to 3D (M6). Upstream's xr_depth.c (the
// downscale capture, the readback the stage thread finishes, the map upload)
// and the 3D half of xr_gl.c's renderVideoFrame (upsample, offset search, a
// warp per eye with opposite disparity), rewritten for Vulkan around lane D's
// depth engine (depth/depth_engine.h) and lane S's passes. GPLv3.
//
//   frame loop   xrDepthFrame once a display frame: the engine's lifecycle
//                (started the first time the 3D is on, flat with a toast on
//                FAILED), the governor's rate, the newest map copied into
//                the next of DEPTH_TEX_COUNT depth textures (staging buffer
//                per texture, nothing allocated per frame), the end of a
//                stereoWaiting
//   frame loop   xrDepthPicture before a picture is warped: when the engine
//                says a capture is due, mlxr_pass_downscale into the model
//                sized image and a copy into one of DEPTH_PAIRS host visible
//                buffers, keyed by the frame's timeline value; the upsample
//                and the offset search; what each eye's warp takes
//   frame loop   xrDepthRecordWarp: one mlxr_pass_warp per eye into the
//                halves of a double wide target
//   frame loop   xrDepthSubmitted after the submission: the captures it
//                carries go to the stage thread
//   stage thread waits for the timeline value, mlxr_depth_submit()s the
//                mapped pixels (model input conversion and the scene cut
//                check happen in there), frees the buffer
//   depth thread (the engine's) runs the model and normalises the map
#ifndef XR_DEPTH_H
#define XR_DEPTH_H

#include "xr_renderer.h"
#include "passes.h"
#include "depth_engine.h"

struct Settings;

// The engine behind the 3D, as a table so the tests can put a fake one in
// (tests/m6). The real one is depth/depth_engine.h, present when the depth
// library is built in (MLXR_HAVE_DEPTH).
typedef struct XrDepthEngineOps {
    const MlxrDepthModelSpec* (*spec)(MlxrDepthModelId id);
    MlxrDepthEngine* (*start)(const MlxrDepthConfig* cfg);
    void (*stop)(MlxrDepthEngine* e);
    MlxrDepthState (*state)(MlxrDepthEngine* e);
    MlxrDepthState (*waitReady)(MlxrDepthEngine* e, int timeoutMs, char* err, size_t errLen);
    void (*size)(MlxrDepthEngine* e, int* w, int* h);
    void (*setRate)(MlxrDepthEngine* e, int perSecond);
    void (*setParams)(MlxrDepthEngine* e, const MlxrDepthParams* p);
    void (*reset)(MlxrDepthEngine* e);
    int (*due)(MlxrDepthEngine* e, int64_t nowNs);
    int (*submit)(MlxrDepthEngine* e, const uint8_t* rgba, size_t stride, int w, int h, int flip,
                  uint64_t frameId, int64_t captureNs);
    const MlxrDepthResult* (*acquire)(MlxrDepthEngine* e, uint64_t haveSeq);
    void (*release)(MlxrDepthEngine* e, const MlxrDepthResult* r);
    void (*stats)(MlxrDepthEngine* e, MlxrDepthStats* s);
} XrDepthEngineOps;

// The engine in use (NULL: none, the models are unavailable). Set before
// xrApplySettings: the host hands in xrDepthRealOps() when the depth library
// is built in (MLXR_HAVE_DEPTH), the tests a fake.
const XrDepthEngineOps* xrDepthEngineOps(void);
void xrDepthSetEngineOps(const XrDepthEngineOps* ops);
// depth/depth_engine.h's, in xr_depth_engine.c (library mlxr_xrdepth, only
// with LiteRT)
const XrDepthEngineOps* xrDepthRealOps(void);

// config.ini's 3D choices into the context: stereoMode from depth_source
// (a model whose file is in assets/depth, a test pattern, or off with the
// reason in depthReason), the switch as it was last left, the debug view.
// Called from xrApplySettings, after ctx->assetsDir is known.
void xrDepthConfigure(XrCtx* ctx, const struct Settings* s);
// The model's file for the session's model, "" when there is none
const char* xrDepthModelPath(XrCtx* ctx);

// The width of the picture's swapchain (and the curved picture's own image)
// for a picture this wide: double with a stereo mode, as upstream's chain
int xrDepthChainWidth(XrCtx* ctx, int pictureWidth);

// Once a display frame on the frame loop, after the input pass and before a
// picture is drawn. With ctx->frameCmd recording, a new map goes into the
// next depth texture.
void xrDepthFrame(XrCtx* ctx);

// What one picture's warp takes, per eye, on top of the video, the grade
// and the sharpening (filled by xrDepthPicture)
typedef struct XrDepthDraw {
    int eyes;              // 1 flat (into the left half, or the whole of a mono target), 2 stereo
    int upsampled, occluded;
    MlxrWarpParams eye[2]; // depth, offsets, disparity, eye, convergence, ... (video and grade left 0)
} XrDepthDraw;

// Before a picture is warped (a new decoded frame: newFrame 1; or the one
// in hand drawn again), into ctx->frameCmd: the capture for the model when
// one is due, the upsample and the offset search, and each eye's warp
// parameters. frameIndex counts the stream's frames, frameNs is when this
// one arrived (CLOCK_MONOTONIC).
void xrDepthPicture(XrCtx* ctx, const MlxrVideo* video, int newFrame, uint64_t frameIndex, int64_t frameNs,
                    XrDepthDraw* out);

// The warp of every eye the draw asks for: eye e into target->rect moved
// right by e times its width (upstream's glViewport(eye * videoWidth, ...)).
// common carries the video, the grade, the tint and the sharpening.
void xrDepthRecordWarp(XrCtx* ctx, VkCommandBuffer cmd, const MlxrTarget* target, const MlxrWarpParams* common,
                       const XrDepthDraw* draw);

// After the frame's submission that signalled serial on ctx->gpuTimeline:
// the captures it recorded go to the stage thread
void xrDepthSubmitted(XrCtx* ctx, uint64_t serial);

// The 5 s stats line's 3D part and the Stats overlay's lines ("" without a
// stereo mode). The line's counters start again after each call.
void xrDepthStatsLine(XrCtx* ctx, char* buf, int len);
void xrDepthOverlay(XrCtx* ctx, char* buf, int len);

// Numbers for tests and the report
typedef struct XrDepthInfo {
    int engineState;       // -1 not started, else MlxrDepthState
    int mapW, mapH;
    uint64_t captures;     // downscales recorded
    uint64_t submitted;    // handed to the engine by the stage thread
    uint64_t submitFailed;
    uint64_t skipsNoPair;  // a capture was due with both buffers in use
    uint64_t uploads;      // maps copied into a depth texture
    uint64_t uploadBlocked;// a slot the GPU could still be reading (must stay 0)
    uint64_t pairReuseBad; // a buffer recorded into while the stage thread held it (must stay 0)
    uint64_t lastFrameId;  // of the newest map uploaded
    uint64_t frameIdBackwards; // maps whose frame id went backwards (must stay 0)
    int readIndex;
    int rateSet;           // the rate last handed to the engine
    double ageMsAvg;       // capture to display, over the stats window
    double ageFramesAvg;
    float stageWaitMsAvg;  // timeline wait in the stage thread
    float submitMsAvg;     // mlxr_depth_submit
    VkImageView depthView[DEPTH_TEX_COUNT];
    VkImage depthImage[DEPTH_TEX_COUNT];
    VkImage upsampleImage, offsetImage, downscaleImage;
} XrDepthInfo;
void xrDepthGetInfo(XrCtx* ctx, XrDepthInfo* info);

// End of the session (the device idle): stage thread joined, engine stopped,
// everything freed
void xrDepthDestroy(XrCtx* ctx);

#endif
