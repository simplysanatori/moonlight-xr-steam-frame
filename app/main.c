// Moonlight XR Native: entry point and the launcher's network side. GPLv3.
//
//   moonlight-xr                                     the PC list, then the app list, in the headset
//   moonlight-xr --host NAME|ADDR [--app NAME|ID]    straight into that app (Desktop by default)
//                [--mode WxHxFPS] [--bitrate KBPS] [--h264] [--wake] [--sw-decode] [--seconds N]
//   moonlight-xr --file clip.mp4 [--seconds N] [--sw-decode]   local test video, no host
//
// Hand-off from the flat launcher (launcher/, a patched moonlight-qt) adds:
//   --exit-to-caller   the stream's end (the bar's exit, SteamVR quit) ends the host's
//                      session and then the process, instead of showing the VR app list.
//                      Exit code 0 is normal; otherwise the last stderr line starting
//                      "MLXR_ERROR: " is the message (codes below, EXIT_*). 11 = left with
//                      the disconnect chord (host session kept): not an error.
//   --packet-size N    video packet size (bytes, local network), 0/absent = 1392
//   --audio stereo|51|71   the audio channel layout to ask the host for
//
// Hosts and the client identity live in ~/.config/moonlight-xr (pair with
// mlxr-cli, or import Moonlight Qt's pairing with backend/seed-from-moonlight-qt.sh);
// the settings in ~/.config/moonlight-xr/config.ini; the log in
// ~/Games/MoonlightXRNative/moonlight.log.
//
// One OpenXR session for the life of the app (xr_session.c). The frame loop
// presses the lists and the bar's exit; a worker thread here does what that
// asks of the network (serverinfo, applist, launch, quit) and starts and
// stops the stream. Leaving a stream ends the host's session (the owner's
// rule) and comes back to the app list; quitting the app does the same first.
#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../audio/opus_out.h"
#include "../backend/backend.h"
#include "../backend/backend_limelight.h"
#include "../stream/evdev_keyboard.h"
#include "../input/gamepads.h"
#include "../stream/file_source.h"
#include "../stream/frame_slot.h"
#include "../stream/hw_video.h"
#include "../stream/session.h"
#include "../stream/vr_keyboard.h"
#include "../xr/mlxr_log.h"
#include "../xr/xr_pad_bridge.h"
#include "../xr/xr_renderer.h"
#include "../xr/xr_session.h"
#include "launcher.h"
#include "panels.h"
#include "settings.h"

static FrameSlot g_slot;
static Settings g_settings;
static atomic_int g_streaming;
static int g_fileHw;
static int g_focused;
static VrKeyboard g_vrkb;

// ---- --exit-to-caller: how the process ends for the flat launcher ----
// The exit codes are the hand-off contract (launcher/REPORT.md).
enum {
    EXIT_OK = 0,
    EXIT_FAILED = 1,         // the VR session could not start, or an unclassified failure
    EXIT_USAGE = 2,          // bad command line
    EXIT_HOST_UNREACHABLE = 3,
    EXIT_NO_HOST = 4,        // no such host in hosts.ini, or not paired here
    EXIT_NO_APP = 5,
    EXIT_LAUNCH_FAILED = 6,  // the host refused the launch/resume, or the stream would not connect
    EXIT_STREAM_ENDED = 10,  // the stream ended by itself (the host closed it, or the network dropped)
    EXIT_DISCONNECTED = 11   // left with the disconnect chord: not an error, the host's session still runs
};
static atomic_int g_exitCode;
static char g_exitMsg[256];
static atomic_int g_exitSet;
static pthread_mutex_t g_exitLock = PTHREAD_MUTEX_INITIALIZER;
static char g_lastError[256]; // the worker's latest launch failure, for the message

// The launcher and the worker's wake-up, under one lock
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wake = PTHREAD_COND_INITIALIZER;
static Launcher g_launch;
static atomic_int g_workerQuit;

// ---- command line choices that override config.ini for this run ----
static struct {
    const char* file;
    const char* host;
    const char* app;
    const char* mode;
    int bitrate, h264Only, wake, swDecode;
    int exitToCaller, packetSize, audio; // audio: 0 stereo (default), 1 5.1, 2 7.1
    double seconds;
} g_args;

// With --exit-to-caller: remember why the process ends (the first reason wins) and
// make the frame loop leave. The message goes to stderr at exit as "MLXR_ERROR: ...".
static void endForCaller(int code, const char* fmt, ...) {
    pthread_mutex_lock(&g_exitLock);
    if (!atomic_load(&g_exitSet)) {
        atomic_store(&g_exitSet, 1);
        atomic_store(&g_exitCode, code);
        if (code != EXIT_OK && fmt) {
            va_list ap;
            va_start(ap, fmt);
            vsnprintf(g_exitMsg, sizeof(g_exitMsg), fmt, ap);
            va_end(ap);
            MLOG_E("exit to caller (%d): %s", code, g_exitMsg);
        }
    }
    pthread_mutex_unlock(&g_exitLock);
    xr_app_request_quit();
}

static void setLastError(const char* msg) {
    snprintf(g_lastError, sizeof(g_lastError), "%s", msg ? msg : "");
}

// ---- the input the frame loop hands over -------------------------------------------

static void updateKeyboard(void) {
    int active = atomic_load(&g_streaming) && g_focused && stream_running();
    evkbd_set_active(active);
    gamepadsSetActive(active); // Native: physical gamepads follow the keyboard's gating
}

