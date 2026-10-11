/* The most the host is ever told about the headset's refresh rate. GPLv3.
 *
 * The Steam Frame's display runs at 72 or 90 Hz at most. A stream request for more (a Moonlight
 * setting picked on a flat screen, a 144 or 360 Hz gamescope output) makes a Windows Sunshine
 * host build a virtual display at a rate the headset cannot show. The launch URL's mode, the
 * STREAM_CONFIGURATION's fps and clientRefreshRateX100 all go through this one cap.
 * Header only, so the VR client, the stream code and the tests share it. */
#ifndef MLXR_FPS_CAP_H
#define MLXR_FPS_CAP_H

#define MLXR_FPS_CAP 90         /* the ceiling, whatever the display reports */
#define MLXR_FPS_CAP_FLOOR 30   /* a reported display rate below this is not believed */

/* The highest fps to ask the host for, given the rate SteamVR reports for the headset's display
 * (0 or less when not known yet): that rate when it is below the ceiling (72 on a 72 Hz display),
 * else 90. */
static inline int mlxr_fps_cap_for_display(int display_hz)
{
    if (display_hz >= MLXR_FPS_CAP_FLOOR && display_hz < MLXR_FPS_CAP) return display_hz;
    return MLXR_FPS_CAP;
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
