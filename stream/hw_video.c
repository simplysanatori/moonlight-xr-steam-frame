// Moonlight XR Native: hardware video path (M2). GPLv3.
#include "hw_video.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../audio/thread_prio.h"
#include "../xr/mlxr_log.h"
#include "pacer.h"

struct HwVideo {
    mlxr_vdec* dec;
    pthread_t thread;
    atomic_int stop;
    bool joined;
    void (*requestIdr)(void);
    pthread_mutex_t lock;
    pthread_cond_t cond;
    // Native (lane av-latency): decoded frames waiting for the frame loop,
    // oldest first (was a one-frame mailbox); the pacer picks from it
    mlxr_vframe queue[MLXR_PACER_MAX_QUEUED];
    int queued, head;
    MlxrPacer pacer;
    int pacerHz;
    int pacerLowLatency; // Native (lane perf): the pacer's rules (mode 2)
    int cushion;         // Native (lane perf): the pacer's lenient target
    atomic_int prioMode;  // Native (lane perf): decode thread priority wanted (MlxrPrioMode)
    atomic_uint_fast64_t submittedOk, decodedOut; // Native (lane perf): AUs in the decoder
    // Native (lane perf): the latch. When the loop finds the queue empty while
    // an AU went into the decoder moments ago and its picture is not out
    // yet, it waits up to latchNs for it instead of repeating the last
    // picture and showing this one a refresh (11 ms) late. Native (lane refresh): the wait is
    // set for a 90 Hz refresh and shrinks with a faster one (latchFor)
    uint64_t latchNs;
    atomic_uint_fast64_t lastSubmitNs; // CLOCK_MONOTONIC, the newest AU queued
    uint64_t lastOutSubmitNs;          // its t_submit_ns, the newest picture out (lock)
    // Native (lane av-latency): with reference frame invalidation on, a
    // picture the decoder flags as damaged asks the host for an IDR frame
    // (at most every 500 ms), the old behaviour, as a safety net
    bool idrOnCorrupt;
    uint64_t lastCorruptIdrNs;
    // Native (lane au-scatter): the IDR request limit of the decoder error
    // paths (MLXR_VIDEO_IDR_MIN_MS), shared by the decode thread and the
    // session's submit thread
    uint64_t idrMinNs;
    atomic_uint_fast64_t lastErrIdrNs;
    bool formatChange; // decode thread waits until the renderer is done
    HwVideoStats st;
};

static uint64_t monoNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// Native (lane au-scatter): true when a decoder error may ask for an IDR
// frame now (none asked in the last idrMinNs); the caller then asks
static bool errorIdrDue(HwVideo* v) {
    uint64_t now = monoNs();
    uint64_t last = atomic_load(&v->lastErrIdrNs);
    while (last == 0 || now - last >= v->idrMinNs) {
        if (atomic_compare_exchange_weak(&v->lastErrIdrNs, &last, now)) return true;
    }
    return false;
}

static _Atomic(HwVideo*) g_current;
HwVideo* hw_video_current(void) { return atomic_load(&g_current); }
void hw_video_set_current(HwVideo* v) { atomic_store(&g_current, v); }

// Lock held: the n oldest queued frames straight back to the decoder
static void dropOldest(HwVideo* v, int n, uint64_t* count) {
    for (int i = 0; i < n && v->queued > 0; i++) {
        mlxr_vdec_release(v->dec, &v->queue[v->head]);
        v->head = (v->head + 1) % MLXR_PACER_MAX_QUEUED;
        v->queued--;
        if (count) (*count)++;
    }
}

// Lock held. Native (lane perf): nowNs > 0 times the frame's decode and its
// wait in the queue
static void popOldest(HwVideo* v, mlxr_vframe* out, uint64_t nowNs) {
    *out = v->queue[v->head];
    v->head = (v->head + 1) % MLXR_PACER_MAX_QUEUED;
    v->queued--;
    v->st.taken++;
    if (nowNs > 0 && out->t_dequeue_ns >= out->t_submit_ns && nowNs >= out->t_dequeue_ns) {
        uint64_t dec = out->t_dequeue_ns - out->t_submit_ns, wait = nowNs - out->t_dequeue_ns;
        v->st.takenTimed++;
        v->st.decodeNsSum += dec;
        if (dec > v->st.decodeNsMax) v->st.decodeNsMax = dec;
        v->st.waitNsSum += wait;
        if (wait > v->st.waitNsMax) v->st.waitNsMax = wait;
    }
}

