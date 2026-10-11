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
    // Native (lane perf): per-stage latency of the frames the loop took, summed
    // (the log takes deltas): decode = AU queued to picture out, wait = picture
    // out to taken by the loop (the pacer queue); frames of a burst skipped
    // for the newest after a stall; AUs in the decoder (queued, picture not
    // out yet) at each submit, summed and the most
    uint64_t staleDrops;
    uint64_t takenTimed;
    uint64_t decodeNsSum, decodeNsMax, waitNsSum, waitNsMax;
    uint64_t inFlightSum, inFlightSamples, inFlightMax;
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
// Native (lane av-latency): the frame loop, once per display refresh. mode
// (MlxrPaceMode, pacer.h) smooth or low latency: the pacer's pick (oldest
// queued, catch-up drops only when the queue stays long); newest: the newest,
// as hw_video_take. All keep the refresh statistics in HwVideoStats; nowNs
// (CLOCK_MONOTONIC, 0: not timed) times the taken frame's wait in the queue.
bool hw_video_take_refresh(HwVideo* v, int mode, int displayHz, uint64_t nowNs, mlxr_vframe* out);
// Native (lane perf): raise the decode thread's priority (MlxrPrioMode of
// audio/thread_prio.h) when it starts; set before frames flow
void hw_video_set_thread_priority(HwVideo* v, int mode);
// Frame loop: a frame that will not be handed to the GPU after all.
void hw_video_release(HwVideo* v, const mlxr_vframe* f);
mlxr_vdec* hw_video_decoder(HwVideo* v);
// Frame loop: true while a resolution change waits for the renderer to drop
// its imports; call hw_video_format_change_done() after vkimp drain+reset.
bool hw_video_format_change_pending(HwVideo* v);
void hw_video_format_change_done(HwVideo* v);
void hw_video_stats(HwVideo* v, HwVideoStats* s);
// Native (lane perf): start the next window's maxima (decode, wait, AUs in the decoder)
void hw_video_reset_max(HwVideo* v);
// Stops the decode thread but keeps the decoder open: the renderer may still
// hold imports whose frames go back to it. Called when the stream ends.
void hw_video_stop(HwVideo* v);
// Full close: only after the renderer destroyed its imports (xr_app_run returned).
void hw_video_close(HwVideo* v);

// The one stream's hardware decoder, published by the session for the frame
// loop (NULL when software decode is in use).
HwVideo* hw_video_current(void);
void hw_video_set_current(HwVideo* v);
