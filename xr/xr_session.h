// Moonlight XR Native: the OpenXR session and frame loop (M1: a world-locked
// video quad and a controller pointer). GPLv3.
#pragma once
#include <stdint.h>

struct FrameSlot;

typedef struct XrAppHooks {
    void* user;
    // Pointer position on the video in stream pixels (0..w-1, 0..h-1).
    void (*pointer)(void* user, int x, int y, int w, int h);
    // button: 1 left, 2 middle, 3 right (moonlight-common-c BUTTON_*); down 1/0.
    void (*button)(void* user, int button, int down);
    // High-resolution vertical scroll, 120 per notch, positive = up.
    void (*scroll)(void* user, int amount);
    // Called once per second from the frame loop with a status line to add to the log.
    void (*stats)(void* user, char* buf, int len);
} XrAppHooks;

typedef struct XrAppConfig {
    struct FrameSlot* video;   // decoded frames to show (YUV420P AVFrames)
    XrAppHooks hooks;
    float screenDistance;      // metres in front of the LOCAL origin
    float screenWidth;         // metres
    double seconds;            // stop after this long (0 = until SteamVR ends the session)
} XrAppConfig;

// Runs until the session ends, the time limit passes or xr_app_request_quit().
// Returns 0 on a clean exit, 1 when there is no runtime or headset.
int xr_app_run(const XrAppConfig* cfg);
void xr_app_request_quit(void);
