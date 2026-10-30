// See surround.h. Port of BinauralRenderer.java / VirtualSurround.java.
#include "surround.h"

#include "hrtf.h"
#include "xr_audio.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LFE 3
#define REAR_LEFT 4
#define REAR_RIGHT 5
#define REAR_DELAY_MAX_MS 20.0f
#define REAR_LOWPASS_HZ 5000.0f
#define REAR_GAIN_DB -1.5f
#define CROSSFADE_DEGREES 0.25f
#define SIDE_GAIN 0.70710678f

// Native (lane polish-3): a stereo stream as the two front speakers of a
// 5.1 set, in front of the screen at the usual 30 degrees either side
static const float AZIMUTHS_2[2] = {-30.0f, 30.0f};
static const float AZIMUTHS_6[6] = {-30.0f, 30.0f, 0.0f, 0.0f, -135.0f, 135.0f};
static const float AZIMUTHS_8[8] = {-30.0f, 30.0f, 0.0f, 0.0f, -150.0f, 150.0f, -90.0f, 90.0f};

struct MlxrSurround {
    MlxrHrtf* hrtf;
    AudioFft* fft;
    int channels, rate, taps;
    const float* azimuths;
    float gains[8];

    // Per channel: the last block's tail then this block's samples
    float* history;
    int historyStride;
    int capacity;

    int keyLow[8], keyHigh[8];
    float keyFar[8];

    // Direct path only
    float* filterLeft;
    float* filterRight;
    float* oldLeft;
    float* oldRight;

    int haveFilters;
    float filterYaw;

    // Rear treatment, one of each per rear channel
    float* rearDelay[2];
    int rearDelayLen;
    int rearDelayAt[2];
    float rearLowpass[2];
    float rearCoefficient, rearGain;

    // Scratch
    float* ear[4];    // left, right, old left, old right (capacity frames)
    float* plain;     // downmix (capacity * 2)
    float* tail;      // taps - 1

    _Atomic float yaw, pitch;
    _Atomic int enabled;
    int active;       // the mode the last block was rendered in
    int started;
};

void mlxr_surround_params_default(MlxrSurroundParams* p) {
    memset(p, 0, sizeof *p);
    p->channels = 6;
    p->sampleRate = 48000;
    p->maxBlockFrames = 240;
    p->rearDelayMs = 0.0f;
    p->forceDirect = 0;
    p->equalise = 1;
}

float mlxr_surround_azimuth(int channels, int channel) {
    if (channel < 0 || channel >= channels) {
        return 0.0f;
    }
    return channels == 2 ? AZIMUTHS_2[channel]
           : channels == 6 ? AZIMUTHS_6[channel] : channels == 8 ? AZIMUTHS_8[channel] : 0.0f;
}

static void failed(char* err, int errLen, const char* what) {
    if (err != NULL && errLen > 0) {
        snprintf(err, (size_t) errLen, "%s", what);
    }
}

static int ensureCapacity(MlxrSurround* s, int frames) {
    if (frames <= s->capacity) {
        return 0;
    }
    int tail = s->taps - 1;
    int stride = tail + frames;
    float* grown = (float*) calloc((size_t) s->channels * (size_t) stride, sizeof(float));
    float* e[4];
    float* plain = (float*) malloc(sizeof(float) * 2 * (size_t) frames);
    for (int i = 0; i < 4; i++) {
        e[i] = (float*) malloc(sizeof(float) * (size_t) frames);
    }
    if (grown == NULL || plain == NULL || e[0] == NULL || e[1] == NULL || e[2] == NULL
        || e[3] == NULL) {
        free(grown);
        free(plain);
        for (int i = 0; i < 4; i++) {
            free(e[i]);
        }
        return -1;
    }
    if (s->history != NULL) {
        for (int c = 0; c < s->channels; c++) {
            memcpy(grown + (size_t) c * stride, s->history + (size_t) c * s->historyStride,
                   sizeof(float) * (size_t) tail);
        }
    }
    free(s->history);
    s->history = grown;
    s->historyStride = stride;
    free(s->plain);
    s->plain = plain;
    for (int i = 0; i < 4; i++) {
        free(s->ear[i]);
        s->ear[i] = e[i];
    }
    s->capacity = frames;
    return 0;
}

