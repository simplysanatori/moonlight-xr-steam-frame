// See hrtf.h. Port of Hrtf.java; the arithmetic is kept as upstream has it.
#include "hrtf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STEP_DEGREES 5.0f
#define ZERO_CROSSINGS 32
#define FFT_SIZE 512
#define PRE_RING 32
#define SMOOTH_OCTAVE (1.0 / 6.0)
#define LIMIT_DB 12.0
#define BAND_LOW_HZ 300.0
#define BAND_HIGH_HZ 15000.0

struct MlxrHrtf {
    float* measuredLeft[MLXR_HRTF_MEASURED];
    float* measuredRight[MLXR_HRTF_MEASURED];
    int sourceTaps;
    float* ringLeft;   // RING * ringTaps
    float* ringRight;
    int ringRate;
    int ringTaps;
};

static void seterr(char* err, int len, const char* fmt, const char* a, int b) {
    if (err != NULL && len > 0) {
        snprintf(err, (size_t) len, fmt, a, b);
    }
}

static int16_t shortAt(const uint8_t* b, size_t at) {
    return (int16_t) (b[at] | (b[at + 1] << 8));
}

static int32_t intAt(const uint8_t* b, size_t at) {
    return (int32_t) ((uint32_t) b[at] | ((uint32_t) b[at + 1] << 8) | ((uint32_t) b[at + 2] << 16)
                      | ((uint32_t) b[at + 3] << 24));
}

static int tagAt(const uint8_t* b, size_t at, const char* tag) {
    return memcmp(b + at, tag, 4) == 0;
}

// 44.1 kHz stereo 16 bit PCM. Chunks are found rather than assumed.
static int readWav(const char* dir, const char* name, float** left, float** right, int* frames,
                   char* err, int errLen) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        seterr(err, errLen, "cannot open %s (%d)", path, 0);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 12 || size > (8 << 20)) {
        fclose(f);
        seterr(err, errLen, "%s has an odd size (%d)", name, (int) size);
        return -1;
    }
    uint8_t* bytes = (uint8_t*) malloc((size_t) size);
    if (bytes == NULL || fread(bytes, 1, (size_t) size, f) != (size_t) size) {
        free(bytes);
        fclose(f);
        seterr(err, errLen, "cannot read %s (%d)", name, 0);
        return -1;
    }
    fclose(f);
    if (!tagAt(bytes, 0, "RIFF") || !tagAt(bytes, 8, "WAVE")) {
        free(bytes);
        seterr(err, errLen, "%s is not a RIFF WAVE file (%d)", name, 0);
        return -1;
    }
    long fmt = -1, data = -1;
    long dataLength = 0;
    long at = 12;
    while (at + 8 <= size) {
        long length = intAt(bytes, (size_t) at + 4);
        if (length < 0 || at + 8 + length > size) {
            length = size - at - 8;
        }
        if (tagAt(bytes, (size_t) at, "fmt ")) {
            fmt = at + 8;
        } else if (tagAt(bytes, (size_t) at, "data")) {
            data = at + 8;
            dataLength = length;
        }
        at += 8 + length + (length & 1);
    }
    if (fmt < 0 || fmt + 16 > size || data < 0) {
        free(bytes);
        seterr(err, errLen, "%s has no fmt or data chunk (%d)", name, 0);
        return -1;
    }
    int format = shortAt(bytes, (size_t) fmt);
    int channels = shortAt(bytes, (size_t) fmt + 2);
    int rate = intAt(bytes, (size_t) fmt + 4);
    int bits = shortAt(bytes, (size_t) fmt + 14);
    if (format != 1 || channels != 2 || rate != MLXR_HRTF_SOURCE_RATE || bits != 16) {
        free(bytes);
        seterr(err, errLen, "%s is not 44.1 kHz stereo 16 bit PCM (%d)", name, format);
        return -1;
    }
    int n = (int) (dataLength / 4);
    if (n <= 0) {
        free(bytes);
        seterr(err, errLen, "%s has no samples (%d)", name, 0);
        return -1;
    }
    float* l = (float*) malloc(sizeof(float) * (size_t) n);
    float* r = (float*) malloc(sizeof(float) * (size_t) n);
    if (l == NULL || r == NULL) {
        free(l);
        free(r);
        free(bytes);
        seterr(err, errLen, "out of memory loading %s (%d)", name, 0);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        l[i] = shortAt(bytes, (size_t) data + 4 * (size_t) i) / 32768.0f;
        r[i] = shortAt(bytes, (size_t) data + 4 * (size_t) i + 2) / 32768.0f;
    }
    free(bytes);
    *left = l;
    *right = r;
    *frames = n;
    return 0;
}

