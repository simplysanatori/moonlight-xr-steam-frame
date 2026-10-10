// A short synthesised UI click (no asset), played through the shared
// PulseAudio context on a stream of its own, so it works with or without a
// stream session running and the server mixes it with the game audio.
//
// Threading: init/shutdown from a control thread. play() and set_volume() are
// callable from any thread, including the frame loop: play() only sets an
// atomic bit and posts a semaphore, never touching PulseAudio or blocking. A
// helper thread does the writing, and corks the stream again about half a
// second after the last click so an idle UI does not hold the sound device awake.
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

/** Opens the click stream (sink NULL = default) and starts the helper. 0 on success. */
int mlxr_click_init(const char* sink, char* err, int errLen);
void mlxr_click_shutdown(void);

/** 0..1, default 0.5 (a press peaks around -18 dBFS at the default). Any thread. */
void mlxr_click_set_volume(float volume);
/** Non-blocking. Silently does nothing if init failed or was not called. */
void mlxr_click_play(MlxrClickKind kind);

/**
 * The click's samples: mono float at the given rate, peak ~1.0 before volume.
 * Returns the frame count (<= maxFrames), 0 for a bad kind. Pure; for tests.
 */
int mlxr_click_synthesize(MlxrClickKind kind, int rate, float* out, int maxFrames);

#ifdef __cplusplus
}
#endif
#endif