static void statsHook(void* user, char* buf, int len) {
    (void)user;
    char s[200] = "", a[128] = "";
    if (g_fileHw && hw_video_current()) {
        HwVideoStats h;
        hw_video_stats(hw_video_current(), &h);
        snprintf(s, sizeof(s), "hw decode %.1f ms (last %.1f), frames %llu, taken %llu, skipped %llu, errors %llu",
                 h.avgDecodeMs, h.lastDecodeMs, (unsigned long long)h.frames, (unsigned long long)h.taken,
                 (unsigned long long)h.skipped, (unsigned long long)h.errors);
    }
    if (atomic_load(&g_streaming) && !g_args.file) {
        stream_stats(s, sizeof(s));
        MlxrOpusOutStats st;
        mlxr_opus_out_stats(&st);
        snprintf(a, sizeof(a), "audio %dch %.0f ms buffered, %u underruns, %u lost (stream totals)", st.channels, st.bufferedMs,
                 st.underruns, st.lostPackets);
    }
    snprintf(buf, (size_t)len, "decoded %llu, dropped %llu, decode %.1f ms; %s%s%s",
             (unsigned long long)g_slot.produced, (unsigned long long)g_slot.dropped, g_slot.lastDecodeMs, s,
             a[0] ? "; " : "", a);
}

// Native (lane av-latency): every 5 s from the log while streaming. What the
// host and network add to the picture (and so to the host cursor), and the
// audio per window: underruns split by cause (late data vs the host going
// quiet), the cushion, the longest gap between packets, and how long the
// audio receive thread waited for a CPU (starvation) vs network gaps
static void latencyHook(void* user, char* buf, int len) {
    (void)user;
    buf[0] = 0;
    if (g_args.file || !atomic_load(&g_streaming)) return;
    char v[200];
    stream_latency_line(v, sizeof(v));
    static MlxrOpusOutStats prev;
    static int64_t prevNs;
    static int saidPriority;
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    if (st.packets < prev.packets || st.underruns < prev.underruns || st.threadCpuNs < prev.threadCpuNs) {
        memset(&prev, 0, sizeof(prev)); // a new stream
        saidPriority = 0;
    }
    double secs = prevNs > 0 ? (now - prevNs) / 1e9 : 5.0;
    if (!saidPriority && st.threadPriority[0]) {
        MLOG_I("audio: receive thread %s", st.threadPriority);
        saidPriority = 1;
    }
    double waitMs = (st.threadWaitNs - prev.threadWaitNs) / 1e6;
    // The host's audio clock against ours, from every packet since the first
    // (a steady excess shows as regular trims; host silences skew it low)
    double hostPpm = 0.0;
    if (st.packets > 1 && st.lastPacketNs > st.firstPacketNs + 10000000000LL && st.samplesPerFrame > 0) {
        double sent = (double)(st.packets - 1) * st.samplesPerFrame / 48000.0;
        hostPpm = (sent / ((st.lastPacketNs - st.firstPacketNs) / 1e9) - 1.0) * 1e6;
    }
    double sinkHz = secs > 0.0 ? (st.sinkFrames - prev.sinkFrames) / secs : 0.0;
    double cpuPct = secs > 0.0 ? (st.threadCpuNs - prev.threadCpuNs) / 1e7 / secs : 0.0;
    snprintf(buf, (size_t)len,
             "%s; audio cushion %.0f ms, %.0f ms buffered, underruns +%u (%u late of which %u outages, %u after "
             "silence), lost +%u, dropped %.0f ms, trimmed %.0f ms, longest packet gap %d ms, sink took %.0f Hz, "
             "host clock %+.0f ppm, receive thread waited %.1f ms for a CPU (%.1f%% busy), decode %.2f ms avg "
             "(max %.2f)",
             v, st.jitterTargetMs, st.bufferedMs, st.underruns - prev.underruns,
             st.lateUnderruns - prev.lateUnderruns, st.outages - prev.outages,
             st.silenceUnderruns - prev.silenceUnderruns,
             st.lostPackets - prev.lostPackets, (st.droppedFrames - prev.droppedFrames) / 48.0,
             (st.trimmedFrames - prev.trimmedFrames) / 48.0, mlxr_opus_out_take_gap_max(), sinkHz, hostPpm, waitMs,
             cpuPct, st.processUs / 1000.0,
             st.processMaxUs / 1000.0);
    prev = st;
    prevNs = now;
}

static void pointerHook(void* user, int x, int y, int w, int h) {
    (void)user;
    stream_pointer(x, y, w, h);
    updateKeyboard(); // picks up the moment the stream connects
}

static void mouseMoveHook(void* user, int dx, int dy) {
    (void)user;
    stream_mouse_move(dx, dy);
}

static void buttonHook(void* user, int button, int down) {
    (void)user;
    if (atomic_load(&g_streaming)) stream_button(button, down);
    else MLOG_I("button %d %s", button, down ? "down" : "up");
}

static void scrollHook(void* user, int amount) {
    (void)user;
    stream_scroll(amount);
}

static void focusHook(void* user, int focused) {
    (void)user;
    g_focused = focused;
    updateKeyboard();
}

// The on-screen keyboard (VrKeyboard): keys and text to the host
// Windows virtual keys that are always extended keys (E0 scan codes): the
// arrows and navigation cluster (distinct from the numpad's VKs), right
// Ctrl/Alt, Win, Menu, PrtSc. Hosts tell them from numpad keys by this flag.
static int vkExtended(int vk) {
    switch (vk) {
    case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: // PgUp..Down
    case 0x2C: case 0x2D: case 0x2E:                                                         // PrtSc Ins Del
    case 0x5B: case 0x5C: case 0x5D:                                                         // Win Win Menu
    case 0xA3: case 0xA5:                                                                    // RCtrl RAlt
    case 0x6F: case 0x90:                                                                    // Divide NumLock
        return 1;
    default:
        return 0;
    }
}

