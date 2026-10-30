// The in-headset HUD's figures, words, slow connection rule and the perf CSV rows
// (xr_hud.c, xr_perflog.c). Native (lane wave3-hud).
#include "check.h"
#include "xr_hud.h"
#include "xr_perflog.h"

#include <string.h>

// Running totals after `frames` frames of 20 kB, 4 ms host encode, 2 ms receive, 1 ms queue, 3 ms decode
static MlxrHudCum totals(uint64_t frames, uint64_t lost) {
    MlxrHudCum c;
    memset(&c, 0, sizeof(c));
    c.valid = 1;
    c.frames = frames;
    c.lost = lost;
    c.bytes = frames * 20000;
    c.hostN = frames;
    c.hostMsSum = 4.0 * (double)frames;
    c.recvMsSum = 2.0 * (double)frames;
    c.queueMsSum = 1.0 * (double)frames;
    c.decodeN = frames;
    c.decodeMsSum = 3.0 * (double)frames;
    c.rttMs = 6;
    return c;
}

static void testWindow(void) {
    MlxrHudCum a = totals(0, 0), b = totals(90, 1);
    MlxrHudFigures f;
    CHECK(mlxr_hud_window(&a, &b, 1.0, 89.0, &f) == 1);
    CHECK(f.valid);
    // 90 frames of 20 kB in a second
    CHECK_NEAR(f.mbps, 14.4, 1e-9);
    CHECK_NEAR(f.hostMs, 4.0, 1e-9);
    CHECK_NEAR(f.recvMs, 2.0, 1e-9);
    CHECK_NEAR(f.queueMs, 1.0, 1e-9);
    CHECK_NEAR(f.decodeMs, 3.0, 1e-9);
    // host + rtt/2 + receive + queue + decode
    CHECK_NEAR(f.latencyMs, 4.0 + 3.0 + 2.0 + 1.0 + 3.0, 1e-9);
    CHECK_NEAR(f.lossPct, 100.0 / 91.0, 1e-9);
    CHECK_NEAR(f.fps, 89.0, 1e-9);

    // the next window is the difference, not the total
    MlxrHudCum c = totals(270, 1);
    c.bytes = b.bytes + 180 * 40000;
    CHECK(mlxr_hud_window(&b, &c, 2.0, 90.0, &f) == 1);
    CHECK_NEAR(f.mbps, 180.0 * 40000.0 * 8.0 / 1e6 / 2.0, 1e-9);
    CHECK_NEAR(f.lossPct, 0.0, 1e-9);

    // a stream that began again: counters below the last reading, so from zero
    MlxrHudCum fresh = totals(30, 0);
    CHECK(mlxr_hud_window(&c, &fresh, 1.0, 30.0, &f) == 1);
    CHECK(f.valid);
    CHECK_NEAR(f.mbps, 30.0 * 20000.0 * 8.0 / 1e6, 1e-9);

    // no frames in the window: valid reading, nothing to say
    CHECK(mlxr_hud_window(&c, &c, 1.0, 0.0, &f) == 1);
    CHECK(!f.valid);

    // not streaming
    MlxrHudCum none;
    memset(&none, 0, sizeof(none));
    CHECK(mlxr_hud_window(&c, &none, 1.0, 0.0, &f) == 0);
    CHECK(!f.valid);
    CHECK(mlxr_hud_window(NULL, &b, 1.0, 60.0, &f) == 1);
    CHECK(f.valid);

    // the extras are per window too
    MlxrHudCum d = totals(90, 1);
    d.fecRecovered = 7;
    d.fecFailed = 2;
    d.udpDrops = 40;
    MlxrHudCum e = d;
    e.frames += 60;
    e.bytes += 60 * 20000;
    e.hostN += 60;
    e.decodeN += 60;
    e.fecRecovered += 3;
    e.udpDrops += 5;
    CHECK(mlxr_hud_window(&d, &e, 1.0, 60.0, &f) == 1);
    CHECK(f.fecRecovered == 3 && f.fecFailed == 0 && f.udpDrops == 5);
    // the started-with bitrate is carried, not differenced
    CHECK(f.setKbps == 0);
    e.setKbps = 98000;
    CHECK(mlxr_hud_window(&d, &e, 1.0, 60.0, &f) == 1 && f.setKbps == 98000);
}

