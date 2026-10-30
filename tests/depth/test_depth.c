// Depth model tests: each shipped model on ray traced scenes with known depth
// (ordering, rank correlation with the truth), run-to-run and thread-count
// stability, fp16 vs int8 agreement, fp16 arithmetic vs fp32, the
// normalisation, and the threaded engine's hand-off and rate. GPLv3.
//   test_depth [group]   groups: spec order stable agree arith norm motion scale engine
#define _GNU_SOURCE
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "depth_engine.h"
#include "litert_backend.h"
#include "scene.h"
#include "xr_depthmap.h"

#ifndef MLXR_DEPTH_ASSETS
#define MLXR_DEPTH_ASSETS "assets/depth"
#endif

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static char* modelPath(MlxrDepthModelId id) {
    static char buf[MLXR_DEPTH_MODEL_COUNT][1024];
    snprintf(buf[id], sizeof buf[id], "%s/%s", MLXR_DEPTH_ASSETS, mlxr_depth_model_spec(id)->file);
    return buf[id];
}

static MlxrDepthModel* load(MlxrDepthModelId id, int threads, unsigned flags) {
    char err[256];
    MlxrDepthModelOptions o = { threads, flags };
    MlxrDepthModel* m = mlxr_depth_model_load(modelPath(id), &o, err, sizeof err);
    CHECK(m != NULL, "load %s: %s", mlxr_depth_model_spec(id)->name, err);
    return m;
}

// Runs the model on a scene; returns a malloc'd copy of the output
static float* runScene(MlxrDepthModel* m, SceneId s, float shift) {
    int w = mlxr_depth_model_width(m), h = mlxr_depth_model_height(m);
    uint8_t* rgba = malloc((size_t)w * h * 4);
    sceneRender(s, w, h, shift, rgba, NULL);
    mlxr_depth_rgba8_to_input(rgba, (size_t)w * 4, w, h, 0, mlxr_depth_model_input(m));
    free(rgba);
    int rc = mlxr_depth_model_run(m, NULL);
    CHECK(rc == 0, "run");
    float* out = malloc((size_t)w * h * sizeof(float));
    memcpy(out, mlxr_depth_model_output(m), (size_t)w * h * sizeof(float));
    return out;
}

static int cmpf(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}

// Median of v where part == want (and, with rowLo/rowHi, in those rows)
static float medianWhere(const float* v, const uint8_t* part, int w, int h, int want,
                         int rowLo, int rowHi) {
    float* tmp = malloc((size_t)w * h * sizeof(float));
    int n = 0;
    for (int y = rowLo; y < rowHi; y++)
        for (int x = 0; x < w; x++)
            if (part[y * w + x] == want) tmp[n++] = v[y * w + x];
    float med = NAN;
    if (n > 0) {
        qsort(tmp, (size_t)n, sizeof(float), cmpf);
        med = tmp[n / 2];
    }
    free(tmp);
    return med;
}

typedef struct { float v; int i; } Ranked;
static int cmpr(const void* a, const void* b) {
    float x = ((const Ranked*)a)->v, y = ((const Ranked*)b)->v;
    return x < y ? -1 : x > y;
}
static void ranks(const float* v, int n, float* r) {
    Ranked* t = malloc(sizeof(Ranked) * n);
    for (int i = 0; i < n; i++) { t[i].v = v[i]; t[i].i = i; }
    qsort(t, (size_t)n, sizeof(Ranked), cmpr);
    for (int i = 0; i < n;) {
        int j = i;
        while (j + 1 < n && t[j + 1].v == t[i].v) j++;
        float rank = 0.5f * (i + j);
        for (int k = i; k <= j; k++) r[t[k].i] = rank;
        i = j + 1;
    }
    free(t);
}
// Spearman rank correlation
static float spearman(const float* a, const float* b, int n) {
    float* ra = malloc(sizeof(float) * n);
    float* rb = malloc(sizeof(float) * n);
    ranks(a, n, ra);
    ranks(b, n, rb);
    double ma = 0, mb = 0;
    for (int i = 0; i < n; i++) { ma += ra[i]; mb += rb[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; i++) {
        double x = ra[i] - ma, y = rb[i] - mb;
        sab += x * y; saa += x * x; sbb += y * y;
    }
    free(ra);
    free(rb);
    return (float)(sab / sqrt(saa * sbb));
}

// Min-max to 0..1 over the robust range, as the map is normalised
static void normalise(const float* v, int n, float* out) {
    float lo, hi;
    robustRange(v, n, &lo, &hi);
    for (int i = 0; i < n; i++) {
        float x = (v[i] - lo) / (hi - lo);
        out[i] = x < 0 ? 0 : (x > 1 ? 1 : x);
    }
}

static void resizeBilinear(const float* src, int sw, int sh, float* dst, int dw, int dh) {
    for (int y = 0; y < dh; y++) {
        float fy = (y + 0.5f) * sh / dh - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        int y1 = y0 + 1;
        if (y0 < 0) y0 = 0;
        if (y1 > sh - 1) y1 = sh - 1;
        if (y0 > sh - 1) y0 = sh - 1;
        for (int x = 0; x < dw; x++) {
            float fx = (x + 0.5f) * sw / dw - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            int x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x1 > sw - 1) x1 = sw - 1;
            if (x0 > sw - 1) x0 = sw - 1;
            float a = src[y0 * sw + x0] * (1 - tx) + src[y0 * sw + x1] * tx;
            float b = src[y1 * sw + x0] * (1 - tx) + src[y1 * sw + x1] * tx;
            dst[y * dw + x] = a * (1 - ty) + b * ty;
        }
    }
}

