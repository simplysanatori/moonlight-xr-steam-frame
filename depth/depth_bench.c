// mlxr-depth-bench: maps a second, latency (p50/p95) and memory of each depth
// model on 1..N XNNPACK threads, on chosen CPU sets, plus a run of the
// threaded engine at a given rate. GPLv3.
//
//   mlxr-depth-bench [--assets DIR] [--model KEY[,KEY]] [--threads 1,2,3,4]
//                    [--cpus all|a720|fast|x4|MASK[,..]] [--runs N] [--fp16 0|1|both]
//                    [--engine SECONDS] [--rate N]
#define _GNU_SOURCE
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "depth_engine.h"
#include "litert_backend.h"

#ifndef MLXR_DEPTH_ASSETS
#define MLXR_DEPTH_ASSETS "assets/depth"
#endif

static long statusKb(const char* key) {
    FILE* f = fopen("/proc/self/status", "r");
    char line[256];
    long v = -1;
    size_t n = strlen(key);
    while (f != NULL && fgets(line, sizeof line, f)) {
        if (strncmp(line, key, n) == 0) {
            v = atol(line + n + 1);
        }
    }
    if (f) fclose(f);
    return v;
}

static int cmpf(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}

static float pct(const float* sorted, int n, float p) {
    int i = (int)ceilf(p * n) - 1;
    return sorted[i < 0 ? 0 : (i >= n ? n - 1 : i)];
}

// The Frame's cores as the user session sees them: 3..7 (cpu0-2 are not
// ours). cpu0-1 Cortex-A520, cpu2-6 Cortex-A720 (2-4 at 3.15 GHz, 5-6 at
// 2.96 GHz), cpu7 Cortex-X4 3.05 GHz.
static uint64_t parseMask(const char* s, char* label, size_t len) {
    if (strcmp(s, "all") == 0) {
        snprintf(label, len, "all");
        return 0;
    }
    if (strcmp(s, "a720") == 0) {
        snprintf(label, len, "A720 3-6");
        return 0x78;
    }
    if (strcmp(s, "fast") == 0) {
        snprintf(label, len, "3,4,7 (A720 3.15+X4)");
        return 0x98;
    }
    if (strcmp(s, "x4") == 0) {
        snprintf(label, len, "7 (X4)");
        return 0x80;
    }
    uint64_t m = strtoull(s, NULL, 0);
    snprintf(label, len, "mask 0x%llx", (unsigned long long)m);
    return m;
}

static void setAffinity(uint64_t mask) {
    cpu_set_t set;
    CPU_ZERO(&set);
    if (mask == 0) {
        for (int c = 0; c < CPU_SETSIZE && c < 64; c++) CPU_SET(c, &set);
    } else {
        for (int c = 0; c < 64; c++) if (mask & (1ULL << c)) CPU_SET(c, &set);
    }
    sched_setaffinity(0, sizeof set, &set);
}

// A frame of something like a picture: a sky gradient, a floor, a few boxes
static void synthFrame(uint8_t* rgba, int w, int h, int t) {
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t* p = rgba + ((size_t)y * w + x) * 4;
            int floor = y > h / 2;
            float v = (float)y / h;
            int r = floor ? 90 + (int)(80 * v) : 120 + (int)(60 * v);
            int g = floor ? 70 + (int)(60 * v) : 160 + (int)(40 * v);
            int b = floor ? 50 : 220;
            int bx = (x - (t * 3) % w + w) % w;
            if (bx > w / 3 && bx < w / 3 + w / 6 && y > h / 3 && y < h * 4 / 5) {
                r = 200; g = 40; b = 40;
            }
            p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = 255;
        }
    }
}

