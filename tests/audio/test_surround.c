// The virtual surround: a single-channel impulse on each speaker yields ear
// levels and an interaural delay consistent with its azimuth, the head pose
// moves it ("held to the screen"), turning crossfades exactly between the old
// and new filters, and the FFT kernel agrees with the direct one.
#include "check.h"
#include "surround.h"
#include "tutil.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#ifndef HRTF_DIR
#error HRTF_DIR
#endif

#define BLOCK 240
#define BLOCKS 4
#define N (BLOCK * BLOCKS)
#define RAD(d) ((d) * (float) M_PI / 180.0f)

static float outL[N], outR[N];

// An impulse on one channel in block 0, silence after; the stereo result in outL/outR
static void impulse(MlxrSurround* s, int channels, int ch, float yawRad) {
    static float in[BLOCK * 8], out[BLOCK * 2];
    mlxr_surround_reset(s);
    for (int b = 0; b < BLOCKS; b++) {
        memset(in, 0, sizeof in);
        if (b == 0) {
            in[20 * channels + ch] = 1.0f;
        }
        mlxr_surround_render_yaw(s, in, BLOCK, yawRad, out);
        for (int f = 0; f < BLOCK; f++) {
            outL[b * BLOCK + f] = out[2 * f];
            outR[b * BLOCK + f] = out[2 * f + 1];
        }
    }
}

typedef struct {
    double l, r;  // ear energies
    int lag;      // positive: left ear first
} Ears;

static Ears measure(MlxrSurround* s, int channels, int ch, float yawDeg) {
    impulse(s, channels, ch, RAD(yawDeg));
    Ears e = {energyOf(outL, N), energyOf(outR, N), lagLeftToRight(outL, outR, N, 60)};
    return e;
}

static MlxrSurround* make(int channels, int forceDirect) {
    MlxrSurroundParams p;
    mlxr_surround_params_default(&p);
    p.hrtfDir = HRTF_DIR;
    p.channels = channels;
    p.forceDirect = forceDirect;
    char err[200] = "";
    MlxrSurround* s = mlxr_surround_create(&p, err, sizeof err);
    if (s == NULL) {
        fprintf(stderr, "create failed: %s\n", err);
    }
    CHECK(s != NULL);
    return s;
}

