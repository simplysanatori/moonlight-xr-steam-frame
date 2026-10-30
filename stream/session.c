// Moonlight XR Native: moonlight-common-c session. Modelled on moonlight-qt's
// app/streaming/session.cpp. GPLv3.
#include "session.h"

#include <dirent.h>
#include <openssl/rand.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../audio/thread_prio.h"
#include "../xr/mlxr_log.h"
#include "../input/gamepads.h"
#include "../input/rumble.h"
#include "../xr/xr_pad_bridge.h"
#include "../backend/fps_cap.h"
#include "../backend/netdiag.h"
#include "../xr/xr_stream_prefs.h"
#include "au_gather.h"
#include "commonc_ext.h"
#include "cadence.h"
#include "xr_hud.h"
#include "hw_video.h"
#include "rfi_watch.h"
#include "log_throttle.h"
#include "sw_decoder.h"

static FrameSlot* g_video;
static int g_useHw = 1;
static HwVideo* g_hw;
static SwDecoder* g_dec;
static uint8_t* g_au;
static size_t g_auCap;
// Native (lane au-scatter): the hardware path's parts of one decode unit,
// kept from call to call as g_au is, and the time of the last mismatch warning
static MlxrAuGather g_gather;
static uint64_t g_gatherWarnUs;
static atomic_int g_connected, g_failed;
static atomic_int g_termCode; // Native (lane wave3-session): the code common-c's connectionTerminated gave
static atomic_uint_fast64_t g_units, g_idr;
static int g_lastErr;
static int g_rfi; // Native (lane av-latency): reference frame invalidation advertised for this stream
// Native (lane perf): the RFI watchdog (rfi_watch.h), under g_latLock, and the
// video threads' priority (MLXR_VIDEO_PRIORITY)
static MlxrRfiWatch g_rfiWatch;
static MlxrRfiWatch g_rfiPrev; // counters at the last latency line
static MlxrPrioMode g_videoPrio = MLXR_PRIO_NICE;
static MlxrCadence g_cadence; // Native (lane perf): the host's frame intervals, under g_latLock
// Native (lane deploy-res-fix): about one "Unrecoverable frame" line a second reaches moonlight.log
// (log_throttle.h); the loss counters and the watchdog still see every line. Under g_latLock.
static MlxrLogThrottle g_logThrottle;
// Native (lane wave1-burst): the IDR request cap ([stream] rfi_idr_ms) on our own asks, the burst
// tracker and its totals at the last latency line, and the stream interface's receive drops that
// the latency line's udp part does not show ("rx drop misc", netdiag.h). Under g_latLock, but for
// g_rx* (the latency line's caller only).
static MlxrIdrCap g_idrCap, g_idrCapPrev;
static MlxrBurst g_burst, g_burstPrev;
static char g_rxIf[16];
static mlxr_rx_misc g_rxPrev;

// Native (lane av-latency): video latency parts per stats window, written by
// common-c's decoder thread (drSubmit), read and reset by the log
static pthread_mutex_t g_latLock = PTHREAD_MUTEX_INITIALIZER;
static struct {
    uint64_t frames, hostN;
    double hostMsSum, hostMsMax;     // Sunshine's own capture + encode time
    double recvMsSum, recvMsMax;     // first packet to frame assembled
    double queueMsSum, queueMsMax;   // assembled to handed to our decoder
    uint64_t netLost, idrWaitDrops, rfiWaitDrops, idrRequests; // from common-c's log lines
} g_lat;

// Native (lane wave3-hud): the running totals the in-headset HUD and the slow connection badge take windows of
// (xr_hud.h). Unlike g_lat they are never reset within a stream. Under g_latLock.
static MlxrHudCum g_hud;
static int g_setKbps;   // Native (lane wave4b-abr): the bitrate this stream started with, for the HUD and the stats line
static atomic_int g_poor;   // the connection status common-c last reported is "poor"

// Native (lane netpath-quality): the whole stream's network figures. The window counters above are
// reset at every latency line, so each window is folded in here first. Under g_latLock.
static struct {
    uint64_t frames, lost, transitN;
    double jitterVarSum; // sum of the windows' variance of the arrival jitter, weighted by intervals
    uint32_t rttMs;
} g_netTot;

