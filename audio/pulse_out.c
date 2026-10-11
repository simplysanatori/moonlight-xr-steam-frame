#include "pulse_out.h"

#include "jitter.h"
#include "pcmq.h"

#include <pulse/pulseaudio.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// ---- the shared context ----------------------------------------------------

static struct {
    pthread_mutex_t lock;
    int refs;
    pa_threaded_mainloop* loop;
    pa_context* ctx;
    _Atomic int failed;
} G = {.lock = PTHREAD_MUTEX_INITIALIZER};

static void fail(char* err, int errLen, const char* msg) {
    if (err != NULL && errLen > 0) {
        snprintf(err, (size_t) errLen, "%s", msg);
    }
}

static void contextState(pa_context* c, void* userdata) {
    pa_context_state_t st = pa_context_get_state(c);
    if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) {
        atomic_store(&G.failed, 1);
    }
}

static int nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int) (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// 64-bit: the cushion keeps -1 as "none yet" and must not see a wrap
static int64_t nowMs64(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int mlxr_pulse_acquire(pa_threaded_mainloop** loop, pa_context** ctx, char* err, int errLen) {
    pthread_mutex_lock(&G.lock);
    if (G.refs > 0 && !atomic_load(&G.failed)) {
        G.refs++;
        *loop = G.loop;
        *ctx = G.ctx;
        pthread_mutex_unlock(&G.lock);
        return 0;
    }
    if (G.refs > 0) {
        fail(err, errLen, "the PulseAudio connection is down");
        pthread_mutex_unlock(&G.lock);
        return -1;
    }
    atomic_store(&G.failed, 0);
    G.loop = pa_threaded_mainloop_new();
    if (G.loop == NULL) {
        fail(err, errLen, "pa_threaded_mainloop_new failed");
        pthread_mutex_unlock(&G.lock);
        return -1;
    }
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "Moonlight XR");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "org.moonlightxr.native");
    pa_proplist_sets(props, PA_PROP_MEDIA_ROLE, "game");
    G.ctx = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(G.loop), "Moonlight XR",
                                         props);
    pa_proplist_free(props);
    if (G.ctx == NULL) {
        pa_threaded_mainloop_free(G.loop);
        G.loop = NULL;
        fail(err, errLen, "pa_context_new failed");
        pthread_mutex_unlock(&G.lock);
        return -1;
    }
    pa_context_set_state_callback(G.ctx, contextState, NULL);
    if (pa_threaded_mainloop_start(G.loop) != 0) {
        pa_context_unref(G.ctx);
        pa_threaded_mainloop_free(G.loop);
        G.ctx = NULL;
        G.loop = NULL;
        fail(err, errLen, "pa_threaded_mainloop_start failed");
        pthread_mutex_unlock(&G.lock);
        return -1;
    }
    pa_threaded_mainloop_lock(G.loop);
    int rc = pa_context_connect(G.ctx, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);
    pa_threaded_mainloop_unlock(G.loop);
    int ok = 0;
    if (rc == 0) {
        int start = nowMs();
        while (nowMs() - start < 3000) {
            pa_threaded_mainloop_lock(G.loop);
            pa_context_state_t st = pa_context_get_state(G.ctx);
            pa_threaded_mainloop_unlock(G.loop);
            if (st == PA_CONTEXT_READY) {
                ok = 1;
                break;
            }
            if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) {
                break;
            }
            usleep(2000);
        }
    }
    if (!ok) {
        pa_threaded_mainloop_lock(G.loop);
        pa_context_disconnect(G.ctx);
        pa_context_unref(G.ctx);
        pa_threaded_mainloop_unlock(G.loop);
        pa_threaded_mainloop_stop(G.loop);
        pa_threaded_mainloop_free(G.loop);
        G.ctx = NULL;
        G.loop = NULL;
        fail(err, errLen, "cannot connect to the PulseAudio server");
        pthread_mutex_unlock(&G.lock);
        return -1;
    }
    G.refs = 1;
    *loop = G.loop;
    *ctx = G.ctx;
    pthread_mutex_unlock(&G.lock);
    return 0;
}