void mlxr_surround_reset(MlxrSurround* s) {
    if (s->history != NULL) {
        memset(s->history, 0,
               sizeof(float) * (size_t) s->channels * (size_t) s->historyStride);
    }
    if (s->fft != NULL) {
        audioFftReset(s->fft);
    }
    for (int i = 0; i < 2; i++) {
        if (s->rearDelay[i] != NULL) {
            memset(s->rearDelay[i], 0, sizeof(float) * (size_t) s->rearDelayLen);
        }
        s->rearDelayAt[i] = 0;
        s->rearLowpass[i] = 0.0f;
    }
    s->haveFilters = 0;
    s->filterYaw = 0.0f;
    s->active = atomic_load(&s->enabled);
}

MlxrSurround* mlxr_surround_create(const MlxrSurroundParams* p, char* err, int errLen) {
    if (p->channels != 2 && p->channels != 6 && p->channels != 8) {
        failed(err, errLen, "not a layout the virtual surround places (need 2, 6 or 8 channels)");
        return NULL;
    }
    MlxrSurround* s = (MlxrSurround*) calloc(1, sizeof *s);
    if (s == NULL) {
        failed(err, errLen, "out of memory");
        return NULL;
    }
    s->channels = p->channels;
    s->rate = p->sampleRate;
    s->azimuths = p->channels == 2 ? AZIMUTHS_2 : p->channels == 6 ? AZIMUTHS_6 : AZIMUTHS_8;
    atomic_init(&s->yaw, 0.0f);
    atomic_init(&s->pitch, 0.0f);
    atomic_init(&s->enabled, 1);
    s->hrtf = mlxr_hrtf_load(p->hrtfDir, p->equalise, err, errLen);
    if (s->hrtf == NULL) {
        free(s);
        return NULL;
    }
    s->taps = mlxr_hrtf_taps(s->hrtf, s->rate);
    if (mlxr_hrtf_prepare(s->hrtf, s->rate) != 0) {
        failed(err, errLen, "out of memory preparing the HRTF ring");
        mlxr_surround_destroy(s);
        return NULL;
    }
    for (int c = 0; c < s->channels; c++) {
        s->gains[c] = c < 2 ? 1.0f : SIDE_GAIN;
    }
    s->tail = (float*) malloc(sizeof(float) * (size_t) s->taps);
    if (s->tail == NULL) {
        failed(err, errLen, "out of memory");
        mlxr_surround_destroy(s);
        return NULL;
    }
    if (!p->forceDirect) {
        const float *ringL, *ringR;
        mlxr_hrtf_ring(s->hrtf, &ringL, &ringR);
        s->fft = audioFftCreate(s->taps, s->channels, LFE, MLXR_HRTF_RING, ringL, ringR);
    }
    if (s->fft == NULL) {
        size_t n = (size_t) s->channels * (size_t) s->taps;
        s->filterLeft = (float*) calloc(n, sizeof(float));
        s->filterRight = (float*) calloc(n, sizeof(float));
        s->oldLeft = (float*) calloc(n, sizeof(float));
        s->oldRight = (float*) calloc(n, sizeof(float));
        if (!s->filterLeft || !s->filterRight || !s->oldLeft || !s->oldRight) {
            failed(err, errLen, "out of memory");
            mlxr_surround_destroy(s);
            return NULL;
        }
    }
    float delayMs = fmaxf(0.0f, fminf(REAR_DELAY_MAX_MS, p->rearDelayMs));
    s->rearDelayLen = (int) lroundf(delayMs * (float) s->rate / 1000.0f);
    for (int i = 0; i < 2 && s->rearDelayLen > 0; i++) {
        s->rearDelay[i] = (float*) calloc((size_t) s->rearDelayLen, sizeof(float));
        if (s->rearDelay[i] == NULL) {
            failed(err, errLen, "out of memory");
            mlxr_surround_destroy(s);
            return NULL;
        }
    }
    s->rearCoefficient = (float) (1.0 - exp(-2.0 * M_PI * REAR_LOWPASS_HZ / s->rate));
    s->rearGain = (float) pow(10.0, REAR_GAIN_DB / 20.0);
    if (ensureCapacity(s, p->maxBlockFrames > 0 ? p->maxBlockFrames : 240) != 0) {
        failed(err, errLen, "out of memory");
        mlxr_surround_destroy(s);
        return NULL;
    }
    mlxr_surround_reset(s);
    return s;
}

