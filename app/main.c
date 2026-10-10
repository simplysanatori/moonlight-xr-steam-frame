// Moonlight XR Native: entry point. GPLv3.
//
//   moonlight-xr [--host NAME|ADDR] [--app NAME|ID] [--mode WxHxFPS] [--bitrate KBPS]
//                [--h264] [--wake] [--seconds N]
//   moonlight-xr --file clip.mp4 [--seconds N]      local test video, no host
//
// Hosts and the client identity live in ~/.config/moonlight-xr (pair with
// mlxr-cli, or import Moonlight Qt's pairing with backend/seed-from-moonlight-qt.sh).
#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../backend/backend.h"
#include "../audio/opus_out.h"
#include "../backend/backend_limelight.h"
#include "../stream/evdev_keyboard.h"
#include "../stream/file_source.h"
#include "../stream/hw_video.h"
#include "../stream/frame_slot.h"
#include "../stream/session.h"
#include "../xr/mlxr_log.h"
#include "../xr/xr_session.h"

static FrameSlot g_slot;
static void updateKeyboard(void);
static int g_streaming;
static int g_fileHw;

static void statsHook(void* user, char* buf, int len) {
    (void)user;
    char s[160] = "", a[128] = "";
    if (g_fileHw && hw_video_current()) {
        HwVideoStats h;
        hw_video_stats(hw_video_current(), &h);
        snprintf(s, sizeof(s), "hw decode %.1f ms (last %.1f), frames %llu, taken %llu, skipped %llu, errors %llu",
                 h.avgDecodeMs, h.lastDecodeMs, (unsigned long long)h.frames, (unsigned long long)h.taken,
                 (unsigned long long)h.skipped, (unsigned long long)h.errors);
    }
    if (g_streaming) {
        stream_stats(s, sizeof(s));
        MlxrOpusOutStats st;
        mlxr_opus_out_stats(&st);
        snprintf(a, sizeof(a), "audio %dch %.0f ms buffered, %u underruns, %u lost", st.channels, st.bufferedMs,
                 st.underruns, st.lostPackets);
    }
    snprintf(buf, (size_t)len, "decoded %llu, dropped %llu, decode %.1f ms; %s; %s", (unsigned long long)g_slot.produced,
             (unsigned long long)g_slot.dropped, g_slot.lastDecodeMs, s, a);
}

static void pointerHook(void* user, int x, int y, int w, int h) {
    (void)user;
    if (g_streaming) {
        stream_pointer(x, y, w, h);
        updateKeyboard(); // picks up the moment the stream connects
    }
}

static void buttonHook(void* user, int button, int down) {
    (void)user;
    if (g_streaming) stream_button(button, down);
    else MLOG_I("button %d %s", button, down ? "down" : "up");
}

static void scrollHook(void* user, int amount) {
    (void)user;
    if (g_streaming) stream_scroll(amount);
}

static int g_focused;

static void updateKeyboard(void) { evkbd_set_active(g_streaming && g_focused && stream_running()); }

static void focusHook(void* user, int focused) {
    (void)user;
    g_focused = focused;
    updateKeyboard();
}

static void sendKey(short vk, int down, char modifiers, void* user) {
    (void)user;
    if (stream_running())
        LiSendKeyboardEvent((short)(0x8000 | vk), down ? KEY_ACTION_DOWN : KEY_ACTION_UP, modifiers);
}

static void backendLog(int level, const char* msg, void* ud) {
    (void)ud;
    if (level <= 1) mlxr_logf(level == 0 ? 'E' : 'I', "backend: %s", msg);
}

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
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    for (int port = 7; port <= 9; port += 2) {
        a.sin_port = htons((uint16_t)port);
        sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr*)&a, sizeof(a));
    }
    close(s);
    MLOG_I("wake: magic packet sent to %s", h->name);
}

typedef struct StreamJob {
    StreamHost host;
    STREAM_CONFIGURATION cfg;
    AUDIO_RENDERER_CALLBACKS audio;
    MlxrOpusOutConfig audioCfg; // read by the audio init callback; lives as long as the job
} StreamJob;

static void* streamThread(void* arg) {
    StreamJob* j = arg;
    if (stream_start(&j->host, &j->cfg, &g_slot, &j->audio, &j->audioCfg) != 0) {
        MLOG_E("stream did not start");
        xr_app_request_quit();
    }
    return NULL;
}

