// Moonlight XR Native: what the native host adds to the ported renderer
// modules: the settings that only exist here, the rows that come in later
// milestones, the launcher list, the UI atlas and the panel art. GPLv3.
#ifndef XR_NATIVE_H
#define XR_NATIVE_H

#include "xr_renderer.h"
#include "settings.h"
#include "panels.h"

// Native setting ids past upstream's SETTING_ ones (xr_shared.h), handed back
// in IN_SETTING the same way
#define SETTING_POINTER_DOT 24
// Which notices the toast says, NOTICES_ALL 0, IMPORTANT 1 or OFF 2
#define SETTING_NOTICES 25
// The cog's tab, COG_TAB_*: the next session opens the cog on it
#define SETTING_COG_TAB 26
// Native (lane wave2-bind-wire): how still the cursor is held around a click,
// CLICK_STEADY_OFF 0, NORMAL 1 or STEADY 2
#define SETTING_CLICK_STEADY 27
// Not upstream's: the knob's name for xr_display.c's log lines
#define PROP_REFRESH "the refresh knob"

// The curve the panel or the preference asks for (xr_ui.c): what is stored,
// whether or not the launcher's flat list has the picture's place now
float xrCurveSetting(XrCtx* ctx);

// ---- rows the native client cannot act on yet (xr_prefs.c) ----------------
// 0 when the row is live as far as the native client goes, else the reason's
// UiStr. None on the Display tab since M5. The 3D tab is dead through
// stereoMode (M6).
int xrRowLater(XrCtx* ctx, int face, int row);
// The Display tab's rows xrRowLater kills, as UiView.display_later bits
unsigned xrDisplayLaterMask(XrCtx* ctx);
// The picker's cell for a stored environment id (ENV_ID_*), and the
// environment key's value for a cell (1 passthrough, 2 void, 3..5 the rooms)
int xrCellForEnvironment(int env);
int xrEnvironmentSetting(int cell);

// ---- settings <-> context (xr_prefs.c) ---------------------------------------
// The context's state as config.ini holds it (the Settings the panels draw
// from). Only fields the context owns are written; the rest are left.
void xrSettingsFromCtx(XrCtx* ctx, Settings* s);
// The context's state from config.ini, before the first frame: everything the
// JNI setters and nativeInit's arguments carried upstream
void xrApplySettings(XrCtx* ctx, const Settings* s);
// A setting the input pass handed back (IN_SETTING, IN_SETTING_VALUE) written
// into s, which the caller saves. Returns 1 if s changed.
int xrStoreSetting(XrCtx* ctx, Settings* s, int setting, int value);
// The screen placement (IN_POSE) into s's [screen] keys and back
void xrPoseToSettings(const float* pose, float curve, Settings* s);
void xrPoseFromSettings(const Settings* s, float* pose);
// The distance and width the Screen tab's reset goes back to (upstream: the
// 2D settings' sliders; native: config.ini's defaults)
void xrDefaultPlacement(float* distance, float* width, float* curvature);

// ---- the launcher list (xr_panels.c) ------------------------------------------
// A point on the picture (u, v from its top left) on the list standing in its
// place: the list's own uv, 0 when off the list
int launcherListUv(XrCtx* ctx, float u, float v, float* lu, float* lv);
// The list's hit under a point of the list (its last drawing's hits), or -1
int xrPanelsLauncherHit(XrCtx* ctx, float lu, float lv);

