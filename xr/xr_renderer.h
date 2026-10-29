// Moonlight XR Native: the renderer's context, ported from Moonlight XR's
// xr-renderer/xr_renderer.h (Gilleece, GPLv3; Steam Frame fork fields kept).
//
// Upstream's XrCtx is one struct every renderer module reads. This is the same
// struct with everything GL, EGL, JNI and Android taken out, the Vulkan side
// and the native glue added, and the fields of modules that come later (the
// rooms in M5, the depth model and 3D warp in M6, the controller models) kept
// only where the ported UI and input code reads them. Field names, comments
// and the constants below are upstream's; differences are marked "native".
//
// Who uses it: xr_ui.c, xr_input.c, xr_layers.c, xr_display.c, xr_room.c,
// xr_model.c, xr_ambilight.c (ports), xr_assets.c (the UI atlas), xr_panels.c
// (lane U's rasterisers), xr_world.c (the GPU side the room, the glow and the
// models share), xr_session.c (the host).
#ifndef XR_RENDERER_H
#define XR_RENDERER_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <stdatomic.h>

#include <vulkan/vulkan.h>
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "xr_math.h"
#include "xr_shared.h"
#include "xr_depthmap.h"
#include "xr_roommesh.h"
#include "xr_layout.h"
#include "xr_rate.h"
#include "xr_gate.h"
#include "xr_pinch.h"
#include "xr_notice.h"
#include "xr_glow.h"
#include "xr_grade.h"
#include "xr_keys.h"
#include "xr_kbfull.h"
#include "xr_controller.h"
#include "xr_headaim.h"
#include "xr_clickhold.h"
#include "xr_pressfix.h"
#include "xr_gamepad.h"
#include "room_logic.h"

#define TAG "moonlight-xr"

// Native: Android's log priorities, which xr_log.c still sorts lines by
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
// Not an Android priority, only ever seen inside xrLog: a key event, which
// logcat gets as info while the file keeps it at the quiet level
#define PRIO_EVENT 90

