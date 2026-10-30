// See xr_perflog.h. Native (lane wave3-hud). GPLv3.
#include "xr_perflog.h"

#include <stdio.h>

static const char* const CODEC_WORDS[] = { "auto", "av1", "hevc", "h264" };

static const char HEADER[] =
    "t_s,width,height,stream_fps,bitrate_kbps,codec,display_hz,stereo,environment,sharpen,supersample,"
    "slow,mbps,latency_ms,host_ms,recv_ms,queue_ms,decode_ms,rtt_ms,loss_pct,video_fps,fec_recovered,"
    "fec_failed,udp_drops,comp_cpu_ms,comp_gpu_ms,comp_dropped,app_cpu_ms,app_gpu_ms";

size_t mlxr_perf_header(char* buf, size_t cap) {
    int n = snprintf(buf, cap, "%s\n", HEADER);
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

size_t mlxr_perf_row(const MlxrPerfRow* r, char* buf, size_t cap) {
    const MlxrHudFigures* f = &r->fig;
    const MlxrCompositorMetrics* m = &r->comp;
    int codec = r->codec >= 0 && r->codec < 4 ? r->codec : 0;
    // A figure the runtime did not give is an empty cell, not a zero
    char cpu[24] = "", gpu[24] = "", dropped[24] = "", appCpu[24] = "", appGpu[24] = "";
    if (m->valid && m->haveCpu) {
        snprintf(cpu, sizeof(cpu), "%.2f", (double)m->cpuMs);
    }
    if (m->valid && m->haveGpu) {
        snprintf(gpu, sizeof(gpu), "%.2f", (double)m->gpuMs);
    }
    if (m->valid && m->haveDropped) {
        snprintf(dropped, sizeof(dropped), "%u", m->dropped);
    }
    if (m->valid && m->haveApp) {
        snprintf(appCpu, sizeof(appCpu), "%.2f", (double)m->appCpuMs);
        snprintf(appGpu, sizeof(appGpu), "%.2f", (double)m->appGpuMs);
    }
    int n = snprintf(buf, cap, "%.0f,%d,%d,%d,%d,%s,%.0f,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%.2f,"
                     "%.1f,%u,%u,%llu,%s,%s,%s,%s,%s\n",
                     r->tSec, r->width, r->height, r->streamFps, r->bitrateKbps, CODEC_WORDS[codec], r->displayHz,
                     r->stereo != 0, r->environment, r->sharpen, r->supersample, r->slow != 0, f->mbps,
                     f->latencyMs, f->hostMs, f->recvMs, f->queueMs, f->decodeMs, f->rttMs, f->lossPct, f->fps,
                     f->fecRecovered, f->fecFailed, (unsigned long long)f->udpDrops, cpu, gpu, dropped, appCpu,
                     appGpu);
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

int mlxr_perf_path(const char* dir, int year, int mon, int day, int hour, int min, int sec,
                   char* out, size_t cap) {
    int n = snprintf(out, cap, "%s/perf-%04d%02d%02d-%02d%02d%02d.csv", dir, year, mon, day, hour, min, sec);
    return n > 0 && (size_t)n < cap;
}