static void testAzimuths(int channels) {
    MlxrSurround* s = make(channels, 0);
    if (s == NULL) {
        return;
    }
    CHECK(strcmp(mlxr_surround_kernel(s), "FFT") == 0);
    CHECK(mlxr_surround_taps(s) == 175);
    Ears fl = measure(s, channels, 0, 0), fr = measure(s, channels, 1, 0);
    Ears fc = measure(s, channels, 2, 0), lfe = measure(s, channels, 3, 0);
    Ears bl = measure(s, channels, 4, 0), br = measure(s, channels, 5, 0);
    printf("  %d ch: FL %.2f dB lag %d | FR %.2f dB lag %d | FC %.2f dB lag %d | BL %.2f dB lag %d"
           " | BR %.2f dB lag %d\n", channels, 10 * log10(fl.l / fl.r), fl.lag,
           10 * log10(fr.l / fr.r), fr.lag, 10 * log10(fc.l / fc.r), fc.lag,
           10 * log10(bl.l / bl.r), bl.lag, 10 * log10(br.l / br.r), br.lag);

    // Front left (-30): the left ear is louder and hears it first (0.26 ms ~ 12 samples)
    CHECK(fl.l > 1.3 * fl.r);
    CHECK(fl.lag >= 5 && fl.lag <= 25);
    // Front right is its mirror image
    CHECK(fr.r > 1.3 * fr.l);
    CHECK(fr.lag <= -5 && fr.lag >= -25);
    CHECK_NEAR(fl.l / fr.r, 1.0, 0.01);
    CHECK_NEAR(fl.r / fr.l, 1.0, 0.01);
    CHECK(fl.lag == -fr.lag);
    // Centre: no interaural difference
    CHECK_NEAR(10 * log10(fc.l / fc.r), 0.0, 0.5);
    CHECK(abs(fc.lag) <= 1);
    // The LFE goes to both ears alike, unfiltered: an impulse of 0.707
    impulse(s, channels, 3, 0);
    CHECK_NEAR(outL[20], 0.70710678, 1e-4);
    CHECK_NEAR(outR[20], 0.70710678, 1e-4);
    CHECK_NEAR(energyOf(outL, N), 0.5, 1e-3);
    CHECK_NEAR(lfe.l, lfe.r, 1e-6);
    CHECK(lfe.lag == 0);
    // Rear pair (-135 / -150): still on the left of the head, the left ear first
    CHECK(bl.l > 1.1 * bl.r);
    CHECK(bl.lag >= 3);
    CHECK(br.r > 1.1 * br.l);
    CHECK(br.lag <= -3);
    CHECK_NEAR(bl.l / br.r, 1.0, 0.02);
    if (channels == 8) {
        Ears sl = measure(s, channels, 6, 0), sr = measure(s, channels, 7, 0);
        printf("  8 ch: SL %.2f dB lag %d | SR %.2f dB lag %d\n", 10 * log10(sl.l / sl.r), sl.lag,
               10 * log10(sr.l / sr.r), sr.lag);
        // Hard left: the far ear is shadowed by the head, ~0.6 ms ~ 28 samples
        CHECK(sl.l > 2.0 * sl.r);
        CHECK(sl.lag >= 15 && sl.lag <= 45);
        CHECK(sr.r > 2.0 * sr.l);
        CHECK(sr.lag <= -15 && sr.lag >= -45);
        // Further round than the front pair
        CHECK(sl.lag > fl.lag);
        CHECK(sl.l / sl.r > fl.l / fl.r);
    }
    // The rear pair is turned down and duller than a front speaker at the same azimuth
    CHECK(bl.l + bl.r < fl.l + fl.r);

    // Sound is delivered in the same call: an impulse at frame 0 of the first block
    // produces output inside that block (onset ~1 ms plus the EQ's 32 samples)
    {
        static float in[BLOCK * 8], out[BLOCK * 2];
        mlxr_surround_reset(s);
        memset(in, 0, sizeof in);
        in[0 * channels + 2] = 1.0f;
        mlxr_surround_render_yaw(s, in, BLOCK, 0.0f, out);
        double early = 0;
        for (int f = 0; f < 120; f++) {
            early += (double) out[2 * f] * out[2 * f];
        }
        CHECK(early > 0.05);
    }
    mlxr_surround_destroy(s);
}

static void testHeadPose(int channels) {
    MlxrSurround* s = make(channels, 0);
    if (s == NULL) {
        return;
    }
    // Head turned 90 degrees to the left: the screen is now at the right ear, so
    // the centre speaker moves to the right
    Ears left90 = measure(s, channels, 2, 90), right90 = measure(s, channels, 2, -90);
    Ears back = measure(s, channels, 2, 180);
    printf("  FC head +90: %.2f dB lag %d | head -90: %.2f dB lag %d | head 180: %.2f dB lag %d\n",
           10 * log10(left90.l / left90.r), left90.lag, 10 * log10(right90.l / right90.r),
           right90.lag, 10 * log10(back.l / back.r), back.lag);
    CHECK(left90.r > 2.0 * left90.l);
    CHECK(left90.lag <= -15);
    CHECK(right90.l > 2.0 * right90.r);
    CHECK(right90.lag >= 15);
    CHECK_NEAR(left90.r / right90.l, 1.0, 0.01);
    CHECK_NEAR(10 * log10(back.l / back.r), 0.0, 0.6);
    // Held to the screen: front-left seen with the head turned 30 degrees left is
    // straight ahead (az -30 + 30 = 0); turned 60 degrees it is front-right
    Ears fl30 = measure(s, channels, 0, 30);
    Ears fl60 = measure(s, channels, 0, 60);
    Ears fc0 = measure(s, channels, 2, 0);
    CHECK_NEAR(10 * log10(fl30.l / fl30.r), 0.0, 1.0);
    CHECK(abs(fl30.lag) <= 1);
    // (the centre speaker is 3 dB down, so half the energy of a front speaker at the same azimuth)
    CHECK_NEAR(fl30.l, 2.0 * fc0.l, 0.02 * fc0.l);
    CHECK(fl60.r > 1.3 * fl60.l);
    CHECK(fl60.lag < 0);
    // A full turn is the identity, and a turn is symmetric in sign
    Ears full = measure(s, channels, 0, 360);
    Ears none = measure(s, channels, 0, 0);
    CHECK_NEAR(full.l / none.l, 1.0, 1e-3);
    CHECK_NEAR(full.r / none.r, 1.0, 1e-3);
    // The stored pose is what render() uses
    static float in[BLOCK * 8], out1[BLOCK * 2], out2[BLOCK * 2];
    mlxr_surround_reset(s);
    memset(in, 0, sizeof in);
    in[10 * channels + 2] = 1.0f;
    mlxr_surround_set_pose(s, RAD(90), RAD(10));
    float yaw, pitch;
    mlxr_surround_get_pose(s, &yaw, &pitch);
    CHECK_NEAR(yaw, RAD(90), 1e-6);
    CHECK_NEAR(pitch, RAD(10), 1e-6);
    mlxr_surround_render(s, in, BLOCK, out1);
    mlxr_surround_reset(s);
    mlxr_surround_render_yaw(s, in, BLOCK, RAD(90), out2);
    CHECK(memcmp(out1, out2, sizeof out1) == 0);
    // NaN / infinite poses are ignored
    mlxr_surround_set_pose(s, NAN, INFINITY);
    mlxr_surround_get_pose(s, &yaw, &pitch);
    CHECK_NEAR(yaw, RAD(90), 1e-6);
    CHECK_NEAR(pitch, RAD(10), 1e-6);
    mlxr_surround_destroy(s);
}