static void vrkbKey(void* user, short keyCode, char action, char modifiers) {
    (void)user;
    if (vkExtended((unsigned short)keyCode & 0xff)) modifiers |= MODIFIER_EXTENDED;
    if (stream_running()) LiSendKeyboardEvent((short)(0x8000 | (unsigned short)keyCode), action, modifiers);
}

static void vrkbText(void* user, const char* utf8) {
    (void)user;
    if (stream_running()) LiSendUtf8TextEvent(utf8, (unsigned)strlen(utf8));
}

static void keyHook(void* user, int code, int mods) {
    (void)user;
    vrkb_type(&g_vrkb, code, mods);
}

static void modifiersHook(void* user, int mods) {
    (void)user;
    vrkb_hold(&g_vrkb, mods);
}

// The evdev keyboard (a real keyboard in the room)
static void sendKey(short vk, int down, char modifiers, void* user) {
    (void)user;
    if (stream_running())
        LiSendKeyboardEvent((short)(0x8000 | vk), down ? KEY_ACTION_DOWN : KEY_ACTION_UP, modifiers);
}

// Every frame: the lists' presses and the exit to the launcher, its view back
static void uiHook(void* user, const XrUiEvents* ev, XrUiLauncher* show) {
    (void)user;
    pthread_mutex_lock(&g_lock);
    int poke = 0;
    if (ev->exitConfirmed) {
        MLOG_I("exit confirmed in %s", launcher_state_name(g_launch.state));
        launcher_exit(&g_launch);
        poke = 1;
    }
    if (ev->listKind != 0) {
        launcher_press(&g_launch, ev->listKind, ev->listA, ev->listB);
        poke = 1;
    }
    if (g_launch.notice[0] != '\0') {
        xr_app_notice(g_launch.notice);
        g_launch.notice[0] = '\0';
    }
    // Started by the flat Moonlight UI: it shows "Starting <app>..." itself and hides its
    // window when told the headset has the picture, so nothing here shows the lists or
    // their loading state
    static int saidStreaming;
    if (g_args.exitToCaller && ev->pictureShown && !saidStreaming) {
        saidStreaming = 1;
        MLOG_I("picture up in the headset: the flat UI hides its window");
        printf("MLXR_STREAMING\n");
        fflush(stdout);
    }
    LaunchView v;
    launcher_view(&g_launch, &v);
    if (g_args.exitToCaller) v.mode = 0;
    show->mode = v.mode;
    show->pcs = v.pcs;
    show->pcCount = v.pcCount;
    show->pcName = v.pcName;
    show->apps = v.apps;
    show->appCount = v.appCount;
    show->page = v.page;
    show->loading = v.loading;
    show->failed = v.failed;
    if (poke || g_launch.ncmd > 0) pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}

// ---- the network side (the worker) ------------------------------------------------

static void backendLog(int level, const char* msg, void* ud) {
    (void)ud;
    if (level <= 1) mlxr_logf(level == 0 ? 'E' : 'I', "backend: %s", msg);
}

typedef struct Net {
    char* dir;
    mlxr_identity id;
    int haveId;
    mlxr_hosts hosts;
    mlxr_server* srv[LAUNCH_MAX_PCS];
    mlxr_computer comp[LAUNCH_MAX_PCS];
    char address[LAUNCH_MAX_PCS][128];
    int streamPc;
    char* rtsp;
} Net;

static Net g_net;

// Wake-on-LAN magic packet to the LAN broadcast address.
static void wake(const mlxr_host_record* h) {
    if (!h->has_mac) {
        MLOG_W("wake: no MAC stored for %s", h->name);
        return;
    }
    uint8_t pkt[102];
    memset(pkt, 0xff, 6);
    for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, h->mac, 6);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    struct sockaddr_in a = { 0 };
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    for (int port = 7; port <= 9; port += 2) {
        a.sin_port = htons((uint16_t)port);
        sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr*)&a, sizeof(a));
    }
    close(s);
    MLOG_I("wake: magic packet sent to %s", h->name);
}

// Tries the stored address, then the extra ones; keeps the server on the first that answers.
static mlxr_server* connectHost(Net* n, int i, const char* override) {
    mlxr_host_record* rec = &n->hosts.v[i];
    char cands[8][128];
    int nc = 0;
    if (override) snprintf(cands[nc++], 128, "%s", override);
    if (rec->address[0]) snprintf(cands[nc++], 128, "%s", rec->address);
    char extra[256];
    snprintf(extra, sizeof(extra), "%s", rec->extra_addresses);
    char* save = NULL;
    for (char* t = strtok_r(extra, ",", &save); t && nc < 8; t = strtok_r(NULL, ",", &save))
        snprintf(cands[nc++], 128, "%s", t);
    char* pem = NULL;
    mlxr_error err = { 0 };
    if (mlxr_host_cert_load(n->dir, rec, &pem, &err) != MLXR_OK) {
        MLOG_W("%s is not paired here (%s): pair with mlxr-cli pair", rec->name, err.msg);
        return NULL;
    }
    for (int c = 0; c < nc; c++) {
        mlxr_server* s = mlxr_server_new(&n->id, cands[c], rec->http_port ? rec->http_port : MLXR_DEFAULT_HTTP_PORT);
        mlxr_server_set_https_port(s, rec->https_port ? rec->https_port : MLXR_DEFAULT_HTTPS_PORT);
        mlxr_server_set_pinned_cert(s, pem, &err);
        if (mlxr_server_get_info(s, true, &n->comp[i], NULL, &err) == MLXR_OK) {
            MLOG_I("host %s answers at %s: %s, app version %s, codecs 0x%x, %s, current game %d", rec->name,
                   cands[c], n->comp[i].state, n->comp[i].app_version, n->comp[i].server_codec_mode_support,
                   n->comp[i].paired ? "paired" : "NOT paired", n->comp[i].current_game);
            snprintf(n->address[i], sizeof(n->address[i]), "%s", cands[c]);
            free(pem);
            if (n->srv[i]) mlxr_server_free(n->srv[i]);
            n->srv[i] = s;
            return s;
        }
        MLOG_W("host %s not reachable at %s: %s", rec->name, cands[c], err.msg);
        mlxr_server_free(s);
    }
    free(pem);
    return NULL;
}

