// Moonlight XR Native: hardware video path (M2). moonlight-common-c's decode
// units go to the Iris V4L2 decoder; a decode thread receives frames into a
// latest-frame mailbox the XR frame loop takes from. Frames the loop never
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
} HwVideoStats;

// requestIdr is called (from the decode thread) on decoder errors.
HwVideo* hw_video_open(mlxr_codec codec, uint32_t width, uint32_t height, void (*requestIdr)(void));
// Stream receive thread: one access unit in pieces. Returns 0, or -1 when the
// session should answer DR_NEED_IDR.
int hw_video_submit(HwVideo* v, const void* const* parts, const uint32_t* sizes, uint32_t n, int64_t pts);
// Frame loop: newest decoded frame, if one arrived since the last take.
bool hw_video_take(HwVideo* v, mlxr_vframe* out);
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
