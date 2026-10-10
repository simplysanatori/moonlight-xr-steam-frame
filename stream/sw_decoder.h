// Moonlight XR Native: FFmpeg software H.264/HEVC decoder (M1 path; M2
// replaces it with V4L2 hardware decode). Frames go to a FrameSlot as
// YUV420P. GPLv3.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "frame_slot.h"

typedef struct SwDecoder SwDecoder;

enum { SWDEC_H264 = 1, SWDEC_HEVC = 2 };

SwDecoder* sw_decoder_open(int codec, FrameSlot* out);
// One Annex-B access unit. Returns 0, or -1 when the decoder needs an IDR.
int sw_decoder_submit(SwDecoder* d, const uint8_t* data, size_t len);
void sw_decoder_close(SwDecoder* d);