// In place radix 2, 512 points
static void fft(double* real, double* imaginary, int inverse) {
    const int n = FFT_SIZE;
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            double swap = real[i];
            real[i] = real[j];
            real[j] = swap;
            swap = imaginary[i];
            imaginary[i] = imaginary[j];
            imaginary[j] = swap;
        }
    }
    for (int length = 2; length <= n; length <<= 1) {
        double step = (inverse ? 2.0 : -2.0) * M_PI / length;
        int half = length / 2;
        for (int start = 0; start < n; start += length) {
            for (int k = 0; k < half; k++) {
                double angle = step * k;
                double c = cos(angle);
                double s = sin(angle);
                int a = start + k;
                int b = a + half;
                double oddReal = real[b] * c - imaginary[b] * s;
                double oddImaginary = real[b] * s + imaginary[b] * c;
                real[b] = real[a] - oddReal;
                imaginary[b] = imaginary[a] - oddImaginary;
                real[a] += oddReal;
                imaginary[a] += oddImaginary;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < n; i++) {
            real[i] /= n;
            imaginary[i] /= n;
        }
    }
}

static void loadResponse(const float* response, int len, double* real, double* imaginary) {
    for (int i = 0; i < FFT_SIZE; i++) {
        real[i] = i < len ? response[i] : 0.0;
        imaginary[i] = 0.0;
    }
}

static void addPower(const float* response, int len, double* real, double* imaginary,
                     double* power) {
    loadResponse(response, len, real, imaginary);
    fft(real, imaginary, 0);
    for (int bin = 0; bin <= FFT_SIZE / 2; bin++) {
        power[bin] += real[bin] * real[bin] + imaginary[bin] * imaginary[bin];
    }
}

static int binOf(double hz) {
    return (int) floor(hz * FFT_SIZE / MLXR_HRTF_SOURCE_RATE + 0.5);
}

// The inverse of the smoothed average spectrum, levelled over the band and
// kept within the limit. gain has FFT_SIZE/2 + 1 entries.
static void correction(const double* power, int responses, double* gain) {
    const int bins = FFT_SIZE / 2;
    double smooth[FFT_SIZE / 2 + 1];
    double spread = pow(2.0, SMOOTH_OCTAVE);
    for (int bin = 1; bin <= bins; bin++) {
        int from = (int) floor(bin / spread);
        if (from < 1) {
            from = 1;
        }
        int to = (int) ceil(bin * spread);
        if (to > bins) {
            to = bins;
        }
        double sum = 0.0;
        for (int at = from; at <= to; at++) {
            sum += power[at];
        }
        smooth[bin] = sum / (to - from + 1) / responses;
    }
    smooth[0] = smooth[1];
    int low = binOf(BAND_LOW_HZ);
    int high = binOf(BAND_HIGH_HZ);
    if (high > bins) {
        high = bins;
    }
    double logSum = 0.0;
    for (int bin = low; bin <= high; bin++) {
        logSum += 0.5 * log(smooth[bin]);
    }
    double reference = exp(logSum / (high - low + 1));
    double most = pow(10.0, LIMIT_DB / 20.0);
    for (int bin = 0; bin <= bins; bin++) {
        double at = reference / sqrt(smooth[bin]);
        gain[bin] = fmax(1.0 / most, fmin(most, at));
    }
    for (int bin = 0; bin < low; bin++) {
        gain[bin] = gain[low];
    }
    for (int bin = high + 1; bin <= bins; bin++) {
        gain[bin] = gain[high];
    }
}

