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
#include "xr_hud.h"
#include "xr_glow.h"
#include "xr_grade.h"
#include "xr_keys.h"
#include "xr_kbfull.h"
#include "xr_hostkeys.h"
#include "xr_cards.h"
#include "xr_splash.h"
#include "xr_controller.h"
#include "xr_headaim.h"
#include "xr_clickhold.h"
#include "xr_pressfix.h"
#include "xr_gamepad.h"
#include "xr_bindrun.h"
#include "xr_scroll.h"
#include "xr_lightgun.h"
#include "xr_padgyro.h"
#include "xr_ray.h"
#include "xr_raylog.h"
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

// Native: the Host sheet, over the middle of the picture, a little wider as it
// has a grid of twenty cells
#define HOST_WIDTH_FRAC 0.46f

// The panels that fade in and out, each with a fade of its own
#define FADE_COG 0
#define FADE_PICKER 1
#define FADE_KB 2
#define FADE_EXIT 3
#define FADE_REPORT 4
#define FADE_HINT 5
#define FADE_KOFI 6
#define FADE_HOST 7
#define FADE_PANELS 8
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
    // Native (lane wave3-hud): what the overlay shows (STATS_STYLE_ from xr_hud.h: full lines, HUD,
    // Lite), and what the clock strip over the cog shows beside the time: the headset battery
    // (present, percent, charging; a machine with none shows nothing) and the slow connection badge
    int statsStyle;
    int batteryPresent;
    int batteryPercent;
    int batteryCharging;
    int slowConn;
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
    // Native (PLAN 4.3): the 3D tab's 3D source (STEREO_SOURCE_*); anything
    // but Depth makes stereoMode DEPTH_MODE_SBS, a pair the host drew, side by
    // side or top and bottom, full size per eye with sbsFull. depthModeSet is
    // the mode depth_source gave at the start, which Depth goes back to.
    int stereoSource;
    int sbsFull;
    int depthModeSet;
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
    // Native (lane wave5-depth, PLAN 5.2): the gaps the shift opens filled
    // from the background side rather than by stretching the nearer edge
    // (MLXR_HOLE_STRETCH; on, MLXR_HOLE_FILL=0 turns it off)
    int holeFill;
    // Native (lane wave5-depth, PLAN 5.4): [3d] motion_reset as a luma change
    // 0..1 that starts a depth texel's average again; 0 off
    float depthMotion;
    // Native (lane wave5-depth, PLAN 5.6): [3d] depth_global and depth_scale
    // as the engine's remap gains (upstream depthGlobal, depthLocal); 1 is
    // the map as made
    float depthGlobal;
    float depthLocal;
    // Native (lane wave5-depth, PLAN 5.5): [debug] depth_threads, read when
    // the model starts (MLXR_DEPTH_THREADS overrides it)
    int depthThreads;
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
    // Native (lane wave3-visual): the Picture tab's Glow edge (GLOW_FEATHER_),
    // and the one the glow image was last drawn with. Feathered, the glow is
    // drawn over the picture rather than under it.
    int glowFeather;
    int glowDrawnFeather;
    int ambilightOn;
    float ambiIntensity;
    int ambiOverride;

    // The picture grade, in the preferences' whole units in PICTURE_ order,
    // and the sums they come to (native: handed to mlxr_pass_warp)
    int pictureUnits[PICTURE_VALUES];
    int gradeOn;
    PictureGrade grade;
    // Native (lane wave3-visual): Background dim in whole percent (BG_DIM_), and
    // the void's colour as typed (sRGB 0..1; black when config.ini has none)
    int bgDim;
    float voidColor[3];

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
    // Native (lane wave3-visual): the same 16x16 layer is also the void's
    // colour. Its images, and the colour (sRGB red, green, blue, alpha) the
    // newest one was cleared to, so a change of dim or void colour clears
    // another rather than making the swapchain again.
    VkImage passthroughImages[8];
    uint32_t passthroughImageCount;
    float backdropDrawn[4];
    int backdropDrawnValid;
    float backdropWant[4];
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
    // Native (lane wave6-splash): the logo shown while a stream has no picture (xr_splash.c)
    XrSplashState splashUi;

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
    // Native (lane wave3-hud): XR_META_performance_metrics offered (xr_metrics.c reads the compositor's times)
    int metricsSupported;

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
    // Native (lane ray-glitch, xr_ray.h): each controller's aim and grip held
    // across a short loss of tracking, the source that pointed last, and
    // where each hand's ray came from this frame (AIM_)
    PoseHold aimHold[HAND_COUNT];
    PoseHold gripHold[HAND_COUNT];
    PickState pick;
    int aimSrc[HAND_COUNT];
    // The ray recorder, MLXR_RAY_DEBUG (docs/dev/ray-debug.md): the CSV, this
    // frame's row, and the throttle on its log lines
    int rayDebug;
    FILE* rayFile;
    long rayBytes;
    int64_t rayFlushNs;
    RayLogRow rayRow;
    int64_t rayLogWindowNs;
    int rayLogLines;
    int rayLogDropped;
    unsigned raySaidFlags[HAND_COUNT];
    int raySaidSrc[HAND_COUNT];
    int raySaidSteep[HAND_COUNT];
    int raySaidHand;
    // Native (lane ray-glitch): the runtime reads the pointer's frame makes,
    // NULL for OpenXR's own. A test points them at recorded rows, so a CSV
    // from the headset goes through the same code (tests/m4/test_replay.c).
    XrResult (*locateSpace)(struct XrCtx* ctx, XrSpace space, XrSpace base, XrTime time,
                            XrSpaceLocation* out);
    int (*poseActive)(struct XrCtx* ctx, XrAction action, int hand);
    float (*readFloat)(struct XrCtx* ctx, XrAction action, int hand);
    int (*readBool)(struct XrCtx* ctx, XrAction action, int hand);
    XrVector2f (*readVec2)(struct XrCtx* ctx, XrAction action, int hand);
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
    // Native (lane wave2-lightgun-gyro, PLAN 2.9): the right thumbstick's touch sensor, for the gyro
    // ratchet. Created with the Frame profile's actions and offered on the first rung of its binding
    // ladder only (input/thumbstick/touch, which the probe found on both hands); stickTouchBound says
    // a Frame profile took it. Where it is not bound, or the right hand is on another profile,
    // stick_touch reads as always.
    XrAction stickTouchAction;
    int stickTouchBound;
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
    // Native (lane wave2-bind-wire): the stick's wheel, smooth and both ways
    // (xr_scroll.h), in place of the whole notches it used to carry
    ScrollAcc scroll;
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
    // Native (lane wave2-bind-wire): the Click steadiness level that set
    // pressCfg and clickHoldPx (CLICK_STEADY_), and the stick's wheel speed in
    // percent (scroll_speed)
    int clickSteady;
    int scrollSpeed;
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
    // Native (lane wave2-lightgun-gyro, PLAN 2.3): the lightgun pointer policy (xr_lightgun.h). The
    // settings are config.ini's (lightgun, lightgun_reload, lightgun_recoil); lightgun is what they
    // make with the model that is drawn, kept up to date every input frame, and the policy never
    // writes into the settings above (pointerSleepOn is still the user's)
    int lightgunSetting;
    int lightgunReloadSetting;
    int lightgunRecoilSetting;
    LightgunPolicy lightgun;
    LightgunReload lightgunReload;
    long recoilPulses;

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
    // PAD_GRIP_ : what the grips are on the pad (the pad_grip setting)
    int padGripMode;
    PadGuide padGuide;
    PadToggle padToggle;
    PadChord padChord;
    int padGrip[HAND_COUNT];
    PadHand padRead[HAND_COUNT];
    // Native (lane wave2-bind-wire): the controllers as the pad and the
    // bindings read them, after the switch shortcut has held its parts back
    PadHand padSeen[HAND_COUNT];
    // Native (lane wave2-bind-wire): bindings.ini (xr/core/xr_bind.h) and the
    // table for each mode with the host app's own lines over it (bindTable);
    // the same with whole sticks spread over their directions and the Guide
    // chord left to padGuideStep (bindRunTable), which is what runs
    XrBindSet bindSet;
    int bindLoaded;
    int bindReady;
    char bindApp[XR_BIND_APP_MAX];
    XrBindTable bindTable[XR_BIND_MODES];
    XrBindTable bindRunTable[XR_BIND_MODES];
    // The gamepad table is not the defaults, so the pad's buttons, triggers
    // and sticks come from it rather than padMapGrips; and its Guide is
    // View with Menu held, which padGuideStep times
    int bindPadCustom;
    int bindGuideHold;
    // What the controls hold (xr_bindrun.h), the mode it last ran in plus one
    // (0 not yet), the controls down last frame for the hysteresis, and
    // whether only app commands go (gamepad mode with a panel up)
    XrBindRun bindRun;
    int bindModeRan;
    uint64_t bindRaw;
    int bindAppsOnly;
    // What they send this frame: the host mouse buttons pointer mode's table
    // holds (VR_BUTTON_ bits, X1 8 and X2 16 included) and gamepad mode's,
    // the wheel in 120ths of a notch, stick:mouse's motion in pixels
    int bindMouse;
    int bindPadMouse;
    int bindWheelV;
    int bindWheelH;
    int bindMouseDX;
    int bindMouseDY;
    float bindMouseCarry[HAND_COUNT][2];
    ScrollAcc padScroll;
    int64_t bindPadNs;
    // key: actions going down and up, for the session to send (dispatchInput)
    int bindKeyCount;
    XrBindKey bindKeys[XR_BIND_KEY_QUEUE];
    PadState pad;
    int padAttached;
    int padResting;
    int padHeld;
    int padPutAway;
    int padButtonsSaid;
    PadRumble rumble;
    int rumbleOn;
    long rumblePulses;
    // Native (lane wave2-lightgun-gyro, PLAN 2.9): the pad_gyro setting (PAD_GYRO_), its ratchet
    // (PAD_GYRO_RATCHET_), the orientation the last sample was taken at (for a runtime with no
    // velocity), whose it was (-1 the head, else the hand), what the log has said, and how many
    // samples went out
    int padGyroSource;
    int padGyroRatchet;
    XrQuaternionf padGyroPrev;
    int64_t padGyroPrevNs;
    int padGyroPrevWho;
    int padGyroSaidHz;
    int padGyroSaidFallback;
    long padGyroSamples;
    int rumbleResultSaid[HAND_COUNT];
    // The controller models (xr_model.c), at each hand's grip in a projection
    // layer of their own (native: rooms/room_gpu's MlxrModel, mlxr_pass_model)
    int modelOn;
    // Native: which model (CONTROLLER_MODEL_*; modelOn with none yet reads as the Frame's), the
    // kind the loaded one (or the load that failed) was asked for, so a change of kind reloads it
    // between frames, and which pose the hold below was kept at
    int modelKind;
    int modelKindAsked;
    int modelAimDrawn;
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
    // Native (lane ray-glitch): the head's +x in the same frame, which the
    // beam's quad lies along when the head sights down it (beamBasis)
    XrVector3f headRight;
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
    // Native (lane layout): Follow, the angular-size lock and the layout kept
    // per PC and app (xr_place.c). The head's pose in the local space as last
    // located; layoutSeen is an AppLayout kept as ints, since settings.h comes
    // after this header.
    int followOn;
    int followLeashDeg;
    FollowState follow;
    int64_t followNs;
    XrPosef headPose;
    int headPoseValid;
    int perAppOn;
    int angularLockOn;
    int lockActive;
    float lockBaseWidth;
    float lockBaseRadius;
    unsigned layoutHash;
    unsigned identityGen;
    int layoutSeen[13];
    int layoutSeenValid;
    int layoutPending;
    int64_t layoutDirtyNs;
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
    // Native: which keyboard the labels are for (KB_MAP_*, config.ini
    // keyboard_keymap); kbLayoutShown carries the map shown in bits 2 and up
    int kbMap;
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
    // Native (lane wave2-bind-wire): the bar's Host button under the ray
    int hostButtonHot;

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

    // Native: the Host sheet (xr_host.c). hostConfirm is the cell asking for a
    // yes, or -1. macros is macros.ini as last read; hostRun plays the
    // sequence a cell chose, a key a frame; hostChord is "Send keys" waiting
    // for a combo on the keyboard.
    int hostOpen;
    int hostReady;
    int hostHoverZone;
    int hostConfirm;
    XrPosef hostPose;
    float hostW, hostH;
    XrMacros macros;
    XrHostRun hostRun;
    XrHostChord hostChord;
    // Native (lane wave3-cards): the first-run controls card (PLAN 3.9) and the pad card
    // (PLAN 2b.6), xr_padcard.c. *Up is what the frame wants drawn, *Ready that the panel's art has
    // landed in the atlas. The pad card is held in the lap (padLive is the pad as last read).
    FirstRun firstRun;
    int firstRunUp;
    int firstRunReady;
    int padCardMode;
    int padCardUp;
    int padCardReady;
    PadLive padLive;
    // Native (PLAN 4.1): the Host sheet's "Switch to PC VR" was confirmed; xr_session.c turns it into
    // XrUiEvents.pcvrConfirmed once and clears it
    int pcvrRequest;
    // Native (lane wave4-apollo): the Host sheet's PC page. hostPage is the page showing; hostInfo what the
    // host offers (xr_app_set_host_info); hostCompose "Send text to PC" having the keyboard; and
    // hostActKind (XR_HOSTACT_*, with hostActArg and hostActText) the thing a press chose, which the
    // frame loop hands to the app once.
    int hostPage;
    XrHostInfo hostInfo;
    XrHostCompose hostCompose;
    int hostActKind, hostActArg;
    char hostActText[XR_COMPOSE_MAX];
    // Native: the Compact keyboard's locked modifiers, and the hand holding the key
    KbSticky kbSticky;
    int kbStickyHand;

    float panelCurve;
    float prefCurvature;
    float panelSeparation;
    float separationCurrent;
    // Native (lane wave3-visual): 3D Eye Balance, -1..1: the share of the
    // disparity the left eye takes beyond an even split (eyeShares)
    float eyeBalance;
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
// Native (lane wave3-visual): the colour (sRGB red, green, blue, alpha) of the
// backdrop behind the picture and the rooms, and whether there is one (a
// passthrough that is dimmed or not, a void that is not black). The layer's
// swapchain is cleared to it and the picture's own projection layer clears
// to it when it stands in for that layer. passthroughNow: the cameras are
// being blended in this frame.
int xrBackdropColor(XrCtx* ctx, int passthroughNow, float out[4]);
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
// Whether the letterbox detector runs and its crop applies: not on a
// host-rendered stereo pair (native, PLAN 4.3), whose seam and halves are no
// letterbox
int ambiBarDetectOn(const XrCtx* ctx);
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
int exitPromptZoneFor(float u, float v, int withDisconnect);
int exitPromptHasDisconnect(const XrCtx* ctx);
XrPosef reportSheetPose(XrCtx* ctx, float* outWidth, float* outHeight);
XrPosef handHintPose(XrCtx* ctx, float* outWidth, float* outHeight);
XrPosef kofiSheetPose(XrCtx* ctx, float* outWidth, float* outHeight);
// xr_kofi.c: runs Steam's browser on the Frame for the Ko-fi page (NULL: the default address).
// Once per press, never blocks. 1 when a program was started.
int kofiOpenPage(const char* url);
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
void xrSetPadGrip(XrCtx* ctx, int mode);
// Native (lane wave2-bind-wire): config.ini click_steadiness (CLICK_STEADY_)
// and scroll_speed (percent)
void xrSetClickSteadiness(XrCtx* ctx, int level);
void xrSetScrollSpeed(XrCtx* ctx, int percent);
// Native (lane wave2-lightgun-gyro): config.ini lightgun (LIGHTGUN_), lightgun_reload and
// lightgun_recoil; pad_gyro (PAD_GYRO_) and pad_gyro_ratchet (PAD_GYRO_RATCHET_)
void xrSetLightgun(XrCtx* ctx, int setting, int reload, int recoil);
void xrSetPadGyro(XrCtx* ctx, int source, int ratchet);
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