// Turning crossfades over the block between the old and new filters, exactly:
// block = was * old + (1 - was) * new, with was = 1 - (f + 1) / frames
static float sig(int n) {
    return 0.3f * sinf(2.0f * (float) M_PI * 1000.0f * (float) n / 48000.0f)
           + 0.2f * sinf(2.0f * (float) M_PI * 3100.0f * (float) n / 48000.0f);
}

static void renderRun(MlxrSurround* s, int channels, int blocks, const float* yaws, float* out) {
    static float in[BLOCK * 8];
    mlxr_surround_reset(s);
    for (int b = 0; b < blocks; b++) {
        memset(in, 0, sizeof in);
        for (int f = 0; f < BLOCK; f++) {
            in[f * channels + 0] = sig(b * BLOCK + f);
            in[f * channels + 2] = 0.5f * sig(b * BLOCK + f + 7);
            in[f * channels + 5] = 0.2f * sig(b * BLOCK + f + 3);
        }
        mlxr_surround_render_yaw(s, in, BLOCK, RAD(yaws[b]), out + (size_t) b * BLOCK * 2);
    }
}

static void testCrossfade(void) {
    const int channels = 6;
    MlxrSurround* s = make(channels, 0);
    if (s == NULL) {
        return;
    }
    enum { B = 6 };
    static float a[B * BLOCK * 2], b[B * BLOCK * 2], c[B * BLOCK * 2];
    float yawsA[B] = {0, 0, 0, 0, 0, 0};
    float yawsB[B] = {20, 20, 20, 20, 20, 20};
    float yawsC[B] = {0, 0, 0, 20, 20, 20};  // turn at block 3
    renderRun(s, channels, B, yawsA, a);
    renderRun(s, channels, B, yawsB, b);
    renderRun(s, channels, B, yawsC, c);
    // Before the turn the run is run A; after the turn block it is run B
    double before = 0, after = 0, fade = 0;
    for (int i = 0; i < 3 * BLOCK * 2; i++) {
        before = fmax(before, fabs(c[i] - a[i]));
    }
    for (int i = 4 * BLOCK * 2; i < B * BLOCK * 2; i++) {
        after = fmax(after, fabs(c[i] - b[i]));
    }
    for (int f = 0; f < BLOCK; f++) {
        float was = 1.0f - (f + 1.0f) / BLOCK;
        for (int ear = 0; ear < 2; ear++) {
            int i = (3 * BLOCK + f) * 2 + ear;
            fade = fmax(fade, fabs(c[i] - (was * a[i] + (1.0f - was) * b[i])));
        }
    }
    printf("  crossfade: before %.2g, fade block %.2g, after %.2g\n", before, fade, after);
    CHECK(before < 1e-5);
    CHECK(fade < 1e-4);
    CHECK(after < 1e-4);
    // And it is a real transition: the runs differ substantially in that block
    double apart = 0;
    for (int i = 3 * BLOCK * 2; i < 4 * BLOCK * 2; i++) {
        apart = fmax(apart, fabs(a[i] - b[i]));
    }
    CHECK(apart > 0.02);

    // A head movement under the threshold (0.25 degrees) keeps its filters: no fade
    float yawsD[B] = {0, 0, 0, 0.1f, 0.1f, 0.1f};
    static float d[B * BLOCK * 2];
    renderRun(s, channels, B, yawsD, d);
    double maxd = 0;
    for (int i = 3 * BLOCK * 2; i < B * BLOCK * 2; i++) {
        maxd = fmax(maxd, fabs(d[i] - a[i]));
    }
    CHECK(maxd < 0.01);  // 0.1 degree is a tiny change; and no discontinuity step
    mlxr_surround_destroy(s);
}

