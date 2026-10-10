// The depth thread and its hand-off. See depth_engine.h. GPLv3.
#define _GNU_SOURCE
#include "depth_engine.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xr_depthmap.h"
#include "xr_rate.h"

// Upstream xr_renderer.h: about a tenth of the frame, in texels of a 256
// square map, scaled with the map on each axis
#define DEPTH_LOWPASS_RADIUS 11

// Captures in flight: one being filled by submit, one waiting, one running
#define INPUT_SLOTS 3
// Results: the newest, one the frame loop holds, one being written
#define RESULT_SLOTS 3
#define RING 64
#define RATE_RING 32

int64_t mlxr_depth_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

void mlxr_depth_params_default(MlxrDepthParams* p) {
    p->tauMs = DEPTH_TAU_DEFAULT_MS;
    p->rangeTauMs = DEPTH_RANGE_TAU_DEFAULT_MS;
    p->cutLevel = 1;
    p->global = 1.0f;
    p->local = 1.0f;
    p->convergence = 0.5f;
}

// ---- normalisation: nativeUploadDepth's CPU half ----

struct MlxrDepthNorm {
    int w, h;
    float* tau;
    float* norm;
    float* low;
    float* scratch;
    float* colSums;
    int tauValid;
    int64_t tauNs;
    DepthRange range;
    int64_t rangeNs;
};

MlxrDepthNorm* mlxr_depth_norm_create(int w, int h) {
    MlxrDepthNorm* n = calloc(1, sizeof *n);
    if (n == NULL) {
        return NULL;
    }
    size_t count = (size_t)w * h;
    n->w = w;
    n->h = h;
    n->tau = malloc(count * sizeof(float));
    n->norm = malloc(count * sizeof(float));
    n->low = malloc(count * sizeof(float));
    n->scratch = malloc(count * sizeof(float));
    n->colSums = malloc((size_t)w * sizeof(float));
    if (!n->tau || !n->norm || !n->low || !n->scratch || !n->colSums) {
        mlxr_depth_norm_free(n);
        return NULL;
    }
    return n;
}

void mlxr_depth_norm_free(MlxrDepthNorm* n) {
    if (n == NULL) {
        return;
    }
    free(n->tau);
    free(n->norm);
    free(n->low);
    free(n->scratch);
    free(n->colSums);
    free(n);
}

static void normForget(MlxrDepthNorm* n) {
    n->tauValid = 0;
    n->range.valid = 0;
}

// depthTauMap: the per texel average over real time, ahead of the range
static const float* tauMap(MlxrDepthNorm* n, int tauMs, const float* out, int count,
                           int64_t now) {
    if (tauMs <= 0) {
        n->tauValid = 0;
        return out;
    }
    int seed = !n->tauValid;
    float alpha = seed ? 1.0f
                       : depthTauAlpha((float)(now - n->tauNs) / 1e9f, (float)tauMs / 1000.0f);
    depthTauBlend(n->tau, out, count, alpha, seed);
    n->tauValid = 1;
    n->tauNs = now;
    return n->tau;
}

