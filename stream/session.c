// Moonlight XR Native: moonlight-common-c session. Modelled on moonlight-qt's
// app/streaming/session.cpp. GPLv3.
#include "session.h"

#include <openssl/rand.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../xr/mlxr_log.h"
#include "hw_video.h"
#include "sw_decoder.h"

static FrameSlot* g_video;
static int g_useHw = 1;
static HwVideo* g_hw;
static SwDecoder* g_dec;
static uint8_t* g_au;
static size_t g_auCap;
static atomic_int g_connected, g_failed;
static atomic_uint_fast64_t g_units, g_idr;
static int g_lastErr;

// ---- video callbacks (decoder thread of moonlight-common-c)

static int drSetup(int videoFormat, int width, int height, int redrawRate, void* ctx, int flags) {
    (void)ctx;
    (void)flags;
    int hevc = (videoFormat & VIDEO_FORMAT_MASK_H265) != 0;
    MLOG_I("video setup: %s %dx%d at %d fps, %s decode", hevc ? "HEVC" : "H.264", width, height, redrawRate,
           g_useHw ? "hardware" : "software");
    if (g_useHw) {
        g_hw = hw_video_open(hevc ? MLXR_CODEC_HEVC : MLXR_CODEC_H264, (uint32_t)width, (uint32_t)height,
                             LiRequestIdrFrame);
        if (g_hw) {
            hw_video_set_current(g_hw);
            return 0;
        }
        MLOG_W("hardware decoder unavailable; falling back to software");
    }
    g_dec = sw_decoder_open(hevc ? SWDEC_HEVC : SWDEC_H264, g_video);
    return g_dec ? 0 : -1;
}

static void drCleanup(void) {
    hw_video_stop(g_hw); // the decoder itself closes after the renderer (stream_close_video)
    sw_decoder_close(g_dec);
    g_dec = NULL;
    free(g_au);
    g_au = NULL;
    g_auCap = 0;
}

static int drSubmit(PDECODE_UNIT du) {
    if (g_hw) {
        const void* parts[64];
        uint32_t sizes[64], n = 0;
        for (PLENTRY e = du->bufferList; e && n < 64; e = e->next) {
            parts[n] = e->data;
            sizes[n++] = (uint32_t)e->length;
        }
        atomic_fetch_add(&g_units, 1);
        if (hw_video_submit(g_hw, parts, sizes, n, (int64_t)du->presentationTimeUs) != 0) {
            atomic_fetch_add(&g_idr, 1);
            return DR_NEED_IDR;
        }
        return DR_OK;
    }
    if ((size_t)du->fullLength + 64 > g_auCap) {
        g_auCap = (size_t)du->fullLength + 64 + 65536;
        g_au = realloc(g_au, g_auCap);
    }
    size_t off = 0;
    for (PLENTRY e = du->bufferList; e; e = e->next) {
        memcpy(g_au + off, e->data, (size_t)e->length);
        off += (size_t)e->length;
    }
    memset(g_au + off, 0, 64); // FFmpeg input padding
    atomic_fetch_add(&g_units, 1);
    if (sw_decoder_submit(g_dec, g_au, off) != 0) {
        atomic_fetch_add(&g_idr, 1);
        return DR_NEED_IDR;
    }
    return DR_OK;
}

// ---- connection callbacks

static void clStageStarting(int stage) { MLOG_I("stream stage %s starting", LiGetStageName(stage)); }
static void clStageFailed(int stage, int err) {
    MLOG_E("stream stage %s failed: %d", LiGetStageName(stage), err);
    g_lastErr = err;
}
static void clStarted(void) {
    MLOG_I("stream connected");
    atomic_store(&g_connected, 1);
}
static void clTerminated(int err) {
    MLOG_W("stream terminated: %d", err);
    g_lastErr = err;
    atomic_store(&g_connected, 0);
    atomic_store(&g_failed, 1);
}
static void clLog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    MLOG_I("common-c: %s", buf);
}
static void clStatus(int status) { MLOG_I("connection status: %s", status == CONN_STATUS_POOR ? "poor" : "okay"); }

