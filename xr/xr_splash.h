// Moonlight XR Native (lane wave6-splash): the logo and one line shown in the
// headset while a stream has no picture yet, and again while it reconnects.
// Upstream Moonlight XR keeps its logo up until the first frame; this is the
// same idea, head locked like the toast, fading out over about 300 ms when the
// picture lands. No setting.
//
// The state step (xrSplashStep) is pure; the rest is glue: the line is drawn
// as a panel by the panel worker (xr_panels.c, JOB_SPLASH) into the atlas slot
// ATLAS_SPLASH from the logo in assets/branding/, and a quad layer is appended
// to the frame's layers only while the splash is up or fading. A frame with a
// picture and no fade in progress carries no splash layer. GPLv3.
#pragma once
#include <stdint.h>

#include <openxr/openxr.h>

#include "canvas.h"

struct XrCtx;
struct XrFrameOut;

#define SPLASH_FADE_NS 300000000LL
// The fade is drawn in this many steps (SteamVR has no layer colour scale, so
// each step is a redraw of the art at a lower alpha; the worker keeps only
// the newest)
#define SPLASH_STEPS 5

typedef struct XrSplashState {
    int wasStreaming;   // streaming on the last step: a new stream starts a new "first frame"
    int seenFrame;      // a picture has been shown since this stream began, so the next wait is a reconnect
    int up;             // the splash is wanted this frame
    int reconnect;      // wording: 1 "Reconnecting", 0 "Connecting to <PC>"
    int fading;         // a picture is here and the splash is on its way out
    int64_t fadeFromNs;
    float level;        // 0 gone .. 1 full
    // the art in the atlas: what it was last asked to be, and what has landed
    unsigned askedKey, drawnKey;
    int askedStep, drawnStep;  // 1..SPLASH_STEPS
    int artReady;
    char line[160];
    XrCompositionLayerQuad quad;   // the layer, alive until xrEndFrame
} XrSplashState;

// One step of the state. streaming: a stream is the picture's source;
// launcherUp: the launcher's list is in the picture's place; pictureShown: a
// frame of video is on screen. Returns the level (0 nothing, 1 full, between
// while it fades out). Does not touch the art.
float xrSplashStep(XrSplashState* s, int streaming, int launcherUp, int pictureShown, int64_t nowNs);

// The wording for the state ("Connecting to <PC>…" or "Reconnecting…"); host
// may be NULL or empty
void xrSplashLine(int reconnect, const char* host, char* out, int cap);

// The art, drawn into a canvas the size of the splash slot (ATLAS_SPLASH_W x
// ATLAS_SPLASH_H): the logo, and the line on a dark pill under it, at
// alpha (0..255) of full. Called by the panel worker.
void xrSplashDraw(UiCanvas* cv, const char* line, int alpha);

// Once per frame after the layers are gathered (xrBuildLayers): steps the
// state from the context, asks for the art when the words or the fade step
// changed, and appends the splash quad to out while it shows. Frame loop
// only. Returns 1 when a layer was appended.
int xrSplashLayers(struct XrCtx* ctx, struct XrFrameOut* out, int64_t nowNs);

// 1 while the splash is up or fading (the tests, the log)
int xrSplashShowing(const struct XrCtx* ctx);