static void groupSpec(void) {
    printf("spec\n");
    for (int i = 0; i < MLXR_DEPTH_MODEL_COUNT; i++) {
        const MlxrDepthModelSpec* s = mlxr_depth_model_spec((MlxrDepthModelId)i);
        CHECK(s != NULL && (int)s->id == i, "spec %d", i);
        CHECK(mlxr_depth_model_find(s->key) == s, "find key %s", s->key);
        CHECK(mlxr_depth_model_find(s->file) == s, "find file %s", s->file);
    }
    CHECK(mlxr_depth_model_find("nope") == NULL, "unknown key");
    char err[256] = "";
    MlxrDepthModelOptions o = { 1, 0 };
    CHECK(mlxr_depth_model_load("/nonexistent.tflite", &o, err, sizeof err) == NULL && err[0],
          "missing file fails with a reason");
}

// Near brighter: larger output on the sphere than on the wall, on the floor's
// near rows than its far rows, and a rank correlation with 1/z (the truth)
static void groupOrder(void) {
    printf("order\n");
    for (int id = 0; id < MLXR_DEPTH_MODEL_COUNT; id++) {
        const MlxrDepthModelSpec* spec = mlxr_depth_model_spec((MlxrDepthModelId)id);
        MlxrDepthModel* m = load((MlxrDepthModelId)id, 4, 0);
        if (m == NULL) continue;
        int w = mlxr_depth_model_width(m), h = mlxr_depth_model_height(m), n = w * h;
        CHECK(w == spec->width && h == spec->height, "%s size %dx%d", spec->name, w, h);
        float* z = malloc(sizeof(float) * n);
        uint8_t* rgba = malloc((size_t)n * 4);
        uint8_t* part = malloc((size_t)n);
        sceneRender(SCENE_ROOM, w, h, 0.0f, rgba, z);
        scenePart(SCENE_ROOM, w, h, 0.0f, part);
        free(rgba);
        float* out = runScene(m, SCENE_ROOM, 0.0f);
        int finite = 1;
        for (int i = 0; i < n; i++) finite &= isfinite(out[i]) != 0;
        CHECK(finite, "%s output finite", spec->name);
        float* nrm = malloc(sizeof(float) * n);
        normalise(out, n, nrm);
        float near = medianWhere(nrm, part, w, h, PART_NEAR, 0, h);
        float wall = medianWhere(nrm, part, w, h, PART_WALL, 0, h);
        float box = medianWhere(nrm, part, w, h, PART_BOX, 0, h);
        float floorNear = medianWhere(nrm, part, w, h, PART_FLOOR, h * 9 / 10, h);
        float floorFar = medianWhere(nrm, part, w, h, PART_FLOOR, h / 2, h * 6 / 10);
        float* inv = malloc(sizeof(float) * n);
        for (int i = 0; i < n; i++) inv[i] = 1.0f / z[i];
        float rho = spearman(out, inv, n);
        printf("  %-26s room: sphere %.2f box %.2f wall %.2f floor near %.2f far %.2f, "
               "spearman vs 1/z %.3f\n", spec->name, near, box, wall, floorNear, floorFar, rho);
        CHECK(near > wall + 0.2f, "%s: sphere %.2f nearer than wall %.2f", spec->name, near, wall);
        CHECK(box > wall, "%s: box %.2f nearer than wall %.2f", spec->name, box, wall);
        CHECK(near > box, "%s: sphere %.2f nearer than box %.2f", spec->name, near, box);
        CHECK(floorNear > floorFar, "%s: floor near %.2f > far %.2f", spec->name, floorNear, floorFar);
        CHECK(rho > 0.75f, "%s: spearman %.3f", spec->name, rho);
        free(out);

        sceneRender(SCENE_DISC, w, h, 0.0f, (rgba = malloc((size_t)n * 4)), z);
        free(rgba);
        scenePart(SCENE_DISC, w, h, 0.0f, part);
        out = runScene(m, SCENE_DISC, 0.0f);
        normalise(out, n, nrm);
        float disc = medianWhere(nrm, part, w, h, PART_NEAR, 0, h);
        float back = medianWhere(nrm, part, w, h, PART_WALL, 0, h);
        printf("  %-26s disc: disc %.2f backdrop %.2f\n", spec->name, disc, back);
        // No floor, no perspective: only the occlusion edge and the size say
        // which is nearer, so the margin is smaller than the room's
        CHECK(disc > back + 0.05f, "%s: disc %.2f nearer than backdrop %.2f", spec->name, disc, back);
        free(out);
        free(nrm);
        free(inv);
        free(z);
        free(part);
        mlxr_depth_model_free(m);
    }
}