static int benchModel(const char* path, const MlxrDepthModelSpec* spec, int threads,
                      uint64_t mask, const char* maskLabel, int fp16, int runs) {
    setAffinity(mask);
    long rss0 = statusKb("VmRSS");
    char err[256];
    MlxrDepthModelOptions opt = { threads, fp16 ? MLXR_DEPTH_FP16 : 0 };
    MlxrDepthModel* m = mlxr_depth_model_load(path, &opt, err, sizeof err);
    if (m == NULL) {
        printf("| %s | %d | %s | %s | load failed: %s |\n", spec->name, threads, maskLabel,
               fp16 ? "fp16" : "fp32", err);
        return -1;
    }
    int w = mlxr_depth_model_width(m), h = mlxr_depth_model_height(m);
    uint8_t* rgba = malloc((size_t)w * h * 4);
    synthFrame(rgba, w, h, 0);
    mlxr_depth_rgba8_to_input(rgba, (size_t)w * 4, w, h, 0, mlxr_depth_model_input(m));
    float first = 0.0f;
    mlxr_depth_model_run(m, &first);
    for (int i = 0; i < 2; i++) {
        mlxr_depth_model_run(m, NULL);
    }
    long rss1 = statusKb("VmRSS");
    float* t = malloc(sizeof(float) * runs);
    int64_t wall0 = mlxr_depth_now_ns();
    struct timespec c0, c1;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c0);
    for (int i = 0; i < runs; i++) {
        mlxr_depth_model_run(m, &t[i]);
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c1);
    double wallMs = (mlxr_depth_now_ns() - wall0) / 1e6;
    double cpuMs = (c1.tv_sec - c0.tv_sec) * 1e3 + (c1.tv_nsec - c0.tv_nsec) / 1e6;
    qsort(t, (size_t)runs, sizeof(float), cmpf);
    float sum = 0;
    for (int i = 0; i < runs; i++) sum += t[i];
    float mean = sum / runs;
    printf("| %s | %d | %s | %s | %.1f | %.1f | %.1f | %.1f | %.2f | %.0f | %.0f | %.0f |\n",
           spec->name, threads, maskLabel, fp16 ? "fp16" : "fp32", 1000.0f / mean,
           pct(t, runs, 0.5f), pct(t, runs, 0.95f), first, cpuMs / wallMs,
           mlxr_depth_model_load_ms(m), (rss1 - rss0) / 1024.0, statusKb("VmHWM") / 1024.0);
    fflush(stdout);
    free(t);
    free(rgba);
    mlxr_depth_model_free(m);
    setAffinity(0);
    return 0;
}

// The threaded engine fed a 60 fps synthetic stream for some seconds
static void benchEngine(const char* path, int threads, uint64_t mask, int fp16, int rate,
                        int seconds) {
    MlxrDepthConfig cfg = { 0 };
    cfg.modelPath = path;
    cfg.threads = threads;
    cfg.flags = fp16 ? MLXR_DEPTH_FP16 : 0;
    cfg.perSecond = rate;
    cfg.cpuMask = mask;
    mlxr_depth_params_default(&cfg.params);
    MlxrDepthEngine* e = mlxr_depth_start(&cfg);
    char err[256];
    if (e == NULL || mlxr_depth_wait_ready(e, 30000, err, sizeof err) != MLXR_DEPTH_READY) {
        printf("engine failed: %s\n", e ? err : "no thread");
        if (e) mlxr_depth_stop(e);
        return;
    }
    int w, h;
    mlxr_depth_size(e, &w, &h);
    uint8_t* rgba = malloc((size_t)w * h * 4);
    uint64_t have = 0, got = 0, frame = 0;
    double ageSum = 0, ageMax = 0;
    int64_t start = mlxr_depth_now_ns();
    int64_t period = 1000000000LL / 60;
    int64_t next = start;
    float conv = 0, norm = 0;
    while (mlxr_depth_now_ns() - start < (int64_t)seconds * 1000000000LL) {
        int64_t now = mlxr_depth_now_ns();
        if (now < next) {
            struct timespec ts = { 0, (long)(next - now) };
            nanosleep(&ts, NULL);
            continue;
        }
        next += period;
        frame++;
        if (mlxr_depth_due(e, now)) {
            synthFrame(rgba, w, h, (int)frame);
            mlxr_depth_submit(e, rgba, (size_t)w * 4, w, h, 0, frame, now);
        }
        const MlxrDepthResult* r = mlxr_depth_acquire(e, have);
        if (r != NULL) {
            have = r->seq;
            got++;
            double age = (mlxr_depth_now_ns() - r->captureNs) / 1e6;
            ageSum += age;
            if (age > ageMax) ageMax = age;
            conv += r->convertMs;
            norm += r->normMs;
            mlxr_depth_release(e, r);
        }
    }
    MlxrDepthStats s;
    mlxr_depth_stats(e, &s);
    printf("engine: %dx%d, %d threads, rate %d: %.1f maps/s, model avg %.1f p95 %.1f ms, "
           "submit-to-map %.1f ms, capture-to-frame-loop avg %.1f max %.1f ms, convert %.2f ms, "
           "normalise %.2f ms, %llu submitted, %llu replaced, %llu failed, warmup %.0f ms\n",
           w, h, s.threads, rate, s.mapsPerSecond, s.modelMsAvg, s.modelMsP95, s.latencyMsAvg,
           got ? ageSum / got : 0, ageMax, got ? conv / got : 0, got ? norm / got : 0,
           (unsigned long long)s.submitted, (unsigned long long)s.replaced,
           (unsigned long long)s.failed, s.warmupMs);
    free(rgba);
    mlxr_depth_stop(e);
}

