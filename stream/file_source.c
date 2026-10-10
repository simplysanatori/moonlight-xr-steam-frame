// Moonlight XR Native: local file test source. GPLv3.
#include "file_source.h"

#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../xr/mlxr_log.h"
#include "hw_video.h"
#include "sw_decoder.h"

struct FileSource {
    char* path;
    FrameSlot* out;
    pthread_t thread;
    atomic_int stop;
    int hw;
    HwVideo* hv; // one decoder for every loop of the clip: imports happen once
};

static void sleepUntil(struct timespec* t) { clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, t, NULL); }

static void* run(void* arg) {
    FileSource* s = arg;
    while (!atomic_load(&s->stop)) {
        AVFormatContext* fmt = NULL;
        if (avformat_open_input(&fmt, s->path, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
            MLOG_E("cannot open %s", s->path);
            return NULL;
        }
        int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
        if (vi < 0) {
            MLOG_E("no video stream in %s", s->path);
            avformat_close_input(&fmt);
            return NULL;
        }
        AVStream* st = fmt->streams[vi];
        int hevc = st->codecpar->codec_id == AV_CODEC_ID_HEVC;
        // Annex B, as moonlight-common-c delivers it.
        const AVBitStreamFilter* f = av_bsf_get_by_name(hevc ? "hevc_mp4toannexb" : "h264_mp4toannexb");
        AVBSFContext* bsf = NULL;
        av_bsf_alloc(f, &bsf);
        avcodec_parameters_copy(bsf->par_in, st->codecpar);
        bsf->time_base_in = st->time_base;
        av_bsf_init(bsf);
        SwDecoder* dec = NULL;
        if (s->hw && !s->hv) {
            s->hv = hw_video_open(hevc ? MLXR_CODEC_HEVC : MLXR_CODEC_H264, (uint32_t)st->codecpar->width,
                                  (uint32_t)st->codecpar->height, NULL);
            if (s->hv) hw_video_set_current(s->hv);
            else s->hw = 0;
        }
        if (!s->hw) dec = sw_decoder_open(hevc ? SWDEC_HEVC : SWDEC_H264, s->out);
        double fps = av_q2d(st->avg_frame_rate);
        if (fps <= 1) fps = 60;
        long periodNs = (long)(1e9 / fps);
        MLOG_I("file source %s: %s %dx%d at %.2f fps", s->path, hevc ? "HEVC" : "H.264", st->codecpar->width,
               st->codecpar->height, fps);
        struct timespec next;
        clock_gettime(CLOCK_MONOTONIC, &next);
        AVPacket* pkt = av_packet_alloc();
        while (!atomic_load(&s->stop) && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == vi && av_bsf_send_packet(bsf, pkt) == 0) {
                while (av_bsf_receive_packet(bsf, pkt) == 0) {
                    if (s->hv) {
                        const void* part = pkt->data;
                        uint32_t size = (uint32_t)pkt->size;
                        hw_video_submit(s->hv, &part, &size, 1, pkt->pts);
                    } else {
                        sw_decoder_submit(dec, pkt->data, (size_t)pkt->size);
                    }
                    av_packet_unref(pkt);
                    next.tv_nsec += periodNs;
                    while (next.tv_nsec >= 1000000000L) {
                        next.tv_nsec -= 1000000000L;
                        next.tv_sec++;
                    }
                    sleepUntil(&next);
                }
            }
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
        if (dec) sw_decoder_close(dec);
        av_bsf_free(&bsf);
        avformat_close_input(&fmt);
    }
    return NULL;
}

FileSource* file_source_start(const char* path, FrameSlot* out, int hw) {
    FileSource* s = calloc(1, sizeof(*s));
    s->hw = hw;
    s->path = strdup(path);
    s->out = out;
    if (pthread_create(&s->thread, NULL, run, s) != 0) {
        free(s->path);
        free(s);
        return NULL;
    }
    return s;
}

void file_source_stop(FileSource* s) {
    if (!s) return;
    atomic_store(&s->stop, 1);
    pthread_join(s->thread, NULL);
    if (s->hv) {
        hw_video_set_current(NULL);
        hw_video_close(s->hv);
    }
    free(s->path);
    free(s);
}