static float maxAbsDiff(const float* a, const float* b, int n, float* range) {
    float d = 0, lo = INFINITY, hi = -INFINITY;
    for (int i = 0; i < n; i++) {
        d = fmaxf(d, fabsf(a[i] - b[i]));
        lo = fminf(lo, a[i]);
        hi = fmaxf(hi, a[i]);
    }
    *range = hi - lo;
    return d;
}

// The same input twice gives the same map; 1 and 4 threads agree
static void groupStable(void) {
    printf("stable\n");
    for (int id = 0; id < MLXR_DEPTH_MODEL_COUNT; id++) {
        const MlxrDepthModelSpec* spec = mlxr_depth_model_spec((MlxrDepthModelId)id);
        MlxrDepthModel* m4 = load((MlxrDepthModelId)id, 4, 0);
        MlxrDepthModel* m1 = load((MlxrDepthModelId)id, 1, 0);
        if (m4 == NULL || m1 == NULL) { mlxr_depth_model_free(m4); mlxr_depth_model_free(m1); continue; }
        int n = mlxr_depth_model_width(m4) * mlxr_depth_model_height(m4);
        float* a = runScene(m4, SCENE_ROOM, 0.0f);
        float* other = runScene(m4, SCENE_DISC, 0.0f);
        float* b = runScene(m4, SCENE_ROOM, 0.0f);
        float* c = runScene(m1, SCENE_ROOM, 0.0f);
        float range;
        float dRepeat = maxAbsDiff(a, b, n, &range);
        float dThreads = maxAbsDiff(a, c, n, &range);
        printf("  %-26s repeat max diff %.3g, 1 vs 4 threads %.3g (range %.3g)\n", spec->name,
               dRepeat, dThreads, range);
        CHECK(dRepeat == 0.0f, "%s repeat differs by %g", spec->name, dRepeat);
        CHECK(dThreads <= 1e-3f * range, "%s threads differ by %g of %g", spec->name, dThreads, range);
        free(a); free(b); free(c); free(other);
        mlxr_depth_model_free(m4);
        mlxr_depth_model_free(m1);
    }
}