// ---- the UI atlas (xr_assets.c) ---------------------------------------------------
// Slots past lane U's panels (UiPanelId), in free corners of the same atlas
enum {
    ATLAS_BEAM = UI_PANEL_COUNT,
    ATLAS_DOT,
    ATLAS_OUTLINE,
    ATLAS_CORNER,
    ATLAS_HANDLE,
    ATLAS_OVERLAY,
    ATLAS_BEAM_FAINT,
    ATLAS_SLOTS
};
// Native: 416 x 400, the whole free corner under the clock strip (x 1632..2048,
// y 1840..2240), so the longest stats line wraps rather than being cut off
#define ATLAS_OVERLAY_W 416
#define ATLAS_OVERLAY_H 400
// Native: the resting beam shown over the bar with the ray off, a short strip
// of the beam's art at a third of its strength, stretched along the beam
// (x 1280..1344, y 2496..2536, under the beam's own slot)
#define ATLAS_BEAM_FAINT_H 40
#define BEAM_FAINT_LEVEL 0.30f
UiRectPx xrAtlasRect(int slot);
// The CPU copy the panels draw into (RGBA8 premultiplied, UI_ATLAS_W wide)
uint8_t* xrAtlasPixels(XrCtx* ctx);
// A slot was redrawn: its pixels are copied into the staging buffer and go up
// into each swapchain image as that is next used. Frame loop only, after the
// last frame's fence (the staging buffer is the copies' source)
void xrAtlasTouch(XrCtx* ctx, int slot);
XrSwapchain xrAtlasSwapchain(XrCtx* ctx);
// Created with the session (format R8G8B8A8_SRGB, upstream's SRGB8_ALPHA8:
// the panels' bytes go in as they are and the compositor decodes them)
int xrAtlasCreate(XrCtx* ctx);
void xrAtlasDestroy(XrCtx* ctx);
// Records the copies of every slot changed since the image about to be shown
// last had them, in a command buffer the caller submits before the release.
// Acquires, waits and fills *acquired; returns 0 when nothing is due (no
// acquire), 1 when the caller must submit and then release, -1 on failure.
int xrAtlasRecord(XrCtx* ctx, VkCommandBuffer cmd, int* acquired);
void xrAtlasRelease(XrCtx* ctx);
// The art drawn in code (xr_assets.c buildHandleArt and uploadPointerArt):
// the beam, the dot, the hover ring, the corner bracket and the move pill
void xrDrawCodeArt(uint8_t* atlas);
// Headless use (the snapshot tool, tests): an atlas in plain memory
int xrAtlasCreateCpu(XrCtx* ctx);

// ---- the panel art (xr_panels.c) ---------------------------------------------------
typedef struct XrLauncherView {
    int mode;                  // LAUNCHER_PCS / LAUNCHER_APPS
    const UiPcItem* pcs;
    int pcCount;
    const char* pcName;
    const UiAppItem* apps;
    int appCount;
    int page;
    int loading, failed;
} XrLauncherView;
// Builds what each panel shows from the context and the live settings, and
// redraws into the atlas the ones whose inputs changed since their last drawing
// (native: in worker mode, lands the worker's finished drawings and hands it
// the new ones; frame loop only, after the frame's fence)
void xrPanelsUpdate(XrCtx* ctx, const Settings* live, const XrLauncherView* launcher,
                    const char* statsText, const char* clockText, int64_t now);
int xrPanelsCreate(XrCtx* ctx);
void xrPanelsDestroy(XrCtx* ctx);
// The hit regions of the launcher list's last drawing
const UiHits* xrPanelsLauncherHits(XrCtx* ctx);
// The view the cog panel was last drawn from (tests)
const UiView* xrPanelsLastView(XrCtx* ctx);

// Panel drawings since the last call and their average cost (the stats line)
void xrPanelsStats(XrCtx* ctx, int* draws, double* ms);
// Native: the longest wait, since the last call, from a panel's change to its
// drawing landing in the atlas (worker mode; 0 when drawn inline)
double xrPanelsWorstWaitMs(XrCtx* ctx);
// Native: draw the panels on a worker thread from now on (swapchain atlas:
// the session). Without it every drawing happens inside xrPanelsUpdate.
int xrPanelsSetAsync(XrCtx* ctx);
// Native: wait for the worker's drawings and land them (tests)
void xrPanelsFlush(XrCtx* ctx);
// The report form, the toast's words and the app's own notices
void xrPanelsReportKey(XrCtx* ctx, int code);
void xrPanelsReportEvent(XrCtx* ctx, int event, const char* reportDir);
void xrPanelsToast(XrCtx* ctx, int kind, int arg);
void xrPanelsNotice(XrCtx* ctx, const char* text);

// ---- the layers (xr_layers.c) ---------------------------------------------------------
// Everything a frame carries, assembled in draw order (upstream's
// nativeEndFrame without the video draw, which the host does first)
typedef struct XrFrameIn {
    float distance, quadWidth, curvature;   // the preferences' placement
    int headLocked;
    int eyeSwap;
    int passthrough;
    // The curved picture's projection layer, when the host drew one
    const XrCompositionLayerProjection* curved;
    // M5: the supersampled picture's swapchain and size, standing in for the
    // picture's own, when the host drew one
    XrSwapchain picture;
    int pictureW, pictureH;
} XrFrameIn;
#define XR_FRAME_MAX_LAYERS 24
typedef struct XrFrameOut {
    const XrCompositionLayerBaseHeader* order[XR_FRAME_MAX_LAYERS];
    uint32_t count;
    XrEnvironmentBlendMode blend;
} XrFrameOut;
// Fills out with the frame's layers; the storage lives in ctx until the next
// call. Steps the fades, the placement and the toast as upstream did.
void xrBuildLayers(XrCtx* ctx, const XrFrameIn* in, XrFrameOut* out);
// The pose and size the picture goes up at this frame (after updatePlacement)
void xrPictureView(XrCtx* ctx, XrPosef* pose, float* width, float* height, XrSpace* space);


