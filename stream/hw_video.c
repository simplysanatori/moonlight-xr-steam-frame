// Moonlight XR Native: hardware video path (M2). GPLv3.
#include "hw_video.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

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
    // Native (lane av-latency): with reference frame invalidation on, a
    // picture the decoder flags as damaged asks the host for an IDR frame
    // (at most every 500 ms), the old behaviour, as a safety net
    bool idrOnCorrupt;
    uint64_t lastCorruptIdrNs;
    bool formatChange; // decode thread waits until the renderer is done
    HwVideoStats st;
};

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

// Lock held
static void popOldest(HwVideo* v, mlxr_vframe* out) {
    *out = v->queue[v->head];
    v->head = (v->head + 1) % MLXR_PACER_MAX_QUEUED;
    v->queued--;
    v->st.taken++;
}

static void* decodeThread(void* arg) {
    HwVideo* v = arg;
    while (!atomic_load(&v->stop)) {
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
            if (mlxr_vdec_needs_idr(r)) {
                v->st.idrRequests++;
                if (v->requestIdr) v->requestIdr();
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
        pthread_mutex_lock(&v->lock);
        if (v->queued == MLXR_PACER_MAX_QUEUED) { // the loop fell behind: the oldest goes back
            dropOldest(v, 1, &v->st.skipped);
        }
        v->queue[(v->head + v->queued) % MLXR_PACER_MAX_QUEUED] = f;
        v->queued++;
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
    mlxr_pacer_init(&v->pacer, 90);
    v->pacerHz = 90;
    pthread_mutex_init(&v->lock, NULL);
    pthread_cond_init(&v->cond, NULL);
    pthread_create(&v->thread, NULL, decodeThread, v);
    MLOG_I("video: hardware %s decoder open (%ux%u)", codec == MLXR_CODEC_HEVC ? "HEVC" : "H.264", width, height);
    return v;
}

int hw_video_submit(HwVideo* v, const void* const* parts, const uint32_t* sizes, uint32_t n, int64_t pts) {
    int r = mlxr_vdec_submit_parts(v->dec, parts, sizes, n, pts);
    v->st.submitted++;
    if (r != MLXR_VDEC_OK) {
        v->st.errors++;
        if (mlxr_vdec_needs_idr(r)) {
            v->st.idrRequests++;
            return -1;
        }
    }
    return 0;
}

void hw_video_set_idr_on_corrupt(HwVideo* v, bool on) { v->idrOnCorrupt = on; }

bool hw_video_take(HwVideo* v, mlxr_vframe* out) {
    pthread_mutex_lock(&v->lock);
    bool got = v->queued > 0;
    if (got) {
        dropOldest(v, v->queued - 1, &v->st.skipped);
        popOldest(v, out);
    }
    pthread_mutex_unlock(&v->lock);
    return got;
}

bool hw_video_take_refresh(HwVideo* v, int paced, int displayHz, mlxr_vframe* out) {
    pthread_mutex_lock(&v->lock);
    if (displayHz > 0 && displayHz != v->pacerHz) {
        MlxrPacer keep = v->pacer;
        mlxr_pacer_init(&v->pacer, displayHz);
        v->pacer.refreshes = keep.refreshes;
        v->pacer.shown = keep.shown;
        v->pacer.holes = keep.holes;
        v->pacer.catchUpDrops = keep.catchUpDrops;
        v->pacer.queuedSum = keep.queuedSum;
        v->pacerHz = displayHz;
    }
    bool got;
    if (paced) {
        int drop = 0;
        int show = mlxr_pacer_refresh(&v->pacer, v->queued, &drop);
        dropOldest(v, drop, NULL);
        got = show && v->queued > 0;
        if (got) popOldest(v, out);
    }
    else {
        int queued = v->queued;
        got = queued > 0;
        if (got) {
            dropOldest(v, queued - 1, &v->st.skipped);
            popOldest(v, out);
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