static void foldNetWindow(uint64_t frames, uint64_t lost, const MlxrCadence* cad, uint32_t rtt) {
    g_netTot.frames += frames;
    g_netTot.lost += lost;
    if (cad->transitN) {
        double n = (double)cad->transitN, mean = cad->transitSum / n;
        g_netTot.jitterVarSum += fmax(0.0, cad->transitSq / n - mean * mean) * n;
        g_netTot.transitN += cad->transitN;
    }
    if (rtt) g_netTot.rttMs = rtt;
}

int stream_network_figures(StreamNetFigures* out) {
    uint32_t rtt = 0, var = 0;
    if (atomic_load(&g_connected)) LiGetEstimatedRttInfo(&rtt, &var);
    pthread_mutex_lock(&g_latLock);
    // the folded windows plus the one still open
    uint64_t frames = g_netTot.frames, lost = g_netTot.lost, n = g_netTot.transitN;
    double varSum = g_netTot.jitterVarSum;
    uint32_t last = g_netTot.rttMs;
    if (g_cadence.transitN) {
        double cn = (double)g_cadence.transitN, mean = g_cadence.transitSum / cn;
        varSum += fmax(0.0, g_cadence.transitSq / cn - mean * mean) * cn;
        n += g_cadence.transitN;
    }
    frames += g_lat.frames;
    lost += g_lat.netLost;
    pthread_mutex_unlock(&g_latLock);
    out->framesReceived = frames;
    out->framesLost = lost;
    out->jitterMs = n ? sqrt(varSum / (double)n) : 0.0;
    out->rttMs = rtt ? rtt : last;
    return frames + lost > 0;
}

static uint64_t mlxr_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

// Native (lane wave1-burst): a closed burst's summary line. The tracker keeps monotonic ms; the line
// names the wall time it started at, as the log's own timestamps do.
static void logBurst(const MlxrBurstSummary* b, int capMs) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t wallMs = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
    uint64_t ago = mlxr_now_ms() - b->startMs;
    uint64_t at = wallMs > ago ? wallMs - ago : 0;
    time_t sec = (time_t)(at / 1000u);
    struct tm tmv;
    localtime_r(&sec, &tmv);
    char start[32];
    snprintf(start, sizeof(start), "%02d:%02d:%02d.%03u", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (unsigned)(at % 1000u));
    char line[512];
    mlxr_burst_text(b, start, capMs, line, sizeof(line));
    MLOG_I("video: %s", line);
}

// Native (lane wave1-burst): every IDR frame request of ours goes through here: the RFI watchdog's
// and hw_video's (decoder errors, damaged pictures). At most one per [stream] rfi_idr_ms; the
// value is read at each ask, so the settings applied after the stream started count too.
static int takeIdrAsk(uint64_t nowMs) {
    g_idrCap.minGapMs = xrStreamPrefsRfiIdrMs();
    int go = mlxr_idr_cap_take(&g_idrCap, nowMs);
    mlxr_burst_idr_ask(&g_burst, go);
    return go;
}

static void requestIdrCapped(void) {
    pthread_mutex_lock(&g_latLock);
    int go = takeIdrAsk(mlxr_now_ms());
    pthread_mutex_unlock(&g_latLock);
    if (go) LiRequestIdrFrame();
}