// Tries the stored address, then the extra ones; leaves the server on the first that answers.
static mlxr_server* connectHost(const mlxr_identity* id, const char* dir, mlxr_host_record* rec, const char* override,
                                mlxr_computer* comp, char* addrOut, size_t addrCap) {
    char cands[8][128];
    int n = 0;
    if (override) snprintf(cands[n++], 128, "%s", override);
    if (rec->address[0]) snprintf(cands[n++], 128, "%s", rec->address);
    char extra[256];
    snprintf(extra, sizeof(extra), "%s", rec->extra_addresses);
    for (char* t = strtok(extra, ","); t && n < 8; t = strtok(NULL, ",")) snprintf(cands[n++], 128, "%s", t);
    char* pem = NULL;
    mlxr_error err = {0};
    if (mlxr_host_cert_load(dir, rec, &pem, &err) != MLXR_OK) {
        MLOG_E("%s is not paired here (%s): pair with mlxr-cli pair", rec->name, err.msg);
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        mlxr_server* s = mlxr_server_new(id, cands[i], rec->http_port ? rec->http_port : MLXR_DEFAULT_HTTP_PORT);
        mlxr_server_set_https_port(s, rec->https_port ? rec->https_port : MLXR_DEFAULT_HTTPS_PORT);
        mlxr_server_set_pinned_cert(s, pem, &err);
        if (mlxr_server_get_info(s, true, comp, NULL, &err) == MLXR_OK) {
            MLOG_I("host %s answers at %s: %s, app version %s, codecs 0x%x, %s, current game %d", rec->name, cands[i],
                   comp->state, comp->app_version, comp->server_codec_mode_support,
                   comp->paired ? "paired" : "NOT paired", comp->current_game);
            snprintf(addrOut, addrCap, "%s", cands[i]);
            free(pem);
            return s;
        }
        MLOG_W("host %s not reachable at %s: %s", rec->name, cands[i], err.msg);
        mlxr_server_free(s);
    }
    free(pem);
    return NULL;
}