// The correction rings on both sides, so the result is rotated to bring what
// lands before the onset along with it. Replaces *response (length taps ->
// PRE_RING + taps).
static int corrected(float** response, int taps, double* real, double* imaginary,
                     const double* gain) {
    loadResponse(*response, taps, real, imaginary);
    fft(real, imaginary, 0);
    const int bins = FFT_SIZE / 2;
    for (int bin = 0; bin < FFT_SIZE; bin++) {
        double at = gain[bin <= bins ? bin : FFT_SIZE - bin];
        real[bin] *= at;
        imaginary[bin] *= at;
    }
    fft(real, imaginary, 1);
    float* out = (float*) malloc(sizeof(float) * (size_t) (PRE_RING + taps));
    if (out == NULL) {
        return -1;
    }
    for (int i = 0; i < PRE_RING + taps; i++) {
        out[i] = (float) real[(i + FFT_SIZE - PRE_RING) % FFT_SIZE];
    }
    free(*response);
    *response = out;
    return 0;
}

static int diffuseField(MlxrHrtf* h, int taps) {
    double real[FFT_SIZE], imaginary[FFT_SIZE];
    double power[FFT_SIZE / 2 + 1];
    double gain[FFT_SIZE / 2 + 1];
    memset(power, 0, sizeof power);
    for (int i = 0; i < MLXR_HRTF_MEASURED; i++) {
        addPower(h->measuredLeft[i], taps, real, imaginary, power);
        addPower(h->measuredRight[i], taps, real, imaginary, power);
    }
    correction(power, 2 * MLXR_HRTF_MEASURED, gain);
    for (int i = 0; i < MLXR_HRTF_MEASURED; i++) {
        if (corrected(&h->measuredLeft[i], taps, real, imaginary, gain) != 0
            || corrected(&h->measuredRight[i], taps, real, imaginary, gain) != 0) {
            return -1;
        }
    }
    return PRE_RING + taps;
}

MlxrHrtf* mlxr_hrtf_load(const char* dir, int equalise, char* err, int errLen) {
    MlxrHrtf* h = (MlxrHrtf*) calloc(1, sizeof *h);
    if (h == NULL) {
        seterr(err, errLen, "out of memory (%s%d)", "", 0);
        return NULL;
    }
    int taps = 0;
    for (int i = 0; i < MLXR_HRTF_MEASURED; i++) {
        char name[32];
        snprintf(name, sizeof name, "H0e%03da.wav", i * 5);
        int frames = 0;
        if (readWav(dir, name, &h->measuredLeft[i], &h->measuredRight[i], &frames, err,
                    errLen) != 0) {
            mlxr_hrtf_free(h);
            return NULL;
        }
        if (i == 0) {
            taps = frames;
        } else if (frames != taps) {
            seterr(err, errLen, "%s has a different length (%d)", name, frames);
            mlxr_hrtf_free(h);
            return NULL;
        }
    }
    h->sourceTaps = taps;
    if (equalise) {
        int n = diffuseField(h, taps);
        if (n < 0) {
            seterr(err, errLen, "out of memory (%s%d)", "", 0);
            mlxr_hrtf_free(h);
            return NULL;
        }
        h->sourceTaps = n;
    }
    return h;
}

void mlxr_hrtf_free(MlxrHrtf* h) {
    if (h == NULL) {
        return;
    }
    for (int i = 0; i < MLXR_HRTF_MEASURED; i++) {
        free(h->measuredLeft[i]);
        free(h->measuredRight[i]);
    }
    free(h->ringLeft);
    free(h->ringRight);
    free(h);
}

int mlxr_hrtf_taps(const MlxrHrtf* h, int sampleRate) {
    if (sampleRate == MLXR_HRTF_SOURCE_RATE) {
        return h->sourceTaps;
    }
    return (int) ceil((double) h->sourceTaps * sampleRate / MLXR_HRTF_SOURCE_RATE);
}

static double kernel(double at, double cutoff, double half) {
    double x = cutoff * at;
    double sinc = fabs(x) < 1e-9 ? 1.0 : sin(M_PI * x) / (M_PI * x);
    double window = 0.42 + 0.5 * cos(M_PI * at / half) + 0.08 * cos(2.0 * M_PI * at / half);
    return cutoff * sinc * window;
}

