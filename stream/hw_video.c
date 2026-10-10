// Moonlight XR Native: hardware video path (M2). GPLv3.
#include "hw_video.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "../xr/mlxr_log.h"

struct HwVideo {
    mlxr_vdec* dec;
    pthread_t thread;
    atomic_int stop;
    bool joined;
    void (*requestIdr)(void);
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool havePending;
    mlxr_vframe pending;
    bool formatChange; // decode thread waits until the renderer is done
    HwVideoStats st;
};

static _Atomic(HwVideo*) g_current;
HwVideo* hw_video_current(void) { return atomic_load(&g_current); }
void hw_video_set_current(HwVideo* v) { atomic_store(&g_current, v); }

static void* decodeThread(void* arg) {
    HwVideo* v = arg;
    while (!atomic_load(&v->stop)) {
        mlxr_vframe f;
        int r = mlxr_vdec_receive(v->dec, &f, 50);
        if (r == MLXR_VDEC_AGAIN) continue;
        if (r == MLXR_VDEC_FORMAT_CHANGE) {
            MLOG_I("video: resolution change, waiting for the renderer to drop its imports");
            pthread_mutex_lock(&v->lock);
            if (v->havePending) {
                mlxr_vdec_release(v->dec, &v->pending);
                v->havePending = false;
            }
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
        double ms = (double)(f.t_dequeue_ns - f.t_submit_ns) * 1e-6;
        pthread_mutex_lock(&v->lock);
        if (v->havePending) { // the loop never took it: straight back to the decoder
            mlxr_vdec_release(v->dec, &v->pending);
            v->st.skipped++;
        }
        v->pending = f;
        v->havePending = true;
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

bool hw_video_take(HwVideo* v, mlxr_vframe* out) {
    pthread_mutex_lock(&v->lock);
    bool got = v->havePending;
    if (got) {
        *out = v->pending;
        v->havePending = false;
        v->st.taken++;
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
    if (v->havePending) mlxr_vdec_release(v->dec, &v->pending);
    mlxr_vdec_close(v->dec);
    pthread_mutex_destroy(&v->lock);
    pthread_cond_destroy(&v->cond);
    free(v);
}
