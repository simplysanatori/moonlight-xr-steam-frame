// Moonlight XR Native: latest-decoded-frame mailbox. GPLv3.
#include "frame_slot.h"

#include <libavutil/frame.h>

void frame_slot_init(FrameSlot* s) {
    pthread_mutex_init(&s->lock, NULL);
    s->pending = NULL;
    s->produced = s->consumed = s->dropped = 0;
    s->lastDecodeMs = 0;
}

void frame_slot_destroy(FrameSlot* s) {
    av_frame_free(&s->pending);
    pthread_mutex_destroy(&s->lock);
}

void frame_slot_put(FrameSlot* s, const AVFrame* frame, double decodeMs) {
    AVFrame* f = av_frame_clone(frame);
    if (!f) return;
    pthread_mutex_lock(&s->lock);
    if (s->pending) {
        av_frame_free(&s->pending);
        s->dropped++;
    }
    s->pending = f;
    s->produced++;
    s->lastDecodeMs = decodeMs;
    pthread_mutex_unlock(&s->lock);
}

AVFrame* frame_slot_take(FrameSlot* s) {
    pthread_mutex_lock(&s->lock);
    AVFrame* f = s->pending;
    s->pending = NULL;
    if (f) s->consumed++;
    pthread_mutex_unlock(&s->lock);
    return f;
}