// Windowed sinc resampling of one filter, scaled by the rate ratio so its
// gain holds.
static void resample(const float* source, int sourceLen, int rate, int taps, float* out) {
    if (rate == MLXR_HRTF_SOURCE_RATE) {
        memcpy(out, source, sizeof(float) * (size_t) sourceLen);
        return;
    }
    double ratio = (double) rate / MLXR_HRTF_SOURCE_RATE;
    double cutoff = ratio < 1.0 ? ratio : 1.0;
    double half = ZERO_CROSSINGS / cutoff;
    for (int n = 0; n < taps; n++) {
        double at = n / ratio;
        int from = (int) ceil(at - half);
        int to = (int) floor(at + half);
        if (from < 0) {
            from = 0;
        }
        if (to > sourceLen - 1) {
            to = sourceLen - 1;
        }
        double sum = 0.0;
        for (int m = from; m <= to; m++) {
            sum += source[m] * kernel(at - m, cutoff, half);
        }
        out[n] = (float) (sum / ratio);
    }
}

static void reverseInto(const float* in, int n, float* out) {
    for (int i = 0; i < n; i++) {
        out[i] = in[n - 1 - i];
    }
}

int mlxr_hrtf_prepare(MlxrHrtf* h, int sampleRate) {
    if (sampleRate == h->ringRate && h->ringLeft != NULL) {
        return 0;
    }
    int taps = mlxr_hrtf_taps(h, sampleRate);
    float* ringL = (float*) malloc(sizeof(float) * (size_t) MLXR_HRTF_RING * (size_t) taps);
    float* ringR = (float*) malloc(sizeof(float) * (size_t) MLXR_HRTF_RING * (size_t) taps);
    float* left = (float*) malloc(sizeof(float) * (size_t) MLXR_HRTF_MEASURED * (size_t) taps);
    float* right = (float*) malloc(sizeof(float) * (size_t) MLXR_HRTF_MEASURED * (size_t) taps);
    if (ringL == NULL || ringR == NULL || left == NULL || right == NULL) {
        free(ringL);
        free(ringR);
        free(left);
        free(right);
        return -1;
    }
    for (int i = 0; i < MLXR_HRTF_MEASURED; i++) {
        resample(h->measuredLeft[i], h->sourceTaps, sampleRate, taps, left + (size_t) i * taps);
        resample(h->measuredRight[i], h->sourceTaps, sampleRate, taps, right + (size_t) i * taps);
    }
    for (int i = 0; i < MLXR_HRTF_RING; i++) {
        // Beyond 180 the source is on the other side: the measurement at 360
        // minus the azimuth, heard with the ears swapped
        int mirrored = i >= MLXR_HRTF_MEASURED;
        int m = mirrored ? MLXR_HRTF_RING - i : i;
        reverseInto(mirrored ? right + (size_t) m * taps : left + (size_t) m * taps, taps,
                    ringL + (size_t) i * taps);
        reverseInto(mirrored ? left + (size_t) m * taps : right + (size_t) m * taps, taps,
                    ringR + (size_t) i * taps);
    }
    free(left);
    free(right);
    free(h->ringLeft);
    free(h->ringRight);
    h->ringLeft = ringL;
    h->ringRight = ringR;
    h->ringTaps = taps;
    h->ringRate = sampleRate;
    return 0;
}

void mlxr_hrtf_ring(const MlxrHrtf* h, const float** left, const float** right) {
    *left = h->ringLeft;
    *right = h->ringRight;
}

float mlxr_hrtf_between(float azimuthDegrees, int into[2]) {
    float azimuth = fmodf(azimuthDegrees, 360.0f);
    if (azimuth < 0.0f) {
        azimuth += 360.0f;
    }
    float step = azimuth / STEP_DEGREES;
    int low = (int) step;
    if (low >= MLXR_HRTF_RING) {
        low = MLXR_HRTF_RING - 1;
    }
    into[0] = low;
    into[1] = low + 1 == MLXR_HRTF_RING ? 0 : low + 1;
    return step - (float) low;
}

void mlxr_hrtf_filter_for(const MlxrHrtf* h, float azimuthDegrees, float* left, float* right) {
    int entries[2];
    float far = mlxr_hrtf_between(azimuthDegrees, entries);
    float near = 1.0f - far;
    const int taps = h->ringTaps;
    const float* lowLeft = h->ringLeft + (size_t) entries[0] * taps;
    const float* highLeft = h->ringLeft + (size_t) entries[1] * taps;
    const float* lowRight = h->ringRight + (size_t) entries[0] * taps;
    const float* highRight = h->ringRight + (size_t) entries[1] * taps;
    for (int i = 0; i < taps; i++) {
        left[i] = near * lowLeft[i] + far * highLeft[i];
        right[i] = near * lowRight[i] + far * highRight[i];
    }
}