int main(int argc, char** argv) {
    const char* assets = MLXR_DEPTH_ASSETS;
    char models[256] = "zipdepth,zipdepth-int8,midas";
    char threadList[64] = "1,2,3,4";
    char cpuList[128] = "all";
    const char* fp16Arg = "0";
    int runs = 30, engineSeconds = 0, rate = 0;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        const char* v = i + 1 < argc ? argv[i + 1] : "";
        if (!strcmp(a, "--assets")) { assets = v; i++; }
        else if (!strcmp(a, "--model")) { snprintf(models, sizeof models, "%s", v); i++; }
        else if (!strcmp(a, "--threads")) { snprintf(threadList, sizeof threadList, "%s", v); i++; }
        else if (!strcmp(a, "--cpus")) { snprintf(cpuList, sizeof cpuList, "%s", v); i++; }
        else if (!strcmp(a, "--runs")) { runs = atoi(v); i++; }
        else if (!strcmp(a, "--fp16")) { fp16Arg = v; i++; }
        else if (!strcmp(a, "--engine")) { engineSeconds = atoi(v); i++; }
        else if (!strcmp(a, "--rate")) { rate = atoi(v); i++; }
        else { fprintf(stderr, "usage: see the head of depth/depth_bench.c\n"); return 2; }
    }
    if (runs < 1) runs = 1;
    double load[3] = { 0 };
    getloadavg(load, 3);
    cpu_set_t mine;
    CPU_ZERO(&mine);
    sched_getaffinity(0, sizeof mine, &mine);
    char cpus[64] = "";
    for (int c = 0; c < 64 && c < CPU_SETSIZE; c++) {
        if (CPU_ISSET(c, &mine)) {
            snprintf(cpus + strlen(cpus), sizeof cpus - strlen(cpus), "%s%d", cpus[0] ? "," : "", c);
        }
    }
    printf("mlxr-depth-bench: CPUs %s of %ld online, load %.2f %.2f %.2f, runs %d\n", cpus,
           sysconf(_SC_NPROCESSORS_ONLN), load[0], load[1], load[2], runs);
    printf("\n| model | threads | cpus | arith | maps/s | p50 ms | p95 ms | first ms | cpu/wall "
           "| load ms | +RSS MB | peak RSS MB |\n|---|---|---|---|---|---|---|---|---|---|---|---|\n");
    int fpModes[2], nModes = 0;
    if (!strcmp(fp16Arg, "both")) { fpModes[nModes++] = 0; fpModes[nModes++] = 1; }
    else fpModes[nModes++] = atoi(fp16Arg) != 0;

    char mcopy[256];
    snprintf(mcopy, sizeof mcopy, "%s", models);
    for (char* mk = strtok(mcopy, ","); mk; mk = strtok(NULL, ",")) {
        const MlxrDepthModelSpec* spec = mlxr_depth_model_find(mk);
        if (spec == NULL) { printf("unknown model %s\n", mk); continue; }
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", assets, spec->file);
        char ccopy[128];
        snprintf(ccopy, sizeof ccopy, "%s", cpuList);
        char* csave = NULL;
        for (char* ck = strtok_r(ccopy, ",", &csave); ck; ck = strtok_r(NULL, ",", &csave)) {
            char label[32];
            uint64_t mask = parseMask(ck, label, sizeof label);
            char tcopy[64];
            snprintf(tcopy, sizeof tcopy, "%s", threadList);
            char* tsave = NULL;
            for (char* tk = strtok_r(tcopy, ",", &tsave); tk; tk = strtok_r(NULL, ",", &tsave)) {
                for (int f = 0; f < nModes; f++) {
                    benchModel(path, spec, atoi(tk), mask, label, fpModes[f], runs);
                }
            }
        }
        if (engineSeconds > 0) {
            char ccopy2[128];
            snprintf(ccopy2, sizeof ccopy2, "%s", cpuList);
            char label[32];
            uint64_t mask = parseMask(strtok(ccopy2, ","), label, sizeof label);
            const char* tk = strrchr(threadList, ',');
            int threads = atoi(tk ? tk + 1 : threadList);
            printf("\n%s, %s, ", spec->name, label);
            benchEngine(path, threads, mask, fpModes[nModes - 1], rate, engineSeconds);
            printf("\n");
        }
    }
    return 0;
}
