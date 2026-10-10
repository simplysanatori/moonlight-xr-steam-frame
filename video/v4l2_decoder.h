// Moonlight XR Native - V4L2 stateful (M2M) H.264/HEVC decoder for the
// Steam Frame's Iris decoder (/dev/video-dec0), zero-copy dma-buf output.
//
// Copyright (C) 2026 Moonlight XR Native contributors.
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 3 as published by
// the Free Software Foundation. It is distributed WITHOUT ANY WARRANTY; see
// LICENSE.txt for the full license text.
//
// Why direct ioctls and not FFmpeg: the host's FFmpeg 7.0 h264_v4l2m2m /
// hevc_v4l2m2m expose no AV_PIX_FMT_DRM_PRIME and no hw_config; they hand out
// mmap'd NV12 pointers, never a dma-buf fd (see video/REPORT.md).
//
// Lifecycle
//   mlxr_vdec_open()      opens the device, sets the OUTPUT (bitstream) format,
//                         allocates OUTPUT buffers, subscribes to
//                         V4L2_EVENT_SOURCE_CHANGE and streams OUTPUT on.
//                         CAPTURE buffers are NOT allocated yet: the Iris driver
//                         reports a 384x256 placeholder until the first
//                         SOURCE_CHANGE event (SteamOS #2869), and streaming
//                         into too-small buffers makes it DMA past their end.
//   mlxr_vdec_submit()    one Annex-B access unit (all NALs of one picture,
//                         with VPS/SPS/PPS in front of IDRs). Never blocks for
//                         decoding; waits at most a few ms for a free OUTPUT
//                         buffer.
//   mlxr_vdec_receive()   waits up to timeout_ms for the next decoded frame.
//                         Handles the initial SOURCE_CHANGE internally (allocates
//                         CAPTURE, EXPBUFs every buffer once).
//   mlxr_vdec_release()   gives a frame's buffer back to the decoder (QBUF).
//                         Call it only after the GPU finished sampling it
//                         (vk_import does this from a fence/timeline value).
//   mlxr_vdec_reset()     drops queued bitstream (OUTPUT STREAMOFF/STREAMON);
//                         the next submit must be an IDR with parameter sets.
//   mlxr_vdec_close()     full teardown. Close any Vulkan imports first.
//
// Mid-stream resolution change: when the driver signals SOURCE_CHANGE while
// CAPTURE is already allocated, receive() keeps returning the frames decoded
// before the change, then returns MLXR_VDEC_FORMAT_CHANGE once the decoder has
// drained (LAST buffer). The owner must then: wait for the GPU to go idle,
// release every frame it holds, destroy all Vulkan imports
// (mlxr_vkimp_reset), and call mlxr_vdec_apply_format_change(), which frees
// and reallocates CAPTURE at the new size. Frames carry a 'generation' that
// changes with every CAPTURE allocation.
//
// Threading: submit() and reset() belong to one thread (the stream receive
// thread). receive() belongs to one thread (decode thread or frame loop).
// release() may be called from any thread. apply_format_change() and close()
// must not race with anything else.
#ifndef MLXR_V4L2_DECODER_H
#define MLXR_V4L2_DECODER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { MLXR_CODEC_H264 = 0, MLXR_CODEC_HEVC = 1 } mlxr_codec;

typedef enum {
	MLXR_VDEC_OK = 0,
	MLXR_VDEC_AGAIN = 1,          // receive: no frame within the timeout
	MLXR_VDEC_FORMAT_CHANGE = 2,  // receive: drained for a new size, see above
	// Errors (< 0). Every error except E_INVALID is one the stream session
	// should answer with LiRequestIdrFrame(); E_FATAL additionally needs a
	// close + open (the device or firmware is in a bad state).
	MLXR_VDEC_E_DECODE = -1,  // a corrupt frame (V4L2_BUF_FLAG_ERROR) or dropped AU
	MLXR_VDEC_E_BUSY = -2,    // submit: no free OUTPUT buffer, AU dropped
	MLXR_VDEC_E_TOO_BIG = -3, // submit: AU larger than an OUTPUT buffer, dropped
	MLXR_VDEC_E_IO = -4,      // an ioctl failed; logged with its name and errno
	MLXR_VDEC_E_FATAL = -5,   // decoder unusable, reopen it
	MLXR_VDEC_E_INVALID = -6, // API misuse (bad argument, wrong state)
} mlxr_vdec_status;

const char *mlxr_vdec_status_str(int status);

// True for every status the stream session should turn into an IDR request.
static inline bool mlxr_vdec_needs_idr(int status)
{
	return status < 0 && status != MLXR_VDEC_E_INVALID;
}

