// Moonlight XR Native, tests/m6: the headless device of tests/m5 with the
// hardware decoder's import extensions on as well (xr_vk.c asks for them in
// the session), and a frame helper shaped like the session's frame loop:
// xrWorldFrameBegin, xrDepthFrame, the body, one submission signalling the
// frame's timeline value, xrDepthSubmitted. GPLv3.
#pragma once
#include "m5_gpu.h"
#include "xr_depth.h"
#include "settings.h"

// 0 on success; withImport adds mlxr_vkimp_device_extensions()
int m6_gpu_init(M5Gpu* g, int withImport);

// A context like the session's for the 3D: defaults, the engine table, the
// assets directory (NULL: the source tree's), the settings, the device
XrCtx* m6_ctx(M5Gpu* g, const XrDepthEngineOps* ops, const char* assetsDir, const Settings* s);
void m6_ctx_free(XrCtx* ctx);

// One frame. inFlight: how many earlier frames may still be on the GPU when
// this one is recorded (0 waits for each, as the session's fence does).
// gpuDone is the timeline's real counter.
void m6_frame(M5Gpu* g, XrCtx* ctx, int inFlight, void (*body)(XrCtx* ctx, VkCommandBuffer cmd, void* user),
              void* user);
// Waits for every frame
void m6_idle(M5Gpu* g, XrCtx* ctx);

// PNG of an RGBA8 buffer (alpha forced opaque when opaque is set)
int m6_png(const char* path, const uint8_t* rgba, int w, int h, int opaque);