static int hasCert(Net* n, int i) {
    char* pem = NULL;
    mlxr_error err = { 0 };
    int ok = mlxr_host_cert_load(n->dir, &n->hosts.v[i], &pem, &err) == MLXR_OK;
    free(pem);
    return ok;
}

static void loadHosts(Net* n) {
    mlxr_error err = { 0 };
    mlxr_hosts_load(n->dir, &n->hosts, &err);
    LaunchPc pcs[LAUNCH_MAX_PCS];
    int count = (int)n->hosts.n < LAUNCH_MAX_PCS ? (int)n->hosts.n : LAUNCH_MAX_PCS;
    for (int i = 0; i < count; i++) {
        memset(&pcs[i], 0, sizeof(pcs[i]));
        snprintf(pcs[i].name, sizeof(pcs[i].name), "%s", n->hosts.v[i].name);
        pcs[i].online = UI_PC_REFRESHING;
        pcs[i].paired = hasCert(n, i);
        pcs[i].canWake = n->hosts.v[i].has_mac;
    }
    pthread_mutex_lock(&g_lock);
    launcher_hosts(&g_launch, pcs, count);
    pthread_mutex_unlock(&g_lock);
    MLOG_I("launcher: %d computers in hosts.ini", count);
}

static void refreshPcs(Net* n) {
    int count = (int)n->hosts.n < LAUNCH_MAX_PCS ? (int)n->hosts.n : LAUNCH_MAX_PCS;
    for (int i = 0; i < count && !atomic_load(&g_workerQuit); i++) {
        mlxr_server* s = hasCert(n, i) ? connectHost(n, i, NULL) : NULL;
        pthread_mutex_lock(&g_lock);
        launcher_pc_status(&g_launch, i, s ? UI_PC_ONLINE : UI_PC_OFFLINE, hasCert(n, i) && (!s || n->comp[i].paired),
                           s ? n->comp[i].current_game : 0);
        pthread_mutex_unlock(&g_lock);
    }
}

static int fetchApps(Net* n, int pc, LaunchApp* out, int cap) {
    if (pc < 0 || pc >= (int)n->hosts.n) return -1;
    mlxr_server* s = n->srv[pc] ? n->srv[pc] : connectHost(n, pc, NULL);
    mlxr_app* apps = NULL;
    size_t na = 0;
    mlxr_error err = { 0 };
    if (!s || mlxr_server_get_apps(s, &apps, &na, &err) != MLXR_OK) {
        // The server may have gone stale: connect again once
        s = connectHost(n, pc, NULL);
        if (!s || mlxr_server_get_apps(s, &apps, &na, &err) != MLXR_OK) {
            MLOG_W("app list of %s: %s", n->hosts.v[pc].name, err.msg);
            return -1;
        }
    }
    int count = (int)na < cap ? (int)na : cap;
    for (int i = 0; i < count; i++) {
        snprintf(out[i].name, sizeof(out[i].name), "%s", apps[i].name ? apps[i].name : "");
        out[i].id = apps[i].id;
    }
    mlxr_apps_free(apps, na);
    // The host's current game, for the Running pill
    mlxr_computer c;
    if (mlxr_server_get_info(s, true, &c, NULL, &err) == MLXR_OK) n->comp[pc] = c;
    MLOG_I("app list of %s: %d apps, current game %d", n->hosts.v[pc].name, count, n->comp[pc].current_game);
    return count;
}

typedef struct StreamJob {
    STREAM_CONFIGURATION cfg;
    AUDIO_RENDERER_CALLBACKS audio;
    MlxrOpusOutConfig audioCfg; // read by the audio init callback; lives as long as the job
    SERVER_INFORMATION si;
} StreamJob;
static StreamJob g_job;

