// Virtual surround: a decoded stereo, 5.1 or 7.1 block in, a stereo block of
// the same length out. Port of Moonlight XR's BinauralRenderer/VirtualSurround/
// HeadYaw (GPLv3) over xr_audio's C kernels. Native (lane polish-3): stereo
// too, as two speakers in front of the screen at -30 and 30 degrees, so the
// sound stays with the screen as the head turns.
//
// Each speaker but the LFE is convolved with the KEMAR filters for where it
// stands around the listener and the lot is summed to a left and a right ear.
// Speaker azimuths are clockwise from the middle of the screen and the head
// yaw is added to them, so turning the head left swings the speakers right and
// the sound stays "held to the screen". A head that moved since the last block
// renders through the old and the new filters and crossfades over the block.
// Every frame handed in comes back out of the same call: no block of latency.
//
// Input channel order (Moonlight / Opus decode order):
//   2.0: FL FR                     (-30/30, as the 5.1 front pair)
//   5.1: FL FR FC LFE BL BR        (BL/BR are the 110 degree surrounds, -135/135)
//   7.1: FL FR FC LFE BL BR SL SR  (back -150/150, side -90/90)
//
// Threading: mlxr_surround_set_pose / set_enabled / the getters may be called
// from any thread at any time (lock-free atomics). create / destroy / reset /
// render must not run concurrently with each other: render is called by the
// one audio thread.
//
// Disabled: render() downmixes to stereo without HRTFs (L = FL + .707 (FC + LFE
// + BL + SL), R likewise, as Android's own downmix has it) so the same
// stereo stream carries the sound; a stereo input passes through as it is.
// The two are crossfaded over one block when the flag changes. The
// convolution history keeps running while disabled so re-enabling does not
// start from stale audio.
#ifndef MLXR_SURROUND_H
#define MLXR_SURROUND_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MlxrSurround MlxrSurround;

typedef struct {
    const char* hrtfDir;     // directory with the 37 KEMAR WAVs (assets/hrtf/kemar)
    int channels;            // 2, 6 or 8
    int sampleRate;          // 48000
    int maxBlockFrames;      // expected block (240 for 5 ms); larger blocks reallocate
    float rearDelayMs;       // 0 as upstream ships it; 0..20
    int forceDirect;         // skip the FFT kernel (tests; slow)
    int equalise;            // diffuse field EQ; 1 as upstream (0 for tests of raw KEMAR)
} MlxrSurroundParams;

/** Fills defaults: 6 channels, 48 kHz, 240 frames, no rear delay, FFT, equalised. */
void mlxr_surround_params_default(MlxrSurroundParams* p);

/** Loads the HRTFs and builds the renderer. NULL on failure, message in err. */
MlxrSurround* mlxr_surround_create(const MlxrSurroundParams* p, char* err, int errLen);
void mlxr_surround_destroy(MlxrSurround* s);

/** Forgets everything carried between blocks (stream restart). */
void mlxr_surround_reset(MlxrSurround* s);

/**
 * Head pose against the screen, radians, any thread. Yaw is positive with the
 * head turned LEFT of the screen (0 with the screen locked to the head).
 * Pitch is positive looking up; the KEMAR set only has the horizontal plane,
 * so pitch is stored and readable but does not change the rendering.
 * NaN / infinite values are ignored (treated as 0 when rendering).
 */
void mlxr_surround_set_pose(MlxrSurround* s, float yawRadians, float pitchRadians);
void mlxr_surround_get_pose(const MlxrSurround* s, float* yawRadians, float* pitchRadians);

/** Runtime on/off for the HRTF rendering (see "Disabled" above). Any thread. */
void mlxr_surround_set_enabled(MlxrSurround* s, bool enabled);
bool mlxr_surround_is_enabled(const MlxrSurround* s);

/**
 * Renders interleaved float frames (channels per frame, nominally +-1) to
 * interleaved stereo, using the pose last set. in and out must not overlap.
 */
void mlxr_surround_render(MlxrSurround* s, const float* in, int frames, float* out);

/** As above with the yaw given (used by tests; ignores the stored pose). */
void mlxr_surround_render_yaw(MlxrSurround* s, const float* in, int frames, float yawRadians,
                              float* out);

/** The stateless plain downmix (channels 2, 6 or 8; 2 is a copy), unclamped, as used when disabled. */
void mlxr_downmix_to_stereo(int channels, const float* in, int frames, float* out);

int mlxr_surround_taps(const MlxrSurround* s);
/** "FFT" or "direct". */
const char* mlxr_surround_kernel(const MlxrSurround* s);
/** Speaker azimuth in degrees (clockwise, 0 = screen centre) for a channel. */
float mlxr_surround_azimuth(int channels, int channel);

#ifdef __cplusplus
}
#endif
#endif