// ZipDepth fp16 512x288 vs int8 256 (resized to 512x288), and MiDaS for
// reference: rank correlation and mean difference on the room, whose depth is
// spread over the whole frame. On the disc most of the frame is one flat
// backdrop whose own ordering is noise, so there they only have to agree on
// which part is nearer.
static void groupAgree(void) {
    printf("agree\n");
    MlxrDepthModel* f = load(MLXR_DEPTH_ZIPDEPTH_512x288_FP16, 4, 0);
    MlxrDepthModel* q = load(MLXR_DEPTH_ZIPDEPTH_256_INT8DR, 4, 0);
    MlxrDepthModel* mi = load(MLXR_DEPTH_MIDAS_256_FP16, 4, 0);
    if (f == NULL || q == NULL || mi == NULL) goto out;
    const int fw = 512, fh = 288, n = fw * fh;
    uint8_t* part = malloc((size_t)n);
    for (int s = 0; s < 2; s++) {
        scenePart((SceneId)s, fw, fh, 0.0f, part);
        float* a = runScene(f, (SceneId)s, 0.0f);
        float* b = runScene(q, (SceneId)s, 0.0f);
        float* c = runScene(mi, (SceneId)s, 0.0f);
        float* br = malloc(sizeof(float) * n);
        float* cr = malloc(sizeof(float) * n);
        resizeBilinear(b, 256, 256, br, fw, fh);
        resizeBilinear(c, 256, 256, cr, fw, fh);
        float* na = malloc(sizeof(float) * n);
        float* nb = malloc(sizeof(float) * n);
        float* nc = malloc(sizeof(float) * n);
        normalise(a, n, na);
        normalise(br, n, nb);
        normalise(cr, n, nc);
        double mad = 0, madm = 0;
        for (int i = 0; i < n; i++) { mad += fabsf(na[i] - nb[i]); madm += fabsf(na[i] - nc[i]); }
        mad /= n;
        madm /= n;
        float rho = spearman(a, br, n);
        float rhom = spearman(a, cr, n);
        float nearA = medianWhere(na, part, fw, fh, PART_NEAR, 0, fh);
        float farA = medianWhere(na, part, fw, fh, PART_WALL, 0, fh);
        float nearB = medianWhere(nb, part, fw, fh, PART_NEAR, 0, fh);
        float farB = medianWhere(nb, part, fw, fh, PART_WALL, 0, fh);
        printf("  %s: zipdepth fp16 vs int8 spearman %.3f, normalised mean abs diff %.3f "
               "(near/far fp16 %.2f/%.2f int8 %.2f/%.2f); fp16 vs midas spearman %.3f, diff %.3f\n",
               s == 0 ? "room" : "disc", rho, mad, nearA, farA, nearB, farB, rhom, madm);
        if (s == 0) {
            CHECK(rho > 0.9f, "room fp16 vs int8 spearman %.3f", rho);
            CHECK(mad < 0.1, "room fp16 vs int8 mean abs diff %.3f", mad);
        }
        CHECK(nearA > farA && nearB > farB, "fp16 and int8 agree on near vs far");
        free(a); free(b); free(c); free(br); free(cr); free(na); free(nb); free(nc);
    }
    free(part);
out:
    mlxr_depth_model_free(f);
    mlxr_depth_model_free(q);
    mlxr_depth_model_free(mi);
}

// XNNPACK fp16 arithmetic against fp32 on the same model
static void groupArith(void) {
    printf("arith\n");
    for (int id = 0; id < MLXR_DEPTH_MODEL_COUNT; id++) {
        const MlxrDepthModelSpec* spec = mlxr_depth_model_spec((MlxrDepthModelId)id);
        MlxrDepthModel* a = load((MlxrDepthModelId)id, 4, 0);
        MlxrDepthModel* b = load((MlxrDepthModelId)id, 4, MLXR_DEPTH_FP16);
        if (a == NULL || b == NULL) { mlxr_depth_model_free(a); mlxr_depth_model_free(b); continue; }
        int n = mlxr_depth_model_width(a) * mlxr_depth_model_height(a);
        float* x = runScene(a, SCENE_ROOM, 0.0f);
        float* y = runScene(b, SCENE_ROOM, 0.0f);
        float* nx = malloc(sizeof(float) * n);
        float* ny = malloc(sizeof(float) * n);
        normalise(x, n, nx);
        normalise(y, n, ny);
        double mad = 0;
        float mx = 0;
        for (int i = 0; i < n; i++) { float d = fabsf(nx[i] - ny[i]); mad += d; mx = fmaxf(mx, d); }
        mad /= n;
        printf("  %-26s fp16 vs fp32 arithmetic: normalised mean abs diff %.4f, max %.4f "
               "(%.2f / %.2f levels of 255)\n", spec->name, mad, mx, mad * 255, mx * 255);
        CHECK(mad < 0.01, "%s fp16 arithmetic mean diff %.4f", spec->name, mad);
        free(x); free(y); free(nx); free(ny);
        mlxr_depth_model_free(a);
        mlxr_depth_model_free(b);
    }
}