void mlxr_depth_norm_run(MlxrDepthNorm* n, const MlxrDepthParams* p, const float* raw,
                         const float* guide, int resets, int64_t now, uint8_t* outRgba,
                         float* outDepth) {
    const int w = n->w, h = n->h, count = w * h;
    if (resets & DEPTH_RESET_TEXEL) {
        n->tauValid = 0;
    }
    if (resets & DEPTH_RESET_RANGE) {
        n->range.valid = 0;
    }
    const float* src = tauMap(n, p->tauMs, raw, count, now);
    float lo, hi;
    robustRange(src, count, &lo, &hi);
    depthRangeStep(&n->range, lo, hi, (float)(now - n->rangeNs) / 1e9f,
                   (float)p->rangeTauMs / 1000.0f);
    n->rangeNs = now;
    const float rangeLo = n->range.lo;
    const float scale = depthRangeScale(&n->range);
    for (int i = 0; i < count; i++) {
        // A NaN from the model or from a texel of the average nothing finite
        // has reached yet would otherwise sail through the clamps
        float v = (depthRead(src[i], lo) - rangeLo) * scale;
        n->norm[i] = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    const float kg = p->global, kl = p->local, conv = p->convergence;
    // The low pass only splits overall shape from local detail, so it is
    // skipped when the remap does nothing (upstream: ~10 ms on a Quest)
    int remapping = kg < 0.995f || kg > 1.005f || kl < 0.995f || kl > 1.005f;
    if (remapping) {
        lowPass(n->norm, n->low, n->scratch, n->colSums, w, h,
                DEPTH_LOWPASS_RADIUS * w / DEPTH_TEX_SIZE_DEFAULT,
                DEPTH_LOWPASS_RADIUS * h / DEPTH_TEX_SIZE_DEFAULT);
    }
    for (int i = 0; i < count; i++) {
        float v = remapping ? conv + kg * (n->low[i] - conv) + kl * (n->norm[i] - n->low[i])
                            : n->norm[i];
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        if (outDepth != NULL) {
            outDepth[i] = v;
        }
        if (outRgba != NULL) {
            uint8_t* d = outRgba + (size_t)i * 4;
            d[0] = (uint8_t)(guide[i * 3 + 0] * 255.0f + 0.5f);
            d[1] = (uint8_t)(guide[i * 3 + 1] * 255.0f + 0.5f);
            d[2] = (uint8_t)(guide[i * 3 + 2] * 255.0f + 0.5f);
            d[3] = (uint8_t)(v * 255.0f + 0.5f);
        }
    }
}

// ---- the engine ----

enum { SLOT_FREE, SLOT_FILLING, SLOT_PENDING, SLOT_RUNNING };

typedef struct {
    int state;
    float* rgb;
    uint64_t frameId;
    int64_t captureNs, submitNs;
    float convertMs;
    int resets;
} InputSlot;

typedef struct {
    MlxrDepthResult r;
    uint8_t* rgba;
    float* depth;
    float* raw;
    int held;
} ResultSlot;

struct MlxrDepthEngine {
    MlxrDepthConfig cfg;
    char path[1024];
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;      // the depth thread: a capture, a rate change, exit
    pthread_cond_t stateCond; // waiters on READY/FAILED
    int exit;
    MlxrDepthState state;
    char err[256];
    int w, h;

    InputSlot in[INPUT_SLOTS];
    int pending;              // slot index or -1
    ResultSlot out[RESULT_SLOTS];
    int latest;               // slot index or -1
    uint64_t seq;

    int perSecond;
    int64_t lastStartNs;
    DepthGate gate;
    MlxrDepthParams params;
    int resetAll;
    int cutClear;

    // Submit side (one thread)
    DepthCut cut;
    int carry;                // resets of runs that made no map (depth thread)

    // Stats
    MlxrDepthStats st;
    float modelRing[RING];
    float latRing[RING];
    int ringN, ringI;
    int64_t doneRing[RATE_RING];
    int doneN, doneI;
};

static void setState(MlxrDepthEngine* e, MlxrDepthState s, const char* why) {
    pthread_mutex_lock(&e->lock);
    e->state = s;
    if (why != NULL) {
        snprintf(e->err, sizeof e->err, "%s", why);
    }
    pthread_cond_broadcast(&e->stateCond);
    pthread_mutex_unlock(&e->lock);
}

static int allocSlots(MlxrDepthEngine* e) {
    size_t count = (size_t)e->w * e->h;
    for (int i = 0; i < INPUT_SLOTS; i++) {
        e->in[i].rgb = malloc(count * 3 * sizeof(float));
        if (e->in[i].rgb == NULL) {
            return 0;
        }
    }
    for (int i = 0; i < RESULT_SLOTS; i++) {
        ResultSlot* o = &e->out[i];
        o->rgba = malloc(count * 4);
        o->depth = malloc(count * sizeof(float));
        o->raw = e->cfg.keepRaw ? malloc(count * sizeof(float)) : NULL;
        if (o->rgba == NULL || o->depth == NULL || (e->cfg.keepRaw && o->raw == NULL)) {
            return 0;
        }
    }
    return 1;
}

static int sortFloat(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}

static void* depthThread(void* arg) {
    MlxrDepthEngine* e = arg;
    if (e->cfg.cpuMask != 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        for (int c = 0; c < 64 && c < CPU_SETSIZE; c++) {
            if (e->cfg.cpuMask & (1ULL << c)) {
                CPU_SET(c, &set);
            }
        }
        // Before the model loads, so the XNNPACK pool's workers inherit it
        pthread_setaffinity_np(pthread_self(), sizeof set, &set);
    }

    char err[256] = "";
    MlxrDepthModelOptions opt = { e->cfg.threads, e->cfg.flags };
    MlxrDepthModel* m = mlxr_depth_model_load(e->path, &opt, err, sizeof err);
    if (m == NULL) {
        setState(e, MLXR_DEPTH_FAILED, err);
        return NULL;
    }
    e->w = mlxr_depth_model_width(m);
    e->h = mlxr_depth_model_height(m);
    const size_t count = (size_t)e->w * e->h;
    MlxrDepthNorm* norm = mlxr_depth_norm_create(e->w, e->h);
    if (norm == NULL || !allocSlots(e)) {
        mlxr_depth_norm_free(norm);
        mlxr_depth_model_free(m);
        setState(e, MLXR_DEPTH_FAILED, "out of memory");
        return NULL;
    }
    e->st.loadMs = mlxr_depth_model_load_ms(m);
    e->st.threads = mlxr_depth_model_threads(m);

    int warm = e->cfg.warmupRuns < 0 ? 0 : (e->cfg.warmupRuns == 0 ? 3 : e->cfg.warmupRuns);
    float* input = mlxr_depth_model_input(m);
    for (size_t i = 0; i < count * 3; i++) {
        input[i] = 0.5f;
    }
    int64_t warmStart = mlxr_depth_now_ns();
    for (int i = 0; i < warm; i++) {
        if (mlxr_depth_model_run(m, NULL) != 0) {
            mlxr_depth_norm_free(norm);
            mlxr_depth_model_free(m);
            setState(e, MLXR_DEPTH_FAILED, "warmup run failed");
            return NULL;
        }
    }
    e->st.warmupMs = (float)(mlxr_depth_now_ns() - warmStart) / 1e6f;
    setState(e, MLXR_DEPTH_READY, NULL);

    pthread_mutex_lock(&e->lock);
    for (;;) {
        if (e->exit) {
            break;
        }
        if (e->pending < 0) {
            pthread_cond_wait(&e->wake, &e->lock);
            continue;
        }
        // Held to the rate: a run starts most of a period after the last one
        // at the earliest, on whichever capture is newest by then. Most, not
        // all: with the frame loop's gate (mlxr_depth_due) already spacing
        // the captures, a full period here would only add the gate's jitter
        // to every run and slip the rate.
        if (e->perSecond > 0 && e->lastStartNs != 0) {
            int64_t due = e->lastStartNs + 800000000LL / e->perSecond;
            int64_t now = mlxr_depth_now_ns();
            if (now < due) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                int64_t t = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec + (due - now);
                ts.tv_sec = t / 1000000000LL;
                ts.tv_nsec = t % 1000000000LL;
                pthread_cond_timedwait(&e->wake, &e->lock, &ts);
                continue;
            }
        }
        int slot = e->pending;
        e->pending = -1;
        InputSlot* in = &e->in[slot];
        in->state = SLOT_RUNNING;
        MlxrDepthParams params = e->params;
        if (e->resetAll) {
            e->resetAll = 0;
            normForget(norm);
            e->carry = 0;
        }
        int64_t startNs = mlxr_depth_now_ns();
        e->lastStartNs = startNs;
        pthread_mutex_unlock(&e->lock);

        memcpy(input, in->rgb, count * 3 * sizeof(float));
        float modelMs = 0.0f;
        int ok = mlxr_depth_model_run(m, &modelMs) == 0;
        int resets = in->resets | e->carry;

        pthread_mutex_lock(&e->lock);
        int dst = -1;
        if (ok) {
            for (int i = 0; i < RESULT_SLOTS; i++) {
                if (i != e->latest && !e->out[i].held) {
                    dst = i;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&e->lock);

        if (!ok || dst < 0) {
            // Whatever the cut check found waits for the next map made
            // (depthResetsDrop)
            e->carry = resets;
            pthread_mutex_lock(&e->lock);
            if (!ok) {
                e->st.failed++;
            } else {
                e->st.noSlot++;
            }
            in->state = SLOT_FREE;
            continue;
        }
        e->carry = 0;

        int64_t normStart = mlxr_depth_now_ns();
        ResultSlot* o = &e->out[dst];
        const float* raw = mlxr_depth_model_output(m);
        mlxr_depth_norm_run(norm, &params, raw, in->rgb, resets, normStart, o->rgba, o->depth);
        if (o->raw != NULL) {
            memcpy(o->raw, raw, count * sizeof(float));
        }
        int64_t doneNs = mlxr_depth_now_ns();

        pthread_mutex_lock(&e->lock);
        MlxrDepthResult* r = &o->r;
        r->width = e->w;
        r->height = e->h;
        r->rgba = o->rgba;
        r->depth = o->depth;
        r->raw = o->raw;
        r->frameId = in->frameId;
        r->captureNs = in->captureNs;
        r->submitNs = in->submitNs;
        r->startNs = startNs;
        r->doneNs = doneNs;
        r->convertMs = in->convertMs;
        r->modelMs = modelMs;
        r->normMs = (float)(doneNs - normStart) / 1e6f;
        r->resets = resets;
        r->seq = ++e->seq;
        e->latest = dst;
        in->state = SLOT_FREE;

        e->st.maps++;
        e->modelRing[e->ringI] = modelMs;
        e->latRing[e->ringI] = (float)(doneNs - in->submitNs) / 1e6f;
        e->ringI = (e->ringI + 1) % RING;
        if (e->ringN < RING) {
            e->ringN++;
        }
        e->doneRing[e->doneI] = doneNs;
        e->doneI = (e->doneI + 1) % RATE_RING;
        if (e->doneN < RATE_RING) {
            e->doneN++;
        }
    }
    pthread_mutex_unlock(&e->lock);
    mlxr_depth_norm_free(norm);
    mlxr_depth_model_free(m);
    return NULL;
}

MlxrDepthEngine* mlxr_depth_start(const MlxrDepthConfig* cfg) {
    MlxrDepthEngine* e = calloc(1, sizeof *e);
    if (e == NULL || cfg == NULL || cfg->modelPath == NULL) {
        free(e);
        return NULL;
    }
    e->cfg = *cfg;
    snprintf(e->path, sizeof e->path, "%s", cfg->modelPath);
    e->cfg.modelPath = e->path;
    e->params = cfg->params;
    e->perSecond = cfg->perSecond;
    e->pending = -1;
    e->latest = -1;
    pthread_mutex_init(&e->lock, NULL);
    pthread_condattr_t ca;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
    pthread_cond_init(&e->wake, &ca);
    pthread_condattr_destroy(&ca);
    pthread_cond_init(&e->stateCond, NULL);
    if (pthread_create(&e->thread, NULL, depthThread, e) != 0) {
        pthread_mutex_destroy(&e->lock);
        pthread_cond_destroy(&e->wake);
        pthread_cond_destroy(&e->stateCond);
        free(e);
        return NULL;
    }
    pthread_setname_np(e->thread, "mlxr-depth");
    return e;
}

void mlxr_depth_stop(MlxrDepthEngine* e) {
    if (e == NULL) {
        return;
    }
    pthread_mutex_lock(&e->lock);
    e->exit = 1;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
    pthread_join(e->thread, NULL);
    for (int i = 0; i < INPUT_SLOTS; i++) {
        free(e->in[i].rgb);
    }
    for (int i = 0; i < RESULT_SLOTS; i++) {
        free(e->out[i].rgba);
        free(e->out[i].depth);
        free(e->out[i].raw);
    }
    pthread_mutex_destroy(&e->lock);
    pthread_cond_destroy(&e->wake);
    pthread_cond_destroy(&e->stateCond);
    free(e);
}

MlxrDepthState mlxr_depth_state(MlxrDepthEngine* e) {
    pthread_mutex_lock(&e->lock);
    MlxrDepthState s = e->state;
    pthread_mutex_unlock(&e->lock);
    return s;
}

MlxrDepthState mlxr_depth_wait_ready(MlxrDepthEngine* e, int timeoutMs, char* err,
                                     size_t errLen) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    int64_t t = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec + (int64_t)timeoutMs * 1000000LL;
    ts.tv_sec = t / 1000000000LL;
    ts.tv_nsec = t % 1000000000LL;
    pthread_mutex_lock(&e->lock);
    while (e->state == MLXR_DEPTH_LOADING) {
        if (timeoutMs < 0) {
            pthread_cond_wait(&e->stateCond, &e->lock);
        } else if (pthread_cond_timedwait(&e->stateCond, &e->lock, &ts) == ETIMEDOUT) {
            break;
        }
    }
    MlxrDepthState s = e->state;
    if (err != NULL && errLen > 0) {
        snprintf(err, errLen, "%s", e->err);
    }
    pthread_mutex_unlock(&e->lock);
    return s;
}

void mlxr_depth_size(MlxrDepthEngine* e, int* w, int* h) {
    pthread_mutex_lock(&e->lock);
    int ready = e->state == MLXR_DEPTH_READY;
    *w = ready ? e->w : 0;
    *h = ready ? e->h : 0;
    pthread_mutex_unlock(&e->lock);
}

void mlxr_depth_set_rate(MlxrDepthEngine* e, int perSecond) {
    pthread_mutex_lock(&e->lock);
    e->perSecond = perSecond > 0 ? perSecond : 0;
    // A capture due a slower rate's period away comes in to the new one's
    if (e->perSecond > 0 && e->gate.dueNs != 0) {
        int64_t limit = mlxr_depth_now_ns() + 1000000000LL / e->perSecond;
        if (e->gate.dueNs > limit) {
            e->gate.dueNs = limit;
        }
    }
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
}

void mlxr_depth_set_params(MlxrDepthEngine* e, const MlxrDepthParams* p) {
    pthread_mutex_lock(&e->lock);
    e->params = *p;
    pthread_mutex_unlock(&e->lock);
}

void mlxr_depth_reset(MlxrDepthEngine* e) {
    pthread_mutex_lock(&e->lock);
    e->resetAll = 1;
    e->cutClear = 1;
    e->gate.dueNs = 0;
    pthread_mutex_unlock(&e->lock);
}

int mlxr_depth_due(MlxrDepthEngine* e, int64_t nowNs) {
    pthread_mutex_lock(&e->lock);
    // The gate sees every frame, so it keeps the gap between them
    int due = e->perSecond > 0 ? depthGateDue(&e->gate, nowNs, e->perSecond) : 1;
    due = due && e->state == MLXR_DEPTH_READY && e->pending < 0;
    pthread_mutex_unlock(&e->lock);
    return due;
}

int mlxr_depth_submit(MlxrDepthEngine* e, const uint8_t* rgba, size_t stride, int w, int h,
                      int flip, uint64_t frameId, int64_t captureNs) {
    int64_t start = mlxr_depth_now_ns();
    pthread_mutex_lock(&e->lock);
    int slot = -1;
    if (e->state == MLXR_DEPTH_READY && w == e->w && h == e->h) {
        for (int i = 0; i < INPUT_SLOTS; i++) {
            if (e->in[i].state == SLOT_FREE) {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&e->lock);
        return -1;
    }
    InputSlot* in = &e->in[slot];
    in->state = SLOT_FILLING;
    int cutLevel = e->params.cutLevel;
    int cutClear = e->cutClear;
    e->cutClear = 0;
    pthread_mutex_unlock(&e->lock);
    if (cutClear) {
        depthCutClear(&e->cut);
    }

    mlxr_depth_rgba8_to_input(rgba, stride, w, h, flip, in->rgb);
    // The scene cut detector, on every capture in the order submitted
    // (xr_depth.c depthCutCheck)
    int resets = 0;
    if (cutLevel == 0) {
        depthCutClear(&e->cut);
    } else {
        DepthThumb thumb;
        float diff, hist;
        depthThumbMake(&thumb, in->rgb, w, h);
        resets = depthCutResets(depthCutStep(&e->cut, &thumb, &diff, &hist));
    }
    in->frameId = frameId;
    in->captureNs = captureNs;
    in->submitNs = mlxr_depth_now_ns();
    in->convertMs = (float)(in->submitNs - start) / 1e6f;

    pthread_mutex_lock(&e->lock);
    int replaced = 0;
    if (e->pending >= 0) {
        // The waiting capture will make no map: its resets go to this one
        resets |= e->in[e->pending].resets;
        e->in[e->pending].state = SLOT_FREE;
        e->st.replaced++;
        replaced = 1;
    }
    in->resets = resets;
    in->state = SLOT_PENDING;
    e->pending = slot;
    e->st.submitted++;
    pthread_cond_broadcast(&e->wake);
    pthread_mutex_unlock(&e->lock);
    return replaced ? 0 : 1;
}

const MlxrDepthResult* mlxr_depth_acquire(MlxrDepthEngine* e, uint64_t haveSeq) {
    const MlxrDepthResult* r = NULL;
    pthread_mutex_lock(&e->lock);
    if (e->latest >= 0 && e->out[e->latest].r.seq > haveSeq) {
        e->out[e->latest].held++;
        r = &e->out[e->latest].r;
    }
    pthread_mutex_unlock(&e->lock);
    return r;
}

void mlxr_depth_release(MlxrDepthEngine* e, const MlxrDepthResult* r) {
    if (r == NULL) {
        return;
    }
    pthread_mutex_lock(&e->lock);
    for (int i = 0; i < RESULT_SLOTS; i++) {
        if (&e->out[i].r == r && e->out[i].held > 0) {
            e->out[i].held--;
        }
    }
    pthread_mutex_unlock(&e->lock);
}

void mlxr_depth_stats(MlxrDepthEngine* e, MlxrDepthStats* s) {
    float ring[RING];
    pthread_mutex_lock(&e->lock);
    *s = e->st;
    s->state = e->state;
    s->width = e->w;
    s->height = e->h;
    s->perSecond = e->perSecond;
    int n = e->ringN;
    memcpy(ring, e->modelRing, sizeof ring);
    float lat = 0.0f;
    for (int i = 0; i < n; i++) {
        lat += e->latRing[i];
    }
    s->latencyMsAvg = n > 0 ? lat / n : 0.0f;
    s->mapsPerSecond = 0.0f;
    if (e->doneN >= 2) {
        int newest = (e->doneI + RATE_RING - 1) % RATE_RING;
        int oldest = (e->doneI + RATE_RING - e->doneN) % RATE_RING;
        int64_t span = e->doneRing[newest] - e->doneRing[oldest];
        if (span > 0) {
            s->mapsPerSecond = (float)((e->doneN - 1) * 1e9 / (double)span);
        }
    }
    pthread_mutex_unlock(&e->lock);
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += ring[i];
    }
    s->modelMsAvg = n > 0 ? sum / n : 0.0f;
    if (n > 0) {
        qsort(ring, (size_t)n, sizeof(float), sortFloat);
        s->modelMsP95 = ring[(int)ceilf(0.95f * n) - 1];
    } else {
        s->modelMsP95 = 0.0f;
    }
}