void mlxr_pulse_release(void) {
    pthread_mutex_lock(&G.lock);
    if (G.refs > 0 && --G.refs == 0) {
        pa_threaded_mainloop_lock(G.loop);
        pa_context_disconnect(G.ctx);
        pa_context_unref(G.ctx);
        pa_threaded_mainloop_unlock(G.loop);
        pa_threaded_mainloop_stop(G.loop);
        pa_threaded_mainloop_free(G.loop);
        G.ctx = NULL;
        G.loop = NULL;
    }
    pthread_mutex_unlock(&G.lock);
}

// ---- one stream ------------------------------------------------------------

#define CHUNK_FRAMES 1024

struct MlxrPulseOut {
    pa_threaded_mainloop* loop;
    pa_context* ctx;
    pa_stream* stream;
    MlxrPcmQueue* queue;
    int channels, rate;
    size_t frameBytes;
    float* scratch;

    int fadeAfterUnderrun;
    _Atomic int corked;
    _Atomic int dead;
    _Atomic uint32_t underruns;
    _Atomic uint64_t written;
    _Atomic float latencyMs;
    _Atomic float tlengthMs, minreqMs;
    int lastLatencyMs;

    // Adaptive cushion: jit is touched only with the mainloop lock held (write,
    // pump, the underflow callback); the atomics mirror it for stats()
    int adaptive;
    MlxrJitter jit;
    int serverFrames;           // tlength granted, in frames
    int slackFrames;            // queueCapMs in frames
    _Atomic int jitTargetMs, gapMaxMs;
    _Atomic uint32_t jitLate, jitSilence, jitGrows, jitShrinks;
};

void mlxr_pulse_out_params_default(MlxrPulseOutParams* p) {
    memset(p, 0, sizeof *p);
    p->streamName = "Moonlight XR";
    p->role = "game";
    p->channels = 2;
    p->rate = 48000;
    p->tlengthMs = 20;
    p->minreqMs = 5;
    p->prebufMs = -1;
    p->queueCapMs = 10;
    p->adaptive = false;
    p->jitterMinMs = 0;
    p->jitterMaxMs = 40;
}

// Mainloop lock held
static void sampleLatency(MlxrPulseOut* po) {
    int t = nowMs();
    if (t - po->lastLatencyMs >= 250) {
        po->lastLatencyMs = t;
        pa_usec_t usec;
        int neg;
        if (pa_stream_get_latency(po->stream, &usec, &neg) == 0 && !neg) {
            atomic_store(&po->latencyMs, (float) usec / 1000.0f);
        }
    }
}

// Mainloop lock held
static void pump(MlxrPulseOut* po) {
    if (atomic_load(&po->dead) || po->stream == NULL
        || pa_stream_get_state(po->stream) != PA_STREAM_READY || atomic_load(&po->corked)) {
        return;
    }
    // After an underrun (or at the start) wait until the server's buffer and
    // the cushion can both be filled, so the next late burst meets a full
    // buffer instead of a trickle that runs dry again
    if (po->adaptive
        && !mlxr_jitter_may_play(&po->jit, mlxr_pcmq_frames(po->queue), po->serverFrames, po->rate)) {
        sampleLatency(po);
        return;
    }
    size_t w = pa_stream_writable_size(po->stream);
    if (w == (size_t) -1) {
        return;
    }
    size_t frames = w / po->frameBytes;
    while (frames > 0) {
        int ask = frames < CHUNK_FRAMES ? (int) frames : CHUNK_FRAMES;
        int n = mlxr_pcmq_pop(po->queue, po->scratch, ask);
        if (n <= 0) {
            break;
        }
        if (pa_stream_write(po->stream, po->scratch, (size_t) n * po->frameBytes, NULL, 0,
                            PA_SEEK_RELATIVE) < 0) {
            break;
        }
        atomic_fetch_add(&po->written, (uint64_t) n);
        frames -= (size_t) n;
        if (n < ask) {
            break;
        }
    }
    sampleLatency(po);
}

static void writeCallback(pa_stream* s, size_t nbytes, void* userdata) {
    pump((MlxrPulseOut*) userdata);
}