// The curved picture as mlxr_pass_panel draws it (xr_layers.c): its size and
// bend (radius 0 when flat), and one eye's mvp (GL conventions, model = the
// picture's pose)
void xrCurveShape(XrCtx* ctx, float* width, float* height, float* radius);
// Whether the picture is curved (outside a room) this frame
int xrPictureCurved(XrCtx* ctx);
// Whether the picture goes up through the app's own projection layer (the
// curved picture's path): curved, or in 3D flat too, since SteamVR shows no
// quad layer seen by one eye only (XR_EYE_VISIBILITY_LEFT/RIGHT)
int xrPictureProjected(XrCtx* ctx);
void xrCurveMvp(XrFovf fov, XrPosef eye, XrPosef surface, float* mvp);
// Native (lane eye-swapchains): one view of a projection layer showing the
// whole eyeW x eyeH image of that eye's own swapchain
void xrProjectionEye(XrCompositionLayerProjectionView* out, const XrView* view, XrSwapchain chain, int eyeW,
                     int eyeH);
// Native (lane eye-swapchains): 1 when the layer's two views name two
// different swapchains and neither eye's rect reaches past SteamVR's 3458
// (STEAMVR_TEXTURE_MAX), which it would downsample every frame
int xrProjectionEyesOwn(const XrCompositionLayerProjection* layer);

// ---- for the tests (xr_input.c) ----------------------------------------------
// What one ray in the local space lands on through the frame's hover pass
// (HOVER_*), and where on the plane that claimed it
int xrHoverAt(XrCtx* ctx, XrPosef aim, float* outU, float* outV, int* outCorner);
// One frame of the right hand's grab: trigger and grip 0 or 1, the thumbstick's
// tilt north positive; returns what the ray was over
int xrGrabStepAt(XrCtx* ctx, XrPosef aim, int trigger, int grip, float stickY);
// Native (lane grab-pull): the same with the whole thumbstick, x east positive
int xrGrabStepStick(XrCtx* ctx, XrPosef aim, int trigger, int grip, XrVector2f stick);
// Native (lane ray-glitch): the pointer's part of one input frame at now
// (nanoseconds): the head and the sources read through the seams on the
// context (locateSpace, poseActive, readFloat, readBool, readVec2 and
// pfnLocateHandJoints), the controllers' clocks, the pick of the source that
// points and of the hand whose ray is drawn. What it made of them is left in
// ctx->rayRow, as the recorder writes it (tests/m4/test_replay.c).
void xrPointerStep(XrCtx* ctx, int64_t now, int headLocked);

// ---- Native (lane wave2-bind-wire): bindings.ini (xr_input.c) -------------
// Reads bindings.ini (path NULL: xrBindDefaultPath, which is written with the
// defaults the first time), logs its errors as warnings and what it changes
// in one line, says "bindings.ini: N errors, see log" on the toast when it
// has any, and makes the tables. Returns the number of errors.
int xrBindLoad(XrCtx* ctx, const char* path);
// The host app the stream shows (its name as the app list has it, "" none),
// whose [app "Name"] lines go over the modes'
void xrBindSetApp(XrCtx* ctx, const char* app);
// For the tests: the bindings' part of a frame as xrUpdateInput runs it, off
// ctx->padSeen, the triggers (triggerDown, triggerSwallowed) and the mode,
// with the ray on the picture. In gamepad mode *pad comes in as padMapGrips
// made it and goes out as the bindings leave it.
void xrBindFrame(XrCtx* ctx, int64_t now, float dt, PadState* pad, float* out);

// ---- the context's defaults (xr_prefs.c) -----------------------------------
// What upstream's nativeInit set before any setter ran
void xrCtxDefaults(XrCtx* ctx, int videoWidth, int videoHeight, int fps);

#endif
