// End to end through the real PulseAudio API (PipeWire on SteamOS), into a
// temporary null sink that this test loads and removes again, recording the
// sink's monitor: tone level and frequency, latency numbers, underruns, queue
// cap under a burst, mute, and the UI click. Nothing is played on the speakers;
// the default sink is checked to be unchanged. Skips (ctest code 77) when
// there is no sound server.
#include "check.h"
#include "click.h"
#include "opus_layouts.h"
#include "opus_out.h"
#include "pulse_out.h"
#include "tutil.h"

#include <opus_multistream.h>
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RATE 48000
// SteamOS routes ordinary output streams through Valve's spatializer and on to the
// speakers; a node name with "virtual" and "sink" in it is exempt, so the test's
// streams go to the null sink alone and nothing is heard.
#define STREAM_NAME "mlxr-test virtual sink"

static pa_threaded_mainloop* gLoop;
static pa_context* gCtx;
static uint32_t gModule = PA_INVALID_INDEX;
static char gSink[64];
static char gMonitor[80];

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void sleepMs(double ms) {
    struct timespec ts = {(time_t) (ms / 1000), (long) (fmod(ms, 1000.0) * 1e6)};
    nanosleep(&ts, NULL);
}

static void waitOp(pa_operation* op) {
    while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
        pa_threaded_mainloop_unlock(gLoop);
        usleep(1000);
        pa_threaded_mainloop_lock(gLoop);
    }
    pa_operation_unref(op);
}

static void onServerInfo(pa_context* c, const pa_server_info* i, void* ud) {
    snprintf((char*) ud, 256, "%s", i->default_sink_name ? i->default_sink_name : "");
}

static void defaultSink(char* out) {
    pa_threaded_mainloop_lock(gLoop);
    waitOp(pa_context_get_server_info(gCtx, onServerInfo, out));
    pa_threaded_mainloop_unlock(gLoop);
}

static void onModuleIndex(pa_context* c, uint32_t idx, void* ud) {
    *(uint32_t*) ud = idx;
}

static void onModuleList(pa_context* c, const pa_module_info* i, int eol, void* ud) {
    if (eol > 0 || i == NULL) {
        return;
    }
    if (!strcmp(i->name, "module-null-sink") && i->argument && strstr(i->argument, "mlxr_test_")) {
        // a leftover from a crashed run
        pa_operation* op = pa_context_unload_module(c, i->index, NULL, NULL);
        if (op) pa_operation_unref(op);
    }
}

static void unloadSink(void) {
    if (gModule != PA_INVALID_INDEX && gLoop != NULL) {
        pa_threaded_mainloop_lock(gLoop);
        pa_operation* op = pa_context_unload_module(gCtx, gModule, NULL, NULL);
        if (op) waitOp(op);
        pa_threaded_mainloop_unlock(gLoop);
        gModule = PA_INVALID_INDEX;
    }
}

static int loadSink(void) {
    snprintf(gSink, sizeof gSink, "mlxr_test_%d", (int) getpid());
    snprintf(gMonitor, sizeof gMonitor, "%s.monitor", gSink);
    char args[300];
    snprintf(args, sizeof args,
             "sink_name=%s sink_properties=device.description=mlxr_test rate=48000 channels=2 "
             "channel_map=front-left,front-right", gSink);
    pa_threaded_mainloop_lock(gLoop);
    waitOp(pa_context_get_module_info_list(gCtx, onModuleList, NULL));
    uint32_t idx = PA_INVALID_INDEX - 1;
    pa_operation* op = pa_context_load_module(gCtx, "module-null-sink", args, onModuleIndex, &idx);
    if (op) waitOp(op);
    pa_threaded_mainloop_unlock(gLoop);
    gModule = idx == PA_INVALID_INDEX - 1 ? PA_INVALID_INDEX : idx;
    sleepMs(300);  // let the sink and its monitor appear
    return gModule == PA_INVALID_INDEX ? -1 : 0;
}