static void underflowCallback(pa_stream* s, void* userdata) {
    MlxrPulseOut* po = (MlxrPulseOut*) userdata;
    atomic_fetch_add(&po->underruns, 1);
    if (po->adaptive) {
        mlxr_jitter_underrun(&po->jit);
    }
    if (po->fadeAfterUnderrun) {
        mlxr_pcmq_mark_discontinuity(po->queue);
    }
}

static void stateCallback(pa_stream* s, void* userdata) {
    MlxrPulseOut* po = (MlxrPulseOut*) userdata;
    pa_stream_state_t st = pa_stream_get_state(s);
    if (st == PA_STREAM_FAILED || st == PA_STREAM_TERMINATED) {
        atomic_store(&po->dead, 1);
    }
}

static int msToBytes(const MlxrPulseOut* po, int ms) {
    return (int) ((long long) ms * po->rate / 1000) * (int) po->frameBytes;
}

MlxrPulseOut* mlxr_pulse_out_open(const MlxrPulseOutParams* p, char* err, int errLen) {
    if (p->channels < 1 || p->channels > 2 || p->rate < 8000) {
        fail(err, errLen, "unsupported stream format");
        return NULL;
    }
    MlxrPulseOut* po = (MlxrPulseOut*) calloc(1, sizeof *po);
    if (po == NULL) {
        fail(err, errLen, "out of memory");
        return NULL;
    }
    po->channels = p->channels;
    po->rate = p->rate;
    po->fadeAfterUnderrun = !p->noFadeAfterUnderrun;
    po->frameBytes = sizeof(float) * (size_t) p->channels;
    po->scratch = (float*) malloc(po->frameBytes * CHUNK_FRAMES);
    int capFrames = p->queueCapMs * p->rate / 1000;
    if (capFrames < 1) {
        capFrames = 1;
    }
    po->slackFrames = capFrames;
    po->adaptive = p->adaptive;
    int allocFrames = capFrames;
    if (po->adaptive) {
        mlxr_jitter_init(&po->jit, p->jitterMinMs, p->jitterMaxMs);
        // Room for the largest cushion plus a refill of a generous server
        // buffer; the cap in force is set once the server says what it granted
        MlxrJitter big = po->jit;
        big.targetMs = big.maxMs;
        int serverMax = (p->tlengthMs > 0 ? p->tlengthMs : 20) * 2 * p->rate / 1000;
        allocFrames = mlxr_jitter_cap_frames(&big, serverMax, capFrames, p->rate);
        atomic_init(&po->jitTargetMs, po->jit.targetMs);
    }
    po->queue = mlxr_pcmq_create(p->channels, allocFrames);
    if (po->scratch == NULL || po->queue == NULL) {
        fail(err, errLen, "out of memory");
        mlxr_pulse_out_close(po);
        return NULL;
    }
    atomic_init(&po->latencyMs, 0.0f);
    atomic_init(&po->tlengthMs, 0.0f);
    atomic_init(&po->minreqMs, 0.0f);
    if (mlxr_pulse_acquire(&po->loop, &po->ctx, err, errLen) != 0) {
        po->loop = NULL;
        mlxr_pulse_out_close(po);
        return NULL;
    }
    int tlength = p->tlengthMs > 0 ? p->tlengthMs : 20;
    int minreq = p->minreqMs > 0 ? p->minreqMs : 5;
    int prebuf = p->prebufMs >= 0 ? p->prebufMs : tlength / 2;

    pa_sample_spec ss = {.format = PA_SAMPLE_FLOAT32LE, .rate = (uint32_t) p->rate,
                         .channels = (uint8_t) p->channels};
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_MEDIA_ROLE, p->role != NULL ? p->role : "game");
    // PipeWire names the node after application.name unless told otherwise, and
    // SteamOS's WirePlumber decides on spatialising by the node name
    pa_proplist_sets(props, "node.name", p->streamName != NULL ? p->streamName : "Moonlight XR");

    pa_threaded_mainloop_lock(po->loop);
    po->stream = pa_stream_new_with_proplist(po->ctx, p->streamName != NULL ? p->streamName
                                                                            : "Moonlight XR",
                                             &ss, NULL, props);
    pa_proplist_free(props);
    if (po->stream == NULL) {
        pa_threaded_mainloop_unlock(po->loop);
        fail(err, errLen, pa_strerror(pa_context_errno(po->ctx)));
        mlxr_pulse_out_close(po);
        return NULL;
    }
    pa_stream_set_write_callback(po->stream, writeCallback, po);
    pa_stream_set_underflow_callback(po->stream, underflowCallback, po);
    pa_stream_set_state_callback(po->stream, stateCallback, po);
    pa_buffer_attr attr;
    attr.maxlength = (uint32_t) msToBytes(po, tlength * 4);
    attr.tlength = (uint32_t) msToBytes(po, tlength);
    attr.minreq = (uint32_t) msToBytes(po, minreq);
    attr.prebuf = (uint32_t) msToBytes(po, prebuf);
    attr.fragsize = (uint32_t) -1;
    int flags = PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE
                | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_NOT_MONOTONIC;
    int rc = pa_stream_connect_playback(po->stream, p->sink, &attr, (pa_stream_flags_t) flags,
                                        NULL, NULL);
    pa_threaded_mainloop_unlock(po->loop);
    if (rc < 0) {
        fail(err, errLen, pa_strerror(pa_context_errno(po->ctx)));
        mlxr_pulse_out_close(po);
        return NULL;
    }
    int ready = 0;
    int start = nowMs();
    while (nowMs() - start < 3000) {
        pa_threaded_mainloop_lock(po->loop);
        pa_stream_state_t st = pa_stream_get_state(po->stream);
        pa_threaded_mainloop_unlock(po->loop);
        if (st == PA_STREAM_READY) {
            ready = 1;
            break;
        }
        if (st == PA_STREAM_FAILED || st == PA_STREAM_TERMINATED) {
            break;
        }
        usleep(2000);
    }
    if (!ready) {
        fail(err, errLen, "the PulseAudio stream did not become ready");
        mlxr_pulse_out_close(po);
        return NULL;
    }
    pa_threaded_mainloop_lock(po->loop);
    const pa_buffer_attr* got = pa_stream_get_buffer_attr(po->stream);
    if (got != NULL) {
        atomic_store(&po->tlengthMs, 1000.0f * (float) (got->tlength / po->frameBytes) / p->rate);
        atomic_store(&po->minreqMs, 1000.0f * (float) (got->minreq / po->frameBytes) / p->rate);
        po->serverFrames = (int) (got->tlength / po->frameBytes);
    }
    else {
        po->serverFrames = tlength * p->rate / 1000;
    }
    if (po->adaptive) {
        mlxr_pcmq_set_capacity(po->queue, mlxr_jitter_cap_frames(&po->jit, po->serverFrames,
                                                                 po->slackFrames, po->rate));
    }
    pa_threaded_mainloop_unlock(po->loop);
    return po;
}

