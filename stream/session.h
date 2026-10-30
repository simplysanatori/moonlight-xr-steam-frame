// Moonlight XR Native: one streaming session over moonlight-common-c. GPLv3.
#pragma once
#include <Limelight.h>
#include <stdint.h>

#include "frame_slot.h"

typedef struct StreamOptions {
    int width, height, fps;
    int bitrateKbps;
    int allowHevc;          // offer HEVC 8-bit as well as H.264
    int audioConfiguration; // AUDIO_CONFIGURATION_* (stereo for now)
    int remote;             // STREAM_CFG_LOCAL / REMOTE / AUTO
} StreamOptions;

// Fills a STREAM_CONFIGURATION, including a fresh remote-input AES key and IV
// (the launch request sends them to the host as rikey / rikeyid).
void stream_make_config(const StreamOptions* opt, STREAM_CONFIGURATION* cfg);

typedef struct StreamHost {
    const char* address;
    const char* appVersion;   // serverinfo appversion
    const char* gfeVersion;   // may be NULL
    const char* rtspUrl;      // from the launch/resume response, may be NULL
    int codecModeSupport;     // serverinfo ServerCodecModeSupport
} StreamHost;

// Starts the stream; decoded video goes to `video`. audio may be NULL (no sound).
// Returns 0 on success.
int stream_start(const StreamHost* host, STREAM_CONFIGURATION* cfg, FrameSlot* video,
                 PAUDIO_RENDERER_CALLBACKS audio, void* audioContext);
void stream_stop(void);
// Hardware (V4L2) decode on by default; off = FFmpeg software decode.
void stream_set_hw_decode(int on);
// Closes the hardware decoder; call after xr_app_run returned and after stream_stop.
void stream_close_video(void);
int stream_running(void);   // 1 while connected
int stream_failed(void);    // 1 once the connection has terminated or failed

// Input from the frame loop (no-ops when not connected).
void stream_pointer(int x, int y, int w, int h);
void stream_button(int button, int down);
void stream_scroll(int amount);
void stream_hscroll(int amount); // high-res horizontal wheel, 120 per notch
// Relative mouse motion (head aim)
void stream_mouse_move(int dx, int dy);
void stream_stats(char* buf, int len);
// Native (lane av-latency): per-window video latency parts the host and the
// network add (host encode as Sunshine reports it, time to receive a frame,
// wait in common-c's queue) and the RTT variance; resets the window.
void stream_latency_line(char* buf, int len);
// Native (lane netpath-quality): the figures of the stream so far (all windows): frames received and
// lost on the network, arrival jitter and the last RTT. Call before stream_stop. 0 when no frame was seen.
typedef struct StreamNetFigures {
    uint64_t framesReceived, framesLost;
    double jitterMs;
    uint32_t rttMs;
} StreamNetFigures;
int stream_network_figures(StreamNetFigures* out);
// Native (lane wave3-hud): the stream's running totals for the in-headset HUD and the slow connection badge
// (xr_hud.h). Unlike stream_latency_line it resets nothing, so any number of readers can take windows of it.
// 1 with *out filled while connected, else 0 (udpDrops is left for the caller: it is the kernel's counter).
struct MlxrHudCum;
int stream_hud(struct MlxrHudCum* out);
