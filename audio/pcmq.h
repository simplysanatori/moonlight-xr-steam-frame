// A bounded FIFO of interleaved float frames between the thread that decodes
// audio (producer) and the one that hands it to the sound server (consumer).
// A mutex guards it; the critical sections are a memcpy of one packet.
//
// When a push would take the queue past its cap, the OLDEST frames are
// dropped to make room (the listener hears the newest audio and latency stays
// bounded instead of drifting). The next pop after a drop, or a
// mark_discontinuity() (an underrun), fades in over FADE_FRAMES so the splice does not click.
#ifndef MLXR_PCMQ_H
#define MLXR_PCMQ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MLXR_PCMQ_FADE_FRAMES 96  /* 2 ms at 48 kHz */

typedef struct MlxrPcmQueue MlxrPcmQueue;

typedef struct {
    uint64_t pushedFrames;
    uint64_t poppedFrames;
    uint64_t droppedFrames;   // discarded because the queue was past its cap
    uint32_t overruns;        // pushes that had to drop
} MlxrPcmQueueStats;

MlxrPcmQueue* mlxr_pcmq_create(int channels, int capFrames);
void mlxr_pcmq_destroy(MlxrPcmQueue* q);

/** Queues n frames. Returns the number of OLD frames dropped to fit (0 normally). */
int mlxr_pcmq_push(MlxrPcmQueue* q, const float* frames, int n);
/** Takes up to maxFrames. Returns how many (0 when empty). */
int mlxr_pcmq_pop(MlxrPcmQueue* q, float* out, int maxFrames);
int mlxr_pcmq_frames(MlxrPcmQueue* q);
int mlxr_pcmq_capacity(const MlxrPcmQueue* q);
void mlxr_pcmq_set_capacity(MlxrPcmQueue* q, int capFrames);
void mlxr_pcmq_clear(MlxrPcmQueue* q);
/** The next pop fades in (the sink ran dry, so what follows starts from silence). */
void mlxr_pcmq_mark_discontinuity(MlxrPcmQueue* q);
void mlxr_pcmq_stats(MlxrPcmQueue* q, MlxrPcmQueueStats* out);

#ifdef __cplusplus
}
#endif
#endif
