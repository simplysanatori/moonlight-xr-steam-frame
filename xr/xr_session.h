// Moonlight XR Native: the OpenXR session host. One session for the life of
// the app: the launcher's lists, the stream's picture and the UI around it
// (Moonlight XR's renderer core, ported in xr_ui.c, xr_input.c, xr_layers.c,
// xr_display.c) all live in it. GPLv3.
#pragma once
#include <stdint.h>

struct FrameSlot;
struct Settings;
struct UiPcItem;

// What the frame loop hands the app each frame: presses the app acts on
typedef struct XrUiEvents {
    int exitConfirmed;          // the exit prompt's Exit (Quit app) was pressed
    int disconnectConfirmed;    // native: its Disconnect was pressed (the host's session keeps running)
    int listKind, listA, listB; // a press on the launcher's list (UI_HIT_*, 0 none)
    int focused;                // the session has input focus
    int pictureShown;           // native: the stream's picture is on the screen
} XrUiEvents;

// What the app shows in the picture's place while no stream is up
typedef struct XrUiLauncher {
    int mode;                   // 0 none (the stream), 1 PC list, 2 app list
    const void* pcs;            // UiPcItem[]
    int pcCount;
    const char* pcName;
    const void* apps;           // UiAppItem[]
    int appCount;
    int page;
    int loading, failed;
} XrUiLauncher;

typedef struct XrAppHooks {
    void* user;
    // Pointer position on the video in stream pixels (0..w-1, 0..h-1).
    void (*pointer)(void* user, int x, int y, int w, int h);
    // Relative mouse (head aim)
    void (*mouseMove)(void* user, int dx, int dy);
    // button: 1 left, 2 middle, 3 right (moonlight-common-c BUTTON_*); down 1/0.
    void (*button)(void* user, int button, int down);
    // High-resolution vertical scroll, 120 per notch, positive = up.
    void (*scroll)(void* user, int amount);
    // The on-screen keyboard: a key typed (Unicode, or KB_CODE_VK + vk) with the
    // modifiers lit, and the modifiers held (KB_MOD_*), as VrKeyboard takes them
    void (*key)(void* user, int code, int mods);
    void (*modifiers)(void* user, int mods);
    // Called once per second from the frame loop with a status line to add to the log.
    void (*stats)(void* user, char* buf, int len);
    // Session focus changed (1 = the app has input focus in the headset).
    void (*focus)(void* user, int focused);
    // Every frame, on the frame loop: the presses the app acts on, and what it
    // wants shown in the picture's place. Must not block.
    void (*ui)(void* user, const XrUiEvents* ev, XrUiLauncher* show);
    // Native (lane av-latency): every 5 s, only from the log (not the overlay),
    // while streaming: the app's part of the "xr: latency:" line (host encode,
    // network, audio cushion and underruns). Per-window counters reset here.
    void (*latency)(void* user, char* buf, int len);
    // Every frame, right after the input pass: the head's yaw against the picture
    // in radians (positive = turned left), which the virtual surround turns its
    // speakers by. May be NULL. Must not block.
    void (*headYaw)(void* user, float yawRadians);
} XrAppHooks;

typedef struct XrAppConfig {
    struct FrameSlot* video;   // decoded software frames (YUV420P AVFrames)
    XrAppHooks hooks;
    // config.ini: read at start, written as the panel changes things; owned by
    // the caller, used only on the frame loop while xr_app_run runs
    struct Settings* settings;
    const char* settingsPath;  // NULL: the default path
    int videoWidth, videoHeight, fps; // the stream asked for (the picture's shape before a frame)
    double seconds;            // stop after this long (0 = until quit)
    const char* reportDir;     // where a problem report from the headset is saved
    int startStreaming;        // a picture source is up from the start (--file)
} XrAppConfig;

// Runs until the session ends, the time limit passes or xr_app_request_quit().
// Returns 0 on a clean exit, 1 when there is no runtime or headset.
int xr_app_run(const XrAppConfig* cfg);
void xr_app_request_quit(void);
// Any thread: a line on the toast
void xr_app_notice(const char* text);
// Any thread but the frame loop: the picture is gone (the stream stopped);
// blocks until the frame loop has let go of every decoded frame and import,
// after which the decoder may be closed. Returns at once when no session runs.
void xr_app_release_video(void);
// Any thread: a stream is (or is no longer) the picture's source
void xr_app_set_streaming(int on);