void mlxr_surround_destroy(MlxrSurround* s) {
    if (s == NULL) {
        return;
    }
    if (s->fft != NULL) {
        audioFftDestroy(s->fft);
    }
    mlxr_hrtf_free(s->hrtf);
    free(s->history);
    free(s->filterLeft);
    free(s->filterRight);
    free(s->oldLeft);
    free(s->oldRight);
    free(s->rearDelay[0]);
    free(s->rearDelay[1]);
    for (int i = 0; i < 4; i++) {
        free(s->ear[i]);
    }
    free(s->plain);
    free(s->tail);
    free(s);
}

static int isFinite(float v) {
    return !isnan(v) && !isinf(v);
}

void mlxr_surround_set_pose(MlxrSurround* s, float yawRadians, float pitchRadians) {
    if (isFinite(yawRadians)) {
        atomic_store_explicit(&s->yaw, yawRadians, memory_order_relaxed);
    }
    if (isFinite(pitchRadians)) {
        atomic_store_explicit(&s->pitch, pitchRadians, memory_order_relaxed);
    }
}

void mlxr_surround_get_pose(const MlxrSurround* s, float* yawRadians, float* pitchRadians) {
    if (yawRadians != NULL) {
        *yawRadians = atomic_load_explicit((_Atomic float*) &s->yaw, memory_order_relaxed);
    }
    if (pitchRadians != NULL) {
        *pitchRadians = atomic_load_explicit((_Atomic float*) &s->pitch, memory_order_relaxed);
    }
}

void mlxr_surround_set_enabled(MlxrSurround* s, bool enabled) {
    atomic_store(&s->enabled, enabled ? 1 : 0);
}

bool mlxr_surround_is_enabled(const MlxrSurround* s) {
    return atomic_load((_Atomic int*) &s->enabled) != 0;
}

int mlxr_surround_taps(const MlxrSurround* s) {
    return s->taps;
}

const char* mlxr_surround_kernel(const MlxrSurround* s) {
    return s->fft != NULL ? "FFT" : "direct";
}

// A rear speaker is duller and a little quieter, and later if a delay is set
static float rear(MlxrSurround* s, int index, float sample) {
    float delayed = sample;
    if (s->rearDelayLen > 0) {
        float* ring = s->rearDelay[index];
        int at = s->rearDelayAt[index];
        delayed = ring[at];
        ring[at] = sample;
        s->rearDelayAt[index] = at + 1 == s->rearDelayLen ? 0 : at + 1;
    }
    float low = s->rearLowpass[index] + s->rearCoefficient * (delayed - s->rearLowpass[index]);
    s->rearLowpass[index] = low;
    return s->rearGain * low;
}

static void buildFilters(MlxrSurround* s, float* left, float* right, float yawDegrees) {
    for (int c = 0; c < s->channels; c++) {
        if (c == LFE) {
            continue;
        }
        mlxr_hrtf_filter_for(s->hrtf, s->azimuths[c] + yawDegrees, left + (size_t) c * s->taps,
                             right + (size_t) c * s->taps);
    }
}

static void buildKeys(MlxrSurround* s, float yawDegrees) {
    for (int c = 0; c < s->channels; c++) {
        if (c == LFE) {
            continue;
        }
        int entries[2];
        s->keyFar[c] = mlxr_hrtf_between(s->azimuths[c] + yawDegrees, entries);
        s->keyLow[c] = entries[0];
        s->keyHigh[c] = entries[1];
    }
}

