// See xr_hud.h. Native (lane wave3-hud). GPLv3.
#include "xr_hud.h"

#include <stdio.h>
#include <string.h>

static double sub(double a, double b) {
    return a > b ? a - b : 0.0;
}

static uint64_t usub(uint64_t a, uint64_t b) {
    return a > b ? a - b : 0;
}

int mlxr_hud_window(const MlxrHudCum* prev, const MlxrHudCum* cur, double secs, double fps,
                    MlxrHudFigures* out) {
    memset(out, 0, sizeof(*out));
    if (cur == NULL || !cur->valid) {
        return 0;
    }
    static const MlxrHudCum ZERO;
    // Any counter that went down is a stream that began again
    if (prev == NULL || !prev->valid || cur->frames < prev->frames || cur->bytes < prev->bytes
            || cur->lost < prev->lost || cur->decodeN < prev->decodeN || cur->hostN < prev->hostN) {
        prev = &ZERO;
    }
    uint64_t frames = usub(cur->frames, prev->frames);
    uint64_t lost = usub(cur->lost, prev->lost);
    uint64_t hostN = usub(cur->hostN, prev->hostN);
    uint64_t decN = usub(cur->decodeN, prev->decodeN);
    out->rttMs = (double)cur->rttMs;
    out->poor = cur->poor;
    out->fps = fps;
    out->udpDrops = usub(cur->udpDrops, prev->udpDrops);
    out->fecRecovered = (uint32_t)usub(cur->fecRecovered, prev->fecRecovered);
    out->fecFailed = (uint32_t)usub(cur->fecFailed, prev->fecFailed);
    if (secs > 0.0) {
        out->mbps = (double)usub(cur->bytes, prev->bytes) * 8.0 / 1e6 / secs;
    }
    if (frames + lost > 0) {
        out->lossPct = 100.0 * (double)lost / (double)(frames + lost);
    }
    if (frames > 0) {
        out->recvMs = sub(cur->recvMsSum, prev->recvMsSum) / (double)frames;
        out->queueMs = sub(cur->queueMsSum, prev->queueMsSum) / (double)frames;
    }
    if (hostN > 0) {
        out->hostMs = sub(cur->hostMsSum, prev->hostMsSum) / (double)hostN;
    }
    if (decN > 0) {
        out->decodeMs = sub(cur->decodeMsSum, prev->decodeMsSum) / (double)decN;
    }
    out->latencyMs = out->hostMs + out->rttMs * 0.5 + out->recvMs + out->queueMs + out->decodeMs;
    out->valid = frames > 0;
    return 1;
}

// A figure with one decimal under 10 and none above, so the line keeps its length
static void num(char* buf, size_t cap, double v) {
    if (v < 9.95) {
        snprintf(buf, cap, "%.1f", v);
    }
    else {
        snprintf(buf, cap, "%.0f", v);
    }
}

void mlxr_hud_lite(const MlxrHudFigures* f, int slow, char* buf, size_t cap) {
    if (cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (f == NULL || !f->valid) {
        return;
    }
    char speed[16], lat[16], dec[16], loss[16];
    num(speed, sizeof(speed), f->mbps);
    num(lat, sizeof(lat), f->latencyMs);
    num(dec, sizeof(dec), f->decodeMs);
    num(loss, sizeof(loss), f->lossPct);
    snprintf(buf, cap, "%s%s Mb/s | %s ms | dec %s | loss %s%% | %.0f fps", slow ? "! " : "", speed, lat, dec,
             loss, f->fps);
}

int mlxr_metric_set(MlxrCompositorMetrics* m, const char* name, float value) {
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "compositor/cpu_frametime") == 0) {
        m->cpuMs = value;
        m->haveCpu = 1;
    }
    else if (strcmp(name, "compositor/gpu_frametime") == 0) {
        m->gpuMs = value;
        m->haveGpu = 1;
    }
    else if (strcmp(name, "compositor/dropped_frame_count") == 0) {
        m->dropped = value > 0.0f ? (uint32_t)value : 0;
        m->haveDropped = 1;
    }
    else if (strcmp(name, "app/cpu_frametime") == 0) {
        m->appCpuMs = value;
        m->haveApp = 1;
    }
    else if (strcmp(name, "app/gpu_frametime") == 0) {
        m->appGpuMs = value;
        m->haveApp = 1;
    }
    else {
        return 0;
    }
    m->valid = 1;
    return 1;
}