static int launchStream(Net* n, int pc, int appId, int quitFirst) {
    mlxr_error err = { 0 };
    mlxr_server* s = n->srv[pc] ? n->srv[pc] : connectHost(n, pc, NULL);
    if (!s || mlxr_server_get_info(s, true, &n->comp[pc], NULL, &err) != MLXR_OK) {
        s = connectHost(n, pc, NULL);
        if (!s) {
            setLastError("The host stopped answering before the stream could start");
            return -1;
        }
    }
    mlxr_computer* comp = &n->comp[pc];
    if (quitFirst && comp->current_game != 0 && comp->current_game != appId) {
        MLOG_I("ending app %d on %s before starting %d", comp->current_game, n->hosts.v[pc].name, appId);
        mlxr_server_quit_app(s, &err);
        mlxr_server_get_info(s, true, comp, NULL, &err);
    }
    mlxr_launch_params lp;
    mlxr_launch_params_init_ll(&lp);
    int w = 0, h = 0, fps = 0;
    settings_resolution(&g_settings, &w, &h);
    fps = settings_fps_value(&g_settings);
    if (g_args.mode && mlxr_parse_mode(g_args.mode, &w, &h, &fps) != MLXR_OK) {
        MLOG_W("bad --mode %s, using config.ini's", g_args.mode);
        settings_resolution(&g_settings, &w, &h);
    }
    lp.width = w;
    lp.height = h;
    lp.fps = fps;
    lp.audio_configuration = g_args.audio == 2 ? MLXR_AUDIO_71 : g_args.audio == 1 ? MLXR_AUDIO_51 : MLXR_AUDIO_STEREO;
    lp.is_gfe = comp->is_nvidia;
    free(n->rtsp);
    n->rtsp = NULL;
    if (mlxr_server_launch_or_resume(s, comp, appId, &lp, &n->rtsp, &err) != MLXR_OK) {
        MLOG_E("launch: %s", err.msg);
        char why[200];
        snprintf(why, sizeof(why), "The host refused to start the stream: %s", err.msg);
        setLastError(why);
        return -1;
    }
    MLOG_I("%s app %d on %s, rtsp %s", comp->current_game ? "resumed" : "launched", appId, n->hosts.v[pc].name,
           n->rtsp ? n->rtsp : "(none)");
    memset(&g_job, 0, sizeof(g_job));
    mlxr_fill_server_info(&g_job.si, n->address[pc], comp, n->rtsp);
    StreamHost host = { g_job.si.address, g_job.si.serverInfoAppVersion, g_job.si.serverInfoGfeVersion,
                        g_job.si.rtspSessionUrl, g_job.si.serverCodecModeSupport };
    mlxr_fill_stream_config(&g_job.cfg, &lp);
    int bitrate = g_args.bitrate > 0 ? g_args.bitrate : settings_default_bitrate_kbps(w, h, fps);
    if (!g_args.bitrate && g_settings.bitrate_kbps > 0) bitrate = settings_bitrate_kbps(&g_settings);
    g_job.cfg.bitrate = bitrate;
    g_job.cfg.packetSize = 1392;
    g_job.cfg.streamingRemotely = STREAM_CFG_AUTO;
    if (g_args.packetSize > 0) {
        // Moonlight's custom packet size: it also means "treat the host as local"
        g_job.cfg.packetSize = g_args.packetSize;
        g_job.cfg.streamingRemotely = STREAM_CFG_LOCAL;
    }
    int h264Only = g_args.h264Only || g_settings.codec == 3;
    g_job.cfg.supportedVideoFormats = VIDEO_FORMAT_H264 | (h264Only ? 0 : VIDEO_FORMAT_H265);
    g_job.cfg.clientRefreshRateX100 = lp.fps * 100;
    g_job.cfg.colorSpace = COLORSPACE_REC_709;
    g_job.cfg.colorRange = COLOR_RANGE_LIMITED;
    g_job.cfg.encryptionFlags = ENCFLG_ALL;
    mlxr_opus_out_callbacks(&g_job.audio);
    mlxr_opus_out_config_default(&g_job.audioCfg);
    g_job.audioCfg.hrtfDir = access("assets/hrtf/kemar", R_OK) == 0 ? "assets/hrtf/kemar"
                                                                     : MLXR_SOURCE_ASSETS "/hrtf/kemar";
    g_job.audioCfg.surround = g_settings.virtual_surround != 0;
    stream_set_hw_decode(!g_args.swDecode);
    n->streamPc = pc;
    MLOG_I("stream: %dx%d %d fps, %d kbps, %s", w, h, fps, bitrate, h264Only ? "H.264" : "H.264/HEVC");
    if (stream_start(&host, &g_job.cfg, &g_slot, &g_job.audio, &g_job.audioCfg) != 0) {
        stream_stop();
        xr_app_release_video();
        stream_close_video();
        setLastError("The stream could not connect (check the host's firewall and ports)");
        return -1;
    }
    atomic_store(&g_streaming, 1);
    xr_app_set_streaming(1);
    updateKeyboard();
    return 0;
}

// The stream down: input let go, the decoder closed after the renderer let go
// of its frames, and the host's session ended when asked
static void stopStream(Net* n, int quitHost) {
    if (!atomic_load(&g_streaming)) return;
    MLOG_I("stopping stream%s", quitHost ? " and ending the host's session" : "");
    vrkb_release_all(&g_vrkb);
    atomic_store(&g_streaming, 0);
    xr_app_set_streaming(0);
    updateKeyboard();
    stream_stop();
    xr_app_release_video();
    stream_close_video();
    int pc = n->streamPc;
    if (quitHost && pc >= 0 && n->srv[pc]) {
        // Leaving ends the session on the host too (Moonlight's "Quit session"),
        // so the game or desktop stream does not keep running there
        mlxr_error qerr = { 0 };
        mlxr_status qs = mlxr_server_quit_app(n->srv[pc], &qerr);
        if (qs == MLXR_OK) MLOG_I("host session ended on %s", n->hosts.v[pc].name);
        else MLOG_W("could not end the host session: %s (%s)", mlxr_status_name(qs), qerr.msg);
    }
}

