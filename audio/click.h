// A short synthesised UI click (no asset), played through the shared
// PulseAudio context on a stream of its own, so it works with or without a
// stream session running and the server mixes it with the game audio.
//
// Threading: init/shutdown from a control thread. play() and set_volume() are
// callable from any thread, including the frame loop: play() only counts the
// press in an atomic and posts a semaphore, never touching PulseAudio or
// blocking. A helper thread does the writing, every press it finds waiting (up
// to MLXR_CLICK_BURST_MAX of a kind, a few ms apart), and corks the stream again
// about half a second after the last click so an idle UI does not hold the
// sound device awake.
#ifndef MLXR_CLICK_H
#define MLXR_CLICK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MLXR_CLICK_PRESS = 0,  // a button press: two partials, ~35 ms
    MLXR_CLICK_TICK = 1,   // hover / key: higher, softer, ~12 ms
    MLXR_CLICK_KINDS
} MlxrClickKind;

/**
 * Name of the click's PulseAudio stream (before init). Default "Moonlight XR UI
 * virtual sink", which SteamOS does not run through its VR spatializer (see
 * opus_out.h); pass "Moonlight XR UI" to let the system spatialize the click.
 */
void mlxr_click_set_stream_name(const char* name);

/** Opens the click stream (sink NULL = default) and starts the helper. 0 on success. */
int mlxr_click_init(const char* sink, char* err, int errLen);
void mlxr_click_shutdown(void);

/** 0..1, default 0.5 (a press peaks around -18 dBFS at the default). Any thread. */
void mlxr_click_set_volume(float volume);
/** Non-blocking. Silently does nothing if init failed or was not called. */
void mlxr_click_play(MlxrClickKind kind);

/**
 * Native (owner's request): the most presses of one kind that wait to be
 * played. More while those wait are dropped, so a held key's repeat cannot
 * build a long tail.
 */
#define MLXR_CLICK_BURST_MAX 4

/**
 * The press counter play() and the helper share, for tests: count() adds one
 * press (none past MLXR_CLICK_BURST_MAX) and returns how many wait now; take()
 * returns them all and leaves none.
 */
int mlxr_click_count(MlxrClickKind kind);
int mlxr_click_take(MlxrClickKind kind);

/**
 * count copies of a stereo click of frames frames, each 18 ms after the one
 * before and added in, into out. Returns the frame count (<= maxFrames). Pure;
 * for tests.
 */
int mlxr_click_burst(const float* click, int frames, int count, float* out, int maxFrames);

/**
 * The click's samples: mono float at the given rate, peak ~1.0 before volume.
 * Returns the frame count (<= maxFrames), 0 for a bad kind. Pure; for tests.
 */
int mlxr_click_synthesize(MlxrClickKind kind, int rate, float* out, int maxFrames);

#ifdef __cplusplus
}
#endif
#endif