// ---- recorder ---------------------------------------------------------------

#define REC_FRAMES (RATE * 20)
#define CHUNK 240
static float* gRec;
static double* gChunkTime;
static _Atomic long gRecFrames;
static volatile int gRecStop;
static pthread_t gRecThread;

static void* recorder(void* arg) {
    pa_sample_spec ss = {.format = PA_SAMPLE_FLOAT32LE, .rate = RATE, .channels = 2};
    pa_buffer_attr attr = {.maxlength = (uint32_t) -1, .tlength = (uint32_t) -1,
                           .prebuf = (uint32_t) -1, .minreq = (uint32_t) -1,
                           .fragsize = CHUNK * 8};
    int err;
    pa_simple* s = pa_simple_new(NULL, "mlxr-test-rec", PA_STREAM_RECORD, gMonitor, "rec", &ss,
                                 NULL, &attr, &err);
    if (s == NULL) {
        fprintf(stderr, "record: %s\n", pa_strerror(err));
        atomic_store(&gRecFrames, -1);
        return NULL;
    }
    long n = 0;
    while (!gRecStop && n + CHUNK <= REC_FRAMES) {
        if (pa_simple_read(s, gRec + (size_t) n * 2, CHUNK * 8, &err) < 0) {
            break;
        }
        gChunkTime[n / CHUNK] = nowMs();
        n += CHUNK;
        atomic_store(&gRecFrames, n);
    }
    pa_simple_free(s);
    return NULL;
}

// Index of the first frame at or after `from` whose level exceeds `thr`
static long firstAbove(long from, float thr) {
    long n = atomic_load(&gRecFrames);
    for (long i = from; i < n; i++) {
        if (fabsf(gRec[2 * i]) > thr || fabsf(gRec[2 * i + 1]) > thr) {
            return i;
        }
    }
    return -1;
}

// ---- helpers to drive the renderer -----------------------------------------

typedef struct {
    AUDIO_RENDERER_CALLBACKS ar;
    OPUS_MULTISTREAM_CONFIGURATION oc;
    OpusMSEncoder* enc;
    int channels;
    long fed;
    struct timespec t;
} Stream;

static void streamOpen(Stream* s, int channels, int surround, int tlength) {
    memset(s, 0, sizeof *s);
    s->channels = channels;
    mlxr_layout_fill(channels, &s->oc);
    int err;
    s->enc = opus_multistream_encoder_create(RATE, channels, s->oc.streams, s->oc.coupledStreams,
                                             s->oc.mapping, OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                             &err);
    opus_multistream_encoder_ctl(s->enc, OPUS_SET_BITRATE(96000 * s->oc.streams));
    MlxrOpusOutConfig cfg;
    mlxr_opus_out_config_default(&cfg);
    cfg.sink = gSink;
    cfg.streamName = STREAM_NAME;
    cfg.hrtfDir = surround ? HRTF_DIR : NULL;
    cfg.surround = surround != 0;
    cfg.tlengthMs = tlength;
    mlxr_opus_out_callbacks(&s->ar);
    int rc = s->ar.init(channels == 2 ? AUDIO_CONFIGURATION_STEREO : AUDIO_CONFIGURATION_71_SURROUND,
                        &s->oc, &cfg, 0);
    CHECK(rc == 0);
    s->ar.start();
    clock_gettime(CLOCK_MONOTONIC, &s->t);
}

