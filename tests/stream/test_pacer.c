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
// paced: 0 newest, 1 smooth, 2 low latency (MlxrPaceMode)
static Result run(int paced, double fps, double hz, double phaseMs, double jitter, int seconds) {
    MlxrPacer p;
    mlxr_pacer_init_mode(&p, (int)hz, paced == MLXR_PACE_LOW_LATENCY);
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
    r.drops = (int)(p.catchUpDrops + p.staleDrops);
    r.queuedAvg = qsum / refreshes;
    free(arrive);
    return r;
}

// Native (lane perf): the low-latency pacer with a given cushion, 90 fps on
// 90 Hz for 60 s, jitter +-j ms and 2% of frames up to 30 ms late
static Result runCushion(int cushion, double j) {
    MlxrPacer p;
    mlxr_pacer_init_mode(&p, 90, 1);
    p.cushion = cushion;
    unsigned seed = 7;
    int n = 90 * 60;
    double* arrive = malloc(sizeof(double) * (size_t)n);
    double last = -1e9;
    for (int i = 0; i < n; i++) {
        double t = i * 1000.0 / 90.0 + 0.3 + jitterMs(&seed, j);
        if (jitterMs(&seed, 1.0) > 0.96) t += 15.0 + jitterMs(&seed, 15.0);
        if (t < last) t = last;
        arrive[i] = last = t;
    }
    Result r = { 0 };
    int next = 0, queued = 0;
    double qsum = 0.0;
    for (int k = 1; k < n; k++) {
        double now = k * 1000.0 / 90.0;
        while (next < n && arrive[next] <= now) {
            next++;
            if (queued == MLXR_PACER_MAX_QUEUED) r.overflow++;
            else queued++;
        }
        qsum += queued;
        int drop = 0;
        int show = mlxr_pacer_refresh(&p, queued, &drop);
        queued -= drop + show;
    }
    r.shown = (int)p.shown;
    r.holes = (int)p.holes;
    r.drops = (int)(p.catchUpDrops + p.staleDrops);
    r.queuedAvg = qsum / n;
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

    // Native (lane perf): the low-latency mode keeps the jitter cushion...
    Result ll = run(MLXR_PACE_LOW_LATENCY, 90.0, 90.0, 0.3, 4.0, 60);
    print("90 fps +-4 ms, low latency:", ll);
    CHECK(ll.holes < old.holes / 10);
    CHECK(ll.shown > 5200);
    CHECK(ll.queuedAvg <= paced.queuedAvg + 0.01);
    CHECK(ll.maxQueued <= MLXR_PACER_MAX_QUEUED);
    Result llClean = run(MLXR_PACE_LOW_LATENCY, 90.0, 90.0, 5.0, 0.0, 30);
    CHECK(llClean.holes == 0 && llClean.drops == 0 && llClean.overflow == 0 && llClean.queuedAvg <= 1.01);
    Result llSixty = run(MLXR_PACE_LOW_LATENCY, 60.0, 90.0, 0.3, 3.0, 30);
    print("60 fps +-3 ms, low latency:", llSixty);
    CHECK(llSixty.drops == 0 && llSixty.shown >= 1795);
    Result llThirty = run(MLXR_PACE_LOW_LATENCY, 30.0, 90.0, 0.3, 3.0, 30);
    CHECK(llThirty.drops == 0 && llThirty.shown >= 895); // a 30 fps desktop: every frame

    // ... but after a stall shows the newest frame of the burst at once,
    // where the smooth mode runs 2 frames late for the next half second
    MlxrPacer lp, sp;
    mlxr_pacer_init_mode(&lp, 90, 1);
    mlxr_pacer_init_mode(&sp, 90, 0);
    for (int k = 0; k < 45; k++) {
        mlxr_pacer_refresh(&lp, 1, &drop);
        mlxr_pacer_refresh(&sp, 1, &drop);
    }
    // 3 refreshes without a frame (a 33 ms Wi-Fi stall)
    for (int k = 0; k < 3; k++) {
        mlxr_pacer_refresh(&lp, 0, &drop);
        mlxr_pacer_refresh(&sp, 0, &drop);
    }
    // the 3 late frames arrive together, then one a refresh again
    int lq = 3, sq = 3, lateL = 0, lateS = 0;
    for (int k = 0; k < 45; k++) {
        int d = 0;
        int show = mlxr_pacer_refresh(&lp, lq, &d);
        lq -= d + show;
        lateL += lq; // frames still waiting behind the one shown
        show = mlxr_pacer_refresh(&sp, sq, &d);
        sq -= d + show;
        lateS += sq;
        lq++, sq++; // the next frame
    }
    printf("after a 33 ms stall: frames waiting behind the shown one over 45 refreshes: low latency %d, smooth %d\n",
           lateL, lateS);
    CHECK(lp.staleDrops == 2);
    CHECK(lateL == 0);
    CHECK(lateS >= 45); // a frame or more behind for the whole half second
    // a single-refresh wobble is not a stall: both frames get shown
    mlxr_pacer_init_mode(&lp, 90, 1);
    for (int k = 0; k < 45; k++) mlxr_pacer_refresh(&lp, 1, &drop);
    mlxr_pacer_refresh(&lp, 0, &drop);
    int show = mlxr_pacer_refresh(&lp, 2, &drop);
    CHECK(show == 1 && drop == 0 && lp.staleDrops == 0);

    // Native (lane perf): heavy Wi-Fi jitter (+-16 ms, 2% of frames up to
    // 30 ms later still). Cushion 3 (the default) shows far more frames and
    // repeats far fewer than cushion 2 (the first low-latency choice, whose
    // catch-up drops were the skips of the owner's 00:47 run), for ~0.6
    // frames more waiting; with mild jitter the two are alike
    Result c2 = runCushion(2, 16.0), c3 = runCushion(3, 16.0);
    print("+-16 ms, low latency, cushion 2:", c2);
    print("+-16 ms, low latency, cushion 3:", c3);
    CHECK(c3.shown > c2.shown + 300);
    CHECK(c3.holes < c2.holes / 2);
    CHECK(c3.queuedAvg < c2.queuedAvg + 0.8);
    Result m2 = runCushion(2, 4.0), m3 = runCushion(3, 4.0);
    print("+-4 ms, low latency, cushion 2:", m2);
    print("+-4 ms, low latency, cushion 3:", m3);
    CHECK(m3.shown + 20 > m2.shown && m3.queuedAvg < m2.queuedAvg + 0.5);

    CHECK(mlxr_pace_mode_parse(NULL) == MLXR_PACE_LOW_LATENCY);
    CHECK(mlxr_pace_mode_parse("0") == MLXR_PACE_NEWEST);
    CHECK(mlxr_pace_mode_parse("1") == MLXR_PACE_SMOOTH);
    CHECK(mlxr_pace_mode_parse("2") == MLXR_PACE_LOW_LATENCY);
    return checksDone("stream pacer");
}