static void noteFrameLatency(const DECODE_UNIT* du) {
    uint64_t now = LiGetMicroseconds();
    double recv = du->enqueueTimeUs > du->receiveTimeUs ? (du->enqueueTimeUs - du->receiveTimeUs) / 1000.0 : 0.0;
    double queue = now > du->enqueueTimeUs ? (now - du->enqueueTimeUs) / 1000.0 : 0.0;
    MlxrBurstSummary closed;
    pthread_mutex_lock(&g_latLock);
    // Native (lane wave1-burst): an IDR frame mends the picture; each frame moves the burst's clock on
    if (du->frameType == FRAME_TYPE_IDR) mlxr_burst_idr_frame(&g_burst);
    int burstClosed = mlxr_burst_tick(&g_burst, mlxr_now_ms(), &closed);
    int capMs = g_idrCap.minGapMs;
    mlxr_cadence_add(&g_cadence, (uint32_t)du->frameNumber, du->rtpTimestamp, du->receiveTimeUs);
    g_hud.frames++;
    g_hud.bytes += du->fullLength > 0 ? (uint64_t)du->fullLength : 0;
    g_hud.recvMsSum += recv;
    g_hud.queueMsSum += queue;
    if (du->frameHostProcessingLatency > 0) {
        g_hud.hostN++;
        g_hud.hostMsSum += du->frameHostProcessingLatency / 10.0;
    }
    g_lat.frames++;
    g_lat.recvMsSum += recv;
    if (recv > g_lat.recvMsMax) g_lat.recvMsMax = recv;
    g_lat.queueMsSum += queue;
    if (queue > g_lat.queueMsMax) g_lat.queueMsMax = queue;
    if (du->frameHostProcessingLatency > 0) {
        double host = du->frameHostProcessingLatency / 10.0;
        g_lat.hostN++;
        g_lat.hostMsSum += host;
        if (host > g_lat.hostMsMax) g_lat.hostMsMax = host;
    }
    pthread_mutex_unlock(&g_latLock);
    if (burstClosed) logBurst(&closed, capMs);
}

void stream_latency_line(char* buf, int len) {
    uint32_t rtt = 0, var = 0;
    LiGetEstimatedRttInfo(&rtt, &var);
    // Native (lane wave1-burst): receive drops the udp part of the line does not show (netdiag.h)
    mlxr_rx_misc rxNow;
    mlxr_rx_misc_read("/proc", g_rxIf, &rxNow);
    char rx[192];
    mlxr_rx_misc_delta_text(&g_rxPrev, &rxNow, rx, sizeof(rx));
    g_rxPrev = rxNow;
    MlxrBurstSummary closed;
    pthread_mutex_lock(&g_latLock);
    if (rtt) mlxr_rfi_watch_set_rtt(&g_rfiWatch, rtt); // Native (lane wave1-burst): the adaptive wait's round trip
    int burstClosed = mlxr_burst_tick(&g_burst, mlxr_now_ms(), &closed); // a burst the stream froze in
    int capMs = xrStreamPrefsRfiIdrMs();
    foldNetWindow(g_lat.frames, g_lat.netLost, &g_cadence, rtt); // Native (lane netpath-quality)
    // Native (lane deploy-res-fix): the lines the log no longer holds are counted here
    uint64_t unrec = mlxr_log_throttle_window(&g_logThrottle);
    uint64_t unrecTail = mlxr_log_throttle_flush(&g_logThrottle, mlxr_now_ms());
    char cad[360];
    mlxr_cadence_line(&g_cadence, cad, sizeof(cad));
    double n = g_lat.frames ? (double)g_lat.frames : 1.0;
    snprintf(buf, (size_t)len,
             "host encode %.1f ms (max %.1f), frame received over %.1f ms (max %.1f), queued %.1f ms (max %.1f), "
             "rtt %u ms (variance %u), frames lost on the network %llu, dropped waiting for IDR %llu / RFI %llu, "
             "IDR requests %llu; RFI waits %llu (%llu ended by the host's recovery frame), %.0f ms avg (max %llu), "
             "IDR asked by the RFI watchdog %llu (wait %d ms now, %llu put off by a burst), "
             "held back by rfi_idr_ms %d: %llu; bursts %llu (longest %llu ms); %s; "
             "unrecoverable-frame lines %llu; %s",
             g_lat.hostN ? g_lat.hostMsSum / g_lat.hostN : 0.0, g_lat.hostMsMax, g_lat.recvMsSum / n, g_lat.recvMsMax,
             g_lat.queueMsSum / n, g_lat.queueMsMax, rtt, var, (unsigned long long)g_lat.netLost,
             (unsigned long long)g_lat.idrWaitDrops, (unsigned long long)g_lat.rfiWaitDrops,
             (unsigned long long)g_lat.idrRequests, (unsigned long long)(g_rfiWatch.waits - g_rfiPrev.waits),
             (unsigned long long)(g_rfiWatch.recovered - g_rfiPrev.recovered),
             g_rfiWatch.closed > g_rfiPrev.closed
                 ? (double)(g_rfiWatch.waitMsSum - g_rfiPrev.waitMsSum) / (double)(g_rfiWatch.closed - g_rfiPrev.closed)
                 : 0.0,
             (unsigned long long)g_rfiWatch.waitMsMax, (unsigned long long)(g_rfiWatch.idrAsked - g_rfiPrev.idrAsked),
             mlxr_rfi_watch_wait_ms(&g_rfiWatch, mlxr_now_ms()),
             (unsigned long long)(g_rfiWatch.heldByLoss - g_rfiPrev.heldByLoss), capMs,
             (unsigned long long)(g_idrCap.held - g_idrCapPrev.held), (unsigned long long)(g_burst.bursts - g_burstPrev.bursts),
             (unsigned long long)g_burst.burstMsMax, rx, (unsigned long long)unrec, cad);
    g_rfiPrev = g_rfiWatch;
    g_rfiWatch.waitMsMax = 0;
    g_idrCapPrev = g_idrCap;
    g_burstPrev = g_burst;
    g_burst.burstMsMax = 0;
    memset(&g_lat, 0, sizeof(g_lat));
    pthread_mutex_unlock(&g_latLock);
    if (unrecTail) MLOG_I("common-c: Unrecoverable frame ... (+%llu more)", (unsigned long long)unrecTail);
    if (burstClosed) logBurst(&closed, capMs);
}