// Real-time paced: `ms` of a tone on every channel (or on `only` if >= 0)
static void streamPlay(Stream* s, double ms, double hz, float amp, int only) {
    int packets = (int) (ms / 5.0);
    float in[240 * 8];
    unsigned char pkt[4000];
    for (int p = 0; p < packets; p++) {
        for (int f = 0; f < 240; f++) {
            float v = amp * sinf(2.0f * (float) M_PI * (float) hz * (float) (s->fed + f) / RATE);
            for (int c = 0; c < s->channels; c++) {
                in[f * s->channels + c] = (only < 0 || c == only) ? v : 0.0f;
            }
        }
        s->fed += 240;
        int len = opus_multistream_encode_float(s->enc, in, 240, pkt, sizeof pkt);
        if (len > 0) {
            s->ar.decodeAndPlaySample((char*) pkt, len);
        }
        s->t.tv_nsec += 5000000L;
        if (s->t.tv_nsec >= 1000000000L) {
            s->t.tv_nsec -= 1000000000L;
            s->t.tv_sec++;
        }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &s->t, NULL);
    }
}

static void streamClose(Stream* s) {
    s->ar.stop();
    s->ar.cleanup();
    opus_multistream_encoder_destroy(s->enc);
}

static void testTone(int channels, int surround) {
    Stream s;
    long startFrames = atomic_load(&gRecFrames);
    streamOpen(&s, channels, surround, 20);
    double tSend = nowMs();
    streamPlay(&s, 1500, 440.0, 0.5f, channels == 2 ? -1 : 2 /* centre */);
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    sleepMs(100);
    long first = firstAbove(startFrames, 0.02f);
    CHECK(first >= 0);
    if (first < 0) {
        streamClose(&s);
        return;
    }
    double tChunk = gChunkTime[first / CHUNK];
    double endToEnd = tChunk - tSend;  // includes the monitor capture's own chunk
    // Steady window 0.8 s .. 1.3 s after the onset
    long a = first + RATE * 8 / 10, n = RATE / 2;
    double l = toneRms(gRec + 2 * a, (int) n, 2, 440.0, RATE);
    double r = toneRms(gRec + 2 * a + 1, (int) n, 2, 440.0, RATE);
    // stereo: 0.5 amplitude -> 0.354 rms. Surround centre: 0.5 * 0.707 * ear gain (about 0 dB)
    double want = channels == 2 ? 0.5 / sqrt(2.0) : 0.5 * 0.70710678 / sqrt(2.0);
    printf("  %s: tone %.2f / %.2f dB re expected, send->monitor %.1f ms; underruns %u overruns %u"
           " dropped %llu fr; pulse latency %.1f ms (granted tlength %.1f ms) queue %.1f ms; "
           "process %.0f us avg %.0f max\n",
           channels == 2 ? "stereo" : surround ? "7.1 virtual surround" : "7.1 downmix",
           db(l / want), db(r / want), endToEnd, st.underruns, st.overruns,
           (unsigned long long) st.droppedFrames, st.latencyMs, st.tlengthMs, st.queueMs,
           st.processUs, st.processMaxUs);
    CHECK(fabs(db(l / want)) < (channels == 2 ? 1.0 : 3.0));
    CHECK(fabs(db(r / want)) < (channels == 2 ? 1.0 : 3.0));
    if (channels == 2) {
        // Nothing but the tone: the 440 Hz bin holds nearly all the energy
        double total = rmsOf(gRec + 2 * a, (int) n, 2);
        CHECK(l / total > 0.98);
    }
    CHECK(st.underruns <= 2);
    CHECK(st.droppedFrames == 0);
    CHECK(st.latencyMs > 0.0f);
    CHECK(st.latencyMs <= 40.0f);
    CHECK(endToEnd < 120.0);
    streamClose(&s);
    sleepMs(200);
}