void mlxr_pulse_out_close(MlxrPulseOut* po) {
    if (po == NULL) {
        return;
    }
    if (po->loop != NULL) {
        pa_threaded_mainloop_lock(po->loop);
        if (po->stream != NULL) {
            pa_stream_set_write_callback(po->stream, NULL, NULL);
            pa_stream_set_underflow_callback(po->stream, NULL, NULL);
            pa_stream_set_state_callback(po->stream, NULL, NULL);
            if (pa_stream_get_state(po->stream) == PA_STREAM_READY
                || pa_stream_get_state(po->stream) == PA_STREAM_CREATING) {
                pa_stream_disconnect(po->stream);
            }
            pa_stream_unref(po->stream);
            po->stream = NULL;
        }
        pa_threaded_mainloop_unlock(po->loop);
        mlxr_pulse_release();
    }
    mlxr_pcmq_destroy(po->queue);
    free(po->scratch);
    free(po);
}

// Mainloop lock held: the cushion's bookkeeping for one write, after the push
static void jitterAfterPush(MlxrPulseOut* po, int64_t now) {
    int gap = po->jit.lastPacketMs < 0 ? 0 : (int) (now - po->jit.lastPacketMs);
    float serverMs = 1000.0f * (float) po->serverFrames / (float) po->rate;
    if (mlxr_jitter_packet(&po->jit, now, serverMs)) {
        mlxr_pcmq_set_capacity(po->queue, mlxr_jitter_cap_frames(&po->jit, po->serverFrames,
                                                                 po->slackFrames, po->rate));
    }
    pump(po);
    int cut = mlxr_jitter_trim(&po->jit, now, mlxr_pcmq_frames(po->queue), po->rate);
    if (cut > 0) {
        mlxr_pcmq_skip(po->queue, cut);
    }
    if (gap < po->jit.silenceMs && gap > atomic_load(&po->gapMaxMs)) {
        atomic_store(&po->gapMaxMs, gap);
    }
    atomic_store(&po->jitTargetMs, po->jit.targetMs);
    atomic_store(&po->jitLate, po->jit.lateUnderruns);
    atomic_store(&po->jitSilence, po->jit.silenceUnderruns);
    atomic_store(&po->jitGrows, po->jit.grows);
    atomic_store(&po->jitShrinks, po->jit.shrinks);
}