// --host/--app: the PC and app the command line named
static void autostart(Net* n) {
    int pc = -1;
    for (size_t i = 0; i < n->hosts.n && pc < 0; i++) {
        mlxr_host_record* r = &n->hosts.v[i];
        if (!strcasecmp(g_args.host, r->name) || !strcasecmp(g_args.host, r->uuid) || !strcmp(g_args.host, r->address))
            pc = (int)i;
    }
    const char* override = NULL;
    if (pc < 0 && n->hosts.n > 0 && !g_args.exitToCaller) {
        pc = 0; // an address: the first paired host, reached there
        override = g_args.host;
    }
    if (pc < 0) {
        MLOG_E("no paired host matching %s in %s/hosts.ini", g_args.host, n->dir);
        xr_app_notice("No paired host: pair with mlxr-cli");
        if (g_args.exitToCaller) endForCaller(EXIT_NO_HOST, "No paired host matches %s", g_args.host);
        return;
    }
    if (g_args.exitToCaller && !hasCert(n, pc)) {
        endForCaller(EXIT_NO_HOST, "%s is not paired with the VR client (no server certificate)", n->hosts.v[pc].name);
        return;
    }
    if (g_args.wake) {
        wake(&n->hosts.v[pc]);
        sleep(1);
    }
    mlxr_server* s = NULL;
    for (int attempt = 0; !s && attempt < (g_args.wake ? 12 : 1) && !atomic_load(&g_workerQuit); attempt++) {
        if (attempt) sleep(5);
        s = connectHost(n, pc, override);
    }
    if (!s) {
        MLOG_E("host %s is unreachable (is the PC on? try --wake)", n->hosts.v[pc].name);
        pthread_mutex_lock(&g_lock);
        launcher_pc_status(&g_launch, pc, UI_PC_OFFLINE, 1, 0);
        pthread_mutex_unlock(&g_lock);
        if (g_args.exitToCaller) endForCaller(EXIT_HOST_UNREACHABLE, "%s is unreachable (is the PC on?)", n->hosts.v[pc].name);
        return;
    }
    LaunchApp apps[LAUNCH_MAX_APPS];
    int na = fetchApps(n, pc, apps, LAUNCH_MAX_APPS);
    const char* want = g_args.app ? g_args.app : "Desktop";
    char* end = NULL;
    long asNum = strtol(want, &end, 10);
    int appId = -1;
    for (int i = 0; i < na && appId < 0; i++)
        if ((end && !*end && apps[i].id == asNum) || !strcasecmp(apps[i].name, want)) appId = apps[i].id;
    pthread_mutex_lock(&g_lock);
    launcher_pc_status(&g_launch, pc, UI_PC_ONLINE, 1, n->comp[pc].current_game);
    g_launch.pc = pc;
    launcher_apps(&g_launch, pc, apps, na < 0 ? 0 : na, na >= 0);
    if (appId >= 0) {
        MLOG_I("app \"%s\" id %d", want, appId);
        launcher_autostart(&g_launch, pc, appId);
    }
    else {
        MLOG_E("no app \"%s\" on %s", want, n->hosts.v[pc].name);
        g_launch.state = LAUNCH_APPS;
    }
    pthread_mutex_unlock(&g_lock);
    if (appId < 0 && g_args.exitToCaller) {
        if (na < 0) endForCaller(EXIT_FAILED, "Could not get the app list of %s", n->hosts.v[pc].name);
        else endForCaller(EXIT_NO_APP, "%s has no app \"%s\"", n->hosts.v[pc].name, want);
    }
}

static void* worker(void* arg) {
    (void)arg;
    Net* n = &g_net;
    n->streamPc = -1;
    if (!g_args.file) {
        mlxr_set_log(backendLog, NULL);
        mlxr_error err = { 0 };
        n->dir = mlxr_config_dir(&err);
        if (!n->dir || mlxr_identity_load_or_create(n->dir, &n->id, NULL, &err) != MLXR_OK) {
            MLOG_E("identity: %s", err.msg);
        }
        else {
            n->haveId = 1;
            loadHosts(n);
            if (g_args.host) autostart(n);
        }
    }
    int64_t lastRefresh = 0;
    while (!atomic_load(&g_workerQuit)) {
        LaunchCmd c;
        memset(&c, 0, sizeof(c));
        pthread_mutex_lock(&g_lock);
        if (!launcher_take_cmd(&g_launch, &c)) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
            pthread_cond_timedwait(&g_wake, &g_lock, &ts);
            if (!launcher_take_cmd(&g_launch, &c)) c.kind = LCMD_NONE;
        }
        LaunchState state = g_launch.state;
        pthread_mutex_unlock(&g_lock);
        // --exit-to-caller: a quit asked before the XR loop started is forgotten by it; ask again
        if (g_args.exitToCaller && atomic_load(&g_exitSet)) xr_app_request_quit();

        int64_t now = time(NULL);
        switch (c.kind) {
        case LCMD_REFRESH_PCS:
            if (n->haveId) refreshPcs(n);
            lastRefresh = now;
            break;
        case LCMD_FETCH_APPS: {
            static LaunchApp apps[LAUNCH_MAX_APPS];
            int na = n->haveId ? fetchApps(n, c.pc, apps, LAUNCH_MAX_APPS) : -1;
            pthread_mutex_lock(&g_lock);
            launcher_apps(&g_launch, c.pc, apps, na < 0 ? 0 : na, na >= 0);
            if (na >= 0) launcher_pc_status(&g_launch, c.pc, UI_PC_ONLINE, 1, n->comp[c.pc].current_game);
            pthread_mutex_unlock(&g_lock);
            break;
        }
        case LCMD_LAUNCH: {
            int ok = n->haveId && launchStream(n, c.pc, c.appId, c.quitFirst) == 0;
            pthread_mutex_lock(&g_lock);
            if (ok) launcher_stream_started(&g_launch);
            else launcher_stream_failed(&g_launch, NULL);
            pthread_mutex_unlock(&g_lock);
            if (!ok && g_args.exitToCaller)
                endForCaller(EXIT_LAUNCH_FAILED, "%s", g_lastError[0] ? g_lastError : "The stream could not start");
            break;
        }
        case LCMD_STOP:
            stopStream(n, c.quitHost);
            pthread_mutex_lock(&g_lock);
            launcher_stream_ended(&g_launch, 0);
            pthread_mutex_unlock(&g_lock);
            // The bar's exit (the host's session is ended) or the disconnect chord (it stays, so the
            // caller shows the app as running, as Moonlight does after a disconnect)
            if (g_args.exitToCaller) endForCaller(c.quitHost ? EXIT_OK : EXIT_DISCONNECTED, NULL);
            break;
        case LCMD_WAKE:
            if (c.pc >= 0 && c.pc < (int)n->hosts.n) {
                wake(&n->hosts.v[c.pc]);
                sleep(3);
                refreshPcs(n);
            }
            break;
        case LCMD_EXIT:
            stopStream(n, 1);
            xr_app_request_quit();
            break;
        default:
            break;
        }
        // The host ended the stream (or the network did)
        if (state == LAUNCH_STREAMING && atomic_load(&g_streaming) && stream_failed()) {
            stopStream(n, 0);
            pthread_mutex_lock(&g_lock);
            launcher_stream_ended(&g_launch, 1);
            pthread_mutex_unlock(&g_lock);
            if (g_args.exitToCaller)
                endForCaller(EXIT_STREAM_ENDED, "The stream ended: the host closed the session or the connection was lost");
        }
        // The PC list keeps its online states fresh
        if (state == LAUNCH_PCS && n->haveId && now - lastRefresh >= 10) {
            refreshPcs(n);
            lastRefresh = now;
        }
    }
    return NULL;
}