static void testMute(void) {
    Stream s;
    long base = atomic_load(&gRecFrames);
    streamOpen(&s, 2, 0, 20);
    streamPlay(&s, 600, 440.0, 0.5f, -1);
    mlxr_opus_out_set_muted(true);
    double tMute = nowMs();
    streamPlay(&s, 600, 440.0, 0.5f, -1);
    MlxrOpusOutStats st;
    mlxr_opus_out_stats(&st);
    CHECK(st.mutedPackets > 100);
    mlxr_opus_out_set_muted(false);
    streamPlay(&s, 600, 440.0, 0.5f, -1);
    sleepMs(80);
    // Locate the silent stretch in the recording
    long first = firstAbove(base, 0.02f);
    CHECK(first >= 0);
    long end = atomic_load(&gRecFrames);
    long silentRun = 0, longest = 0, runStart = 0, bestStart = 0;
    for (long i = first; i < end; i++) {
        int quiet = fabsf(gRec[2 * i]) < 0.001f && fabsf(gRec[2 * i + 1]) < 0.001f;
        if (quiet) {
            if (silentRun == 0) runStart = i;
            silentRun++;
            if (silentRun > longest) { longest = silentRun; bestStart = runStart; }
        } else {
            silentRun = 0;
        }
    }
    (void) tMute;
    double silentMs = 1000.0 * (double) longest / RATE;
    printf("  mute: longest silent stretch %.0f ms (600 ms muted + restart)\n", silentMs);
    // The gap is about the muted span (600 ms) plus the stream restart (prebuf), and the
    // tone is back after it
    CHECK(silentMs > 500.0 && silentMs < 800.0);
    long back = bestStart + longest;
    CHECK(back < end - RATE / 10);
    CHECK(fabsf(gRec[2 * (back + RATE / 20)]) > 0.0f);
    double l = toneRms(gRec + 2 * (end - RATE / 4), RATE / 8, 2, 440.0, RATE);
    CHECK_NEAR(db(l / (0.5 / sqrt(2.0))), 0.0, 1.5);
    streamClose(&s);
    sleepMs(200);
}

static void testBurstCap(void) {
    MlxrPulseOutParams p;
    mlxr_pulse_out_params_default(&p);
    p.sink = gSink;
    p.streamName = STREAM_NAME;
    p.tlengthMs = 20;
    p.queueCapMs = 10;
    char err[200] = "";
    MlxrPulseOut* po = mlxr_pulse_out_open(&p, err, sizeof err);
    CHECK(po != NULL);
    if (po == NULL) {
        fprintf(stderr, "%s\n", err);
        return;
    }
    float buf[240 * 2];
    for (int i = 0; i < 240; i++) {
        buf[2 * i] = buf[2 * i + 1] = 0.2f * sinf(2.0f * (float) M_PI * 300.0f * (float) i / RATE);
    }
    // 400 ms handed over in a burst, far faster than real time
    long dropped = 0;
    for (int i = 0; i < 80; i++) {
        dropped += mlxr_pulse_out_write(po, buf, 240);
    }
    MlxrPulseOutStats st;
    mlxr_pulse_out_stats(po, &st);
    printf("  burst of 400 ms: dropped %.0f ms, queue %.1f ms, server tlength %.1f ms, written %.0f ms\n",
           1000.0 * (double) st.droppedFrames / RATE, st.queueMs, st.tlengthMs,
           1000.0 * (double) st.writtenFrames / RATE);
    CHECK((uint64_t) dropped == st.droppedFrames);
    CHECK(st.overruns > 0);
    CHECK(st.queueMs <= 10.5f);
    // What was kept is at most what the server asked for plus the queue cap
    CHECK(1000.0 * (double) st.writtenFrames / RATE + st.queueMs <= st.tlengthMs + 10.5 + 5.0);
    CHECK(1000.0 * (double) st.droppedFrames / RATE >= 400.0 - st.tlengthMs - 10.5 - 20.0);
    // Then paced writes: the latency settles at the target, not at 400 ms
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    for (int i = 0; i < 200; i++) {
        mlxr_pulse_out_write(po, buf, 240);
        t.tv_nsec += 5000000L;
        if (t.tv_nsec >= 1000000000L) { t.tv_nsec -= 1000000000L; t.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, NULL);
    }
    mlxr_pulse_out_stats(po, &st);
    printf("  after 1 s paced: pulse latency %.1f ms, queue %.1f ms, underruns %u\n", st.latencyMs,
           st.queueMs, st.underruns);
    CHECK(st.latencyMs <= 40.0f);
    mlxr_pulse_out_close(po);
    sleepMs(100);
}

