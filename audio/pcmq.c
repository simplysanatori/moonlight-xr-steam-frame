#include "pcmq.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct MlxrPcmQueue {
    pthread_mutex_t lock;
    int channels;
    int cap;        // frames the queue may hold
    int alloc;      // frames of storage (>= cap, fixed at create)
    float* data;
    int head, count;
    int fadePending;
    MlxrPcmQueueStats stats;
};

MlxrPcmQueue* mlxr_pcmq_create(int channels, int capFrames) {
    if (channels < 1 || capFrames < 1) {
        return NULL;
    }
    MlxrPcmQueue* q = (MlxrPcmQueue*) calloc(1, sizeof *q);
    if (q == NULL) {
        return NULL;
    }
    q->channels = channels;
    q->cap = q->alloc = capFrames;
    q->data = (float*) calloc((size_t) capFrames * (size_t) channels, sizeof(float));
    if (q->data == NULL) {
        free(q);
        return NULL;
    }
    pthread_mutex_init(&q->lock, NULL);
    return q;
}

void mlxr_pcmq_destroy(MlxrPcmQueue* q) {
    if (q == NULL) {
        return;
    }
    pthread_mutex_destroy(&q->lock);
    free(q->data);
    free(q);
}

int mlxr_pcmq_capacity(const MlxrPcmQueue* q) {
    return q->cap;
}

void mlxr_pcmq_set_capacity(MlxrPcmQueue* q, int capFrames) {
    pthread_mutex_lock(&q->lock);
    q->cap = capFrames < 1 ? 1 : capFrames > q->alloc ? q->alloc : capFrames;
    pthread_mutex_unlock(&q->lock);
}

static void discard(MlxrPcmQueue* q, int n) {
    q->head = (q->head + n) % q->alloc;
    q->count -= n;
}

// Lock held: drops the n oldest frames, the ones that were due fading out
// over XFADE_FRAMES into the ones after the cut. Fewer when the queue does
// not hold n + the crossfade. Returns how many went.
static int crossfadeDiscard(MlxrPcmQueue* q, int n) {
    int fade = MLXR_PCMQ_XFADE_FRAMES;
    if (n > q->count - fade) {
        n = q->count - fade;
    }
    if (n <= 0) {
        return 0;
    }
    for (int f = 0; f < fade; f++) {
        // equal power: the two sides are unrelated audio
        float g = (f + 0.5f) / (float) fade;
        float gin = sinf(g * 1.5707963f), gout = cosf(g * 1.5707963f);
        float* outgoing = q->data + (size_t) ((q->head + f) % q->alloc) * q->channels;
        float* incoming = q->data + (size_t) ((q->head + n + f) % q->alloc) * q->channels;
        for (int c = 0; c < q->channels; c++) {
            incoming[c] = outgoing[c] * gout + incoming[c] * gin;
        }
    }
    discard(q, n);
    return n;
}

int mlxr_pcmq_push(MlxrPcmQueue* q, const float* frames, int n) {
    if (n <= 0) {
        return 0;
    }
    pthread_mutex_lock(&q->lock);
    int dropped = 0, spliced = 0;
    if (n > q->cap) {
        // Only the newest cap frames of an oversized push can be kept
        dropped = q->count + (n - q->cap);
        frames += (size_t) (n - q->cap) * q->channels;
        n = q->cap;
        q->head = 0;
        q->count = 0;
    } else if (q->count + n > q->cap) {
        dropped = q->count + n - q->cap;
        if (crossfadeDiscard(q, dropped) == dropped) {
            spliced = 1;
        }
        else {
            discard(q, dropped);
        }
    }
    int tail = (q->head + q->count) % q->alloc;
    int first = q->alloc - tail < n ? q->alloc - tail : n;
    memcpy(q->data + (size_t) tail * q->channels, frames,
           sizeof(float) * (size_t) first * (size_t) q->channels);
    if (first < n) {
        memcpy(q->data, frames + (size_t) first * q->channels,
               sizeof(float) * (size_t) (n - first) * (size_t) q->channels);
    }
    q->count += n;
    q->stats.pushedFrames += (uint64_t) n;
    if (dropped > 0) {
        q->stats.droppedFrames += (uint64_t) dropped;
        q->stats.overruns++;
        if (!spliced) {
            q->fadePending = 1;
        }
    }
    pthread_mutex_unlock(&q->lock);
    return dropped;
}

int mlxr_pcmq_pop(MlxrPcmQueue* q, float* out, int maxFrames) {
    pthread_mutex_lock(&q->lock);
    int n = q->count < maxFrames ? q->count : maxFrames;
    if (n <= 0) {
        pthread_mutex_unlock(&q->lock);
        return 0;
    }
    int first = q->alloc - q->head < n ? q->alloc - q->head : n;
    memcpy(out, q->data + (size_t) q->head * q->channels,
           sizeof(float) * (size_t) first * (size_t) q->channels);
    if (first < n) {
        memcpy(out + (size_t) first * q->channels, q->data,
               sizeof(float) * (size_t) (n - first) * (size_t) q->channels);
    }
    discard(q, n);
    q->stats.poppedFrames += (uint64_t) n;
    if (q->fadePending) {
        q->fadePending = 0;
        int ramp = n < MLXR_PCMQ_FADE_FRAMES ? n : MLXR_PCMQ_FADE_FRAMES;
        for (int f = 0; f < ramp; f++) {
            float g = (f + 1.0f) / MLXR_PCMQ_FADE_FRAMES;
            for (int c = 0; c < q->channels; c++) {
                out[(size_t) f * q->channels + c] *= g;
            }
        }
    }
    pthread_mutex_unlock(&q->lock);
    return n;
}

int mlxr_pcmq_frames(MlxrPcmQueue* q) {
    pthread_mutex_lock(&q->lock);
    int n = q->count;
    pthread_mutex_unlock(&q->lock);
    return n;
}

void mlxr_pcmq_mark_discontinuity(MlxrPcmQueue* q) {
    pthread_mutex_lock(&q->lock);
    q->fadePending = 1;
    pthread_mutex_unlock(&q->lock);
}

int mlxr_pcmq_skip(MlxrPcmQueue* q, int n) {
    pthread_mutex_lock(&q->lock);
    n = crossfadeDiscard(q, n);
    if (n > 0) {
        q->stats.skippedFrames += (uint64_t) n;
    }
    pthread_mutex_unlock(&q->lock);
    return n;
}

void mlxr_pcmq_clear(MlxrPcmQueue* q) {
    pthread_mutex_lock(&q->lock);
    q->head = q->count = 0;
    q->fadePending = 1;
    pthread_mutex_unlock(&q->lock);
}

void mlxr_pcmq_stats(MlxrPcmQueue* q, MlxrPcmQueueStats* out) {
    pthread_mutex_lock(&q->lock);
    *out = q->stats;
    pthread_mutex_unlock(&q->lock);
}