int mlxr_pulse_out_write(MlxrPulseOut* po, const float* frames, int n) {
    if (atomic_load(&po->dead) || atomic_load(&po->corked) || n <= 0) {
        return 0;
    }
    pa_threaded_mainloop_lock(po->loop);
    int dropped = mlxr_pcmq_push(po->queue, frames, n);
    if (po->adaptive) {
        jitterAfterPush(po, nowMs64());
    }
    else {
        pump(po);
    }
    pa_threaded_mainloop_unlock(po->loop);
    return dropped;
}

static void unrefOp(pa_operation* op) {
    if (op != NULL) {
        pa_operation_unref(op);
    }
}

void mlxr_pulse_out_set_corked(MlxrPulseOut* po, bool corked) {
    if ((atomic_load(&po->corked) != 0) == corked || atomic_load(&po->dead)) {
        return;
    }
    pa_threaded_mainloop_lock(po->loop);
    atomic_store(&po->corked, corked ? 1 : 0);
    if (corked) {
        mlxr_pcmq_clear(po->queue);
        unrefOp(pa_stream_flush(po->stream, NULL, NULL));
        unrefOp(pa_stream_cork(po->stream, 1, NULL, NULL));
    } else {
        unrefOp(pa_stream_cork(po->stream, 0, NULL, NULL));
        if (po->adaptive) {
            mlxr_jitter_restart(&po->jit);
        }
        if (po->fadeAfterUnderrun) {
            mlxr_pcmq_mark_discontinuity(po->queue);
        }
    }
    pa_threaded_mainloop_unlock(po->loop);
}

void mlxr_pulse_out_stats(MlxrPulseOut* po, MlxrPulseOutStats* out) {
    MlxrPcmQueueStats q;
    mlxr_pcmq_stats(po->queue, &q);
    memset(out, 0, sizeof *out);
    out->underruns = atomic_load(&po->underruns);
    out->overruns = q.overruns;
    out->droppedFrames = q.droppedFrames;
    out->writtenFrames = atomic_load(&po->written);
    out->queueMs = 1000.0f * (float) mlxr_pcmq_frames(po->queue) / (float) po->rate;
    out->latencyMs = atomic_load(&po->latencyMs);
    out->tlengthMs = atomic_load(&po->tlengthMs);
    out->minreqMs = atomic_load(&po->minreqMs);
    out->dead = atomic_load(&po->dead);
    out->running = !out->dead;
    out->trimmedFrames = q.skippedFrames;
    if (po->adaptive) {
        out->jitterTargetMs = atomic_load(&po->jitTargetMs);
        out->lateUnderruns = atomic_load(&po->jitLate);
        out->silenceUnderruns = atomic_load(&po->jitSilence);
        out->jitterGrows = atomic_load(&po->jitGrows);
        out->jitterShrinks = atomic_load(&po->jitShrinks);
        out->gapMaxMs = atomic_load(&po->gapMaxMs);
    }
}

int mlxr_pulse_out_take_gap_max(MlxrPulseOut* po) {
    return atomic_exchange(&po->gapMaxMs, 0);
}