static void testClick(void) {
    char err[200] = "";
    mlxr_click_set_stream_name("mlxr-test click virtual sink");
    CHECK(mlxr_click_init(gSink, err, sizeof err) == 0);
    long base = atomic_load(&gRecFrames);
    sleepMs(200);
    double t0 = nowMs();
    mlxr_click_play(MLXR_CLICK_PRESS);
    double call = nowMs() - t0;
    sleepMs(500);
    long first = firstAbove(base, 0.01f);
    CHECK(first >= 0);
    if (first >= 0) {
        double seen = gChunkTime[first / CHUNK] - t0;
        long n = atomic_load(&gRecFrames);
        float peak = 0;
        for (long i = first; i < n; i++) {
            peak = fmaxf(peak, fabsf(gRec[2 * i]));
        }
        // Burst length: frames above 0.002 within 100 ms of the onset
        long last = first;
        for (long i = first; i < first + RATE / 10 && i < n; i++) {
            if (fabsf(gRec[2 * i]) > 0.002f) last = i;
        }
        printf("  click: play() took %.3f ms; heard %.0f ms later (incl. monitor chunk); peak %.3f"
               " (%.1f dBFS); %.0f ms long\n", call, seen, peak, db(peak),
               1000.0 * (double) (last - first) / RATE);
        CHECK(peak > 0.08f && peak < 0.2f);
        CHECK(last - first > RATE / 100 && last - first < RATE / 15);
        CHECK(seen < 250.0);
    }
    // play() never blocks: 20000 calls in a tight loop
    t0 = nowMs();
    for (int i = 0; i < 20000; i++) {
        mlxr_click_play(i & 1 ? MLXR_CLICK_TICK : MLXR_CLICK_PRESS);
    }
    double loop = nowMs() - t0;
    printf("  20000 play() calls: %.2f ms total\n", loop);
    CHECK(loop < 100.0);
    sleepMs(600);
    mlxr_click_set_volume(0.0f);
    long mark = atomic_load(&gRecFrames);
    mlxr_click_play(MLXR_CLICK_PRESS);
    sleepMs(300);
    CHECK(firstAbove(mark + RATE / 10, 0.001f) < 0);  // silent at volume 0 (after the earlier queue drained)
    mlxr_click_shutdown();
    mlxr_click_play(MLXR_CLICK_PRESS);  // after shutdown: a no-op
}

int main(void) {
    char err[200];
    if (mlxr_pulse_acquire(&gLoop, &gCtx, err, sizeof err) != 0) {
        printf("audio_pulse: no sound server (%s), skipped\n", err);
        return 77;
    }
    char before[256], after[256];
    defaultSink(before);
    if (loadSink() != 0) {
        printf("audio_pulse: cannot load a null sink, skipped\n");
        mlxr_pulse_release();
        return 77;
    }
    gRec = (float*) calloc((size_t) REC_FRAMES * 2, sizeof(float));
    gChunkTime = (double*) calloc(REC_FRAMES / CHUNK + 1, sizeof(double));
    pthread_create(&gRecThread, NULL, recorder, NULL);
    sleepMs(300);
    CHECK(atomic_load(&gRecFrames) >= 0);

    printf("default sink before: %s\n", before);
    testTone(2, 0);
    testTone(8, 0);
    testTone(8, 1);
    testMute();
    testBurstCap();
    testClick();

    gRecStop = 1;
    pthread_join(gRecThread, NULL);
    unloadSink();
    defaultSink(after);
    CHECK(strcmp(before, after) == 0);
    mlxr_pulse_release();
    free(gRec);
    free(gChunkTime);
    return checksDone("audio_pulse");
}
