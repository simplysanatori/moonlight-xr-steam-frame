// Small signal helpers shared by the audio tests.
#ifndef AUDIO_TUTIL_H
#define AUDIO_TUTIL_H

#include <math.h>
#include <stddef.h>

static inline double rmsOf(const float* x, int n, int stride) {
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        double v = x[(size_t) i * stride];
        s += v * v;
    }
    return n > 0 ? sqrt(s / n) : 0.0;
}

static inline double energyOf(const float* x, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        s += (double) x[i] * x[i];
    }
    return s;
}

// Amplitude of a sinusoid at freq hz in x (Goertzel, strided), RMS-equivalent
static inline double toneRms(const float* x, int n, int stride, double hz, double rate) {
    double w = 2.0 * M_PI * hz / rate;
    double c = 2.0 * cos(w), s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; i++) {
        double s = x[(size_t) i * stride] + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return sqrt(power) * sqrt(2.0) / n;  // amplitude A -> A/sqrt2 * ... see below
}

static inline double db(double ratio) {
    return 20.0 * log10(ratio > 1e-12 ? ratio : 1e-12);
}

// Lag (in samples) at which R best matches L: positive when R lags L, i.e. the sound
// reached the left ear first
static inline int lagLeftToRight(const float* l, const float* r, int n, int maxLag) {
    int best = 0;
    double bestV = -1e30;
    for (int lag = -maxLag; lag <= maxLag; lag++) {
        double v = 0.0;
        for (int i = 0; i < n; i++) {
            int j = i + lag;
            if (j >= 0 && j < n) {
                v += (double) l[i] * r[j];
            }
        }
        if (v > bestV) {
            bestV = v;
            best = lag;
        }
    }
    return best;
}

#endif
