// Moonlight XR Native: FFmpeg software decoder. GPLv3.
#include "sw_decoder.h"

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../xr/mlxr_log.h"

struct SwDecoder {
    AVCodecContext* ctx;
    AVPacket* pkt;
    AVFrame* frame;
    AVFrame* conv;
    struct SwsContext* sws;
    FrameSlot* out;
    uint64_t errors;
};

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec * 1e-6;
}

SwDecoder* sw_decoder_open(int codec, FrameSlot* out) {
    const AVCodec* c = avcodec_find_decoder(codec == SWDEC_HEVC ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!c) {
        MLOG_E("no software decoder for codec %d", codec);
        return NULL;
    }
    SwDecoder* d = calloc(1, sizeof(*d));
    d->out = out;
    d->ctx = avcodec_alloc_context3(c);
    // Low delay as moonlight-qt does: no frame threading, slice threads only.
    d->ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    d->ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    d->ctx->thread_type = FF_THREAD_SLICE;
    d->ctx->thread_count = 4;
    if (avcodec_open2(d->ctx, c, NULL) < 0) {
        MLOG_E("avcodec_open2 failed");
        avcodec_free_context(&d->ctx);
        free(d);
        return NULL;
    }
    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    d->conv = av_frame_alloc();
    MLOG_I("software decoder %s open", c->name);
    return d;
}

int sw_decoder_submit(SwDecoder* d, const uint8_t* data, size_t len) {
    double t0 = nowMs();
    d->pkt->data = (uint8_t*)data;
    d->pkt->size = (int)len;
    int r = avcodec_send_packet(d->ctx, d->pkt);
    if (r < 0 && r != AVERROR(EAGAIN)) {
        if (d->errors++ < 20) MLOG_W("avcodec_send_packet: %d", r);
        return -1;
    }
    while ((r = avcodec_receive_frame(d->ctx, d->frame)) == 0) {
        AVFrame* f = d->frame;
        if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P) {
            d->sws = sws_getCachedContext(d->sws, f->width, f->height, f->format, f->width, f->height,
                                          AV_PIX_FMT_YUV420P, SWS_POINT, NULL, NULL, NULL);
            av_frame_unref(d->conv);
            d->conv->format = AV_PIX_FMT_YUV420P;
            d->conv->width = f->width;
            d->conv->height = f->height;
            if (av_frame_get_buffer(d->conv, 64) < 0) return -1;
            sws_scale(d->sws, (const uint8_t* const*)f->data, f->linesize, 0, f->height, d->conv->data,
                      d->conv->linesize);
            f = d->conv;
        }
        frame_slot_put(d->out, f, nowMs() - t0);
        av_frame_unref(d->frame);
    }
    if (r != AVERROR(EAGAIN) && r != AVERROR_EOF) {
        if (d->errors++ < 20) MLOG_W("avcodec_receive_frame: %d", r);
        return -1;
    }
    return 0;
}

void sw_decoder_close(SwDecoder* d) {
    if (!d) return;
    av_packet_free(&d->pkt);
    av_frame_free(&d->frame);
    av_frame_free(&d->conv);
    sws_freeContext(d->sws);
    avcodec_free_context(&d->ctx);
    free(d);
}