// The normalisation: the range, near = 1, the guide colour, the resets
static void groupNorm(void) {
    printf("norm\n");
    const int w = 64, h = 32, n = w * h;
    MlxrDepthNorm* nm = mlxr_depth_norm_create(w, h);
    float* raw = malloc(sizeof(float) * n);
    float* guide = malloc(sizeof(float) * n * 3);
    uint8_t* rgba = malloc((size_t)n * 4);
    float* depth = malloc(sizeof(float) * n);
    for (int i = 0; i < n; i++) {
        raw[i] = 10.0f + 90.0f * (float)(i % w) / (w - 1);  // larger to the right = nearer
        guide[i * 3 + 0] = 0.25f; guide[i * 3 + 1] = 0.5f; guide[i * 3 + 2] = 1.0f;
    }
    raw[5] = NAN;
    MlxrDepthParams p;
    mlxr_depth_params_default(&p);
    int64_t t = 1000000000LL;
    mlxr_depth_norm_run(nm, &p, raw, guide, 0, t, rgba, depth);
    CHECK(depth[0] < 0.05f && depth[w - 1] > 0.95f, "range spans the map: %.3f %.3f", depth[0], depth[w - 1]);
    CHECK(depth[5] == 0.0f, "NaN reads as far: %.3f", depth[5]);
    CHECK(rgba[0] == 64 && rgba[1] == 128 && rgba[2] == 255, "guide rgb %d %d %d", rgba[0], rgba[1], rgba[2]);
    int alphaOk = 1, mono = 1;
    for (int i = 0; i < n; i++) alphaOk &= rgba[i * 4 + 3] == (uint8_t)(depth[i] * 255.0f + 0.5f);
    for (int x = 7; x < w; x++) mono &= depth[x] >= depth[x - 1];
    CHECK(alphaOk, "alpha is the depth");
    CHECK(mono, "monotonic in the model output");
    // A scene twice as deep 10 ms later: the averages hold the old one back...
    for (int i = 0; i < n; i++) raw[i] = 100.0f - 90.0f * (float)(i % w) / (w - 1);
    mlxr_depth_norm_run(nm, &p, raw, guide, 0, t + 10000000LL, rgba, depth);
    float held = depth[w - 1];
    // ...and a texel+range reset takes the new one as it is
    mlxr_depth_norm_run(nm, &p, raw, guide, DEPTH_RESET_TEXEL | DEPTH_RESET_RANGE,
                        t + 20000000LL, rgba, depth);
    printf("  after a flip: averaged right edge %.3f, after reset %.3f\n", held, depth[w - 1]);
    CHECK(held > 0.3f, "averages carry over: %.3f", held);
    CHECK(depth[w - 1] < 0.02f && depth[0] > 0.98f, "reset takes the new map: %.3f %.3f",
          depth[0], depth[w - 1]);
    // The remap pulls the overall shape toward convergence
    p.global = 0.5f;
    p.local = 1.4f;
    p.tauMs = 0;
    p.rangeTauMs = 0;
    mlxr_depth_norm_run(nm, &p, raw, guide, 0, t + 30000000LL, rgba, depth);
    CHECK(depth[w / 2] > 0.2f && depth[w / 2] < 0.8f && depth[0] <= 1.0f, "remap stays in range");
    free(raw); free(guide); free(rgba); free(depth);
    mlxr_depth_norm_free(nm);
}

// Native (lane wave5-depth, PLAN 5.4): an edge in the guide moves between two
// maps. With the motion reset the average starts again where the picture
// changed and keeps its history where it did not; without it every texel
// carries the old depth for a while
static void groupMotion(void) {
    printf("motion\n");
    const int w = 64, h = 16, n = w * h;
    float* raw = malloc(sizeof(float) * n);
    float* guide = malloc(sizeof(float) * n * 3);
    MlxrDepthParams p;
    mlxr_depth_params_default(&p);
    p.rangeTauMs = 0;
    for (int pass = 0; pass < 2; pass++) {
        p.motion = pass == 0 ? 0.0f : 0.10f;
        MlxrDepthNorm* nm = mlxr_depth_norm_create(w, h);
        int64_t t = 1000000000LL;
        // First map: a bright object over columns 10..19, near (60) on a far
        // page (20)
        for (int i = 0; i < n; i++) {
            int x = i % w;
            int in = x >= 10 && x < 20;
            float g = in ? 0.9f : 0.1f;
            guide[i * 3 + 0] = guide[i * 3 + 1] = guide[i * 3 + 2] = g;
            raw[i] = in ? 60.0f : 20.0f;
        }
        mlxr_depth_norm_run(nm, &p, raw, guide, 0, t, NULL, NULL);
        // 20 ms on the object has moved to columns 30..39; the model also
        // wobbles the far page a little (21) where nothing moved
        for (int i = 0; i < n; i++) {
            int x = i % w;
            int in = x >= 30 && x < 40;
            float g = in ? 0.9f : 0.1f;
            guide[i * 3 + 0] = guide[i * 3 + 1] = guide[i * 3 + 2] = g;
            raw[i] = in ? 60.0f : 21.0f;
        }
        mlxr_depth_norm_run(nm, &p, raw, guide, 0, t + 20000000LL, NULL, NULL);
        const float* avg = mlxr_depth_norm_average(nm);
        int row = 8 * w;
        float oldSpot = avg[row + 15], newSpot = avg[row + 35], still = avg[row + 50];
        int resets = mlxr_depth_norm_motion_resets(nm);
        printf("  motion %.2f: where it was %.2f, where it is %.2f, the still page %.2f, %d texels started again\n",
               p.motion, oldSpot, newSpot, still, resets);
        // The still page keeps averaging either way: between 20 and 21
        CHECK(still > 20.0f && still < 21.0f, "still page averaged: %.3f", still);
        if (pass == 0) {
            CHECK(oldSpot > 30.0f && newSpot < 50.0f, "without it the old depth trails: %.2f %.2f", oldSpot, newSpot);
            CHECK(resets == 0, "off: no texel starts again (%d)", resets);
        } else {
            CHECK(oldSpot == 21.0f && newSpot == 60.0f, "with it the moved texels take the new map: %.2f %.2f",
                  oldSpot, newSpot);
            CHECK(resets == 20 * h, "the two moved bands start again, nothing else (%d)", resets);
        }
        mlxr_depth_norm_free(nm);
    }
    free(raw);
    free(guide);
}