static MlxrHudFigures sample(void) {
    MlxrHudFigures f;
    memset(&f, 0, sizeof(f));
    f.valid = 1;
    f.mbps = 54.2;
    f.hostMs = 4.2;
    f.recvMs = 1.1;
    f.queueMs = 0.4;
    f.decodeMs = 3.1;
    f.rttMs = 4.0;
    f.latencyMs = f.hostMs + f.rttMs * 0.5 + f.recvMs + f.queueMs + f.decodeMs;
    f.lossPct = 0.2;
    f.fps = 90.0;
    return f;
}

static void testWords(void) {
    char buf[256];
    MlxrHudFigures f = sample();
    mlxr_hud_lite(&f, 0, buf, sizeof(buf));
    CHECK(strcmp(buf, "54 Mb/s | 11 ms | dec 3.1 | loss 0.2% | 90 fps") == 0);
    mlxr_hud_lite(&f, 1, buf, sizeof(buf));
    CHECK(strncmp(buf, "! 54 Mb/s", 9) == 0);
    CHECK(strchr(buf, '\n') == NULL);

    // under 10 keeps a decimal
    f.mbps = 8.34;
    f.latencyMs = 9.4;
    mlxr_hud_lite(&f, 0, buf, sizeof(buf));
    CHECK(strncmp(buf, "8.3 Mb/s | 9.4 ms", 17) == 0);

    // no figures, no line; a small buffer is cut, not overrun
    MlxrHudFigures none;
    memset(&none, 0, sizeof(none));
    mlxr_hud_lite(&none, 0, buf, sizeof(buf));
    CHECK(buf[0] == '\0');
    mlxr_hud_lite(NULL, 0, buf, sizeof(buf));
    CHECK(buf[0] == '\0');
    f = sample();
    char tiny[12];
    mlxr_hud_lite(&f, 0, tiny, sizeof(tiny));
    CHECK(strlen(tiny) < sizeof(tiny));

    // the HUD: latency with its parts, the network, the rate
    mlxr_hud_panel(&f, 0, NULL, 90.0, buf, sizeof(buf));
    CHECK(strstr(buf, "Latency 11 ms\n") == buf);
    CHECK(strstr(buf, "\nHost 4.2, net 3.5, decode 3.1 ms\n") != NULL);
    CHECK(strstr(buf, "RTT 4 ms, loss 0.2%") != NULL);
    CHECK(strstr(buf, "\n54 Mb/s, 90 fps on 90 Hz") != NULL);
    // Native (lane wave4b-abr): the bitrate the stream was started with shows beside the arriving rate
    f.setKbps = 98000;
    mlxr_hud_panel(&f, 0, NULL, 90.0, buf, sizeof(buf));
    CHECK(strstr(buf, "\n54 of 98 Mb/s, 90 fps on 90 Hz") != NULL);
    f.setKbps = 87500;
    mlxr_hud_panel(&f, 0, NULL, 90.0, buf, sizeof(buf));
    CHECK(strstr(buf, "\n54 of 88 Mb/s, 90 fps") != NULL || strstr(buf, "\n54 of 87 Mb/s, 90 fps") != NULL);
    f.setKbps = 6500;
    mlxr_hud_panel(&f, 0, NULL, 90.0, buf, sizeof(buf));
    CHECK(strstr(buf, "\n54 of 6.5 Mb/s, 90 fps") != NULL);
    f.setKbps = 0;
    CHECK(strstr(buf, "FEC") == NULL && strstr(buf, "UDP") == NULL && strstr(buf, "compositor") == NULL);
    CHECK(strstr(buf, "slow") == NULL);
    mlxr_hud_panel(&f, 1, NULL, 0.0, buf, sizeof(buf));
    CHECK(strstr(buf, "Latency 11 ms  - slow connection\n") == buf);
    CHECK(strstr(buf, " on ") == NULL);

    f.fecRecovered = 12;
    f.fecFailed = 1;
    f.udpDrops = 30;
    MlxrCompositorMetrics m;
    memset(&m, 0, sizeof(m));
    CHECK(mlxr_metric_set(&m, "compositor/cpu_frametime", 1.25f));
    CHECK(mlxr_metric_set(&m, "compositor/gpu_frametime", 3.5f));
    CHECK(mlxr_metric_set(&m, "compositor/dropped_frame_count", 4.0f));
    CHECK(!mlxr_metric_set(&m, "device/battery_temperature", 30.0f));
    mlxr_hud_panel(&f, 0, &m, 90.0, buf, sizeof(buf));
    CHECK(strstr(buf, "RTT 4 ms, loss 0.2%, FEC 12/1, UDP drops 30\n") != NULL);
    CHECK(strstr(buf, "\ncompositor cpu 1.2 ms gpu 3.5 ms, 4 dropped") != NULL
          || strstr(buf, "\ncompositor cpu 1.3 ms gpu 3.5 ms, 4 dropped") != NULL);

    // the compositor line alone
    char line[96];
    mlxr_compositor_line(&m, line, sizeof(line));
    CHECK(strncmp(line, "compositor cpu ", 15) == 0);
    memset(&m, 0, sizeof(m));
    mlxr_compositor_line(&m, line, sizeof(line));
    CHECK(line[0] == '\0');
    mlxr_metric_set(&m, "app/cpu_frametime", 2.0f);
    mlxr_compositor_line(&m, line, sizeof(line));
    CHECK(line[0] == '\0');   // our own times are not the compositor's
    mlxr_metric_set(&m, "compositor/dropped_frame_count", 0.0f);
    mlxr_compositor_line(&m, line, sizeof(line));
    CHECK(strcmp(line, "compositor 0 dropped") == 0);
}