void stream_make_config(const StreamOptions* opt, STREAM_CONFIGURATION* cfg) {
    LiInitializeStreamConfiguration(cfg);
    cfg->width = opt->width;
    cfg->height = opt->height;
    cfg->fps = opt->fps;
    cfg->bitrate = opt->bitrateKbps;
    cfg->packetSize = opt->remote == STREAM_CFG_REMOTE ? 1024 : 1392;
    cfg->streamingRemotely = opt->remote;
    cfg->audioConfiguration = opt->audioConfiguration ? opt->audioConfiguration : AUDIO_CONFIGURATION_STEREO;
    cfg->supportedVideoFormats = VIDEO_FORMAT_H264 | (opt->allowHevc ? VIDEO_FORMAT_H265 : 0);
    cfg->clientRefreshRateX100 = opt->fps * 100;
    cfg->colorSpace = COLORSPACE_REC_709;
    cfg->colorRange = COLOR_RANGE_LIMITED;
    cfg->encryptionFlags = ENCFLG_ALL; // Cortex-A has AES instructions
    RAND_bytes((unsigned char*)cfg->remoteInputAesKey, sizeof(cfg->remoteInputAesKey));
    memset(cfg->remoteInputAesIv, 0, sizeof(cfg->remoteInputAesIv));
    RAND_bytes((unsigned char*)cfg->remoteInputAesIv, 4); // only the first 4 bytes are used
}

int stream_start(const StreamHost* host, STREAM_CONFIGURATION* cfg, FrameSlot* video, PAUDIO_RENDERER_CALLBACKS audio,
                 void* audioContext) {
    g_video = video;
    atomic_store(&g_connected, 0);
    atomic_store(&g_failed, 0);
    SERVER_INFORMATION si;
    LiInitializeServerInformation(&si);
    si.address = host->address;
    si.serverInfoAppVersion = host->appVersion;
    si.serverInfoGfeVersion = host->gfeVersion;
    si.rtspSessionUrl = host->rtspUrl;
    si.serverCodecModeSupport = host->codecModeSupport;

    CONNECTION_LISTENER_CALLBACKS cl;
    LiInitializeConnectionCallbacks(&cl);
    cl.stageStarting = clStageStarting;
    cl.stageFailed = clStageFailed;
    cl.connectionStarted = clStarted;
    cl.connectionTerminated = clTerminated;
    cl.logMessage = clLog;
    cl.connectionStatusUpdate = clStatus;

    DECODER_RENDERER_CALLBACKS dr;
    LiInitializeVideoCallbacks(&dr);
    dr.setup = drSetup;
    dr.cleanup = drCleanup;
    dr.submitDecodeUnit = drSubmit;
    dr.capabilities = 0; // decode on common-c's own decoder thread

    AUDIO_RENDERER_CALLBACKS noAudio;
    LiInitializeAudioCallbacks(&noAudio);
    MLOG_I("starting stream to %s: %dx%d %d fps %d kbps, formats 0x%x", host->address, cfg->width, cfg->height,
           cfg->fps, cfg->bitrate, cfg->supportedVideoFormats);
    int r = LiStartConnection(&si, cfg, &cl, &dr, audio ? audio : &noAudio, NULL, 0, audioContext, 0);
    if (r != 0) {
        MLOG_E("LiStartConnection failed: %d (last error %d)", r, g_lastErr);
        atomic_store(&g_failed, 1);
    }
    return r;
}

void stream_set_hw_decode(int on) { g_useHw = on; }

void stream_close_video(void) {
    hw_video_set_current(NULL);
    hw_video_close(g_hw);
    g_hw = NULL;
}

void stream_stop(void) {
    LiStopConnection();
    atomic_store(&g_connected, 0);
}

int stream_running(void) { return atomic_load(&g_connected); }
int stream_failed(void) { return atomic_load(&g_failed); }

void stream_pointer(int x, int y, int w, int h) {
    if (atomic_load(&g_connected)) LiSendMousePositionEvent((short)x, (short)y, (short)w, (short)h);
}

void stream_button(int button, int down) {
    if (atomic_load(&g_connected))
        LiSendMouseButtonEvent(down ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE, button);
}

void stream_scroll(int amount) {
    if (atomic_load(&g_connected)) LiSendHighResScrollEvent((short)amount);
}

void stream_stats(char* buf, int len) {
    uint32_t rtt = 0, var = 0;
    LiGetEstimatedRttInfo(&rtt, &var);
    char hw[160] = "";
    if (g_hw) {
        HwVideoStats h;
        hw_video_stats(g_hw, &h);
        snprintf(hw, sizeof(hw), "; hw decode %.1f ms (last %.1f), frames %llu, skipped %llu, errors %llu",
                 h.avgDecodeMs, h.lastDecodeMs, (unsigned long long)h.frames, (unsigned long long)h.skipped,
                 (unsigned long long)h.errors);
    }
    snprintf(buf, (size_t)len, "units %llu, idr requests %llu, rtt %u ms, pending video %d%s",
             (unsigned long long)atomic_load(&g_units), (unsigned long long)atomic_load(&g_idr), rtt,
             LiGetPendingVideoFrames(), hw);
}