static void* decodeThread(void* arg) {
    HwVideo* v = arg;
    int prioDone = 0;
    while (!atomic_load(&v->stop)) {
        // Native (lane perf): once, above the frame loop's helpers and the
        // depth engine (MLXR_VIDEO_PRIORITY, see session.c)
        if (!prioDone && atomic_load(&v->prioMode) != MLXR_PRIO_OFF) {
            prioDone = 1;
            char said[256];
            mlxr_thread_raise_priority((MlxrPrioMode)atomic_load(&v->prioMode), said, sizeof(said));
            MLOG_I("video: decode thread %s", said);
        }
        mlxr_vframe f;
        int r = mlxr_vdec_receive(v->dec, &f, 50);
        if (r == MLXR_VDEC_AGAIN) continue;
        if (r == MLXR_VDEC_FORMAT_CHANGE) {
            MLOG_I("video: resolution change, waiting for the renderer to drop its imports");
            pthread_mutex_lock(&v->lock);
            dropOldest(v, v->queued, NULL);
            v->formatChange = true;
            while (v->formatChange && !atomic_load(&v->stop)) pthread_cond_wait(&v->cond, &v->lock);
            pthread_mutex_unlock(&v->lock);
            continue;
        }
        if (r != MLXR_VDEC_OK) {
            v->st.errors++;
            // Native (lane au-scatter): a picture the decoder flagged as an
            // error is an AU out of the decoder too; without this the AUs in
            // the decoder only grew and the latch waited for pictures that
            // were never coming
            if (r == MLXR_VDEC_E_DECODE) {
                atomic_fetch_add(&v->decodedOut, 1);
                pthread_mutex_lock(&v->lock);
                pthread_cond_broadcast(&v->cond); // a latched loop stops waiting
                pthread_mutex_unlock(&v->lock);
            }
            if (mlxr_vdec_needs_idr(r) && v->requestIdr && errorIdrDue(v)) {
                v->st.idrRequests++;
                v->requestIdr();
            }
            if (r == MLXR_VDEC_E_FATAL) {
                MLOG_E("video: decoder fatal error; stopping hardware decode");
                break;
            }
            continue;
        }
        if (f.corrupt) {
            v->st.corrupt++;
            if (v->idrOnCorrupt && v->requestIdr && f.t_dequeue_ns - v->lastCorruptIdrNs > 500000000ull) {
                v->lastCorruptIdrNs = f.t_dequeue_ns;
                v->st.idrRequests++;
                v->requestIdr();
            }
        }
        double ms = (double)(f.t_dequeue_ns - f.t_submit_ns) * 1e-6;
        atomic_fetch_add(&v->decodedOut, 1);
        pthread_mutex_lock(&v->lock);
        if (v->queued == MLXR_PACER_MAX_QUEUED) { // the loop fell behind: the oldest goes back
            dropOldest(v, 1, &v->st.skipped);
        }
        v->queue[(v->head + v->queued) % MLXR_PACER_MAX_QUEUED] = f;
        v->queued++;
        if (f.t_submit_ns > v->lastOutSubmitNs) v->lastOutSubmitNs = f.t_submit_ns;
        pthread_cond_broadcast(&v->cond); // a latched loop takes it now
        v->st.frames++;
        v->st.lastDecodeMs = ms;
        v->st.avgDecodeMs = v->st.frames == 1 ? ms : v->st.avgDecodeMs * 0.95 + ms * 0.05;
        pthread_mutex_unlock(&v->lock);
    }
    return NULL;
}

HwVideo* hw_video_open(mlxr_codec codec, uint32_t width, uint32_t height, void (*requestIdr)(void)) {
    HwVideo* v = calloc(1, sizeof(*v));
    mlxr_vdec_config cfg;
    mlxr_vdec_config_defaults(&cfg);
    cfg.codec = codec;
    cfg.width_hint = width;
    cfg.height_hint = height;
    if (mlxr_vdec_open(&cfg, &v->dec) != MLXR_VDEC_OK) {
        MLOG_E("video: cannot open the hardware decoder");
        free(v);
        return NULL;
    }
    v->requestIdr = requestIdr;
    // Native (lane refresh): 90 until the frame loop's first refresh gives the display's rate
    // (hw_video_take_refresh starts the pacer again at it: 72, 90, 120 or 144)
    mlxr_pacer_init(&v->pacer, 90);
    v->pacerHz = 90;
    // Native (lane perf): MLXR_PACER_CUSHION (3, or 2) and MLXR_VIDEO_LATCH_MS
    // (4 by default, 0 off)
    const char* cushion = getenv("MLXR_PACER_CUSHION");
    v->cushion = cushion && cushion[0] == '2' ? 2 : 3;
    v->pacer.cushion = v->cushion;
    const char* latch = getenv("MLXR_VIDEO_LATCH_MS");
    double latchMs = latch && *latch ? atof(latch) : 4.0;
    if (latchMs < 0.0) latchMs = 0.0;
    if (latchMs > 8.0) latchMs = 8.0;
    v->latchNs = (uint64_t)(latchMs * 1e6);
    MLOG_I("video: pacer cushion %d frames (MLXR_PACER_CUSHION=%s), latch for a picture in the decoder %.1f ms "
           "(MLXR_VIDEO_LATCH_MS=%s)",
           v->cushion, cushion ? cushion : "unset", latchMs, latch ? latch : "unset");
    pthread_mutex_init(&v->lock, NULL);
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&v->cond, &ca);
    pthread_condattr_destroy(&ca);
    // Native (lane au-scatter): MLXR_VIDEO_IDR_MIN_MS (100 by default, 0 no limit)
    const char* idrMin = getenv("MLXR_VIDEO_IDR_MIN_MS");
    int idrMinMs = idrMin && *idrMin ? atoi(idrMin) : 100;
    if (idrMinMs < 0) idrMinMs = 0;
    v->idrMinNs = (uint64_t)idrMinMs * 1000000ull;
    MLOG_I("video: decoder errors ask for an IDR frame at most every %d ms (MLXR_VIDEO_IDR_MIN_MS=%s)", idrMinMs,
           idrMin ? idrMin : "unset");
    pthread_create(&v->thread, NULL, decodeThread, v);
    MLOG_I("video: hardware %s decoder open (%ux%u)", codec == MLXR_CODEC_HEVC ? "HEVC" : "H.264", width, height);
    return v;
}