static MlxrHudFigures with(double loss, int poor) {
    MlxrHudFigures f = sample();
    f.lossPct = loss;
    f.poor = poor;
    return f;
}

static void testSlow(void) {
    MlxrSlowConn s;
    mlxr_slow_init(&s);
    MlxrHudFigures fine = with(0.1, 0), lossy = with(3.0, 0), poor = with(0.0, 1);
    int64_t t = 100000;
    // good traffic never raises it
    for (int i = 0; i < 20; i++) {
        CHECK(!mlxr_slow_update(&s, t, &fine));
        t += 1000;
    }
    // loss over the threshold must last 5 s without a break
    CHECK(!mlxr_slow_update(&s, t, &lossy));
    CHECK(!mlxr_slow_update(&s, t + 2000, &lossy));
    CHECK(!mlxr_slow_update(&s, t + 4999, &lossy));
    CHECK(mlxr_slow_update(&s, t + 5000, &lossy));
    t += 5000;
    // a good second does not lower it, 5 s of them do
    CHECK(mlxr_slow_update(&s, t + 1000, &fine));
    CHECK(mlxr_slow_update(&s, t + 2000, &lossy));   // and the count of good seconds starts over
    CHECK(mlxr_slow_update(&s, t + 3000, &fine));
    CHECK(mlxr_slow_update(&s, t + 7999, &fine));
    CHECK(!mlxr_slow_update(&s, t + 8000, &fine));

    // a break in the bad stretch starts it over
    mlxr_slow_init(&s);
    CHECK(!mlxr_slow_update(&s, 0, &lossy));
    CHECK(!mlxr_slow_update(&s, 3000, &lossy));
    CHECK(!mlxr_slow_update(&s, 4000, &fine));
    CHECK(!mlxr_slow_update(&s, 5000, &lossy));
    CHECK(!mlxr_slow_update(&s, 9999, &lossy));
    CHECK(mlxr_slow_update(&s, 10000, &lossy));

    // common-c's own word counts, with no loss at all
    mlxr_slow_init(&s);
    CHECK(!mlxr_slow_update(&s, 0, &poor));
    CHECK(mlxr_slow_update(&s, 5000, &poor));
    // exactly at the threshold is not over it
    MlxrHudFigures edge = with(SLOW_LOSS_PCT, 0);
    mlxr_slow_init(&s);
    CHECK(!mlxr_slow_update(&s, 0, &edge));
    CHECK(!mlxr_slow_update(&s, 9000, &edge));
    // a window with no frames in it is not loss, but poor still counts
    MlxrHudFigures quiet = with(100.0, 0);
    quiet.valid = 0;
    mlxr_slow_init(&s);
    CHECK(!mlxr_slow_update(&s, 0, &quiet));
    CHECK(!mlxr_slow_update(&s, 9000, &quiet));
    quiet.poor = 1;
    CHECK(!mlxr_slow_update(&s, 10000, &quiet));
    CHECK(mlxr_slow_update(&s, 15000, &quiet));
    // the stream ending takes it down at once
    CHECK(!mlxr_slow_update(&s, 15001, NULL));
    CHECK(!s.on);
}

