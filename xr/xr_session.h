// Moonlight XR Native: the OpenXR session host. One session for the life of
// the app: the launcher's lists, the stream's picture and the UI around it
// (Moonlight XR's renderer core, ported in xr_ui.c, xr_input.c, xr_layers.c,
// xr_display.c) all live in it. GPLv3.
#pragma once
#include <stdint.h>

#include "core/xr_hostkeys.h"

struct FrameSlot;
struct Settings;
struct UiPcItem;

// What the frame loop hands the app each frame: presses the app acts on
typedef struct XrUiEvents {
    int exitConfirmed;          // the exit prompt's Exit (Quit app) was pressed
    int disconnectConfirmed;    // native: its Disconnect was pressed (the host's session keeps running)
    int pcvrConfirmed;          // native (PLAN 4.1): the Host sheet's Switch to PC VR was confirmed
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

struct MlxrHudCum;

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
    // Native (lane wave2-bind-wire): the bindings. High-resolution horizontal
    // scroll, 120 per notch, positive = right. A key: action going down
    // (down 1) or up (0): a Windows virtual key and the modifiers it goes with
    // (Moonlight's MODIFIER_ bits), held for as long as its control is. Either
    // may be NULL.
    void (*hscroll)(void* user, int amount);
    void (*bindKey)(void* user, int vk, int mods, int down);
    // Native (lane wave3-hud): about once a second from the frame loop, the stream's running totals for the HUD and
    // the slow connection badge. 1 with *out filled while a stream is up, else 0. May be NULL. Must not block.
    int (*hud)(void* user, struct MlxrHudCum* out);
    // Native (lane wave3-session): the headset came off (present 0) or went back on (1), from
    // XR_EXT_user_presence, on the frame loop. Never called when the runtime does not offer the
    // extension. May be NULL. Must not block.
    void (*presence)(void* user, int present);
    // Native (lane wave4-apollo): a press on the Host sheet's PC page, on the frame loop, while a stream is up:
    // action is XR_HOSTACT_PASTE, _COPY (text is what to set the host's clipboard to) or _CMD (arg is the
    // number of the host command, its place in XrHostInfo.cmd). text is only good for the call. Must not
    // block: it starts the work and says how it went through xr_app_notice. May be NULL.
    void (*hostAction)(void* user, int action, int arg, const char* text);
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
// Native (lane wave2-bind-wire), any thread: the host app being streamed, its
// name as the app list shows it ("" for none), which picks its own lines out
// of bindings.ini
void xr_app_set_host_app(const char* name);
// Native (lane wave4-apollo), any thread: what the host being streamed offers, which the Host sheet's PC
// page shows (NULL: nothing, as with no stream)
void xr_app_set_host_info(const XrHostInfo* info);
// Native, any thread: show or hide the in-world keyboard on the next frame (a
// physical pad's Y in mouse mode, lane wave2b-padmouse)
void xr_app_toggle_keyboard(void);
// The PC and app now streaming (names), which the layout kept per app is keyed
// by. Same function as xr_native.h's.
void xrSetStreamIdentity(const char* host, const char* app);