// ---- video callbacks (decoder thread of moonlight-common-c)

static int drSetup(int videoFormat, int width, int height, int redrawRate, void* ctx, int flags) {
    (void)ctx;
    (void)flags;
    int hevc = (videoFormat & VIDEO_FORMAT_MASK_H265) != 0;
    MLOG_I("video setup: %s %dx%d at %d fps, %s decode", hevc ? "HEVC" : "H.264", width, height, redrawRate,
           g_useHw ? "hardware" : "software");
    if (g_useHw) {
        // Native (lane wave1-burst): the decoder's IDR requests (errors, damaged pictures) go through
        // the same cap as the RFI watchdog's
        g_hw = hw_video_open(hevc ? MLXR_CODEC_HEVC : MLXR_CODEC_H264, (uint32_t)width, (uint32_t)height,
                             requestIdrCapped);
        if (g_hw) {
            hw_video_set_idr_on_corrupt(g_hw, g_rfi);
            hw_video_set_thread_priority(g_hw, g_videoPrio);
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
    mlxr_au_gather_free(&g_gather);
}

static int drSubmit(PDECODE_UNIT du) {
    noteFrameLatency(du);
    if (g_hw) {
        // Native (lane au-scatter): every entry of the decode unit. This
        // was a fixed array of 64 entries, and common-c makes one entry per
        // video packet (1376 bytes at packetSize 1392): any frame over about
        // 86 KB reached the decoder cut short, which flagged the picture
        // as an error. At 126 Mbps and 90 fps that was every frame.
        int n = mlxr_au_gather(&g_gather, du->bufferList, du->fullLength);
        atomic_fetch_add(&g_units, 1);
        if (n <= 0) {
            if (n == 0 && du->fullLength == 0) return DR_OK; // nothing to decode
            uint64_t now = LiGetMicroseconds();
            if (g_gatherWarnUs == 0 || now - g_gatherWarnUs >= 1000000) {
                g_gatherWarnUs = now;
                if (n == MLXR_AU_GATHER_NOMEM)
                    MLOG_W("video: no memory for the %d-byte decode unit's parts: not submitted", du->fullLength);
                else
                    MLOG_W("video: decode unit of %d bytes has %u parts adding up to %llu bytes: not submitted",
                           du->fullLength, g_gather.n, (unsigned long long)g_gather.total);
            }
            if (hw_video_refuse_au(g_hw) != 0) {
                atomic_fetch_add(&g_idr, 1);
                return DR_NEED_IDR;
            }
            return DR_OK;
        }
        if (hw_video_submit(g_hw, g_gather.parts, g_gather.sizes, (uint32_t)n, (int64_t)du->presentationTimeUs) != 0) {
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
// Native (lane perf): common-c's video receive thread (UDP, FEC) and decoder
// thread (hands AUs to the Iris decoder) above the frame loop's helpers, the
// panel worker and the depth engine (~1.5 of the 5 cores SteamOS gives a
// game here, 3-7): a packet burst then is read off the socket at once and a
// frame reaches the decoder without waiting for a CPU. As the audio receive
// thread (MLXR_AUDIO_PRIORITY); MLXR_VIDEO_PRIORITY=off|nice|rt, nice by default.
static void raiseVideoThreads(void) {
    if (g_videoPrio == MLXR_PRIO_OFF) return;
    DIR* d = opendir("/proc/self/task");
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) != NULL) {
        int tid = atoi(e->d_name);
        if (tid <= 0) continue;
        char path[64], comm[32] = "";
        snprintf(path, sizeof(path), "/proc/self/task/%d/comm", tid);
        FILE* f = fopen(path, "r");
        if (!f) continue;
        if (!fgets(comm, sizeof(comm), f)) comm[0] = 0;
        fclose(f);
        comm[strcspn(comm, "\n")] = 0;
        if (strcmp(comm, "VideoRecv") != 0 && strcmp(comm, "VideoDec") != 0) continue;
        char said[256];
        mlxr_thread_raise_priority_tid(tid, g_videoPrio, said, sizeof(said));
        MLOG_I("video: %s thread %s", comm, said);
    }
    closedir(d);
}

static void clStarted(void) {
    MLOG_I("stream connected");
    atomic_store(&g_connected, 1);
    raiseVideoThreads();
}
static void clTerminated(int err) {
    MLOG_W("stream terminated: %d", err);
    g_lastErr = err;
    atomic_store(&g_termCode, err);
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
    char shown[sizeof(buf) + 32]; // buf plus " ... (+N more)"
    int toLog = 1;
    // Native (lane av-latency): count where frames go, for the latency line
    {
        const char* d = strstr(buf, "Network dropped ");
        int lost = 0;
        pthread_mutex_lock(&g_latLock);
        if (d && strstr(buf, " frame") && sscanf(d, "Network dropped %d frame", &lost) == 1) {
            g_lat.netLost += (uint64_t)lost;
            g_hud.lost += lost > 0 ? (uint64_t)lost : 0;
        }
        else if (strstr(buf, "Waiting for IDR frame")) g_lat.idrWaitDrops++;
        else if (strstr(buf, "Waiting for RFI frame")) g_lat.rfiWaitDrops++;
        else if (strstr(buf, "IDR frame request sent")) g_lat.idrRequests++;
        // Native (lane perf): an RFI wait that drags on asks for an IDR frame
        uint64_t nowMs = mlxr_now_ms();
        int askIdr = g_rfi && mlxr_rfi_watch_line(&g_rfiWatch, buf, nowMs);
        // Native (lane wave1-burst): through the IDR cap; the wait it came after, for the log
        // (a held ask is counted in g_idrCap and the burst; the 5 s line and the burst line say how many)
        int askWait = g_rfiWatch.lastAskWaitMs;
        if (askIdr && !takeIdrAsk(nowMs)) askIdr = 0;
        // Native (lane wave1-burst): the burst tracker; a per-frame line it has written once in this
        // burst is only counted. A burst whose quiet gap passed closes first.
        MlxrBurstSummary closed;
        int burstClosed = 0;
        int burstWrite = mlxr_burst_line(&g_burst, buf, nowMs, &closed, &burstClosed);
        int capMs = g_idrCap.minGapMs;
        // Native (lane deploy-res-fix): only after the parsers above; decides what reaches the log
        toLog = mlxr_log_throttle_line(&g_logThrottle, buf, nowMs, shown, sizeof(shown)) && burstWrite;
        pthread_mutex_unlock(&g_latLock);
        if (burstClosed) logBurst(&closed, capMs);
        if (askIdr) {
            MLOG_I("video: RFI recovery not here after %d ms, asking for an IDR frame", askWait);
            LiRequestIdrFrame();
        }
    }
    if (toLog) MLOG_I("common-c: %s", shown);
}
static void clStatus(int status) {
    atomic_store(&g_poor, status == CONN_STATUS_POOR);
    MLOG_I("connection status: %s", status == CONN_STATUS_POOR ? "poor" : "okay");
}

void stream_make_config(const StreamOptions* opt, STREAM_CONFIGURATION* cfg) {
    LiInitializeStreamConfiguration(cfg);
    cfg->width = opt->width;
    cfg->height = opt->height;
    cfg->fps = mlxr_fps_clamp(opt->fps, MLXR_FPS_CAP); // never more than the headset shows (144 at most)
    cfg->bitrate = opt->bitrateKbps;
    cfg->packetSize = opt->remote == STREAM_CFG_REMOTE ? 1024 : 1392;
    cfg->streamingRemotely = opt->remote;
    cfg->audioConfiguration = opt->audioConfiguration ? opt->audioConfiguration : AUDIO_CONFIGURATION_STEREO;
    cfg->supportedVideoFormats = VIDEO_FORMAT_H264 | (opt->allowHevc ? VIDEO_FORMAT_H265 : 0);
    cfg->clientRefreshRateX100 = cfg->fps * 100;
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
    atomic_store(&g_termCode, 0);
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
    // Native: a physical pad (input/gamepads) takes the rumble for its number, else gamepad mode (M3)
    cl.rumble = gamepadsHostRumble;
    cl.rumbleTriggers = gamepadsHostRumbleTriggers;
    // Native: the host's request for a physical pad's gyro/accelerometer and LED. The Frame's own pad
    // (gamepad mode) has neither, so a request for its number is dropped.
    cl.setMotionEventState = gamepadsHostMotionState;
    cl.setControllerLED = gamepadsHostSetLed;

    DECODER_RENDERER_CALLBACKS dr;
    LiInitializeVideoCallbacks(&dr);
    dr.setup = drSetup;
    dr.cleanup = drCleanup;
    dr.submitDecodeUnit = drSubmit;
    dr.capabilities = 0; // decode on common-c's own decoder thread
    // Native (lane av-latency): reference frame invalidation for HEVC on the
    // hardware decoder. Without it every lost frame makes common-c drop all
    // frames until a new IDR frame arrives: in the owner's 23:29 run (80 Mbps
    // over Wi-Fi) 116 IDR requests and ~380 frames dropped waiting, on top of
    // 241 frames lost, up to 24 fps of a 90 fps stream in bad windows. With
    // it Sunshine re-references an older good frame and only the damaged
    // frames go. moonlight-qt leaves it off for generic hevc_v4l2m2m, so a
    // picture the Iris decoder flags as damaged still asks for an IDR frame
    // (hw_video_set_idr_on_corrupt). MLXR_VIDEO_RFI=0 turns it off.
    const char* rfiEnv = getenv("MLXR_VIDEO_RFI");
    g_rfi = g_useHw && !(rfiEnv && rfiEnv[0] == '0');
    if (g_rfi) dr.capabilities |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC;
    MLOG_I("video: reference frame invalidation %s (MLXR_VIDEO_RFI=%s)", g_rfi ? "advertised (HEVC)" : "off",
           rfiEnv ? rfiEnv : "unset");
    // Native (lane perf): the RFI watchdog (rfi_watch.h): 100 ms and 3 dropped
    // frames by default, an IDR request every 300 ms after that
    // Native (lane wave1-burst): 100 ms is now the floor of an adaptive wait (3 round trips, and
    // while a burst still loses frames until it was clean that long), up to MLXR_VIDEO_RFI_IDR_MAX_MS
    const char* idrMsEnv = getenv("MLXR_VIDEO_RFI_IDR_MS");
    int idrMs = idrMsEnv && *idrMsEnv ? atoi(idrMsEnv) : 100;
    const char* idrMaxEnv = getenv("MLXR_VIDEO_RFI_IDR_MAX_MS");
    int idrMaxMs = idrMaxEnv && *idrMaxEnv ? atoi(idrMaxEnv) : 1000;
    int capMs = xrStreamPrefsRfiIdrMs();
    pthread_mutex_lock(&g_latLock);
    mlxr_rfi_watch_init(&g_rfiWatch, idrMs < 0 ? 0 : idrMs, 3, 300);
    mlxr_rfi_watch_set_ceiling(&g_rfiWatch, idrMaxMs);
    g_rfiPrev = g_rfiWatch;
    // Native (lane wave1-burst): the IDR cap and the burst tracker (a burst closes after 1 s without
    // a lost or dropped frame)
    mlxr_idr_cap_init(&g_idrCap, capMs);
    g_idrCapPrev = g_idrCap;
    mlxr_burst_init(&g_burst, 1000);
    g_burstPrev = g_burst;
    mlxr_log_throttle_init(&g_logThrottle, 1000); // Native (lane deploy-res-fix)
    // Native (lane netpath-quality): this stream's network totals start at zero
    memset(&g_netTot, 0, sizeof(g_netTot));
    memset(&g_lat, 0, sizeof(g_lat));
    memset(&g_cadence, 0, sizeof(g_cadence));
    pthread_mutex_unlock(&g_latLock);
    if (g_rfi)
        MLOG_I("video: RFI watchdog: an IDR frame after %d ms of waiting for a recovery frame%s (MLXR_VIDEO_RFI_IDR_MS=%s), "
               "longer by 3 round trips and while a burst still loses frames, up to %d ms (MLXR_VIDEO_RFI_IDR_MAX_MS=%s)",
               idrMs, idrMs > 0 ? "" : " (off)", idrMsEnv ? idrMsEnv : "unset", g_rfiWatch.ceilMs,
               idrMaxEnv ? idrMaxEnv : "unset");
    MLOG_I("video: IDR frame requests of our own at most one per %d ms ([stream] rfi_idr_ms%s); a loss burst is summed "
           "up in one line when it ends",
           capMs, capMs > 0 ? "" : ": no limit");
    // Native (lane wave1-burst): the interface the stream comes in on, for the latency line's rx drop misc
    if (!mlxr_net_ifname_for(host->address, g_rxIf, sizeof(g_rxIf))) g_rxIf[0] = 0;
    mlxr_rx_misc_read("/proc", g_rxIf, &g_rxPrev);
    MLOG_I("video: receive drops beyond the udp counters are read for %s (rx drop misc in the 5 s line)",
           g_rxIf[0] ? g_rxIf : "no interface (not found)");
    const char* prioEnv = getenv("MLXR_VIDEO_PRIORITY");
    g_videoPrio = mlxr_prio_mode_parse(prioEnv, MLXR_PRIO_NICE);
    MLOG_I("video: receive and decode threads to %s (MLXR_VIDEO_PRIORITY=%s)",
           g_videoPrio == MLXR_PRIO_OFF ? "normal priority" : g_videoPrio == MLXR_PRIO_REALTIME ? "realtime" : "nice -10",
           prioEnv ? prioEnv : "unset");

    // Last guard: the SDP's maxFPS and clientRefreshRateX100 (which Apollo builds its virtual
    // display from) never exceed the Frame's 144 Hz, whatever filled the configuration.
    // Native (lane refresh): was 90. The display's own rate (72, 90, 120 or 144, SteamVR's
    // per-app setting) is applied before this, where the launch is built (app/main.c)
    if (cfg->fps > MLXR_FPS_CAP) {
        MLOG_W("stream fps %d is above the headset's %d: clamped", cfg->fps, MLXR_FPS_CAP);
        cfg->fps = MLXR_FPS_CAP;
    }
    if (cfg->clientRefreshRateX100 > MLXR_FPS_CAP * 100) cfg->clientRefreshRateX100 = MLXR_FPS_CAP * 100;
    // Native (lane refresh): the host cadence line judges the frames against this stream's period
    pthread_mutex_lock(&g_latLock);
    memset(&g_cadence, 0, sizeof(g_cadence));
    mlxr_cadence_set_fps(&g_cadence, cfg->fps);
    memset(&g_hud, 0, sizeof(g_hud));
    atomic_store(&g_poor, 0);
    pthread_mutex_unlock(&g_latLock);

    AUDIO_RENDERER_CALLBACKS noAudio;
    LiInitializeAudioCallbacks(&noAudio);
    g_setKbps = cfg->bitrate;
    MLOG_I("starting stream to %s: %dx%d %d fps %d kbps, formats 0x%x", host->address, cfg->width, cfg->height,
           cfg->fps, cfg->bitrate, cfg->supportedVideoFormats);
    xrPadBridgeStart();
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
    xrPadBridgeStop(); // the host hears the pad unplugged before the connection goes
    LiStopConnection();
    atomic_store(&g_connected, 0);
}

int stream_running(void) { return atomic_load(&g_connected); }
int stream_failed(void) { return atomic_load(&g_failed); }
int stream_termination_code(void) { return atomic_load(&g_termCode); }

int stream_exec_server_cmd(int index) {
    if (!atomic_load(&g_connected) || index < 0 || index > 255) return 0;
    return mlxrSendExecServerCmd((uint8_t)index);
}

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

void stream_hscroll(int amount) {
    if (atomic_load(&g_connected)) LiSendHighResHScrollEvent((short)amount);
}

void stream_mouse_move(int dx, int dy) {
    if (atomic_load(&g_connected)) LiSendMouseMoveEvent((short)dx, (short)dy);
}

int stream_hud(MlxrHudCum* out) {
    memset(out, 0, sizeof(*out));
    if (!atomic_load(&g_connected)) return 0;
    uint32_t rtt = 0, var = 0;
    LiGetEstimatedRttInfo(&rtt, &var);
    pthread_mutex_lock(&g_latLock);
    *out = g_hud;
    if (rtt) mlxr_rfi_watch_set_rtt(&g_rfiWatch, rtt); // Native (lane wave1-burst): about once a second
    pthread_mutex_unlock(&g_latLock);
    out->valid = 1;
    out->setKbps = g_setKbps;
    out->rttMs = rtt;
    out->rttVarMs = var;
    out->poor = atomic_load(&g_poor);
    const RTP_VIDEO_STATS* rs = LiGetRTPVideoStats();
    if (rs != NULL) {
        out->fecRecovered = rs->packetCountFecRecovered;
        out->fecFailed = rs->packetCountFecFailed;
    }
    if (g_hw) {
        HwVideoStats h;
        hw_video_stats(g_hw, &h);
        out->decodeN = h.takenTimed;
        out->decodeMsSum = (double)h.decodeNsSum / 1e6;
    }
    return 1;
}

void stream_stats(char* buf, int len) {
    uint32_t rtt = 0, var = 0;
    LiGetEstimatedRttInfo(&rtt, &var);
    char hw[256] = "";
    if (g_hw) {
        HwVideoStats h;
        hw_video_stats(g_hw, &h);
        // Native (lane au-scatter): the largest AU and the AUs refused
        snprintf(hw, sizeof(hw),
                 "; hw decode %.1f ms (last %.1f), frames %llu, skipped %llu, errors %llu, largest AU %llu bytes in %llu "
                 "parts, %llu refused (length mismatch)",
                 h.avgDecodeMs, h.lastDecodeMs, (unsigned long long)h.frames, (unsigned long long)h.skipped,
                 (unsigned long long)h.errors, (unsigned long long)h.largestAuBytes,
                 (unsigned long long)h.largestAuParts, (unsigned long long)h.refusedAus);
    }
    snprintf(buf, (size_t)len, "bitrate set %d kbps, units %llu, idr requests %llu, rtt %u ms, pending video %d%s",
             g_setKbps, (unsigned long long)atomic_load(&g_units), (unsigned long long)atomic_load(&g_idr), rtt,
             LiGetPendingVideoFrames(), hw);
}