// Gamepad mode's Start+Select+L1+R1: leave the stream, the host's session stays
// (the bar's Exit ends it). Called by the pad bridge outside its lock.
static void padDisconnectHook(void* user) {
    (void)user;
    pthread_mutex_lock(&g_lock);
    MLOG_I("disconnect chord in %s: leaving the stream, the host's session stays", launcher_state_name(g_launch.state));
    launcher_disconnect(&g_launch);
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}

// A physical keyboard's Ctrl+Alt+Shift+Q, Moonlight's own: the same, from the
// keyboard thread
static void keyDisconnectHook(void* user) {
    (void)user;
    pthread_mutex_lock(&g_lock);
    MLOG_I("disconnect keys in %s: leaving the stream, the host's session stays", launcher_state_name(g_launch.state));
    launcher_disconnect_keys(&g_launch);
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}

// ---- main ---------------------------------------------------------------------------

static void openLog(void) {
    const char* dir = getenv("MLXR_LOG_DIR");
    char path[512];
    if (dir && *dir) snprintf(path, sizeof(path), "%s/moonlight.log", dir);
    else snprintf(path, sizeof(path), "%s/Games/MoonlightXRNative/moonlight.log", getenv("HOME") ? getenv("HOME") : ".");
    // The log level is config.ini's (off, basic = warnings, errors and the
    // session's story, verbose = everything)
    xrSetFileLog(path, g_settings.file_log);
}

