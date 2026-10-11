// Moonlight XR Native (lane perf): host frame cadence (cadence.h). GPLv3.
#include "cadence.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

void mlxr_cadence_set_fps(MlxrCadence* c, int fps) {
    c->periodTicks = fps > 0 ? (uint32_t)(90000 / fps) : 0;
}

void mlxr_cadence_add(MlxrCadence* c, uint32_t frameNumber, uint32_t rtp90k, uint64_t recvUs) {
    if (c->have && frameNumber == c->lastFrame + 1) {
        uint32_t d = rtp90k - c->lastRtp; // wraps fine
        if (d > 0 && d < 90000) {         // under a second: a real interval
            double ms = d / 90.0;
            int b = ms < 6.0 ? 0 : ms < 9.7 ? 1 : ms < 12.5 ? 2 : ms < 16.0 ? 3 : ms < 25.0 ? 4 : ms < 50.0 ? 5 : 6;
            c->bucket[b]++;
            c->intervals++;
            c->spanTicks += d;
            // 250 ticks = 2.78 ms (360 Hz), 1000 = 11.1 ms (90 Hz); 15 ticks slack.
            // Native (lane refresh): the stream's own period in place of the 1000 (750 at 120 fps)
            uint32_t per = c->periodTicks ? c->periodTicks : 1000;
            uint32_t m250 = d % 250, mPer = d % per;
            int on250 = m250 < 15 || m250 > 235, onPer = mPer < 15 || mPer > per - 15;
            if (on250 && !onPer) c->grid360++;
            // transit = receive - capture, up to a constant offset: its spread
            // is the jitter the network and the host's send add
            double tr = (double)(recvUs - c->lastRecvUs) / 1000.0 - ms;
            c->transitN++;
            c->transitSum += tr;
            c->transitSq += tr * tr;
        }
    }
    c->have = 1;
    c->lastFrame = frameNumber;
    c->lastRtp = rtp90k;
    c->lastRecvUs = recvUs;
}

void mlxr_cadence_line(MlxrCadence* c, char* buf, int len) {
    double fps = c->spanTicks ? c->intervals * 90000.0 / (double)c->spanTicks : 0.0;
    double n = c->transitN ? (double)c->transitN : 1.0;
    double mean = c->transitSum / n;
    double sd = c->transitN ? sqrt(fmax(0.0, c->transitSq / n - mean * mean)) : 0.0;
    snprintf(buf, (size_t)len,
             "host frames %.1f fps by their timestamps over %llu intervals (<6 ms %llu, 6-9.7 %llu, 9.7-12.5 %llu, "
             "12.5-16 %llu, 16-25 %llu, 25-50 %llu, >=50 %llu), %llu on a 2.78 ms grid off the %.1f ms one, "
             "arrival jitter %.1f ms (sd of the interval change)",
             fps, (unsigned long long)c->intervals, (unsigned long long)c->bucket[0], (unsigned long long)c->bucket[1],
             (unsigned long long)c->bucket[2], (unsigned long long)c->bucket[3], (unsigned long long)c->bucket[4],
             (unsigned long long)c->bucket[5], (unsigned long long)c->bucket[6], (unsigned long long)c->grid360,
             (c->periodTicks ? c->periodTicks : 1000) / 90.0, sd);
    int have = c->have;
    uint32_t lf = c->lastFrame, lr = c->lastRtp;
    uint64_t lu = c->lastRecvUs;
    uint32_t per = c->periodTicks;
    memset(c, 0, sizeof(*c));
    c->periodTicks = per;
    c->have = have;
    c->lastFrame = lf;
    c->lastRtp = lr;
    c->lastRecvUs = lu;
}
