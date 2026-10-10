// The MIT KEMAR head related impulse responses, prepared for the virtual
// surround. A C port of Moonlight XR's Hrtf.java (GPLv3).
//
// The compact set has 37 measurements on the horizontal plane (azimuth 0..180
// in 5 degree steps, 128 taps at 44.1 kHz, channel 0 the left ear). Azimuth
// runs clockwise, so 90 is to the listener's right. The head is taken as
// symmetrical: an azimuth past 180 uses the measurement at 360 minus it with
// the ears swapped.
//
// Loading applies diffuse field equalisation (the average power spectrum of
// all 74 responses, smoothed to 1/3 octave, inverted within 12 dB either way,
// zero phase), which moves every response 32 samples later and grows it by 32
// taps. mlxr_hrtf_prepare() resamples to the output rate and lays out the ring
// of 72 azimuths, each filter reversed for the convolution kernels.
//
// Not thread safe: one instance per stream; build it off the audio thread.
#ifndef MLXR_HRTF_H
#define MLXR_HRTF_H

#ifdef __cplusplus
extern "C" {
#endif

#define MLXR_HRTF_MEASURED 37
#define MLXR_HRTF_RING 72
#define MLXR_HRTF_SOURCE_RATE 44100

typedef struct MlxrHrtf MlxrHrtf;

/**
 * Loads H0e000a.wav .. H0e180a.wav from dir (no trailing slash needed) and
 * equalises them unless equalise is 0. Returns NULL on failure with a message
 * in err (if err is non-NULL).
 */
MlxrHrtf* mlxr_hrtf_load(const char* dir, int equalise, char* err, int errLen);
void mlxr_hrtf_free(MlxrHrtf* h);

/** Filter length at a rate (175 at 48 kHz for the equalised set). */
int mlxr_hrtf_taps(const MlxrHrtf* h, int sampleRate);

/** Builds the ring at a rate. 0 on success, -1 on allocation failure. */
int mlxr_hrtf_prepare(MlxrHrtf* h, int sampleRate);

/**
 * The prepared ring: MLXR_HRTF_RING filters of mlxr_hrtf_taps() floats per ear,
 * reversed, entry i at i * taps. Valid until the next prepare at another rate.
 */
void mlxr_hrtf_ring(const MlxrHrtf* h, const float** left, const float** right);

/**
 * The two ring entries an azimuth (degrees, clockwise, any range) lies between,
 * into into[0], into[1], and the fraction of the way from the first to the
 * second.
 */
float mlxr_hrtf_between(float azimuthDegrees, int into[2]);

/**
 * The blended filter pair for an azimuth, reversed, into arrays of at least
 * taps floats. The rate must have been prepared. No allocation.
 */
void mlxr_hrtf_filter_for(const MlxrHrtf* h, float azimuthDegrees, float* left, float* right);

#ifdef __cplusplus
}
#endif
#endif