int main(int argc, char** argv) {
    memset(&g_args, 0, sizeof(g_args));
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--file") && i + 1 < argc) g_args.file = argv[++i];
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) g_args.host = argv[++i];
        else if (!strcmp(argv[i], "--app") && i + 1 < argc) g_args.app = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) g_args.mode = argv[++i];
        else if (!strcmp(argv[i], "--bitrate") && i + 1 < argc) g_args.bitrate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) g_args.seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--h264")) g_args.h264Only = 1;
        else if (!strcmp(argv[i], "--wake")) g_args.wake = 1;
        else if (!strcmp(argv[i], "--sw-decode")) g_args.swDecode = 1;
        else if (!strcmp(argv[i], "--exit-to-caller")) g_args.exitToCaller = 1;
        else if (!strcmp(argv[i], "--packet-size") && i + 1 < argc) g_args.packetSize = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--audio") && i + 1 < argc) {
            const char* a = argv[++i];
            if (!strcmp(a, "stereo")) g_args.audio = 0;
            else if (!strcmp(a, "51") || !strcmp(a, "5.1")) g_args.audio = 1;
            else if (!strcmp(a, "71") || !strcmp(a, "7.1")) g_args.audio = 2;
            else {
                fprintf(stderr, "MLXR_ERROR: --audio takes stereo, 51 or 71\n");
                return EXIT_USAGE;
            }
        }
        else {
            fprintf(stderr,
                    "usage: %s                      PC list and app list in the headset\n"
                    "       %s --host NAME|ADDR [--app NAME|ID] [--mode WxHxFPS] [--bitrate KBPS] [--h264] [--wake] "
                    "[--sw-decode] [--seconds N]\n"
                    "                  [--exit-to-caller] [--packet-size BYTES] [--audio stereo|51|71]\n"
                    "       %s --file VIDEO [--sw-decode] [--seconds N]\n",
                    argv[0], argv[0], argv[0]);
            return EXIT_USAGE;
        }
    }
    if (g_args.app && !g_args.host) {
        fprintf(stderr, "MLXR_ERROR: --app needs --host\n");
        return EXIT_USAGE;
    }
    if (g_args.exitToCaller && (!g_args.host || g_args.file)) {
        fprintf(stderr, "MLXR_ERROR: --exit-to-caller needs --host (and no --file)\n");
        return EXIT_USAGE;
    }
    settings_defaults(&g_settings);
    SettingsLoadInfo info;
    int lr = settings_load(&g_settings, NULL, &info);
    openLog();
    char cfgPath[512];
    settings_default_path(cfgPath, sizeof(cfgPath));
    MLOG_I("== moonlight-xr starting; config %s: %s (%d set, %d unknown, %d unreadable)", cfgPath,
           lr == 0 ? "read" : lr == 1 ? "not there yet, defaults" : "read error", info.applied, info.unknown, info.bad);
    if (lr == 1) settings_save(&g_settings, NULL); // a file to edit, every setting commented with its default
    {
        char args[512] = "";
        for (int i = 1; i < argc; i++) {
            strncat(args, " ", sizeof(args) - strlen(args) - 1);
            strncat(args, argv[i], sizeof(args) - strlen(args) - 1);
        }
        MLOG_I("args:%s (SteamAppId %s)", args[0] ? args : " none", getenv("SteamAppId") ? getenv("SteamAppId") : "none");
    }
    // The panels' pictures (room thumbnails, the Ko-fi code, the gear) from the
    // installed tree when there is one, else the source tree's
    if (access("assets/images", R_OK) == 0) ui_panels_set_assets_dir("assets");

    frame_slot_init(&g_slot);
    VrKeyboardSink sink = { NULL, vrkbKey, vrkbText };
    vrkb_init(&g_vrkb, sink);
    xrPadBridgeSetDisconnectHook(padDisconnectHook, NULL);

    XrAppConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.video = &g_slot;
    cfg.settings = &g_settings;
    cfg.seconds = g_args.seconds;
    settings_resolution(&g_settings, &cfg.videoWidth, &cfg.videoHeight);
    cfg.fps = settings_fps_value(&g_settings);
    char reportDir[512];
    snprintf(reportDir, sizeof(reportDir), "%s/Games/MoonlightXRNative/reports", getenv("HOME") ? getenv("HOME") : ".");
    mkdir(reportDir, 0755);
    cfg.reportDir = reportDir;
    cfg.hooks = (XrAppHooks){ NULL, pointerHook, mouseMoveHook, buttonHook, scrollHook, keyHook, modifiersHook,
                              statsHook, focusHook, uiHook };
    cfg.hooks.latency = latencyHook;

    FileSource* src = NULL;
    if (g_args.file) {
        launcher_init_file(&g_launch);
        src = file_source_start(g_args.file, &g_slot, !g_args.swDecode);
        if (!src) return 1;
        g_fileHw = !g_args.swDecode;
        atomic_store(&g_streaming, 1);
        cfg.startStreaming = 1;
    }
    else {
        launcher_init(&g_launch);
        if (g_args.host) {
            g_launch.state = LAUNCH_STARTING; // the worker's autostart decides
            g_launch.ncmd = 0;
        }
        evkbd_set_disconnect_hook(keyDisconnectHook, NULL);
        evkbd_start(sendKey, NULL);
        // Native: physical gamepads (SDL2), Start+Select+L1+R1 leaves the stream like gamepad mode's chord
        gamepadsStart(padDisconnectHook, NULL);
    }
    pthread_t th;
    pthread_create(&th, NULL, worker, NULL);
    int rc = xr_app_run(&cfg);

    // Quitting the app ends the stream and the host's session first
    atomic_store(&g_workerQuit, 1);
    pthread_mutex_lock(&g_lock);
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
    pthread_join(th, NULL);
    if (src) {
        file_source_stop(src);
        atomic_store(&g_streaming, 0);
    }
    else {
        stopStream(&g_net, 1);
        evkbd_set_active(0);
        evkbd_stop();
        gamepadsSetActive(0);
        gamepadsStop();
    }
    for (int i = 0; i < LAUNCH_MAX_PCS; i++)
        if (g_net.srv[i]) mlxr_server_free(g_net.srv[i]);
    free(g_net.rtsp);
    mlxr_hosts_free(&g_net.hosts);
    if (g_net.haveId) mlxr_identity_free(&g_net.id);
    free(g_net.dir);
    frame_slot_destroy(&g_slot);
    if (g_args.exitToCaller) {
        // What the flat launcher is told: 0 for a normal end (the bar's exit, SteamVR quitting),
        // else a code and, on stderr, one "MLXR_ERROR: message" line
        pthread_mutex_lock(&g_exitLock);
        if (atomic_load(&g_exitSet)) {
            rc = atomic_load(&g_exitCode);
        }
        else if (rc != 0) {
            rc = EXIT_FAILED;
            snprintf(g_exitMsg, sizeof(g_exitMsg), "%s",
                     "The VR session could not start (is SteamVR running with the headset on? see moonlight.log)");
        }
        pthread_mutex_unlock(&g_exitLock);
        MLOG_I("== moonlight-xr exits to its caller (%d%s%s)", rc, g_exitMsg[0] ? ": " : "", g_exitMsg);
        xrLogFlush();
        if (rc != 0 && g_exitMsg[0]) fprintf(stderr, "MLXR_ERROR: %s\n", g_exitMsg);
        fflush(stderr);
        return rc;
    }
    MLOG_I("== moonlight-xr exits (%d)", rc);
    xrLogFlush();
    return rc;
}