static void testToggle(void) {
    const int channels = 6;
    MlxrSurround* s = make(channels, 0);
    if (s == NULL) {
        return;
    }
    enum { B = 6 };
    static float on[B * BLOCK * 2], off[B * BLOCK * 2], mix[B * BLOCK * 2], in[BLOCK * 8];
    float yaws[B] = {0, 0, 0, 0, 0, 0};

    mlxr_surround_set_enabled(s, true);
    renderRun(s, channels, B, yaws, on);
    mlxr_surround_set_enabled(s, false);
    renderRun(s, channels, B, yaws, off);

    // Disabled output is the plain downmix of the input
    static float down[BLOCK * 2];
    for (int f = 0; f < BLOCK; f++) {
        memset(in + f * channels, 0, sizeof(float) * channels);
        in[f * channels + 0] = sig(5 * BLOCK + f);
        in[f * channels + 2] = 0.5f * sig(5 * BLOCK + f + 7);
        in[f * channels + 5] = 0.2f * sig(5 * BLOCK + f + 3);
    }
    mlxr_downmix_to_stereo(channels, in, BLOCK, down);
    double dm = 0;
    for (int i = 0; i < BLOCK * 2; i++) {
        dm = fmax(dm, fabs(off[5 * BLOCK * 2 + i] - fmaxf(-1, fminf(1, down[i]))));
    }
    CHECK(dm < 1e-6);
    CHECK(!mlxr_surround_is_enabled(s));

    // Toggle on at block 3: that block fades from the downmix to the HRTF render
    mlxr_surround_set_enabled(s, false);
    mlxr_surround_reset(s);
    for (int b = 0; b < B; b++) {
        if (b == 3) {
            mlxr_surround_set_enabled(s, true);
        }
        for (int f = 0; f < BLOCK; f++) {
            memset(in + f * channels, 0, sizeof(float) * channels);
            in[f * channels + 0] = sig(b * BLOCK + f);
            in[f * channels + 2] = 0.5f * sig(b * BLOCK + f + 7);
            in[f * channels + 5] = 0.2f * sig(b * BLOCK + f + 3);
        }
        mlxr_surround_render_yaw(s, in, BLOCK, 0.0f, mix + (size_t) b * BLOCK * 2);
    }
    double e0 = 0, e1 = 0, e2 = 0;
    for (int i = 0; i < 3 * BLOCK * 2; i++) {
        e0 = fmax(e0, fabs(mix[i] - off[i]));
    }
    for (int f = 0; f < BLOCK; f++) {
        float to = (f + 1.0f) / BLOCK;
        for (int ear = 0; ear < 2; ear++) {
            int i = (3 * BLOCK + f) * 2 + ear;
            e1 = fmax(e1, fabs(mix[i] - (to * on[i] + (1.0f - to) * off[i])));
        }
    }
    for (int i = 4 * BLOCK * 2; i < B * BLOCK * 2; i++) {
        e2 = fmax(e2, fabs(mix[i] - on[i]));
    }
    printf("  toggle: off %.2g, fade %.2g, on %.2g\n", e0, e1, e2);
    double apartT = 0;
    for (int i = 3 * BLOCK * 2; i < 4 * BLOCK * 2; i++) {
        apartT = fmax(apartT, fabs(on[i] - off[i]));
    }
    CHECK(apartT > 0.02);  // the two renderings really differ, so the fade is a transition
    CHECK(e0 < 1e-6);
    CHECK(e1 < 2e-3);  // the history kept running while disabled, so the fade is to the real render
    CHECK(e2 < 2e-3);
    mlxr_surround_destroy(s);
}