int hw_video_submit(HwVideo* v, const void* const* parts, const uint32_t* sizes, uint32_t n, int64_t pts) {
    // Native (lane au-scatter): the largest AU, for the stats line
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < n; i++) bytes += sizes[i];
    pthread_mutex_lock(&v->lock);
    if (bytes > v->st.largestAuBytes) {
        v->st.largestAuBytes = bytes;
        v->st.largestAuParts = n;
    }
    pthread_mutex_unlock(&v->lock);
    int r = mlxr_vdec_submit_parts(v->dec, parts, sizes, n, pts);
    v->st.submitted++;
    if (r == MLXR_VDEC_OK) {
        atomic_store(&v->lastSubmitNs, monoNs());
        // Native (lane perf): AUs in the decoder now, this one included
        uint64_t in = atomic_fetch_add(&v->submittedOk, 1) + 1;
        uint64_t out = atomic_load(&v->decodedOut);
        uint64_t depth = in > out ? in - out : 0;
        if (depth > 64) depth = 64; // an AU the decoder dropped without a picture
        pthread_mutex_lock(&v->lock);
        v->st.inFlightSum += depth;
        v->st.inFlightSamples++;
        if (depth > v->st.inFlightMax) v->st.inFlightMax = depth;
        pthread_mutex_unlock(&v->lock);
    }
    if (r != MLXR_VDEC_OK) {
        v->st.errors++;
        // Native (lane au-scatter): DR_NEED_IDR only when a request is due
        if (mlxr_vdec_needs_idr(r) && errorIdrDue(v)) {
            v->st.idrRequests++;
            return -1;
        }
    }
    return 0;
}

int hw_video_refuse_au(HwVideo* v) {
    pthread_mutex_lock(&v->lock);
    v->st.refusedAus++;
    pthread_mutex_unlock(&v->lock);
    if (!errorIdrDue(v)) return 0;
    v->st.idrRequests++;
    return -1;
}

void hw_video_set_idr_on_corrupt(HwVideo* v, bool on) { v->idrOnCorrupt = on; }
void hw_video_set_thread_priority(HwVideo* v, int mode) { atomic_store(&v->prioMode, mode); }

bool hw_video_take(HwVideo* v, mlxr_vframe* out) {
    pthread_mutex_lock(&v->lock);
    bool got = v->queued > 0;
    if (got) {
        dropOldest(v, v->queued - 1, &v->st.skipped);
        popOldest(v, out, 0);
    }
    pthread_mutex_unlock(&v->lock);
    return got;
}

// Native (lane refresh): the latch was tuned at 90 Hz (4 ms of an 11.1 ms refresh). On a faster
// display it keeps the same share of the refresh, so the frame loop is never held most of a
// 6.9 ms refresh at 144 Hz: 3.0 ms at 120 Hz, 2.5 ms at 144 Hz. At 90 Hz and below it is as set.
static uint64_t latchFor(uint64_t latchNs, int displayHz) {
    return displayHz > 90 ? latchNs * 90u / (uint64_t)displayHz : latchNs;
}

