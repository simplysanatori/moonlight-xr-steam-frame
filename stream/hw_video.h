// Moonlight XR Native: hardware video path (M2). moonlight-common-c's decode
// units go to the Iris V4L2 decoder; a decode thread receives frames into a
// small queue the XR frame loop takes from (paced or newest, see below). Frames the loop never
// took go straight back to the decoder; frames it took go back through the
// Vulkan importer once the GPU has finished with them. GPLv3.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "../video/v4l2_decoder.h"

typedef struct HwVideo HwVideo;

typedef struct HwVideoStats {
    uint64_t submitted, frames, taken, skipped, errors, idrRequests;
    double lastDecodeMs; // submit -> dequeue of the newest frame
    double avgDecodeMs;
    // Native (lane av-latency), from hw_video_take_refresh: refreshes seen,
    // refreshes that showed a new frame, single-refresh holes (a repeat between
    // two new frames: judder), pacer catch-up drops, frames waiting summed
    uint64_t refreshes, shown, holes, catchUpDrops, queuedSum;
    uint64_t corrupt;    // pictures the decoder flagged V4L2_BUF_FLAG_ERROR
} HwVideoStats;

// requestIdr is called (from the decode thread) on decoder errors.
HwVideo* hw_video_open(mlxr_codec codec, uint32_t width, uint32_t height, void (*requestIdr)(void));
// Stream receive thread: one access unit in pieces. Returns 0, or -1 when the
// session should answer DR_NEED_IDR.
int hw_video_submit(HwVideo* v, const void* const* parts, const uint32_t* sizes, uint32_t n, int64_t pts);
// Native (lane av-latency): ask for an IDR frame when the decoder flags a
// damaged picture (used while reference frame invalidation is advertised).
void hw_video_set_idr_on_corrupt(HwVideo* v, bool on);
// Frame loop: newest decoded frame, if one arrived since the last take
// (older queued ones go back to the decoder, counted as skipped).
bool hw_video_take(HwVideo* v, mlxr_vframe* out);
// Native (lane av-latency): the frame loop, once per display refresh. paced:
// the pacer's pick (pacer.h: oldest queued, catch-up drops only when the
// queue stays long); not paced: the newest, as hw_video_take. Both keep the
// refresh statistics in HwVideoStats.
bool hw_video_take_refresh(HwVideo* v, int paced, int displayHz, mlxr_vframe* out);
// Frame loop: a frame that will not be handed to the GPU after all.
void hw_video_release(HwVideo* v, const mlxr_vframe* f);
mlxr_vdec* hw_video_decoder(HwVideo* v);
// Frame loop: true while a resolution change waits for the renderer to drop
// its imports; call hw_video_format_change_done() after vkimp drain+reset.
bool hw_video_format_change_pending(HwVideo* v);
void hw_video_format_change_done(HwVideo* v);
void hw_video_stats(HwVideo* v, HwVideoStats* s);
// Stops the decode thread but keeps the decoder open: the renderer may still
// hold imports whose frames go back to it. Called when the stream ends.
void hw_video_stop(HwVideo* v);
// Full close: only after the renderer destroyed its imports (xr_app_run returned).
void hw_video_close(HwVideo* v);

// The one stream's hardware decoder, published by the session for the frame
// loop (NULL when software decode is in use).
HwVideo* hw_video_current(void);
void hw_video_set_current(HwVideo* v);
