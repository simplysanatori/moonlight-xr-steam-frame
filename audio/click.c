#include "click.h"

#include "pulse_out.h"

#include <math.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RATE 48000
#define MAX_FRAMES 4800
#define IDLE_CORK_MS 500

static struct {
    MlxrPulseOut* out;
    pthread_t thread;
    sem_t sem;
    int running;
    _Atomic int pending;
    _Atomic float volume;
    float* stereo[MLXR_CLICK_KINDS];
    int frames[MLXR_CLICK_KINDS];
} C;

int mlxr_click_synthesize(MlxrClickKind kind, int rate, float* out, int maxFrames) {
    double f1, f2, decayMs, a2, attackMs, lengthMs;
    switch (kind) {
    case MLXR_CLICK_PRESS:
        f1 = 2400.0; f2 = 1500.0; a2 = 0.6; decayMs = 7.0; attackMs = 0.6; lengthMs = 36.0;
        break;
    case MLXR_CLICK_TICK:
        f1 = 3600.0; f2 = 2200.0; a2 = 0.4; decayMs = 2.8; attackMs = 0.4; lengthMs = 14.0;
        break;
    default:
        return 0;
    }
    int n = (int) (lengthMs * rate / 1000.0);
    if (n > maxFrames) {
        n = maxFrames;
    }
    for (int i = 0; i < n; i++) {
        double t = (double) i / rate;
        double env = exp(-t * 1000.0 / decayMs);
        double attack = t * 1000.0 < attackMs ? t * 1000.0 / attackMs : 1.0;
        // A raised cosine tail so the last sample is exactly zero
        double tailPos = (double) i / (n - 1);
        double tail = tailPos > 0.8 ? 0.5 * (1.0 + cos(M_PI * (tailPos - 0.8) / 0.2)) : 1.0;
        double s = sin(2.0 * M_PI * f1 * t) + a2 * sin(2.0 * M_PI * f2 * t);
        out[i] = (float) (s / (1.0 + a2) * env * attack * tail);
    }
    return n;
}

static void* worker(void* arg) {
    int corked = 1;
    for (;;) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long) IDLE_CORK_MS * 1000000L;
        ts.tv_sec += ts.tv_nsec / 1000000000L;
        ts.tv_nsec %= 1000000000L;
        int rc = corked ? sem_wait(&C.sem) : sem_timedwait(&C.sem, &ts);
        if (!C.running) {
            break;
        }
        if (rc != 0) {
            // Idle: let the device go
            mlxr_pulse_out_set_corked(C.out, true);
            corked = 1;
            continue;
        }
        int bits = atomic_exchange(&C.pending, 0);
        float vol = atomic_load(&C.volume);
        for (int k = 0; k < MLXR_CLICK_KINDS && vol > 0.0f; k++) {
            if (bits & (1 << k)) {
                if (corked) {
                    mlxr_pulse_out_set_corked(C.out, false);
                    corked = 0;
                }
                // Scale a copy so the stored click stays at unit peak
                float buf[MAX_FRAMES * 2];
                int n = C.frames[k];
                for (int i = 0; i < 2 * n; i++) {
                    buf[i] = C.stereo[k][i] * vol;
                }
                mlxr_pulse_out_write(C.out, buf, n);
            }
        }
    }
    return NULL;
}

static char gStreamName[96] = "Moonlight XR UI virtual sink";

void mlxr_click_set_stream_name(const char* name) {
    if (name != NULL) {
        snprintf(gStreamName, sizeof gStreamName, "%s", name);
    }
}

int mlxr_click_init(const char* sink, char* err, int errLen) {
    if (C.out != NULL) {
        return 0;
    }
    atomic_init(&C.volume, 0.5f);
    atomic_init(&C.pending, 0);
    float mono[MAX_FRAMES];
    for (int k = 0; k < MLXR_CLICK_KINDS; k++) {
        int n = mlxr_click_synthesize((MlxrClickKind) k, RATE, mono, MAX_FRAMES);
        C.stereo[k] = (float*) malloc(sizeof(float) * 2 * (size_t) n);
        if (C.stereo[k] == NULL) {
            mlxr_click_shutdown();
            return -1;
        }
        // At the default volume (0.5) a press peaks near -18 dBFS and a tick near -21
        float peak = k == MLXR_CLICK_PRESS ? 0.25f : 0.18f;
        for (int i = 0; i < n; i++) {
            C.stereo[k][2 * i] = C.stereo[k][2 * i + 1] = mono[i] * peak;
        }
        C.frames[k] = n;
    }
    MlxrPulseOutParams p;
    mlxr_pulse_out_params_default(&p);
    p.streamName = gStreamName;
    p.role = "event";
    p.sink = sink;
    p.tlengthMs = 30;
    p.minreqMs = 5;
    p.prebufMs = 0;
    p.queueCapMs = 80;
    p.noFadeAfterUnderrun = true;
    C.out = mlxr_pulse_out_open(&p, err, errLen);
    if (C.out == NULL) {
        mlxr_click_shutdown();
        return -1;
    }
    mlxr_pulse_out_set_corked(C.out, true);
    sem_init(&C.sem, 0, 0);
    C.running = 1;
    if (pthread_create(&C.thread, NULL, worker, NULL) != 0) {
        C.running = 0;
        mlxr_click_shutdown();
        return -1;
    }
    return 0;
}

void mlxr_click_shutdown(void) {
    if (C.running) {
        C.running = 0;
        sem_post(&C.sem);
        pthread_join(C.thread, NULL);
        sem_destroy(&C.sem);
    }
    mlxr_pulse_out_close(C.out);
    C.out = NULL;
    for (int k = 0; k < MLXR_CLICK_KINDS; k++) {
        free(C.stereo[k]);
        C.stereo[k] = NULL;
        C.frames[k] = 0;
    }
}

void mlxr_click_set_volume(float volume) {
    if (!isfinite(volume)) {
        return;
    }
    atomic_store(&C.volume, volume < 0.0f ? 0.0f : volume > 1.0f ? 1.0f : volume);
}

void mlxr_click_play(MlxrClickKind kind) {
    if (!C.running || kind < 0 || kind >= MLXR_CLICK_KINDS) {
        return;
    }
    atomic_fetch_or(&C.pending, 1 << kind);
    sem_post(&C.sem);
}