bool hw_video_take_refresh(HwVideo* v, int mode, int displayHz, uint64_t nowNs, mlxr_vframe* out) {
    pthread_mutex_lock(&v->lock);
    int paced = mode != MLXR_PACE_NEWEST;
    int lowLatency = mode == MLXR_PACE_LOW_LATENCY;
    if ((displayHz > 0 && displayHz != v->pacerHz) || lowLatency != v->pacerLowLatency) {
        MlxrPacer keep = v->pacer;
        if (displayHz > 0) v->pacerHz = displayHz;
        mlxr_pacer_init_mode(&v->pacer, v->pacerHz, lowLatency);
        v->pacer.cushion = v->cushion;
        v->pacerLowLatency = lowLatency;
        v->pacer.refreshes = keep.refreshes;
        v->pacer.shown = keep.shown;
        v->pacer.holes = keep.holes;
        v->pacer.catchUpDrops = keep.catchUpDrops;
        v->pacer.queuedSum = keep.queuedSum;
        v->pacer.staleDrops = keep.staleDrops;
    }
    // Native (lane perf): the latch (see latchNs). Only for a picture that is
    // surely on its way: an AU queued less than 12 ms ago with no picture out
    // for it yet; a stalled decoder therefore costs nothing.
    // Native (lane au-scatter): and only while an AU is in the decoder (an
    // AU that ended in an error leaves no picture to wait for)
    if (v->latchNs > 0 && nowNs > 0 && v->queued == 0) {
        uint64_t sub = atomic_load(&v->lastSubmitNs);
        uint64_t in = atomic_load(&v->submittedOk), out = atomic_load(&v->decodedOut);
        if (sub > v->lastOutSubmitNs && in > out && nowNs < sub + 12000000ull) {
            uint64_t deadline = nowNs + latchFor(v->latchNs, v->pacerHz);
            struct timespec dl = { (time_t)(deadline / 1000000000ull), (long)(deadline % 1000000000ull) };
            v->st.latchWaits++;
            while (v->queued == 0 && !atomic_load(&v->stop) &&
                   atomic_load(&v->submittedOk) > atomic_load(&v->decodedOut)) {
                if (pthread_cond_timedwait(&v->cond, &v->lock, &dl) != 0) break;
            }
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint64_t after = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
            v->st.latchNsSum += after > nowNs ? after - nowNs : 0;
            if (v->queued > 0) v->st.latchHits++;
            nowNs = after;
        }
    }
    bool got;
    if (paced) {
        int drop = 0;
        int show = mlxr_pacer_refresh(&v->pacer, v->queued, &drop);
        dropOldest(v, drop, NULL);
        got = show && v->queued > 0;
        if (got) popOldest(v, out, nowNs);
    }
    else {
        int queued = v->queued;
        got = queued > 0;
        if (got) {
            dropOldest(v, queued - 1, &v->st.skipped);
            popOldest(v, out, nowNs);
        }
        mlxr_pacer_note(&v->pacer, queued, got);
    }
    pthread_mutex_unlock(&v->lock);
    return got;
}

void hw_video_release(HwVideo* v, const mlxr_vframe* f) { mlxr_vdec_release(v->dec, f); }
mlxr_vdec* hw_video_decoder(HwVideo* v) { return v->dec; }

bool hw_video_format_change_pending(HwVideo* v) {
    pthread_mutex_lock(&v->lock);
    bool p = v->formatChange;
    pthread_mutex_unlock(&v->lock);
    return p;
}

void hw_video_format_change_done(HwVideo* v) {
    pthread_mutex_lock(&v->lock);
    int r = mlxr_vdec_apply_format_change(v->dec);
    if (r != MLXR_VDEC_OK) MLOG_E("video: apply_format_change: %s", mlxr_vdec_status_str(r));
    v->formatChange = false;
    pthread_cond_broadcast(&v->cond);
    pthread_mutex_unlock(&v->lock);
}

void hw_video_stats(HwVideo* v, HwVideoStats* s) {
    pthread_mutex_lock(&v->lock);
    *s = v->st;
    s->refreshes = v->pacer.refreshes;
    s->shown = v->pacer.shown;
    s->holes = v->pacer.holes;
    s->catchUpDrops = v->pacer.catchUpDrops;
    s->queuedSum = v->pacer.queuedSum;
    s->staleDrops = v->pacer.staleDrops;
    pthread_mutex_unlock(&v->lock);
}

void hw_video_reset_max(HwVideo* v) {
    pthread_mutex_lock(&v->lock);
    v->st.decodeNsMax = v->st.waitNsMax = v->st.inFlightMax = 0;
    pthread_mutex_unlock(&v->lock);
}

void hw_video_stop(HwVideo* v) {
    if (!v || v->joined) return;
    atomic_store(&v->stop, 1);
    pthread_mutex_lock(&v->lock);
    pthread_cond_broadcast(&v->cond);
    pthread_mutex_unlock(&v->lock);
    pthread_join(v->thread, NULL);
    v->joined = true;
}

void hw_video_close(HwVideo* v) {
    if (!v) return;
    hw_video_stop(v);
    dropOldest(v, v->queued, NULL);
    mlxr_vdec_close(v->dec);
    pthread_mutex_destroy(&v->lock);
    pthread_cond_destroy(&v->cond);
    free(v);
}
