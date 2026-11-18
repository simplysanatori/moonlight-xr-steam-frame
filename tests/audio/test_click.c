// Native (owner's request): the click's press counter and burst. Presses that
// land before the helper wakes are each played, up to MLXR_CLICK_BURST_MAX of a
// kind; a bit per kind used to merge them into one click. No sound device: the
// counter and the burst are the parts play() and the helper share.
#include "check.h"
#include "click.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define RATE 48000
#define GAP (RATE * 18 / 1000)

static void testCount(void) {
    // Nothing waits to begin with, and a take leaves nothing
    CHECK(mlxr_click_take(MLXR_CLICK_PRESS) == 0);
    CHECK(mlxr_click_take(MLXR_CLICK_TICK) == 0);
    // Three presses before the helper wakes are three clicks
    CHECK(mlxr_click_count(MLXR_CLICK_PRESS) == 1);
    CHECK(mlxr_click_count(MLXR_CLICK_PRESS) == 2);
    CHECK(mlxr_click_count(MLXR_CLICK_PRESS) == 3);
    // and each kind counts on its own
    CHECK(mlxr_click_count(MLXR_CLICK_TICK) == 1);
    CHECK(mlxr_click_take(MLXR_CLICK_PRESS) == 3);
    CHECK(mlxr_click_take(MLXR_CLICK_PRESS) == 0);
    CHECK(mlxr_click_take(MLXR_CLICK_TICK) == 1);
    // A held key's repeat stops at the cap
    for (int i = 0; i < 50; i++) {
        mlxr_click_count(MLXR_CLICK_PRESS);
    }
    CHECK(mlxr_click_count(MLXR_CLICK_PRESS) == MLXR_CLICK_BURST_MAX);
    CHECK(mlxr_click_take(MLXR_CLICK_PRESS) == MLXR_CLICK_BURST_MAX);
    // A bad kind counts nothing
    CHECK(mlxr_click_count((MlxrClickKind) 7) == 0);
    CHECK(mlxr_click_count((MlxrClickKind) -1) == 0);
    CHECK(mlxr_click_take((MlxrClickKind) 7) == 0);
}

// A producer thread pressing while this one takes: every press is taken in the
// end, less only those that met a full burst, and none is taken twice
static void* presser(void* arg) {
    int* pressed = (int*) arg;
    for (int i = 0; i < 200000; i++) {
        int n = mlxr_click_count(MLXR_CLICK_PRESS);
        if (n >= MLXR_CLICK_BURST_MAX) {
            // Full: this one may not have counted, which is the cap's job
            __atomic_add_fetch(&pressed[1], 1, __ATOMIC_RELAXED);
        }
        __atomic_add_fetch(&pressed[0], 1, __ATOMIC_RELAXED);
    }
    return NULL;
}

static void testThreads(void) {
    int pressed[2] = { 0, 0 };
    pthread_t t;
    CHECK(pthread_create(&t, NULL, presser, pressed) == 0);
    long taken = 0;
    for (int i = 0; i < 400000; i++) {
        int n = mlxr_click_take(MLXR_CLICK_PRESS);
        CHECK(n >= 0 && n <= MLXR_CLICK_BURST_MAX);
        taken += n;
    }
    pthread_join(t, NULL);
    taken += mlxr_click_take(MLXR_CLICK_PRESS);
    // Every press was taken, less the ones that met a full burst
    CHECK(taken <= pressed[0]);
    CHECK(taken >= pressed[0] - pressed[1]);
    CHECK(taken > 0);
}

static void testBurst(void) {
    float mono[4800];
    int frames = mlxr_click_synthesize(MLXR_CLICK_PRESS, RATE, mono, 4800);
    CHECK(frames > GAP);
    float* click = malloc(sizeof(float) * 2 * (size_t) frames);
    for (int i = 0; i < frames; i++) {
        click[2 * i] = click[2 * i + 1] = mono[i];
    }
    int cap = (MLXR_CLICK_BURST_MAX - 1) * GAP + frames;
    float* out = malloc(sizeof(float) * 2 * (size_t) (cap + 100));

    // One press is the click itself, sample for sample
    CHECK(mlxr_click_burst(click, frames, 1, out, cap) == frames);
    CHECK(memcmp(out, click, sizeof(float) * 2 * (size_t) frames) == 0);

    // Three are three onsets, GAP apart, each as loud as one
    int n = mlxr_click_burst(click, frames, 3, out, cap);
    CHECK(n == 2 * GAP + frames);
    for (int c = 0; c < 3; c++) {
        float peak = 0.0f;
        for (int i = c * GAP; i < c * GAP + 48 && i < n; i++) {
            peak = fmaxf(peak, fabsf(out[2 * i]));
        }
        CHECK(peak > 0.7f);
    }
    // and the end of it is the last one's tail, back to silence
    CHECK(fabsf(out[2 * (n - 1)]) < 1e-4f);
    // Quiet just before the second onset: the first has died away
    float before = 0.0f;
    for (int i = GAP - 48; i < GAP; i++) {
        before = fmaxf(before, fabsf(out[2 * i]));
    }
    CHECK(before < 0.15f);

    // More than the cap plays the cap, and the length stays within maxFrames
    CHECK(mlxr_click_burst(click, frames, 50, out, cap) == cap);
    CHECK(mlxr_click_burst(click, frames, 4, out, 1000) == 1000);
    // Nothing to play is nothing
    CHECK(mlxr_click_burst(click, frames, 0, out, cap) == 0);
    CHECK(mlxr_click_burst(NULL, frames, 2, out, cap) == 0);
    free(out);
    free(click);
}

int main(void) {
    testCount();
    testThreads();
    testBurst();
    return checksDone("audio_click");
}
