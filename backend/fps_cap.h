/* The most the host is ever told about the headset's refresh rate. GPLv3.
 *
 * The Steam Frame's display runs at 72, 90, 120 or 144 Hz, one rate per app: SteamVR's per-app
 * setting preferredRefreshRate picks it, and the OpenXR runtime then offers that one rate. A stream
 * request above the rate the display is on (a Moonlight setting picked on a flat screen, a 165 or
 * 360 Hz gamescope output) makes a Windows Sunshine host build a virtual display at a rate the
 * headset does not show. The launch URL's mode, the STREAM_CONFIGURATION's fps and
 * clientRefreshRateX100 all go through this one cap.
 * Native (lane refresh): the ceiling was 90; it is now 144, and the cap is the display's own rate.
 * Header only, so the VR client, the stream code and the tests share it. */
#ifndef MLXR_FPS_CAP_H
#define MLXR_FPS_CAP_H

#define MLXR_FPS_CAP 144        /* the ceiling, whatever the display reports */
#define MLXR_FPS_CAP_FLOOR 30   /* a reported display rate below this is not believed */
/* Native (lane refresh): the cap while SteamVR has not said what the display is on: the rate
 * packaging/install.sh sets for the app, so a 120 or 144 fps stream only starts on a display
 * known to run at that rate */
#define MLXR_FPS_CAP_UNKNOWN 90

/* The highest fps to ask the host for, given the rate SteamVR reports for the headset's display
 * (0 or less when not known yet): that rate up to the 144 ceiling (72, 90, 120 or 144 on the
 * Frame), 144 for a faster display, 90 while it is not known. */
static inline int mlxr_fps_cap_for_display(int display_hz)
{
    if (display_hz < MLXR_FPS_CAP_FLOOR) return MLXR_FPS_CAP_UNKNOWN;
    return display_hz < MLXR_FPS_CAP ? display_hz : MLXR_FPS_CAP;
}

/* fps limited to cap (the cap itself limited to the ceiling; cap <= 0 means the ceiling). */
static inline int mlxr_fps_clamp(int fps, int cap)
{
    if (cap <= 0 || cap > MLXR_FPS_CAP) cap = MLXR_FPS_CAP;
    return fps > cap ? cap : fps;
}

/* Native (lane artemis-settings): the fps to ask for. match (the flat page's "Match headset",
 * --fps match): the display's own rate under the ceiling; else the requested rate, capped. */
static inline int mlxr_fps_for_request(int requested, int display_hz, int match)
{
    int cap = mlxr_fps_cap_for_display(display_hz);
    if (match || requested <= 0) return cap;
    return requested > cap ? cap : requested;
}

#endif