static float clampUnit(float v) {
    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

void mlxr_downmix_to_stereo(int n, const float* in, int frames, float* out) {
    if (n == 2) {
        memcpy(out, in, sizeof(float) * 2 * (size_t) frames);
        return;
    }
    for (int f = 0; f < frames; f++) {
        const float* x = in + (size_t) f * n;
        float l = x[0] + SIDE_GAIN * (x[2] + x[3] + x[4]);
        float r = x[1] + SIDE_GAIN * (x[2] + x[3] + x[5]);
        if (n == 8) {
            l += SIDE_GAIN * x[6];
            r += SIDE_GAIN * x[7];
        }
        out[2 * f] = l;
        out[2 * f + 1] = r;
    }
}

void mlxr_surround_render_yaw(MlxrSurround* s, const float* in, int frames, float yawRadians,
                              float* out) {
    if (frames <= 0) {
        return;
    }
    if (ensureCapacity(s, frames) != 0) {
        memset(out, 0, sizeof(float) * 2 * (size_t) frames);
        return;
    }
    if (!isFinite(yawRadians)) {
        yawRadians = 0.0f;
    }
    const int n = s->channels;
    const int tail = s->taps - 1;
    // Into the history a channel at a time, the rear pair through its
    // treatment on the way
    for (int c = 0; c < n; c++) {
        float* into = s->history + (size_t) c * s->historyStride + tail;
        if (c == REAR_LEFT || c == REAR_RIGHT) {
            for (int f = 0; f < frames; f++) {
                into[f] = rear(s, c - REAR_LEFT, in[(size_t) f * n + c]);
            }
        } else {
            for (int f = 0; f < frames; f++) {
                into[f] = in[(size_t) f * n + c];
            }
        }
    }

    const int want = atomic_load(&s->enabled);
    const int fade = want != s->active && s->started;
    const int hrtf = want || fade;
    float* binL = s->ear[0];
    float* binR = s->ear[1];
    float* oldL = s->ear[2];
    float* oldR = s->ear[3];
    float yawDegrees = (float) (yawRadians * 180.0 / M_PI);
    int crossfade = 0;
    if (hrtf) {
        if (s->fft != NULL) {
            crossfade = s->haveFilters && fabsf(yawDegrees - s->filterYaw) > CROSSFADE_DEGREES;
            buildKeys(s, yawDegrees);
            audioFftConvolve(s->fft, s->history, s->historyStride, s->keyLow, s->keyHigh,
                             s->keyFar, s->gains, frames, crossfade, binL, binR, oldL, oldR);
            s->haveFilters = 1;
        } else {
            if (!s->haveFilters) {
                buildFilters(s, s->filterLeft, s->filterRight, yawDegrees);
                s->haveFilters = 1;
            } else if (yawDegrees != s->filterYaw) {
                crossfade = fabsf(yawDegrees - s->filterYaw) > CROSSFADE_DEGREES;
                float* t = s->filterLeft;
                s->filterLeft = s->oldLeft;
                s->oldLeft = t;
                t = s->filterRight;
                s->filterRight = s->oldRight;
                s->oldRight = t;
                buildFilters(s, s->filterLeft, s->filterRight, yawDegrees);
            }
            audioConvolve(s->history, s->historyStride, s->filterLeft, s->filterRight, s->taps,
                          s->gains, n, LFE, frames, binL, binR);
            if (crossfade) {
                audioConvolve(s->history, s->historyStride, s->oldLeft, s->oldRight, s->taps,
                              s->gains, n, LFE, frames, oldL, oldR);
            }
        }
        s->filterYaw = yawDegrees;
    }
    if (!want || fade) {
        // The plain downmix of this block, from the unprocessed input
        mlxr_downmix_to_stereo(n, in, frames, s->plain);
    }

    for (int f = 0; f < frames; f++) {
        float l, r;
        if (hrtf) {
            l = binL[f];
            r = binR[f];
            if (crossfade) {
                float was = 1.0f - (f + 1.0f) / frames;
                l += was * (oldL[f] - l);
                r += was * (oldR[f] - r);
            }
            if (fade) {
                // Between the two renderings over the block, towards what is wanted
                float to = (f + 1.0f) / frames;
                float mixL = want ? to : 1.0f - to;
                l = mixL * l + (1.0f - mixL) * s->plain[2 * f];
                r = mixL * r + (1.0f - mixL) * s->plain[2 * f + 1];
            }
        } else {
            l = s->plain[2 * f];
            r = s->plain[2 * f + 1];
        }
        out[2 * f] = clampUnit(l);
        out[2 * f + 1] = clampUnit(r);
    }

    // Keep the last taps - 1 samples for the next block
    for (int c = 0; c < n; c++) {
        float* base = s->history + (size_t) c * s->historyStride;
        memmove(s->tail, base + frames, sizeof(float) * (size_t) tail);
        memcpy(base, s->tail, sizeof(float) * (size_t) tail);
    }
    s->active = want;
    s->started = 1;
}

void mlxr_surround_render(MlxrSurround* s, const float* in, int frames, float* out) {
    float yaw = atomic_load_explicit(&s->yaw, memory_order_relaxed);
    mlxr_surround_render_yaw(s, in, frames, yaw, out);
}
