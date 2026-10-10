// Moonlight XR Native: latest-decoded-frame mailbox between the decoder and
// the frame loop. The producer replaces the pending frame (dropping the older
// one); the consumer takes it. GPLv3.
#pragma once
#include <pthread.h>
#include <stdint.h>

struct AVFrame;

typedef struct FrameSlot {
    pthread_mutex_t lock;
    struct AVFrame* pending; // owned reference, NULL when empty
    uint64_t produced, consumed, dropped;
    double lastDecodeMs;     // submit -> frame out, last frame
} FrameSlot;

void frame_slot_init(FrameSlot* s);
void frame_slot_destroy(FrameSlot* s);
// Takes a new reference to `frame`.
void frame_slot_put(FrameSlot* s, const struct AVFrame* frame, double decodeMs);
// Returns an owned frame (free with av_frame_free) or NULL.
struct AVFrame* frame_slot_take(FrameSlot* s);
