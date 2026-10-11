// Moonlight XR Native (lane av-latency): the video pacer (stream/pacer.h)
// against the newest-frame rule, with a 90 fps stream whose frames arrive
// with network jitter right around the frame loop's take point at 90 Hz.
// GPLv3.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../xrcore/check.h"
#include "../../stream/pacer.h"

typedef struct {
    int shown, replaced, holes, drops, overflow, maxQueued;
    double queuedAvg;
} Result;

// Uniform jitter in [-j, j] ms, from a fixed sequence
static double jitterMs(unsigned* seed, double j) {
    *seed = *seed * 1103515245u + 12345u;
    return ((double)((*seed >> 8) & 0xffff) / 65535.0 * 2.0 - 1.0) * j;
}

// fps frames a second for `seconds`, refreshes at hz; arrival = nominal +
// phase + jitter (sorted by the queue, as a decoder delivers in order)
static Result run(int paced, double fps, double hz, double phaseMs, double jitter, int seconds) {
    MlxrPacer p;
    mlxr_pacer_init(&p, (int)hz);
    unsigned seed = 7;
    int n = (int)(fps * seconds);
    double* arrive = malloc(sizeof(double) * (size_t)n);
    double last = -1e9;
    for (int i = 0; i < n; i++) {
        double t = i * 1000.0 / fps + phaseMs + jitterMs(&seed, jitter);
        if (t < last) t = last; // in order
        arrive[i] = last = t;
    }
    Result r = { 0 };
    int next = 0, queued = 0;
    double qsum = 0.0;
    int refreshes = (int)(hz * seconds);
    for (int k = 1; k < refreshes; k++) {
        double now = k * 1000.0 / hz;
        while (next < n && arrive[next] <= now) {
            next++;
            if (queued == MLXR_PACER_MAX_QUEUED) {
                r.overflow++; // hw_video drops the oldest
            }
            else {
                queued++;
            }
        }
        if (queued > r.maxQueued) r.maxQueued = queued;
        qsum += queued;
        if (paced) {
            int drop = 0;
            int show = mlxr_pacer_refresh(&p, queued, &drop);
            queued -= drop;
            if (show) queued--;
        }
        else {
            int show = queued > 0;
            r.replaced += show ? queued - 1 : 0;
            mlxr_pacer_note(&p, queued, show);
            queued = 0;
        }
    }
    r.shown = (int)p.shown;
    r.holes = (int)p.holes;
    r.drops = (int)p.catchUpDrops;
    r.queuedAvg = qsum / refreshes;
    free(arrive);
    return r;
}

static void print(const char* what, Result r) {
    printf("%-34s shown %5d, one-refresh repeats %4d, replaced %4d, catch-up drops %3d, overflow %3d, "
           "queue max %d avg %.2f\n",
           what, r.shown, r.holes, r.replaced, r.drops, r.overflow, r.maxQueued, r.queuedAvg);
}

int main(void) {
    // 90 fps on 90 Hz, arrivals +-4 ms around the take point: the newest rule
    // repeats and throws away frames all the time; the pacer shows (nearly)
    // every frame once
    Result old = run(0, 90.0, 90.0, 0.3, 4.0, 60);
    Result paced = run(1, 90.0, 90.0, 0.3, 4.0, 60);
    print("90 fps +-4 ms, newest frame:", old);
    print("90 fps +-4 ms, paced:", paced);
    CHECK(old.holes > 600);           // >10 a second
    CHECK(old.replaced > 600);
    CHECK(paced.holes < old.holes / 20);
    CHECK(paced.drops + paced.overflow < 30);
    CHECK(paced.shown > 5300);        // of 5400
    CHECK(paced.maxQueued <= MLXR_PACER_MAX_QUEUED);
    CHECK(paced.queuedAvg < 2.5);     // at most a frame or two of added latency

    // Clean arrivals: no drops and no repeats either way, and the pacer keeps
    // the queue short (no latency for nothing)
    Result clean = run(1, 90.0, 90.0, 5.0, 0.0, 30);
    print("90 fps clean, paced:", clean);
    CHECK(clean.holes == 0 && clean.drops == 0 && clean.overflow == 0);
    CHECK(clean.queuedAvg <= 1.01);

    // The host's clock a little fast (90.2 fps): occasional catch-up drops,
    // the queue stays bounded
    Result fast = run(1, 90.2, 90.0, 0.3, 3.0, 60);
    print("90.2 fps +-3 ms, paced:", fast);
    CHECK(fast.drops + fast.overflow >= 5 && fast.drops + fast.overflow < 40);
    CHECK(fast.maxQueued <= MLXR_PACER_MAX_QUEUED);

    // 60 fps on 90 Hz with jitter: every frame shown, none dropped
    Result sixty = run(1, 60.0, 90.0, 0.3, 3.0, 30);
    print("60 fps +-3 ms, paced:", sixty);
    CHECK(sixty.drops == 0 && sixty.overflow == 0);
    CHECK(sixty.shown >= 1795);

    // A late burst (Wi-Fi): 3 frames (the queue's most) held 30 ms then
    // delivered together; the pacer shows them on consecutive refreshes
    // instead of only the newest
    MlxrPacer p;
    mlxr_pacer_init(&p, 90);
    int drop = 0, shownBurst = 0;
    for (int k = 0; k < 45; k++) mlxr_pacer_refresh(&p, 1, &drop), drop = 0; // steady, one each refresh
    int queued = 3;
    for (int k = 0; k < 3; k++) {
        int show = mlxr_pacer_refresh(&p, queued, &drop);
        queued -= drop + show;
        shownBurst += show;
    }
    CHECK(shownBurst == 3);
    CHECK(p.catchUpDrops == 0);
    return checksDone("stream pacer");
}
