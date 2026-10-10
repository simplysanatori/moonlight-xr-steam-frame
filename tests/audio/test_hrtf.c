// The KEMAR loader: file parsing, diffuse field equalisation, resampling and
// the mirrored ring.
#include "check.h"
#include "hrtf.h"
#include "tutil.h"

#include <stdlib.h>
#include <string.h>

#ifndef HRTF_DIR
#error HRTF_DIR
#endif

// Average power in a band of a response set, via a direct DFT at a few frequencies
static double bandPower(MlxrHrtf* h, int rate, double hz) {
    const float *ringL, *ringR;
    mlxr_hrtf_ring(h, &ringL, &ringR);
    int taps = mlxr_hrtf_taps(h, rate);
    double sum = 0;
    for (int i = 0; i < MLXR_HRTF_RING; i++) {
        for (int ear = 0; ear < 2; ear++) {
            const float* f = (ear ? ringR : ringL) + (size_t) i * taps;
            double re = 0, im = 0;
            for (int n = 0; n < taps; n++) {
                double w = 2.0 * M_PI * hz * n / rate;
                re += f[n] * cos(w);
                im -= f[n] * sin(w);
            }
            sum += re * re + im * im;
        }
    }
    return sum / (2 * MLXR_HRTF_RING);
}

static double spreadDb(MlxrHrtf* h, int rate) {
    // 1/3 octave spaced centres, 400 Hz .. 12.5 kHz
    double lo = 1e9, hi = -1e9;
    for (double hz = 400; hz < 12500; hz *= 1.2599) {
        double d = 10 * log10(bandPower(h, rate, hz));
        lo = fmin(lo, d);
        hi = fmax(hi, d);
    }
    return hi - lo;
}

int main(void) {
    char err[200] = "";
    MlxrHrtf* raw = mlxr_hrtf_load(HRTF_DIR, 0, err, sizeof err);
    MlxrHrtf* eq = mlxr_hrtf_load(HRTF_DIR, 1, err, sizeof err);
    CHECK(raw != NULL && eq != NULL);
    if (!raw || !eq) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    // 128 measured taps; the EQ moves each 32 later and lengthens it by 32
    CHECK(mlxr_hrtf_taps(raw, 44100) == 128);
    CHECK(mlxr_hrtf_taps(eq, 44100) == 160);
    CHECK(mlxr_hrtf_taps(eq, 48000) == 175);
    CHECK(mlxr_hrtf_taps(raw, 48000) == 140);
    CHECK(mlxr_hrtf_prepare(eq, 48000) == 0);
    CHECK(mlxr_hrtf_prepare(raw, 48000) == 0);
    CHECK(mlxr_hrtf_prepare(eq, 44100) == 0);
    CHECK(mlxr_hrtf_prepare(eq, 48000) == 0);

    const int taps = 175;
    const float *rl, *rr;
    mlxr_hrtf_ring(eq, &rl, &rr);
    // Azimuth 0 is symmetric between the ears (the measured pair is not exactly
    // equal, but nearly); 90 is louder on the right, and the ring mirrors
    double e0l = energyOf(rl, taps), e0r = energyOf(rr, taps);
    double e90l = energyOf(rl + 18 * taps, taps), e90r = energyOf(rr + 18 * taps, taps);
    CHECK(e90r > 2 * e90l);
    CHECK_NEAR(e0l / e0r, 1.0, 0.15);
    for (int i = 1; i < MLXR_HRTF_MEASURED - 1; i++) {
        int m = MLXR_HRTF_RING - i;
        for (int n = 0; n < taps; n++) {
            if (rl[(size_t) m * taps + n] != rr[(size_t) i * taps + n]
                || rr[(size_t) m * taps + n] != rl[(size_t) i * taps + n]) {
                CHECK(0);
                n = taps;
                i = 100;
            }
        }
    }
    // The filter starts reversed: the onset (largest tap) sits towards the end
    int peakAt = 0;
    for (int n = 0; n < taps; n++) {
        if (fabsf(rl[n]) > fabsf(rl[peakAt])) {
            peakAt = n;
        }
    }
    CHECK(peakAt > taps / 2);

    // Equalisation flattens the average spectrum across the band
    double rawSpread = spreadDb(raw, 48000), eqSpread = spreadDb(eq, 48000);
    printf("  average spectrum spread 0.4-12.5 kHz: raw %.1f dB, equalised %.1f dB\n", rawSpread,
           eqSpread);
    CHECK(eqSpread < rawSpread - 3.0);
    CHECK(eqSpread < 12.0);

    // between(): wrap, negative, fractions
    int e[2];
    CHECK_NEAR(mlxr_hrtf_between(12.5f, e), 0.5, 1e-5);
    CHECK(e[0] == 2 && e[1] == 3);
    CHECK_NEAR(mlxr_hrtf_between(-5.0f, e), 0.0, 1e-5);
    CHECK(e[0] == 71 && e[1] == 0);
    CHECK_NEAR(mlxr_hrtf_between(357.5f, e), 0.5, 1e-5);
    CHECK(e[0] == 71 && e[1] == 0);
    CHECK_NEAR(mlxr_hrtf_between(720.0f, e), 0.0, 1e-5);
    CHECK(e[0] == 0);

    // filter_for blends the two nearest ring entries
    static float l[175], r[175];
    mlxr_hrtf_filter_for(eq, 7.5f, l, r);
    for (int n = 0; n < taps; n++) {
        float want = 0.5f * (rl[(size_t) 1 * taps + n] + rl[(size_t) 2 * taps + n]);
        CHECK_NEAR(l[n], want, 1e-6);
    }

    // Failure paths
    CHECK(mlxr_hrtf_load("/nonexistent-dir", 1, err, sizeof err) == NULL);
    CHECK(strstr(err, "nonexistent") != NULL);
    mlxr_hrtf_free(raw);
    mlxr_hrtf_free(eq);
    return checksDone("audio_hrtf");
}