// Native (lane wave5-depth, PLAN 5.6): the depth scale. At 100 and 100 the
// map is exactly the plain normalisation; otherwise every texel is
// convergence + global * (shape - convergence) + local * (detail), the shape
// the map's box low pass (radius 11 texels of a 256 map, scaled with it) and
// the detail what is left, held to 0..1
static void groupScale(void) {
    printf("scale\n");
    const int w = 64, h = 32, n = w * h;
    float* raw = malloc(sizeof(float) * n);
    float* guide = malloc(sizeof(float) * n * 3);
    float* plain = malloc(sizeof(float) * n);
    float* got = malloc(sizeof(float) * n);
    float* low = malloc(sizeof(float) * n);
    float* scratch = malloc(sizeof(float) * n);
    float* cols = malloc(sizeof(float) * w);
    for (int i = 0; i < n; i++) {
        int x = i % w, y = i / w;
        // A slope (the overall shape) with a bump on it (local detail)
        raw[i] = 10.0f + 40.0f * (float)y / (h - 1) + ((x - 40) * (x - 40) + (y - 12) * (y - 12) < 25 ? 30.0f : 0.0f);
        guide[i * 3 + 0] = guide[i * 3 + 1] = guide[i * 3 + 2] = 0.5f;
    }
    MlxrDepthParams p;
    mlxr_depth_params_default(&p);
    p.tauMs = 0;
    p.rangeTauMs = 0;
    MlxrDepthNorm* nm = mlxr_depth_norm_create(w, h);
    mlxr_depth_norm_run(nm, &p, raw, guide, 0, 1000000000LL, NULL, plain);
    // 100 and 100 is the plain map, bit for bit
    p.global = 1.0f;
    p.local = 1.0f;
    mlxr_depth_norm_run(nm, &p, raw, guide, 0, 1010000000LL, NULL, got);
    CHECK(memcmp(plain, got, sizeof(float) * n) == 0, "100/100 leaves the map alone");
    lowPass(plain, low, scratch, cols, w, h, 11 * w / 256, 11 * h / 256);
    const float gains[][2] = { { 1.0f, 1.5f }, { 0.5f, 1.0f }, { 0.6f, 1.4f }, { 1.0f, 0.0f } };
    for (size_t k = 0; k < sizeof gains / sizeof gains[0]; k++) {
        p.global = gains[k][0];
        p.local = gains[k][1];
        mlxr_depth_norm_run(nm, &p, raw, guide, 0, 1020000000LL + (int64_t)k * 10000000LL, NULL, got);
        float worst = 0.0f, spreadPlain = 0.0f, spreadGot = 0.0f;
        for (int i = 0; i < n; i++) {
            float want = p.convergence + p.global * (low[i] - p.convergence) + p.local * (plain[i] - low[i]);
            want = want < 0.0f ? 0.0f : (want > 1.0f ? 1.0f : want);
            worst = fmaxf(worst, fabsf(got[i] - want));
        }
        // The bump over the slope beside it, before and after
        int bump = 12 * w + 40, beside = 12 * w + 20;
        spreadPlain = plain[bump] - plain[beside];
        spreadGot = got[bump] - got[beside];
        printf("  global %.1f local %.1f: worst %.2g from the formula, bump over its row %.3f (plain %.3f)\n",
               p.global, p.local, worst, spreadGot, spreadPlain);
        CHECK(worst < 1e-5f, "global %.1f local %.1f follows the remap (%.2g)", p.global, p.local, worst);
        if (p.local > 1.0f) CHECK(spreadGot > spreadPlain, "more local gain pops the bump out more");
        if (p.local == 0.0f) CHECK(fabsf(got[bump] - low[bump]) < 1e-5f, "no local gain leaves the shape alone");
    }
    // The whole map toward the screen plane: half the global gain halves the
    // shape's distance from the convergence where there is no detail
    int flatSpot = 28 * w + 5;
    p.global = 0.5f;
    p.local = 1.0f;
    mlxr_depth_norm_run(nm, &p, raw, guide, 0, 1100000000LL, NULL, got);
    CHECK(fabsf((got[flatSpot] - p.convergence) - 0.5f * (low[flatSpot] - p.convergence)
                - (plain[flatSpot] - low[flatSpot])) < 1e-5f, "global 0.5 halves the shape");
    mlxr_depth_norm_free(nm);
    free(raw); free(guide); free(plain); free(got); free(low); free(scratch); free(cols);
}