static void testKernelsAgree(void) {
    const int channels = 8;
    MlxrSurround* fft = make(channels, 0);
    MlxrSurround* direct = make(channels, 1);
    if (fft == NULL || direct == NULL) {
        return;
    }
    CHECK(strcmp(mlxr_surround_kernel(direct), "direct") == 0);
    static float in[BLOCK * 8], a[BLOCK * 2], b[BLOCK * 2];
    srand(5);
    double worst = 0, peak = 0;
    for (int block = 0; block < 8; block++) {
        for (int i = 0; i < BLOCK * channels; i++) {
            in[i] = 0.25f * (float) (rand() / (double) RAND_MAX * 2.0 - 1.0);
        }
        float yaw = RAD(block == 3 ? 40.0f : block > 3 ? 40.0f : 0.0f);
        mlxr_surround_render_yaw(fft, in, BLOCK, yaw, a);
        mlxr_surround_render_yaw(direct, in, BLOCK, yaw, b);
        for (int i = 0; i < BLOCK * 2; i++) {
            worst = fmax(worst, fabs(a[i] - b[i]));
            peak = fmax(peak, fabs(a[i]));
        }
    }
    printf("  FFT vs direct: worst %.2g of peak %.2f\n", worst, peak);
    CHECK(worst < 1e-3);
    CHECK(peak > 0.1);
    mlxr_surround_destroy(fft);
    mlxr_surround_destroy(direct);
}

// set_pose / set_enabled from another thread while the audio thread renders
static MlxrSurround* gShared;
static volatile int gStop;

static void* poseThread(void* arg) {
    unsigned n = 0;
    while (!gStop) {
        mlxr_surround_set_pose(gShared, RAD((float) (n % 360) - 180.0f), RAD(5));
        if (n % 97 == 0) {
            mlxr_surround_set_enabled(gShared, (n / 97) % 5 != 0);
        }
        n++;
    }
    return NULL;
}

static void testThreads(void) {
    const int channels = 8;
    gShared = make(channels, 0);
    if (gShared == NULL) {
        return;
    }
    pthread_t t;
    pthread_create(&t, NULL, poseThread, NULL);
    static float in[BLOCK * 8], out[BLOCK * 2];
    int bad = 0;
    for (int b = 0; b < 2000; b++) {
        for (int i = 0; i < BLOCK * channels; i++) {
            in[i] = 0.2f * sig(b * 7 + i);
        }
        mlxr_surround_render(gShared, in, BLOCK, out);
        for (int i = 0; i < BLOCK * 2; i++) {
            if (!isfinite(out[i]) || fabsf(out[i]) > 1.0f) {
                bad++;
            }
        }
    }
    gStop = 1;
    pthread_join(t, NULL);
    CHECK(bad == 0);
    mlxr_surround_destroy(gShared);
}

static void testBadInputs(void) {
    MlxrSurroundParams p;
    mlxr_surround_params_default(&p);
    char err[200] = "";
    p.hrtfDir = "/nonexistent";
    CHECK(mlxr_surround_create(&p, err, sizeof err) == NULL);
    CHECK(strlen(err) > 0);
    p.hrtfDir = HRTF_DIR;
    p.channels = 2;
    err[0] = 0;
    CHECK(mlxr_surround_create(&p, err, sizeof err) == NULL);
    CHECK(strlen(err) > 0);
    // Azimuth table
    CHECK_NEAR(mlxr_surround_azimuth(6, 0), -30, 0);
    CHECK_NEAR(mlxr_surround_azimuth(6, 4), -135, 0);
    CHECK_NEAR(mlxr_surround_azimuth(8, 4), -150, 0);
    CHECK_NEAR(mlxr_surround_azimuth(8, 7), 90, 0);
}

int main(void) {
    printf("5.1\n");
    testAzimuths(6);
    testHeadPose(6);
    printf("7.1\n");
    testAzimuths(8);
    testHeadPose(8);
    testCrossfade();
    testToggle();
    testKernelsAgree();
    testThreads();
    testBadInputs();
    return checksDone("audio_surround");
}