void mlxr_compositor_line(const MlxrCompositorMetrics* m, char* buf, size_t cap) {
    if (cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (m == NULL || !m->valid || !(m->haveCpu || m->haveGpu || m->haveDropped)) {
        return;
    }
    size_t n = (size_t)snprintf(buf, cap, "compositor");
    if (m->haveCpu && n < cap) {
        n += (size_t)snprintf(buf + n, cap - n, " cpu %.1f ms", (double)m->cpuMs);
    }
    if (m->haveGpu && n < cap) {
        n += (size_t)snprintf(buf + n, cap - n, " gpu %.1f ms", (double)m->gpuMs);
    }
    if (m->haveDropped && n < cap) {
        snprintf(buf + n, cap - n, "%s%u dropped", m->haveCpu || m->haveGpu ? ", " : " ", m->dropped);
    }
}

void mlxr_hud_panel(const MlxrHudFigures* f, int slow, const MlxrCompositorMetrics* m, double displayHz,
                    char* buf, size_t cap) {
    if (cap == 0) {
        return;
    }
    buf[0] = '\0';
    if (f == NULL || !f->valid) {
        return;
    }
    char lat[16], host[16], net[16], dec[16], loss[16], rate[16];
    num(lat, sizeof(lat), f->latencyMs);
    num(host, sizeof(host), f->hostMs);
    num(net, sizeof(net), f->rttMs * 0.5 + f->recvMs + f->queueMs);
    num(dec, sizeof(dec), f->decodeMs);
    num(loss, sizeof(loss), f->lossPct);
    num(rate, sizeof(rate), f->mbps);
    size_t n = (size_t)snprintf(buf, cap, "Latency %s ms%s\nHost %s, net %s, decode %s ms\nRTT %.0f ms, loss %s%%",
                                lat, slow ? "  - slow connection" : "", host, net, dec, f->rttMs, loss);
    if ((f->fecRecovered > 0 || f->fecFailed > 0) && n < cap) {
        n += (size_t)snprintf(buf + n, cap - n, ", FEC %u/%u", f->fecRecovered, f->fecFailed);
    }
    if (f->udpDrops > 0 && n < cap) {
        n += (size_t)snprintf(buf + n, cap - n, ", UDP drops %llu", (unsigned long long)f->udpDrops);
    }
    if (n < cap) {
        n += (size_t)snprintf(buf + n, cap - n, "\n%s Mb/s, %.0f fps", rate, f->fps);
    }
    if (n < cap && displayHz > 0.0) {
        n += (size_t)snprintf(buf + n, cap - n, " on %.0f Hz", displayHz);
    }
    char comp[96];
    mlxr_compositor_line(m, comp, sizeof(comp));
    if (comp[0] && n < cap) {
        snprintf(buf + n, cap - n, "\n%s", comp);
    }
}

void mlxr_slow_init(MlxrSlowConn* s) {
    s->on = 0;
    s->badSinceMs = -1;
    s->goodSinceMs = -1;
}

int mlxr_slow_update(MlxrSlowConn* s, int64_t nowMs, const MlxrHudFigures* f) {
    if (f == NULL) {
        mlxr_slow_init(s);
        return 0;
    }
    // A window with no frames says nothing about the loss, but the status common-c reports still counts
    int bad = f->poor || (f->valid && f->lossPct > SLOW_LOSS_PCT);
    if (bad) {
        s->goodSinceMs = -1;
        if (s->badSinceMs < 0) {
            s->badSinceMs = nowMs;
        }
        if (!s->on && nowMs - s->badSinceMs >= SLOW_ON_MS) {
            s->on = 1;
        }
    }
    else {
        s->badSinceMs = -1;
        if (s->goodSinceMs < 0) {
            s->goodSinceMs = nowMs;
        }
        if (s->on && nowMs - s->goodSinceMs >= SLOW_OFF_MS) {
            s->on = 0;
        }
    }
    return s->on;
}
