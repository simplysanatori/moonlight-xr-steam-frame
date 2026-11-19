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
// Native (owner's request): presses that land before the helper wakes are each
// played, the later ones this far after the one before, so a fast burst reads
// as that many clicks. A press decays in 7 ms, so they stay apart.
#define BURST_GAP_FRAMES (RATE * 18 / 1000)
#define BURST_MAX_FRAMES ((MLXR_CLICK_BURST_MAX - 1) * BURST_GAP_FRAMES + MAX_FRAMES)

static struct {
    MlxrPulseOut* out;
    pthread_t thread;
    sem_t sem;
    int running;
    // Native (owner's request): presses waiting per kind, at most
    // MLXR_CLICK_BURST_MAX each. A bit per kind merged a burst into one click.
    _Atomic int pending[MLXR_CLICK_KINDS];
    // Native (owner's request): what became of the presses, for the client's
    // diagnostic line (mlxr_click_stats)
    _Atomic long asked, capped, played, silenced, cutFrames;
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
        float vol = atomic_load(&C.volume);
        for (int k = 0; k < MLXR_CLICK_KINDS; k++) {
            // Taken at volume 0 too, so nothing waits to play once it is turned up
            int count = mlxr_click_take((MlxrClickKind) k);
            if (count <= 0) {
                continue;
            }
            if (vol <= 0.0f) {
                atomic_fetch_add(&C.silenced, count);
                continue;
            }
            if (corked) {
                mlxr_pulse_out_set_corked(C.out, false);
                corked = 0;
            }
            // Scaled into a copy so the stored click stays at unit peak
            static float buf[BURST_MAX_FRAMES * 2];
            int n = mlxr_click_burst(C.stereo[k], C.frames[k], count, buf, BURST_MAX_FRAMES);
            for (int i = 0; i < 2 * n; i++) {
                buf[i] *= vol;
            }
            int cut = mlxr_pulse_out_write(C.out, buf, n);
            atomic_fetch_add(&C.played, count);
            if (cut > 0) {
                atomic_fetch_add(&C.cutFrames, cut);
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
    for (int k = 0; k < MLXR_CLICK_KINDS; k++) {
        atomic_init(&C.pending[k], 0);
    }
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
    // Native (owner's request): the stream waits for 2 ms of a click before it
    // plays, and stops again when it runs dry. With 0 it kept running dry
    // between clicks, and the server's read point ran on past where the next
    // click was written, so that click was dropped, all or its start: on the
    // desktop's PipeWire only 15 of 191 presses 40 to 150 ms apart were heard,
    // mostly the first after an idle (the cork starts it afresh). A whole click
    // is written at once, so it starts no later than before.
    p.prebufMs = 2;
    // Native (owner's request): room for a whole burst (4 presses, 90 ms) on
    // top of the server's 30 ms; one click alone waits no longer than before
    p.queueCapMs = 160;
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
    int before = atomic_load(&C.pending[kind]);
    int now = mlxr_click_count(kind);
    atomic_fetch_add(&C.asked, 1);
    if (now == before && now >= MLXR_CLICK_BURST_MAX) {
        atomic_fetch_add(&C.capped, 1);
    }
    sem_post(&C.sem);
}

void mlxr_click_stats(MlxrClickStats* out) {
    out->asked = atomic_load(&C.asked);
    out->capped = atomic_load(&C.capped);
    out->played = atomic_load(&C.played);
    out->silenced = atomic_load(&C.silenced);
    out->cutFrames = atomic_load(&C.cutFrames);
}

int mlxr_click_count(MlxrClickKind kind) {
    if (kind < 0 || kind >= MLXR_CLICK_KINDS) {
        return 0;
    }
    // Native (owner's request): one more, unless MLXR_CLICK_BURST_MAX are
    // already waiting: a held key's repeat builds no long tail
    int n = atomic_load(&C.pending[kind]);
    while (n < MLXR_CLICK_BURST_MAX
           && !atomic_compare_exchange_weak(&C.pending[kind], &n, n + 1)) {
    }
    return n < MLXR_CLICK_BURST_MAX ? n + 1 : n;
}

int mlxr_click_take(MlxrClickKind kind) {
    if (kind < 0 || kind >= MLXR_CLICK_KINDS) {
        return 0;
    }
    return atomic_exchange(&C.pending[kind], 0);
}

int mlxr_click_burst(const float* click, int frames, int count, float* out, int maxFrames) {
    if (click == NULL || frames <= 0 || count <= 0) {
        return 0;
    }
    if (count > MLXR_CLICK_BURST_MAX) {
        count = MLXR_CLICK_BURST_MAX;
    }
    int n = (count - 1) * BURST_GAP_FRAMES + frames;
    if (n > maxFrames) {
        n = maxFrames;
    }
    memset(out, 0, sizeof(float) * 2 * (size_t) n);
    // Each one added in after the one before, the tails overlapping
    for (int c = 0; c < count; c++) {
        int at = c * BURST_GAP_FRAMES;
        for (int i = 0; i < frames && at + i < n; i++) {
            out[2 * (at + i)] += click[2 * i];
            out[2 * (at + i) + 1] += click[2 * i + 1];
        }
    }
    return n;
}