static void testPerf(void) {
    char head[400], row[400];
    size_t hn = mlxr_perf_header(head, sizeof(head));
    CHECK(hn > 0 && head[hn - 1] == '\n');
    MlxrPerfRow r;
    memset(&r, 0, sizeof(r));
    r.tSec = 15.2;
    r.width = 2560;
    r.height = 1440;
    r.streamFps = 90;
    r.bitrateKbps = 150000;
    r.codec = 2;
    r.displayHz = 90.0;
    r.stereo = 1;
    r.environment = 3;
    r.sharpen = 1;
    r.supersample = 2;
    r.fig = sample();
    size_t rn = mlxr_perf_row(&r, row, sizeof(row));
    CHECK(rn > 0 && row[rn - 1] == '\n');
    // one cell for each column
    int hc = 1, rc = 1;
    for (const char* p = head; *p; p++) hc += *p == ',';
    for (const char* p = row; *p; p++) rc += *p == ',';
    CHECK(hc == rc);
    CHECK(strstr(row, "15,2560,1440,90,150000,hevc,90,1,3,1,2,0,54.20,") == row);
    // the runtime gave nothing: empty cells at the end
    CHECK(strstr(row, ",,,,,\n") != NULL);
    // and when it did
    mlxr_metric_set(&r.comp, "compositor/cpu_frametime", 1.5f);
    mlxr_metric_set(&r.comp, "compositor/gpu_frametime", 3.25f);
    mlxr_metric_set(&r.comp, "compositor/dropped_frame_count", 2.0f);
    rn = mlxr_perf_row(&r, row, sizeof(row));
    CHECK(strstr(row, ",1.50,3.25,2,,\n") != NULL);
    // a codec out of range is auto; too small a buffer says so
    r.codec = 9;
    rn = mlxr_perf_row(&r, row, sizeof(row));
    CHECK(strstr(row, ",auto,") != NULL);
    CHECK(mlxr_perf_row(&r, row, 20) == 0);
    CHECK(mlxr_perf_header(head, 10) == 0);

    char path[128];
    CHECK(mlxr_perf_path("/logs", 2026, 10, 29, 9, 5, 7, path, sizeof(path)));
    CHECK(strcmp(path, "/logs/perf-20261029-090507.csv") == 0);
    CHECK(!mlxr_perf_path("/logs", 2026, 10, 29, 9, 5, 7, path, 12));
}

int main(void) {
    testWindow();
    testWords();
    testSlow();
    testPerf();
    return checksDone("hud");
}
