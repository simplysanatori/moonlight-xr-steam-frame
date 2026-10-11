// Moonlight XR Native: physical mice (USB or Bluetooth) read from evdev and
// forwarded to the host, the way the physical keyboard is (stream/evdev_keyboard.c):
// the app has no window under SteamVR, so it reads /dev/input/event* directly (the
// steamos user is in the input group) and grabs the mouse while it is forwarding, so
// the mouse reaches only the host. Behaviour follows moonlight-qt's
// app/streaming/input/mouse.cpp (relative motion, buttons L/R/M/X1/X2, high-res
// vertical and horizontal wheel). GPLv3.
//
// Also here: the filter that keeps the controller ray's absolute pointer from
// fighting a mouse that was just used (evmouse_abs_filter).
//
// The core knows nothing of moonlight-common-c's sends: the sink is hooks, so a test
// feeds events through pipes with a recording sink and a fake grab.
#ifndef MLXR_EVMOUSE_H
#define MLXR_EVMOUSE_H

#include <linux/input.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void (*move)(int dx, int dy, void* user);         // relative motion, host pixels
    void (*button)(int button, int down, void* user); // BUTTON_LEFT/MIDDLE/RIGHT/X1/X2 (Limelight.h)
    void (*scroll)(int amount, void* user);           // 120 per notch, up positive
    void (*hscroll)(int amount, void* user);          // 120 per notch, right positive
} EvMouseSink;

typedef struct {
    int (*grab)(int fd, int on); // NULL: EVIOCGRAB; returns 0 on success
    int scan;                    // look for mice in /dev/input (hotplug every 2 s)
} EvMouseCfg;

typedef void (*EvMouseLog)(char level, const char* line);
void evmouse_set_log(EvMouseLog log);
// Native (lane artemis-settings): 0 drops the back/forward (side/extra) buttons
void evmouse_set_nav_buttons(int on);

// Without a thread (tests drive evmouse_step); evmouse_start is init plus the thread
void evmouse_init(const EvMouseSink* sink, void* user, const EvMouseCfg* cfg);
int evmouse_start(const EvMouseSink* sink, void* user);
// Forward (and grab) only while active; going inactive releases held buttons
void evmouse_set_active(int active);
void evmouse_stop(void);

// One pass of the thread's loop: hotplug scan, grab/release, poll up to timeoutMs,
// read and forward. Returns the number of input events handled.
int evmouse_step(int timeoutMs);
// Adopt an already open event fd (tests use a pipe); returns the device index or -1
int evmouse_add_fd(int fd, const char* name, int hiResWheel);
int evmouse_device_count(void);
int evmouse_grabbed(int index); // 1 grabbed, 0 not, -1 grab failed

// Pure helpers (exposed for tests)
int evmouse_is_mouse(const unsigned long* evBits, const unsigned long* relBits, const unsigned long* keyBits);
int evmouse_button(int linuxCode); // Limelight BUTTON_* or 0

// ---- the controller pointer yields to the mouse ----------------------------------
// While the mouse was used in the last holdMs, controller absolute updates are
// dropped; after that they stay dropped until the aim has moved noticeably (a
// fraction of the video width) from where it was when the mouse was last used.
// Env: MLXR_MOUSE_PARK=0 off, MLXR_MOUSE_PARK_MS (1000), MLXR_MOUSE_PARK_DIST (0.03).
void evmouse_note_use_at(int64_t nowNs);
// 1: drop this absolute update (x,y in a w*h video). Frame-loop thread only.
int evmouse_abs_filter(int x, int y, int w, int h, int64_t nowNs);
void evmouse_filter_reset(void);
void evmouse_filter_config(int enabled, int holdMs, float dist);
int evmouse_filter_parked(void);
int64_t evmouse_now_ns(void);

#endif