typedef struct {
	const char *device;             // default "/dev/video-dec0"
	mlxr_codec codec;
	uint32_t width_hint, height_hint; // stream size if known (default 1920x1080)
	uint32_t capture_buffers;       // CAPTURE buffers to request (default 12)
	uint32_t output_buffers;        // OUTPUT buffers to request (default 6)
	// CAPTURE as Q08C (Qualcomm UBWC-compressed NV12, imported with
	// DRM_FORMAT_MOD_QCOM_COMPRESSED) instead of linear NV12. Experimental:
	// see video/REPORT.md for what was verified.
	bool ubwc;
	bool verbose;                   // log format negotiation to stderr
} mlxr_vdec_config;

#define MLXR_VDEC_MAX_PLANES 2

// A decoded picture. Everything here stays valid until mlxr_vdec_release()
// (or close). The fds belong to the decoder: never close them; dup() before
// handing one to vkAllocateMemory (which consumes it).
typedef struct {
	uint32_t index;        // CAPTURE buffer index
	uint32_t generation;   // CAPTURE allocation generation (bumps on resize)
	uint32_t width, height;             // visible picture size
	uint32_t coded_width, coded_height; // allocated size (image extent to import)
	uint32_t drm_format;   // DRM_FORMAT_NV12
	uint64_t drm_modifier; // DRM_FORMAT_MOD_LINEAR or DRM_FORMAT_MOD_QCOM_COMPRESSED (ubwc)
	uint32_t num_planes;   // memory planes (2: Y, CbCr)
	int fd[MLXR_VDEC_MAX_PLANES];         // dma-buf per plane (same fd for both in NV12)
	uint64_t inode[MLXR_VDEC_MAX_PLANES]; // fstat() st_ino of fd, import cache key
	uint32_t offset[MLXR_VDEC_MAX_PLANES];
	uint32_t pitch[MLXR_VDEC_MAX_PLANES];
	uint64_t object_size;  // dma-buf size in bytes
	// Colour description from the driver (V4L2 enums, 0 = default/unknown).
	uint32_t v4l2_colorspace, v4l2_ycbcr_enc, v4l2_quantization;
	int64_t pts;           // as given to submit()
	uint64_t t_submit_ns;  // CLOCK_MONOTONIC when its AU was queued
	uint64_t t_dequeue_ns; // CLOCK_MONOTONIC when the picture was dequeued
	uint32_t sequence;     // driver sequence number
	bool corrupt;          // driver flagged V4L2_BUF_FLAG_ERROR (still displayable)
} mlxr_vframe;

typedef struct {
	uint64_t aus_submitted, aus_dropped;
	uint64_t frames_out, frames_error;
	uint64_t source_changes;   // SOURCE_CHANGE events handled (initial + mid-stream)
	uint64_t capture_allocs;   // CAPTURE REQBUFS allocations
	uint64_t capture_buffers;  // CAPTURE buffers allocated in total (all generations)
	uint64_t expbufs;          // EXPBUF calls (must equal capture_buffers)
	uint64_t requeue_errors;   // release() QBUF failures or double releases
	uint32_t in_flight_max;    // max AUs queued but not yet output
} mlxr_vdec_stats;

typedef struct mlxr_vdec mlxr_vdec;

void mlxr_vdec_config_defaults(mlxr_vdec_config *cfg);
int mlxr_vdec_open(const mlxr_vdec_config *cfg, mlxr_vdec **out);
int mlxr_vdec_submit(mlxr_vdec *d, const void *au, uint32_t size, int64_t pts);
// Same, for an AU that arrives in pieces (moonlight-common-c's decode unit
// buffer list): the pieces are copied straight into the OUTPUT buffer.
int mlxr_vdec_submit_parts(mlxr_vdec *d, const void *const *parts, const uint32_t *sizes, uint32_t count,
			   int64_t pts);
int mlxr_vdec_receive(mlxr_vdec *d, mlxr_vframe *frame, int timeout_ms);
int mlxr_vdec_release(mlxr_vdec *d, const mlxr_vframe *frame);
// Layout of every CAPTURE buffer of the current generation (picture fields
// zero, pts -1), so the importer can import them all up front instead of on
// first sight. 0 buffers before the first SOURCE_CHANGE.
int mlxr_vdec_get_buffers(mlxr_vdec *d, mlxr_vframe *out, uint32_t max, uint32_t *count);
int mlxr_vdec_reset(mlxr_vdec *d);
int mlxr_vdec_apply_format_change(mlxr_vdec *d);
void mlxr_vdec_get_stats(mlxr_vdec *d, mlxr_vdec_stats *stats);
void mlxr_vdec_close(mlxr_vdec *d);

#ifdef __cplusplus
}
#endif
#endif