int main(int argc, char** argv) {
    const char *file = NULL, *hostArg = NULL, *appArg = "Desktop", *modeArg = "2560x1440x90";
    double seconds = 0;
    int bitrate = 0, h264Only = 0, doWake = 0, swDecode = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) hostArg = argv[++i];
        else if (!strcmp(argv[i], "--app") && i + 1 < argc) appArg = argv[++i];
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) modeArg = argv[++i];
        else if (!strcmp(argv[i], "--bitrate") && i + 1 < argc) bitrate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--h264")) h264Only = 1;
        else if (!strcmp(argv[i], "--wake")) doWake = 1;
        else if (!strcmp(argv[i], "--sw-decode")) swDecode = 1;
        else {
            fprintf(stderr,
                    "usage: %s [--host NAME|ADDR] [--app NAME|ID] [--mode WxHxFPS] [--bitrate KBPS] [--h264] [--wake] "
                    "[--sw-decode] [--seconds N]\n       %s --file VIDEO [--seconds N]\n",
                    argv[0], argv[0]);
            return 2;
        }
    }
    frame_slot_init(&g_slot);
    XrAppConfig cfg = {0};
    cfg.video = &g_slot;
    cfg.screenDistance = 2.0f;
    cfg.screenWidth = 2.4f;
    cfg.seconds = seconds;
    cfg.hooks = (XrAppHooks){NULL, pointerHook, buttonHook, scrollHook, statsHook, focusHook};

    if (file) {
        FileSource* src = file_source_start(file, &g_slot, !swDecode);
        if (!src) return 1;
        g_fileHw = !swDecode;
        int rc = xr_app_run(&cfg);
        file_source_stop(src);
        frame_slot_destroy(&g_slot);
        return rc;
    }

    // ---- host, app, launch
    mlxr_set_log(backendLog, NULL);
    mlxr_error err = {0};
    char* dir = mlxr_config_dir(&err);
    mlxr_identity id;
    if (!dir || mlxr_identity_load_or_create(dir, &id, NULL, &err) != MLXR_OK) {
        MLOG_E("identity: %s", err.msg);
        return 1;
    }
    mlxr_hosts hosts = {0};
    mlxr_hosts_load(dir, &hosts, &err);
    mlxr_host_record* rec = hostArg ? mlxr_hosts_find(&hosts, hostArg) : (hosts.n ? &hosts.v[0] : NULL);
    if (!rec) {
        MLOG_E("no paired host%s%s in %s/hosts.ini", hostArg ? " matching " : "", hostArg ? hostArg : "", dir);
        return 1;
    }
    if (doWake) {
        wake(rec);
        sleep(1);
    }
    mlxr_computer comp;
    char address[128] = "";
    const char* override = hostArg && strcasecmp(hostArg, rec->name) && strcasecmp(hostArg, rec->uuid) ? hostArg : NULL;
    mlxr_server* srv = NULL;
    for (int attempt = 0; !srv && attempt < (doWake ? 12 : 1); attempt++) {
        if (attempt) sleep(5);
        srv = connectHost(&id, dir, rec, override, &comp, address, sizeof(address));
    }
    if (!srv) {
        MLOG_E("host %s is unreachable (is the PC on? try --wake)", rec->name);
        return 1;
    }
    mlxr_app* apps = NULL;
    size_t nApps = 0;
    if (mlxr_server_get_apps(srv, &apps, &nApps, &err) != MLXR_OK) {
        MLOG_E("app list: %s", err.msg);
        return 1;
    }
    int appId = -1;
    char* end = NULL;
    long asNum = strtol(appArg, &end, 10);
    for (size_t i = 0; i < nApps && appId < 0; i++)
        if ((end && !*end && apps[i].id == asNum) || !strcasecmp(apps[i].name, appArg)) appId = apps[i].id;
    if (appId < 0) {
        MLOG_E("no app \"%s\" on %s; %zu apps available, e.g. %s", appArg, rec->name, nApps, nApps ? apps[0].name : "-");
        return 1;
    }
    MLOG_I("app \"%s\" id %d", appArg, appId);

    mlxr_launch_params lp;
    mlxr_launch_params_init_ll(&lp);
    if (mlxr_parse_mode(modeArg, &lp.width, &lp.height, &lp.fps) != MLXR_OK) {
        MLOG_E("bad --mode %s", modeArg);
        return 1;
    }
    lp.audio_configuration = MLXR_AUDIO_STEREO;
    lp.is_gfe = comp.is_nvidia;
    char* rtsp = NULL;
    if (mlxr_server_launch_or_resume(srv, &comp, appId, &lp, &rtsp, &err) != MLXR_OK) {
        MLOG_E("launch: %s", err.msg);
        return 1;
    }
    MLOG_I("%s app %d, rtsp %s", comp.current_game ? "resumed" : "launched", appId, rtsp ? rtsp : "(none)");

    static StreamJob job;
    SERVER_INFORMATION si;
    mlxr_fill_server_info(&si, address, &comp, rtsp);
    job.host = (StreamHost){si.address, si.serverInfoAppVersion, si.serverInfoGfeVersion, si.rtspSessionUrl,
                            si.serverCodecModeSupport};
    mlxr_fill_stream_config(&job.cfg, &lp);
    if (!bitrate) bitrate = lp.width * lp.height >= 2560 * 1440 ? 80000 : 20000; // owner: 1440p needs ~80 Mbps
    job.cfg.bitrate = bitrate;
    job.cfg.packetSize = 1392;
    job.cfg.streamingRemotely = STREAM_CFG_AUTO;
    job.cfg.supportedVideoFormats = VIDEO_FORMAT_H264 | (h264Only ? 0 : VIDEO_FORMAT_H265);
    job.cfg.clientRefreshRateX100 = lp.fps * 100;
    job.cfg.colorSpace = COLORSPACE_REC_709;
    job.cfg.colorRange = COLOR_RANGE_LIMITED;
    job.cfg.encryptionFlags = ENCFLG_ALL;

    mlxr_opus_out_callbacks(&job.audio);
    mlxr_opus_out_config_default(&job.audioCfg);
    // HRTFs ship in assets/ next to the binary (installed tree) or in the source tree.
    job.audioCfg.hrtfDir = access("assets/hrtf/kemar", R_OK) == 0 ? "assets/hrtf/kemar" : MLXR_SOURCE_ASSETS "/hrtf/kemar";
    job.audioCfg.surround = true;

    stream_set_hw_decode(!swDecode);
    g_streaming = 1;
    evkbd_start(sendKey, NULL);
    pthread_t th;
    pthread_create(&th, NULL, streamThread, &job);
    int rc = xr_app_run(&cfg);
    MLOG_I("stopping stream");
    evkbd_set_active(0);
    evkbd_stop();
    pthread_join(th, NULL);
    stream_stop();
    stream_close_video(); // after the renderer dropped its imports
    g_streaming = 0;
    // Leaving the app ends the session on the host too (Moonlight's "Quit
    // session"), so the game or desktop stream does not keep running there.
    mlxr_error qerr = {0};
    mlxr_status qs = mlxr_server_quit_app(srv, &qerr);
    if (qs == MLXR_OK) MLOG_I("host session ended on %s", rec->name);
    else MLOG_W("could not end the host session: %s (%s)", mlxr_status_name(qs), qerr.msg);
    free(rtsp);
    mlxr_apps_free(apps, nApps);
    mlxr_server_free(srv);
    mlxr_hosts_free(&hosts);
    mlxr_identity_free(&id);
    free(dir);
    frame_slot_destroy(&g_slot);
    return rc;
}