static void sleepMs(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

// The engine: ready, frame ids through, a replaced capture, near > far in the
// published map, the rate held
static void groupEngine(void) {
    printf("engine\n");
    MlxrDepthConfig cfg = { 0 };
    cfg.modelPath = modelPath(MLXR_DEPTH_ZIPDEPTH_256_INT8DR);
    cfg.threads = 3;
    cfg.keepRaw = 1;
    mlxr_depth_params_default(&cfg.params);
    MlxrDepthEngine* e = mlxr_depth_start(&cfg);
    CHECK(e != NULL, "engine starts");
    if (e == NULL) return;
    char err[256];
    MlxrDepthState st = mlxr_depth_wait_ready(e, 30000, err, sizeof err);
    CHECK(st == MLXR_DEPTH_READY, "engine ready: %s", err);
    int w, h;
    mlxr_depth_size(e, &w, &h);
    CHECK(w == 256 && h == 256, "engine size %dx%d", w, h);
    uint8_t* rgba = malloc((size_t)w * h * 4);
    uint8_t* part = malloc((size_t)w * h);
    sceneRender(SCENE_ROOM, w, h, 0.0f, rgba, NULL);
    scenePart(SCENE_ROOM, w, h, 0.0f, part);
    CHECK(mlxr_depth_submit(e, rgba, (size_t)w * 4, 128, 128, 0, 1, 0) == -1, "wrong size refused");
    CHECK(mlxr_depth_acquire(e, 0) == NULL, "nothing before the first map");
    int64_t t0 = mlxr_depth_now_ns();
    int r1 = mlxr_depth_submit(e, rgba, (size_t)w * 4, w, h, 0, 101, t0);
    CHECK(r1 == 1, "first submit queued (%d)", r1);
    const MlxrDepthResult* r = NULL;
    for (int i = 0; i < 2000 && r == NULL; i++) {
        r = mlxr_depth_acquire(e, 0);
        if (r == NULL) sleepMs(1);
    }
    CHECK(r != NULL, "a map comes back");
    if (r != NULL) {
        CHECK(r->frameId == 101 && r->captureNs == t0 && r->seq == 1, "frame id %llu seq %llu",
              (unsigned long long)r->frameId, (unsigned long long)r->seq);
        CHECK(r->width == w && r->height == h && r->raw != NULL, "result shape");
        CHECK(r->doneNs >= r->startNs && r->startNs >= r->submitNs, "times in order");
        float* d = malloc(sizeof(float) * w * h);
        memcpy(d, r->depth, sizeof(float) * w * h);
        float near = medianWhere(d, part, w, h, PART_NEAR, 0, h);
        float wall = medianWhere(d, part, w, h, PART_WALL, 0, h);
        printf("  engine map: sphere %.2f wall %.2f, model %.1f ms, convert %.2f ms, "
               "normalise %.2f ms\n", near, wall, r->modelMs, r->convertMs, r->normMs);
        CHECK(near > wall + 0.2f, "engine map near %.2f > far %.2f", near, wall);
        int alphaOk = 1, guideOk = 1;
        for (int i = 0; i < w * h; i++) {
            alphaOk &= r->rgba[i * 4 + 3] == (uint8_t)(r->depth[i] * 255.0f + 0.5f);
            for (int c = 0; c < 3; c++) guideOk &= r->rgba[i * 4 + c] == rgba[i * 4 + c];
        }
        CHECK(alphaOk, "map alpha is the depth");
        CHECK(guideOk, "map rgb is the frame it came from");
        CHECK(mlxr_depth_acquire(e, r->seq) == NULL, "nothing newer yet");
        mlxr_depth_release(e, r);
        free(d);
    }
    // Hold the depth thread to a slow rate so captures queue, then replace
    mlxr_depth_set_rate(e, 2);
    sleepMs(5);
    mlxr_depth_submit(e, rgba, (size_t)w * 4, w, h, 0, 200, mlxr_depth_now_ns());
    int rr = mlxr_depth_submit(e, rgba, (size_t)w * 4, w, h, 0, 201, mlxr_depth_now_ns());
    CHECK(rr == 0, "a second waiting capture replaces the first (%d)", rr);
    CHECK(mlxr_depth_due(e, mlxr_depth_now_ns()) == 0, "not due while a capture waits");
    MlxrDepthStats s;
    mlxr_depth_stats(e, &s);
    CHECK(s.replaced >= 1, "replaced counted");
    // A 60 fps feed for 2 s at 10 maps/s
    mlxr_depth_set_rate(e, 0);
    r = NULL;
    for (int i = 0; i < 2000 && r == NULL; i++) {
        r = mlxr_depth_acquire(e, 1);
        if (r == NULL) sleepMs(1);
    }
    CHECK(r != NULL && r->frameId == 201, "the newest capture ran (frame %llu)",
          r ? (unsigned long long)r->frameId : 0ULL);
    uint64_t have = r ? r->seq : 1;
    mlxr_depth_release(e, r);
    mlxr_depth_set_rate(e, 10);
    mlxr_depth_stats(e, &s);
    uint64_t maps0 = s.maps;
    // Rendered ahead: the ray tracer is slower than a frame and would make
    // the feed itself late
    enum { FEED = 8 };
    uint8_t* feed[FEED];
    for (int i = 0; i < FEED; i++) {
        feed[i] = malloc((size_t)w * h * 4);
        sceneRender(SCENE_ROOM, w, h, i * 0.05f, feed[i], NULL);
    }
    int64_t start = mlxr_depth_now_ns(), next = start;
    uint64_t frame = 1000, lastFrame = 0;
    int ordered = 1;
    while (mlxr_depth_now_ns() - start < 2000000000LL) {
        int64_t now = mlxr_depth_now_ns();
        if (now < next) { sleepMs(1); continue; }
        next += 16666667;
        frame++;
        if (mlxr_depth_due(e, now)) {
            mlxr_depth_submit(e, feed[frame % FEED], (size_t)w * 4, w, h, 0, frame, now);
        }
        const MlxrDepthResult* x = mlxr_depth_acquire(e, have);
        if (x) {
            ordered &= x->frameId > lastFrame;
            lastFrame = x->frameId;
            have = x->seq;
            mlxr_depth_release(e, x);
        }
    }
    mlxr_depth_stats(e, &s);
    float rate = (float)(s.maps - maps0) / 2.0f;
    printf("  at a 10/s target: %.1f maps/s, model avg %.1f ms p95 %.1f ms, submit to map %.1f ms\n",
           rate, s.modelMsAvg, s.modelMsP95, s.latencyMsAvg);
    CHECK(rate >= 8.0f && rate <= 11.0f, "rate held: %.1f maps/s", rate);
    CHECK(ordered, "maps come in frame order");
    mlxr_depth_reset(e);
    for (int i = 0; i < FEED; i++) free(feed[i]);
    free(rgba);
    free(part);
    mlxr_depth_stop(e);

    // A model that will not load says so
    cfg.modelPath = "/nonexistent.tflite";
    e = mlxr_depth_start(&cfg);
    st = mlxr_depth_wait_ready(e, 5000, err, sizeof err);
    CHECK(st == MLXR_DEPTH_FAILED && err[0], "a bad model fails: %s", err);
    CHECK(mlxr_depth_due(e, mlxr_depth_now_ns()) == 0, "never due when failed");
    mlxr_depth_stop(e);
}

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : NULL;
    struct { const char* name; void (*fn)(void); } groups[] = {
        { "spec", groupSpec }, { "order", groupOrder }, { "stable", groupStable },
        { "agree", groupAgree }, { "arith", groupArith }, { "norm", groupNorm }, { "motion", groupMotion },
        { "scale", groupScale },
        { "engine", groupEngine },
    };
    for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
        if (only == NULL || strcmp(only, groups[i].name) == 0) groups[i].fn();
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