void xrLog(int prio, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

#define LOGI(...) xrLog(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGW(...) xrLog(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOGE(...) xrLog(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGEV(...) xrLog(PRIO_EVENT, __VA_ARGS__)

// 64 bit on every ABI: a long is 32 on armeabi-v7a and would wrap in seconds
static inline int64_t nowNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

#define STATS_LOG_INTERVAL_FRAMES 300

// Beam and cursor art share one small swapchain, beam on top, dot below
// (native: two slots of the UI atlas, see xr_assets.c)
#define PTR_TEX_W 64
#define PTR_BEAM_H 256
#define PTR_DOT_H 64
#define PTR_TEX_H (PTR_BEAM_H + PTR_DOT_H)

#define HAND_LEFT  0
#define HAND_RIGHT 1
#define HAND_COUNT 2
// Some headsets aim by looking rather than by pointing. Gaze is a third source
// of a ray, so the pointing code counts it alongside the two hands and
// everything downstream stays the same. Only the hands carry buttons.
#define SRC_GAZE  HAND_COUNT
#define SRC_COUNT (HAND_COUNT + 1)

// Trigger and grip are analog, and a single threshold chatters around the
// crossing, so presses and releases use different ones
#define PRESS_ON   0.65f
#define PRESS_OFF  0.35f
// Thumbstick travel before it counts as a scroll, and how fast a full
// deflection winds the wheel
#define SCROLL_DEADZONE 0.30f
// Native: while the move bar is held, a thumbstick pushed north sends the
// screen away and south brings it closer, at up to this many metres a second
// at full tilt, and never nearer than or further than the Screen tab allows
#define GRAB_PUSH_DEADZONE 0.20f
// Native (lane grab-pull): the tilt that already counts as full tilt. A stick
// pulled back toward the palm seldom reaches the rim, so pulling ran slower
// than pushing; the rest of the way to the rim is full speed too.
#define GRAB_PUSH_FULL 0.85f
// Native (lane grab-pull): how far off north or south (cosine of the angle,
// 60 degrees) a tilt still pushes or pulls; east and west do nothing
#define GRAB_PUSH_SECTOR_COS 0.5f
#define GRAB_PUSH_SPEED 1.5f
#define GRAB_PUSH_NEAR_M 0.3f
#define GRAB_PUSH_FAR_M 8.0f
#define SCROLL_CLICKS_PER_SEC 7.5f

// One euro filter on the hit point. A hand at rest still shakes, and at 3 m
// that tremor is several pixels of cursor, so the cutoff drops when the
// pointer is still and rises with speed to keep fast moves from lagging.
#define POINTER_MIN_CUTOFF 1.2f
#define POINTER_BETA 8.0f
// Native: M2 shipped 1 Hz and 0.01 per pixel a second of stream width and the
// owner accepted it ("the One Euro filter fixes the jitter"); the beta is in
// upstream's units as NATIVE_POINTER_BETA_PER_PX * videoWidth. Lane av-latency
// (owner: "some delay with the cursor") raised the beta to 0.015: the minimum
// cutoff, which is what holds a resting hand still, is unchanged; measured in a
// simulation at 90 Hz with a 2 px hand tremor, the lag behind a moving hand
// drops from 45/23/15/6.5 ms to 37/18/12/4.9 ms at 50/200/400/1500 px/s, and
// the wobble while aiming slowly rises from 0.65 to 0.75 px RMS.
// MLXR_POINTER_MIN_CUTOFF and MLXR_POINTER_BETA (per pixel) override both for
// trying values without a rebuild.
#define NATIVE_POINTER_MIN_CUTOFF 1.0f
#define NATIVE_POINTER_BETA_PER_PX 0.015f
static inline float nativePointerTunable(const char* env, float def) {
    const char* s = getenv(env);
    if (s == NULL || *s == '\0') {
        return def;
    }
    float v = strtof(s, NULL);
    return v > 0.0f && v < 1000.0f ? v : def;
}
// A gap this long means the pointer left the screen or changed hands, and
// filtering across it would slide the cursor in from where it used to be
#define POINTER_RESET_NS 250000000L

// The aim pose gets its own filter so the hover zones, grabs and beam
// origin settle along with the cursor. Position and rotation share the
// tunables since their derivatives are the same order of magnitude.
#define AIM_MIN_CUTOFF 1.0f
#define AIM_BETA 20.0f
// Native: M2 had no aim filter, and filtering the aim as well as the hit point
// adds lag the owner did not have; a cutoff this high passes the pose through
#define NATIVE_AIM_MIN_CUTOFF 1000.0f

// The pointer waits for deliberate movement before it appears, so knocking a
// controller does not throw a laser across the picture, and it goes away again
// once a controller has been put down
#define POINTER_WAKE_SEC 0.5f
#define POINTER_SLEEP_SEC 5.0f
// Metres per second and radians per second. A resting hand manages about a
// tenth of these.
#define POINTER_MOVE_SPEED 0.06f
#define POINTER_TURN_SPEED 0.35f

#define VR_BUTTON_LEFT   0x1
#define VR_BUTTON_RIGHT  0x2
#define VR_BUTTON_MIDDLE 0x4

// Grab thresholds for the grip, and the range a resize is allowed to reach
#define SCREEN_MIN_WIDTH 0.8f
#define SCREEN_MAX_WIDTH 8.0f

#define GRAB_NONE   0
#define GRAB_MOVE   1
#define GRAB_RESIZE 2
// Native (owner's request): the keyboard held by its move bar, in place and
// turn, wherever it is taken to
#define GRAB_KBMOVE 3
// Native (owner's request): the stats window held by a grip, in place and turn
#define GRAB_STATSMOVE 4

// Widened along with the grid so a cell stays about the size it was at three
// columns: five of them now
#define PICKER_WIDTH_FRAC 0.91f
#define OUTLINE_TEX 128

#define COG_WIDTH_FRAC 0.36f
#define COG_THUMB_TEX 64
// Which rows the panel is showing: one of the tabs, or the Room tab, which is
// what the first tab is while a room is up. Numbered past the tabs, since it
// is not a tab of its own and nothing else can reach it.
#ifndef COG_FACE_ROOM
#define COG_FACE_ROOM COG_TAB_COUNT
#endif

// Metres. Deliberately well under the settings slider's 1 m floor, so the
// screen can be brought right up to the face.
#define COG_DIST_MIN 0.2f
#define COG_DIST_MAX 8.0f
// Metres either side of the reference space's eye level
#define COG_HEIGHT_MIN -2.0f
#define COG_HEIGHT_MAX 2.0f
// Radians, 40 degrees each way
#define COG_TILT_MAX 0.6981f
#define COG_TILT_SNAP 0.0388f
// Radians, 90 degrees each way, so the picture can go fully on its side for
// watching while lying down
#define COG_ROLL_MAX 1.5708f
#define COG_ROLL_SNAP 0.0873f

#define KB_WIDTH_FRAC 0.55f
#define KB_MAX_KEYS 96
// Native: the Compact keyboard's layout chip is hit-tested as this key index, past its 44
#define KB_KEY_LAYOUT (KB_MAX_KEYS - 1)

#define EXIT_WIDTH_FRAC 0.30f

// The report sheet, centred over the bottom of the picture with the keyboard
// under it
#define REPORT_WIDTH_FRAC 0.40f

// The hand lock hint, over the middle of the picture, as wide as the report
// sheet. It waits for a hand to have pointed this long.
#define HINT_WIDTH_FRAC 0.40f
#define HINT_POINTING_NS 500000000L

// The Ko-fi sheet, over the middle of the picture like the hint and as wide
#define KOFI_WIDTH_FRAC 0.40f

// The panels that fade in and out, each with a fade of its own
#define FADE_COG 0
#define FADE_PICKER 1
#define FADE_KB 2
#define FADE_EXIT 3
#define FADE_REPORT 4
#define FADE_HINT 5
#define FADE_KOFI 6
#define FADE_PANELS 7
#define FADE_SLOT_SPLASH FADE_PANELS
#define FADE_SLOT_TOAST (FADE_PANELS + 1)
#define FADE_SLOTS (FADE_PANELS + 2)

// The toast hangs off the eyes, ahead and a little below where they look,
// in metres
#define TOAST_W_M 0.56f
#define TOAST_DISTANCE_M 0.80f
// Native: 0.22 m, about 15 degrees below the gaze (was 0.14), so a notice sits
// off the middle of the view instead of across it
#define TOAST_DROP_M 0.22f

// How far the ray runs when it is aimed at nothing at all, in metres
#define FREE_BEAM_M 4.0f

// How long head aim drops frames after the local space is recentred
#define HEAD_AIM_RECENTRE_HOLD_NS 100000000LL
#define HEAD_AIM_COUNT_NS 10000000000L

// The 3d room styles (the same in rooms/room_logic.h)
#define ROOM_STYLE_THEATER 1
#define ROOM_STYLE_GRAND_CINEMA 2
#define ROOM_STYLE_SYNTHWAVE 3
#define ROOM_STYLE_FIRST ROOM_STYLE_THEATER
#define ROOM_STYLE_LAST ROOM_STYLE_SYNTHWAVE
#define ROOM_EYES 2

#define STEREO_WAIT_NS 400000000L

// Three depth textures rather than two: the frame loop advances through them
// in a fixed rotation, so a slot about to be overwritten was last sampled a
// full rotation ago
#define DEPTH_TEX_COUNT 3

// Generous on purpose: the stage thread has no per frame deadline, and a
// short timeout here would only trade a hung capture for a torn one
#define CAPTURE_FENCE_TIMEOUT_NS 500000000ull

// Pixels of the frame the shift fades to nothing over at each side edge
#define EDGE_FADE_PX 8
#define EDGE_FADE_MAX_PX 64

// Pinned headers may predate the extension, values from the OpenXR registry
#ifndef XR_FB_composition_layer_settings
#define XR_FB_composition_layer_settings 1
#define XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME "XR_FB_composition_layer_settings"
#endif

// What the session asks the runtime's performance levels for
#define PERF_LEVEL_NONE 0
#define PERF_LEVEL_SUSTAINED_HIGH 1
#define PERF_LEVEL_BOOST 2

#define RATE_REASK_MS 10000
#define RATE_REASK_MAX 3
#define RATE_CONFIRM_MS 5000
#define RATE_LOG_WINDOWS 5

#define SEAM_INSET_TEXELS 1

// Native: the launcher's lists (PC_LIST / APP_LIST) hang where the picture
// would, as the picture's stand in, while no stream is up
#define LAUNCHER_NONE 0
#define LAUNCHER_PCS 1
#define LAUNCHER_APPS 2

// Native: an image the app owns (the ambilight's samples, the glow, the
// shared depth), with the layout it was last left in
typedef struct XrVkImage {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
    VkImageLayout layout;
    int width, height;
} XrVkImage;

// Letterbox and pillarbox detection (upstream xr_renderer.h)
#define AMBI_BAR_PERIOD 30
#define AMBI_BAR_LUMA 0.09f
#define AMBI_BAR_MAX 10
#define AMBI_BAR_APPLY 4
#define AMBI_BAR_RELEASE 2
#define AMBI_EDGE_LEFT 0
#define AMBI_EDGE_RIGHT 1
#define AMBI_EDGE_BOTTOM 2
#define AMBI_EDGE_TOP 3
#define AMBI_EDGES 4

struct XrUiAtlas;   // xr_assets.c
struct XrPanels;    // xr_panels.c
struct XrCurve;     // xr_session.c: the curved picture's projection layer

typedef struct XrCtx {
    XrInstance instance;
    // The runtime's name and version as the log line gives them
    char runtimeLabel[XR_MAX_RUNTIME_NAME_SIZE + 32];
    XrSystemId systemId;
    XrSession session;
    XrSpace localSpace;
    XrSpace viewSpace;

    // Native: the Vulkan device the session runs on (xr_vk.h's XrVk)
    struct XrVk* vk;

    // The picture's swapchain, which the video path draws every new frame into
    XrSwapchain swapchain;
    int64_t swapchainFormat;
    int swapchainsAlive;

    // Stats overlay, a strip pinned to the picture's top left (native: a slot
    // of the UI atlas, drawn from the session's stats line)
    int overlayHasContent;
    int overlayVisible;
    // Native (owner's request): the stats window is movable. overlayW/H are the
    // pixels of its slot the drawing used (xr_panels.c lays the lines out to fit);
    // statsPose and statsW/H are where it is and how big, metres (statsPlace).
    // Until a grip has moved it, it sits in the picture's top left corner; after,
    // it stays where it was left (its top edge fixed as the lines change in
    // number), until the screen's placement is reset or recentred.
    int overlayW, overlayH;
    XrPosef statsPose;
    float statsW, statsH;
    int statsMoved;
    XrPosef statsMovedPose;
    float statsMovedH;
    int statsOver;   // the pointing ray is on the window (grip takes it)

    // 0 off, 1 normal, 2 quality: sharpening (native: the warp pass's own
    // sharpen, SteamVR has no layer settings) and supersampling (stored only)
    int sharpenMode;
    int supersampleMode;
    int layerFlagsLogged;

    int videoWidth;
    int videoHeight;

    // Stereo test path. When stereoMode is not OFF the swapchain is double
    // wide and each eye gets its own warped copy of the frame (native M6:
    // DEPTH_MODE_MODEL with a depth model in assets/depth and the depth
    // library built in, or one of upstream's test patterns; OFF greys the 3D
    // tab and leaves the bar's 3D button out)
    int stereoMode;
    int depthDebug;
    // The 3D switch on the bar and the 3D tab. Native: remembered in
    // config.ini ([3d] stereo) rather than starting on in every session
    int stereoLive;
    // Just switched back on with a model running: still drawn flat until a
    // map made since then lands, or STEREO_WAIT_NS has gone by
    int stereoWaiting;
    int stereoWaitIndex;
    int64_t stereoWaitNs;
    // Draws the frame again with nothing new from the decoder, so the switch
    // shows at once on a picture that is standing still
    int warpRedraw;
    // How many eyes the last draw left in the swapchain, which the video
    // layers follow: both halves, or the left one to both eyes
    int drawnEyes;
    // The depth map's size for this session, which is the model input's
    int depthTexW;
    int depthTexH;
    // Native: counts the maps uploaded into the depth textures (upstream's
    // staged slot index; a change of it is what ends stereoWaiting)
    atomic_int depthStagedIndex;
    int barTestFramesLogged;
    // Edge aware upsample of the depth map, quarter of the video size, and
    // the occlusion aware offset map (native: xr_depth.c's images)
    float depthSharp;
    int upsampleWidth;
    int upsampleHeight;
    int upsampleEnabled;
    float upsampleSigmaR;
    int occlusionEnabled;
    // The edge fixes, on apart from the cubic read. Pixels for the fade, 0 or
    // 1 for the rest.
    int srcInsetOn;
    int edgeFadePx;
    int depthCubic;
    int seamInset;
    float convergence;
    float separationOverride;
    // Native: the separation the preference holds (config.ini [3d]
    // separation, resolved against the model), which the panel's own
    // panelSeparation overrides while it is >= 0
    float separationPref;
    // Native: the depth model this session runs (depth/litert_backend.h
    // MlxrDepthModelId), and why the 3D is off when it cannot be had
    int depthModel;
    int depthFailed;
    char depthReason[96];
    // Native: the depth model's thread, its staging and the 3D passes
    // (xr_depth.c)
    struct XrDepth* depth;

    // Ambilight (xr_ambilight.c). The frame is sampled down to a tiny colour
    // texture, which the glow behind the screen is then drawn from (native:
    // Vulkan images and mlxr_pass_ambi / _glow_edge / _glow in place of the GL
    // programs, see XrVkImage)
    int ambiReady;
    int ambiFailed;
    XrVkImage ambiTexture;
    // Whether there is anything in that texture yet, since the first frame has
    // nothing to smooth against
    int ambiSeeded;
    float ambiSmooth;
    // Letterbox detection: the detector's own target and its readback
    // (native: a host visible buffer in place of the pixel buffer)
    XrVkImage ambiDetectTexture;
    VkBuffer ambiDetectBuffer;
    VkDeviceMemory ambiDetectMemory;
    void* ambiDetectMap;
    int ambiDetectPending;
    int ambiBarDetect;
    int ambiBarCounter;
    int ambiBarApplied[AMBI_EDGES];
    int ambiBarGrowTicks[AMBI_EDGES];
    int ambiBarGrowMin[AMBI_EDGES];
    int ambiBarShrinkTicks[AMBI_EDGES];
    int ambiBarShrinkMax[AMBI_EDGES];
    // x0, y0, w, h in the sample's uv space, 0 0 1 1 for the whole frame
    float ambiCrop[4];
    XrVkImage glowEdgeTexture;
    int glowNorm;
    // Native: the glow is drawn into an image of the app's own (it is sampled
    // again by the curved picture's pass, and a swapchain image cannot be once
    // released); the flat glow's swapchain gets a copy of it
    XrVkImage glowTexture;
    XrSwapchain glowSwapchain;
    uint32_t glowImageCount;
    XrSwapchainImageVulkan2KHR* glowImages;
    int glowFailed;
    int glowCopied;     // the swapchain holds the newest glow
    int glowSwapchainFilled; // and has had one released at all
    int glowAcquired;   // an image is acquired this frame (released after the submit)
    int glowRendered;
    int glowDrawnOn;
    float glowDrawnLevel;
    int ambilightOn;
    float ambiIntensity;
    int ambiOverride;

    // The picture grade, in the preferences' whole units in PICTURE_ order,
    // and the sums they come to (native: handed to mlxr_pass_warp)
    int pictureUnits[PICTURE_VALUES];
    int gradeOn;
    PictureGrade grade;

    // Which room the picker is on: 0 none, else one of the ROOM_STYLE_ values,
    // with a debug override (native: MLXR_ROOM in the environment)
    int roomStyle;
    int roomOverride;
    // What the scale and brightness properties asked for, or -1 while the
    // params the style ships with own them (native: MLXR_ROOM_SCALE/_DIM)
    float roomScaleOverride;
    float roomDimOverride;
    int roomLightOn;
    int roomBrightness[ROOM_STYLE_LAST + 1];
    int roomGlow[ROOM_STYLE_LAST + 1];
    int roomLightLevel[ROOM_STYLE_LAST + 1];
    int roomScreen[ROOM_STYLE_LAST + 1];
    // Everything the room is drawn with (xr_room.c). Native: the resident
    // room is rooms/room_gpu's MlxrRoom, loaded off the frame loop
    // (struct XrRoomLoader), in place of the GL buffers and textures.
    // Native (lane eye-swapchains): a swapchain per eye, each roomEyeWidth x
    // roomEyeHeight (SteamVR downsamples a texture past 3458 px; both eyes
    // side by side went past it)
    XrSwapchain roomSwapchain[ROOM_EYES];
    uint32_t roomImageCount[ROOM_EYES];
    XrSwapchainImageVulkan2KHR* roomImages[ROOM_EYES];
    VkImageView* roomImageViews[ROOM_EYES];
    struct MlxrRoom* room;
    struct XrRoomLoader* roomLoader;
    float roomTexMix;
    float roomDim;
    int roomBuiltStyle;
    int roomWantedStyle;
    int roomAssetsSeen;
    float roomWantedScale;
    int roomEyeWidth;
    int roomEyeHeight;
    int roomReady;
    int roomFailed;
    int roomRendered;
    int roomAcquired;           // native: a bit per eye's swapchain
    float roomSpillGain;
    float roomFarZ;
    float roomClear[3];
    XrView roomViews[ROOM_EYES];
    int roomViewsValid;
    // Native: one projection layer a frame (xr_world.c xrWorldPlanLayers).
    // SteamVR on the Steam Frame shows only one projection layer correctly:
    // with the room's and the 3D picture's both up the room jittered and lagged
    // the head, and withholding the picture's layer fixed it (owner's A/B,
    // 2026-10-10). So whatever would be a second one is drawn into the first:
    // the room into the picture's layer, the models into the picture's or the
    // room's, and the picture's layer is passthrough's transparent one.
    int roomInPicture;
    int modelsInPicture;
    int modelsInRoom;
    int passthroughInPicture;
    int roomPlacedStyle;
    float roomPlacedWidth;
    int maxEyeWidth;
    int maxEyeHeight;
    int envResTier;
    // Native: one depth image the room's pass and the models' share (both
    // clear it, one after the other in the frame's command buffer)
    XrVkImage worldDepth;
    // A transparent projection layer under everything while passthrough is on:
    // SteamVR on the Steam Frame leaves the background opaque without one
    XrSwapchain passthroughSwapchain;
    int passthroughFailed;
    int roomHoldingScreen;
    XrPosef savedScreenPose;
    float savedScreenWidth;
    float savedScreenRadius;
    int recentredInRoom;
    int recommendedEyeWidth;
    int recommendedEyeHeight;

    XrSessionState sessionState;
    int sessionRunning;
    int exitRequested;
    char exitReason[96];
    int everFocused;
    int64_t waitingSinceNs;
    int64_t waitingLoggedNs;
    int sessionBegins;
    int sessionEnds;
    XrTime predictedDisplayTime;
    int shouldRender;
    int everRendered;
    int focusedFrames;
    int passthroughBlendSaid;

    // Native: no cylinder layers on SteamVR; set when the curved picture is
    // drawn through mlxr_pass_panel into a projection layer instead
    int cylinderSupported;
    float dotSizeSaid;
    int cylinderClampSaid;
    float barSaidWidth;
    Vec3 barSaidEdge;
    int layerSettingsSupported;
    int colorScaleSupported;
    int64_t fadeNs;
    Fade panelFades[FADE_PANELS];
    int panelFadingOut;

    // The launch splash (native: not drawn, starts gone)
    Splash splash;

    // The toast
    NoticeBoard notices;
    int toastArtReady;
    int toastDrawnKind;
    int toastDrawnArg;
    Fade toastFade;

    // The display refresh rate (xr_display.c)
    int refreshRateSupported;
    PFN_xrEnumerateDisplayRefreshRatesFB pfnEnumerateDisplayRefreshRates;
    PFN_xrGetDisplayRefreshRateFB pfnGetDisplayRefreshRate;
    PFN_xrRequestDisplayRefreshRateFB pfnRequestDisplayRefreshRate;
    float displayRates[RATE_MAX];
    int displayRateCount;
    int streamFps;
    float rateAsked;
    float displayRate;
    int64_t rateAskedNs;
    int rateConfirmed;
    int rateSettled;
    float warpRateHeld;
    int rateWarpOn;
    float rateFloorSaid;
    int64_t rateMovedNs;
    int rateReasks;
    int refreshKnob;
    RateBudget rateBudget;
    int rateLogWindows;
    float rateLogFrameMs;
    float rateLogWorstMs;
    long rateLogMissed;
    int64_t frameBeganNs;
    XrTime lastDisplayTime;
    int64_t displayPeriodNs;

    DepthGovernor depthGov;
    DepthGate depthGate;
    int depthRateSetting;
    int depthRateKnob;
    atomic_int depthGaveUp;
    int perfNoticeLevels[2][3];

    int perfSettingsSupported;
    PFN_xrPerfSettingsSetPerformanceLevelEXT pfnPerfSettingsSetPerformanceLevel;
    int perfLevel;

    int virtualKeyboardSupported;
    int maxLayerCount;
    int layerLimitWarned;
    int layerDropWarned;

    // Passthrough is just an environment blend mode: with alpha blend the
    // runtime shows the room wherever our layers do not cover
    int alphaBlendSupported;
    int passthrough;
    // Hand tracking and gaze, as upstream (native: off unless the settings
    // turn them on, see ui/settings.c)
    int handsEnabled;
    int handInteraction;
    int msftHandInteraction;
    XrPath handProfile;
    XrPath msftHandProfile;
    int handTracking;
    int handClickOk;
    int extHandClick;
    int onExtHands[HAND_COUNT];
    int msftHandClick;
    int onMsftHands[HAND_COUNT];
    int eyeGaze;
    int gazeEnabled;
    XrAction gazeAction;
    int lastSnapshot;
    int jointTracking;
    XrHandTrackerEXT handTrackers[HAND_COUNT];
    int handAimOffered;
    int handAim;
    int aimPinch[HAND_COUNT];
    float tipGap[HAND_COUNT];
    int tipsValid[HAND_COUNT];
    int pinchDown[HAND_COUNT];
    int pinchSrc[HAND_COUNT];
    unsigned pinchSrcSaid;
    Vec3 pinchPoint[HAND_COUNT];
    int pinchPointValid[HAND_COUNT];
    XrPosef handRay[HAND_COUNT];
    int handRayValid[HAND_COUNT];
    PFN_xrCreateHandTrackerEXT pfnCreateHandTracker;
    PFN_xrDestroyHandTrackerEXT pfnDestroyHandTracker;
    PFN_xrLocateHandJointsEXT pfnLocateHandJoints;
    int usingHands[SRC_COUNT];
    int profileKind[HAND_COUNT];
    int aimTracked[HAND_COUNT];
    ControllerClock aimClock[HAND_COUNT];
    int controllerAwake;
    GazeBridge gazeBridge;
    int pinchSwallowed[SRC_COUNT];
    int handsLocked;
    TriplePinch triplePinch[HAND_COUNT];
    int hintWanted;
    int hintShown;
    int hintOpen;
    int64_t hintPointingNs;
    int hintReady;
    int hintHoverZone;
    XrPosef hintPose;
    float hintW, hintH;

    // Controller input. The aim ray is intersected with the screen and the hit
    // point drives the host mouse, so the PC sees an ordinary absolute mouse
    XrActionSet actionSet;
    XrAction aimAction;
    XrAction triggerAction;
    XrAction rightClickAction;
    XrAction middleClickAction;
    XrAction scrollAction;
    XrAction grabAction;
    XrAction toggleAction;
    XrAction handAimAction;
    XrAction handPinchAction;
    XrAction handGraspAction;
    XrAction menuAction;
    int menuBound;
    XrAction hapticAction;
    // The Steam Frame controller's own profile, offered when the runtime has
    // XR_VALVE_frame_controller_interaction, and the controls only it has
    XrAction padXAction;
    XrAction padYAction;
    XrAction padUpAction;
    XrAction padLeftAction;
    XrAction padBumperAction;
    XrAction padStartAction;
    // One path per name the profile has gone by (FRAME_PROFILE_PATHS), null for
    // one the runtime did not take bindings on
    XrPath frameProfile[FRAME_PROFILE_COUNT];
    XrPath activeProfile[HAND_COUNT];
    int frameBound;
    int onFrame[HAND_COUNT];
    int hapticBound;
    XrSpace aimSpaces[SRC_COUNT];
    XrSpace handAimSpaces[HAND_COUNT];
    XrPath handPaths[HAND_COUNT];
    int inputReady;
    int picoInteraction;
    int frameInteraction;
    int pointerOn;
    int togglePrev;
    int triggerDown[SRC_COUNT];
    int triggerEdge[SRC_COUNT];
    int triggerSwallowed[SRC_COUNT];
    float triggerValue[HAND_COUNT];
    float triggerHoldMin[HAND_COUNT];
    int triggerDipFrames[HAND_COUNT];
    int triggerRetaps[HAND_COUNT];
    int triggerDipping[HAND_COUNT];
    int trigLogDown[HAND_COUNT];
    int trigLogRaw;
    int trigLogLeft;
    int gripEdge[SRC_COUNT];
    int grabByTrigger;
    int buttonsDown;
    float scrollCarry;
    int64_t lastInputNs;

    EuroState filterU;
    EuroState filterV;
    float pointerMinCutoff;
    float pointerBeta;
    // Native (lane click-hold): the host cursor held for a click, see xr_clickhold.h
    ClickHold clickHold;
    float clickHoldPx;
    // Native (lane pointer-clicks): press stabilisation, double-click snap and
    // the stick click as a right click, see xr_pressfix.h
    PressFix pressFix;
    PressFixCfg pressCfg;
    int stickRightClick;
    int64_t lastHitNs;
    int lastHand;
    // Native (lane av-latency): how far the smoothing holds the cursor behind
    // the hand while it moves, for the log's latency line; summed per window
    // and taken by the session's 5 s log
    double ptrLagMsSum, ptrLagPxSum;
    int ptrLagSamples;
    float ptrRawX, ptrRawY;
    int ptrRawValid;

    EuroState aimFilterPos[HAND_COUNT][3];
    EuroQuatState aimFilterRot[HAND_COUNT];
    float aimMinCutoff;
    float aimBeta;

    int poseSeen[SRC_COUNT];
    XrPosef lastAim[SRC_COUNT];
    float movingFor;
    float stillFor;
    int pointerAwake;
    float pointerWake;
    float pointerSleep;
    int pointerSleepOn;

    // Laser and cursor (native: two slots of the UI atlas)
    int pointerArtReady;
    int beamVisible;
    int beamFree;
    int beamGaze;
    int raySetting;
    int rayFlipped;
    int rayDrawnSaid;
    // Native: the dot at the end of the ray, the owner's setting (the host
    // draws its own cursor into the video and the protocol cannot hide it)
    int dotOn;
    int headAimSetting;
    int headAimFlipped;
    int headAimSensitivity;
    int headAimDeadZone;
    HeadAim headAim;
    PointerNudge headAimNudge;
    int headAimActive;
    int headAimRecentring;
    XrTime headAimRecentreAt;
    int headAimSaid;
    long headAimSentX;
    long headAimSentY;
    XrTime headAimCountNs;
    long inputFrames;
    // Gamepad mode (M3: the pad's state is worked out as upstream does, but
    // nothing sends it to the host yet)
    int padMode;
    float padDeadzone;
    int padShortcut;
    PadToggle padToggle;
    PadChord padChord;
    int padGrip[HAND_COUNT];
    PadHand padRead[HAND_COUNT];
    PadState pad;
    int padAttached;
    int padResting;
    int padHeld;
    int padPutAway;
    int padButtonsSaid;
    PadRumble rumble;
    int rumbleOn;
    long rumblePulses;
    int rumbleResultSaid[HAND_COUNT];
    // The controller models (xr_model.c), at each hand's grip in a projection
    // layer of their own (native: rooms/room_gpu's MlxrModel, mlxr_pass_model)
    int modelOn;
    int modelReady;
    int modelLoadFailed;
    struct MlxrModel* model;
    int modelPassReady;
    int modelPassFailed;
    // Native (lane eye-swapchains): a swapchain per eye, as the room's
    XrSwapchain modelSwapchain[ROOM_EYES];
    uint32_t modelImageCount[ROOM_EYES];
    XrSwapchainImageVulkan2KHR* modelImages[ROOM_EYES];
    VkImageView* modelImageViews[ROOM_EYES];
    int modelEyeWidth;
    int modelEyeHeight;
    int modelShown[HAND_COUNT];
    XrPosef modelGrip[HAND_COUNT];
    // Native: each hand's controls as the model draws them (updateControllerModels,
    // xr_model.c): the trigger, grip, stick, buttons and d-pad moving on the Frame's models
    // (rooms/room_gpu.h's MlxrModelInput, MLXR_IN_COUNT floats; sized here as that
    // header is not in this one)
    float modelInput[HAND_COUNT][24];
    XrView modelViews[ROOM_EYES];
    int modelRendered;
    int modelAcquired;          // native: a bit per eye's swapchain
    int modelsSaid;
    int modelsShowing;
    XrAction gripAction;
    XrSpace gripSpaces[HAND_COUNT];
    int actionsSynced;
    XrVector3f beamStart;
    XrVector3f beamEnd;
    XrVector3f headPos;
    float headTurnRate;
    XrQuaternionf headTurnLast;
    int headTurnLastValid;
    int dragHeldByHead;
    XrQuaternionf screenOrientation;
    float beamWidth;
    float audioYaw;

    // Where the screen actually is. Seeded from the distance and width
    // preferences and then owned by the grab, so moving it does not fight the
    // sliders. Touching either slider puts it back under their control.
    XrPosef screenPose;
    float screenWidth;
    float screenRadius;
    int placementValid;
    int sliderSeen;
    float lastDistance;
    float lastQuadWidth;

    int grabDown[SRC_COUNT];
    int grabMode;
    int grabHand;
    float grabU, grabV;
    XrPosef grabAim;
    XrPosef grabScreen;
    float grabWidth;
    float grabHeight;
    float grabRadius;
    float grabPitch;
    float grabRoll;
    float grabOppX, grabOppY;
    // Native (owner's request): metres either thumbstick has pushed the held
    // screen away from the head (negative: brought closer), and when last
    float grabPush;
    int64_t grabPushNs;
    // Native (lane grab-pull): the most south and most north each stick was
    // tilted during this hold, said when it is let go
    float grabStickLo[HAND_COUNT];
    float grabStickHi[HAND_COUNT];
    int grabRoomPercent;
    float grabRoomReach;
    int grabByGaze;
    Vec3 grabHandStart;
    float grabScale;
    DragRamp grabRamp;
    int roomScreenUnsaved;
    int poseDirty;

    // Hover state, read by the frame loop to decide which handle to draw
    int hoverKind;
    int hoverCorner;
    int handleArtReady;

    int pickerReady;
    int envButtonReady;
    int outlineReady;
    int pickerOpen;
    int pickerHover;
    int pickerChoice;
    int pickerPick;
    int pickerCells;
    int envButtonHot;
    int loggedChoice;

    int cogButtonReady;
    int cogThumbReady;
    int cogOpen;
    int cogTab;
    int cogButtonHot;
    int cogDragSlider;
    int cogDragHand;
    int cogDragFace;
    int cogDragByGaze;
    Vec3 cogDragHandStart;
    float cogDragScale;
    float cogDragStartU;
    DragRamp cogDragRamp;
    int cogReadoutReady;
    int cogReadoutDrawn[READOUT_VALUES];
    int cogMarksReady;
    int cogClockReady;
    int clickSoundOn;
    int clickPending;
    int headLockedPref;
    int cogHoverSlider;
    int cogHoverCell;
    int cogHoverStep;
    XrPosef cogPose;
    float cogW, cogH;

    int kbButtonReady;
    int kbOpen;
    int kbButtonHot;
    int kbState;
    int kbMods;
    int kbHoverKey;
    int kbKeyDown;
    int kbKeyCount;
    float kbKeyRects[KB_MAX_KEYS * 4];
    int kbCodes[KB_STATE_COUNT][KB_MAX_KEYS];
    // Native: the Full keyboard (xr_keyboard.c). kbLayout is the choice
    // (config.ini keyboard_layout); the tables above are for the layout
    // shown, which is Compact while the report sheet is up.
    int kbLayout;
    int kbLayoutShown;
    KbFull kbFull;
    XrPosef kbPose;
    float kbW, kbH;
    // Native (owner's request): the keyboard once its move bar has moved it.
    // Until then it opens under the screen as it always did; after, it opens
    // where it was left, until the screen's placement is reset.
    int kbMoved;
    XrPosef kbMovedPose;

    int stereoButtonReady;
    int stereoButtonHot;
    int rayButtonReady;
    int rayButtonHot;
    int aimButtonReady;
    int aimButtonHot;
    int padButtonReady;
    int padButtonHot;

    int exitButtonReady;
    int exitConfirmOpen;
    int exitButtonHot;
    int exitHoverZone;
    XrPosef exitPose;
    float exitW, exitH;

    int reportReady;
    int reportOpen;
    int reportHoverZone;
    int reportSendReady;
    int cogReportHot;
    XrPosef reportPose;
    float reportW, reportH;
    int cogKofiHot;
    int kofiOpen;
    int kofiReady;
    int kofiHoverZone;
    XrPosef kofiPose;
    float kofiW, kofiH;

    float panelCurve;
    float prefCurvature;
    float panelSeparation;
    float separationCurrent;
    float defaultSeparation;
    float defaultConvergence;
    int presetUnits[COG_PRESET_CELLS];

    long statFrames;
    int64_t statTotalNs;
    int64_t statMaxNs;

    // ---- native ------------------------------------------------------------
    // The UI atlas: every panel, the bar's buttons and the pointer art in one
    // swapchain, shown by quads through sub-rects (xr_assets.c)
    struct XrUiAtlas* atlas;
    // Which panel art is drawn and current (xr_panels.c)
    struct XrPanels* panels;
    // The curved picture: drawn through mlxr_pass_panel into a projection
    // layer (xr_session.c), valid when curveReady
    struct XrCurve* curve;
    int curveReady;
    // The launcher's list in the picture's place while no stream is up:
    // LAUNCHER_PCS or LAUNCHER_APPS, the hit regions its last drawing reported,
    // what the ray is on and the press for the app to act on (-1 none)
    int launcher;
    float launcherAspect;   // the list's width over height
    int launcherHoverHit;   // index into the panel's hits, -1 none
    int launcherPressHit;   // a press this frame, -1 none
    // The video is shown (a frame has been drawn into the swapchain)
    int videoShown;
    // The picture's video sample: nonzero while the stream (or the file) is up
    int streaming;
    // The fork's transparent passthrough projection layer's swapchain, made by
    // the host the first time passthrough goes on (xr_session.c)
    int (*ensurePassthrough)(struct XrCtx* ctx, int size);
    // The GPU side of the room, the glow and the models (xr_world.c): the
    // host's shader passes, the command buffer the frame is recording (NULL
    // when nothing records), the serial its submission signals and the last
    // one the GPU has finished, and where the rooms and the model are read from
    struct MlxrPasses* passes;
    // Native: no session to make swapchains in (the tests, the snapshot
    // tool): the room and the models come up without their swapchains and
    // are drawn through xrRoomRecord / xrModelRecord into the caller's images
    int offscreen;
    VkCommandBuffer frameCmd;
    uint64_t gpuSerial;
    uint64_t gpuDone;
    // Native M6: the timeline semaphore each frame's submission signals
    // gpuSerial on, which the depth stage thread waits on for a readback
    VkSemaphore gpuTimeline;
    char assetsDir[512];
    // What xr_world.c keeps: uploads in flight, rooms waiting for the GPU to
    // be done with them, the GPU timestamps
    struct XrWorld* world;
} XrCtx;

// xr_session.c: instance, session, swapchain and lifecycle
int checkXr(XrResult res, const char* what);
const char* xrResultName(XrResult res);

// xr_log.c: the file log (native: moonlight.log, rolled over at 5 MB)
void xrSetFileLog(const char* path, int level);
void xrLogFlush(void);

// xr_room.c: the 3d rooms
int roomEffective(XrCtx* ctx);
int roomResizable(int style);
void roomLevelsFromTable(XrCtx* ctx);
int roomScreenPercent(XrCtx* ctx, int style);
int roomGlowOn(XrCtx* ctx, int style);
void applyRoomPlacement(XrCtx* ctx, int style, float aspect, int reseeded);
void worldEyeSize(XrCtx* ctx, int* outW, int* outH);
void prepareRoom(XrCtx* ctx);
void renderRoom(XrCtx* ctx);
// Native names for upstream's nativeSetRoomLevels
void xrSetRoomLevels(XrCtx* ctx, int cell, int brightness, int glow, int light, int screen);

// xr_ambilight.c: the frame's colour sample, the letterbox detection, the glow
struct MlxrVideo;
int initAmbilight(XrCtx* ctx);
void ambiEffective(XrCtx* ctx, int* on, float* level);
void runAmbiBarDetect(XrCtx* ctx, const struct MlxrVideo* video);
void finishAmbiBarDetect(XrCtx* ctx);
void runFrameColorSample(XrCtx* ctx, const struct MlxrVideo* video);
void runGlowRender(XrCtx* ctx);
int glowStale(XrCtx* ctx);
void redrawGlow(XrCtx* ctx, const struct MlxrVideo* video);

// xr_model.c: the controller models
void updateControllerModels(XrCtx* ctx);
void renderControllerModels(XrCtx* ctx);

// xr_input.c: actions, hands, the ray and the per frame input pass
int initXrInput(XrCtx* ctx);
void destroyXrInput(XrCtx* ctx);
void refreshInputSource(XrCtx* ctx);
int updatePlacement(XrCtx* ctx, float distance, float quadWidth, float curvature);
void recentreScreen(XrCtx* ctx);
// Native names for upstream's JNI entry points (nativeUpdateInput, ...)
void xrUpdateInput(XrCtx* ctx, float distance, float quadWidth, float curvature, int headLocked,
                   int pointerEnabled, int gazeEnabled, int pointerSleep, float* out);
void xrSetRumble(XrCtx* ctx, int lowMotor, int highMotor);
void xrSetScreenPose(XrCtx* ctx, const float* pose);
void xrSetHandHint(XrCtx* ctx, int wanted);

// xr_ui.c: where the furniture and the panels sit, and what the ray is over
int furnitureOnStandIn(XrCtx* ctx);
XrPosef furniturePose(XrCtx* ctx);
float furnitureWidth(XrCtx* ctx);
float furnitureHeight(XrCtx* ctx);
BarFrame barFrame(XrCtx* ctx);
int barButtonHit(XrCtx* ctx, int slot, float u, float v, const BarFrame* frame);
float cornerSide(XrCtx* ctx);
float effectiveCurvature(XrCtx* ctx);
int cogFace(XrCtx* ctx);
int cogArt(XrCtx* ctx);
float screenPitch(XrCtx* ctx);
XrQuaternionf screenOrient(float yaw, float pitch, float roll);
float screenRoll(XrCtx* ctx);
XrPosef pickerPose(XrCtx* ctx, float* outWidth, float* outHeight);
XrPosef cogPanelPose(XrCtx* ctx, float* outWidth, float* outHeight);
XrPosef kbPanelPose(XrCtx* ctx, float* outWidth, float* outHeight);
// Native: kbPose and the panel's size as they stand now, which is under the
// screen (kbPanelPose) unless the move bar has moved it
void kbPlace(XrCtx* ctx);
// Native: the keyboard's move bar, in the keyboard's plane: the pill's width
// and height and how far its middle is below the panel's centre, metres. The
// pill is the picture's, at the picture's size and gap, so it hangs off the
// panel's bottom edge by the same share of the frame as the picture's does.
void kbBarGeometry(XrCtx* ctx, float* outWidth, float* outHeight, float* outDrop);
// Native: whether the stats window is up, and its pose and size now (statsPose,
// statsW, statsH): the corner of the picture until a grip has moved it
int statsShown(XrCtx* ctx);
void statsPlace(XrCtx* ctx);
int kbKeyAt(XrCtx* ctx, float u, float v);
int headAimCanAct(XrCtx* ctx);
void setHeadAimOn(XrCtx* ctx, int on, const char* from);
void setPadMode(XrCtx* ctx, int on, const char* from);
const char* padShortcutName(int shortcut);
void setStereoLive(XrCtx* ctx, int on, const char* from);
void setRayOn(XrCtx* ctx, int on, const char* from);
int panelUp(XrCtx* ctx);
XrPosef exitPromptPose(XrCtx* ctx, float* outWidth, float* outHeight);
int exitPromptZone(float u, float v);
XrPosef reportSheetPose(XrCtx* ctx, float* outWidth, float* outHeight);
XrPosef handHintPose(XrCtx* ctx, float* outWidth, float* outHeight);
XrPosef kofiSheetPose(XrCtx* ctx, float* outWidth, float* outHeight);
int cogTabRowCount(int face);
int cogRowIsTrack(int face, int row);
int cogRowLive(XrCtx* ctx, int face, int row);
float cogSliderValue(XrCtx* ctx, int face, int slider);
void cogApplySlider(XrCtx* ctx, int face, int slider, float pu);
int cogOptionCells(int option);
int cogOptionValue(XrCtx* ctx, int option, int headLocked);
int cogApplyOption(XrCtx* ctx, int option, int cell);
int cogRowCells(int face, int row);
int cogRoomCellValue(XrCtx* ctx, int row);
void cogApplyRoomCell(XrCtx* ctx, int row, int cell, float* out);
int cogCellInForce(XrCtx* ctx, int face, int row);
void cogApplyCell(XrCtx* ctx, int face, int row, int cell, float* out);
void cogDragEnded(XrCtx* ctx, float* out);
void cogStepTrack(XrCtx* ctx, int face, int row, int dir, float* out);
int cogCellAt(float pu, int cells);
void cogReadouts(XrCtx* ctx, int* values);
void pictureSet(XrCtx* ctx, int row, int units);
void pictureReset(XrCtx* ctx);
// Native names for upstream's JNI setters, fed from config.ini
void xrSetDepthDefaults(XrCtx* ctx, int separation, int convergence, const int* presets);
void xrSetClickSound(XrCtx* ctx, int on);
void xrSetShowRay(XrCtx* ctx, int on);
void xrSetHeadAim(XrCtx* ctx, int on, int sensitivity, int deadZone);
void xrSetGamepad(XrCtx* ctx, int shortcut, int deadzonePercent);
void xrSetPicture(XrCtx* ctx, const int* values);

// xr_assets.c: the UI atlas swapchain, the art drawn in code, and the
// environment choice
int roomStyleForCell(int cell);
int roomCellForStyle(int style);
void xrSetEnvironment(XrCtx* ctx, int choice);

// xr_display.c: the display refresh rate and the performance levels
void probeDisplayExtensions(XrCtx* ctx);
void startDisplay(XrCtx* ctx);
void startPerfLevels(XrCtx* ctx);
void setPerfLevel(XrCtx* ctx, int level);
void perfNotice(XrCtx* ctx, const XrEventDataPerfSettingsEXT* notice);
void displaySessionBegun(XrCtx* ctx);
void displayFocused(XrCtx* ctx);
void displayRateChanged(XrCtx* ctx, float from, float to);
void displayFrameBegun(XrCtx* ctx, const XrFrameState* state);
void displayFrameEnded(XrCtx* ctx);
void setRefreshKnob(XrCtx* ctx, int hz);
void setDepthRate(XrCtx* ctx, int perSecond, const char* why);
void xrSetDepthRate(XrCtx* ctx, int perSecond);
float xrGetDisplayRate(XrCtx* ctx);
// The display rate the runtime last reported, whole Hz, readable from any thread; 0 when not known yet
int xrReportedDisplayHz(void);

#endif
