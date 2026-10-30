// Moonlight XR Native: headless Vulkan tests for mlxr_passes. Each pass runs
// on a synthetic fixture and is checked numerically against the CPU maths
// (xr/core where it exists, else a line for line CPU copy of the shader).
//   mlxr-shader-test <group> <output dir for PPMs>
// Prints "<group>: N checks, M failed" and exits non-zero on a failure.
// GPLv3.
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "passes.h"
#include "vkh.h"
#include "xr_glow.h"
#include "xr_grade.h"
#include "xr_math.h"

static Vkh vk;
static MlxrPasses* P;
static const char* outDir = ".";
static int checks, failed;
static int nv12Ok;
// NV12 against three-plane, in 1/255: lavapipe's VkSamplerYcbcrConversion
// lands 3/255 from the shader's own maths (Mesa 25.2), the Frame's within 2
static int nv12Tol = 2;

// MLXR_SOFT_GPU=1: a software Vulkan driver (lavapipe in CI)
static int softGpu(void) {
    const char* v = getenv("MLXR_SOFT_GPU");
    return v && v[0] && strcmp(v, "0") != 0;
}

#define UNORM VK_FORMAT_R8G8B8A8_UNORM
#define SRGB VK_FORMAT_R8G8B8A8_SRGB
#define SHADER_READ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
#define COLOR_ATT VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL

static void check(int ok, const char* fmt, ...) {
    checks++;
    if (ok) return;
    failed++;
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "  FAIL: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("  ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static int to8(float x) {
    return (int)floorf(clampf(x, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static void ppm(const char* name, const uint8_t* rgba, uint32_t w, uint32_t h) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", outDir, name);
    vkh_write_ppm(path, rgba, w, h);
}

// ---- CPU samplers, mirroring GL/Vulkan bilinear with clamp to edge -------

static float texel8(const uint8_t* px, int w, int h, int comps, int c, int x, int y) {
    x = x < 0 ? 0 : (x >= w ? w - 1 : x);
    y = y < 0 ? 0 : (y >= h ? h - 1 : y);
    return px[((size_t)y * w + x) * comps + c] / 255.0f;
}

static float bilin8(const uint8_t* px, int w, int h, int comps, int c, float u, float v) {
    float x = u * w - 0.5f, y = v * h - 0.5f;
    float fx = floorf(x), fy = floorf(y);
    float ax = x - fx, ay = y - fy;
    int x0 = (int)fx, y0 = (int)fy;
    float a = texel8(px, w, h, comps, c, x0, y0), b = texel8(px, w, h, comps, c, x0 + 1, y0);
    float cc = texel8(px, w, h, comps, c, x0, y0 + 1), d = texel8(px, w, h, comps, c, x0 + 1, y0 + 1);
    return (a * (1 - ax) + b * ax) * (1 - ay) + (cc * (1 - ax) + d * ax) * ay;
}

// ---- video fixtures ------------------------------------------------------

typedef void (*FillFn)(float x, float y, float rgb[3]);

typedef struct {
    uint32_t w, h;
    uint8_t *Y, *U, *V;
    VkhImage y, u, v, nv12;
    VkImageView nv12View;
    MlxrVideo yuv, nv;
} Video;

static void yuvToRgb(float Y, float U, float V, float rgb[3]) {
    float y = (Y - 16.0f / 255.0f) * (255.0f / 219.0f);
    float u = (U - 128.0f / 255.0f) * (255.0f / 224.0f);
    float v = (V - 128.0f / 255.0f) * (255.0f / 224.0f);
    rgb[0] = y + 1.5748f * v;
    rgb[1] = y - 0.1873f * u - 0.4681f * v;
    rgb[2] = y + 1.8556f * u;
}

// What video.glsl computes at frame uv for a three plane video
static void videoCpu(const Video* vd, MlxrChroma chroma, float u, float v, float rgb[3]) {
    int cw = (int)(vd->w + 1) / 2, ch = (int)(vd->h + 1) / 2;
    float shift = chroma == MLXR_CHROMA_LEFT ? 0.25f / cw : 0.0f;
    float Y = bilin8(vd->Y, (int)vd->w, (int)vd->h, 1, 0, u, v);
    float U = bilin8(vd->U, cw, ch, 1, 0, u + shift, v);
    float V = bilin8(vd->V, cw, ch, 1, 0, u + shift, v);
    yuvToRgb(Y, U, V, rgb);
}

static int videoMake(Video* vd, uint32_t w, uint32_t h, FillFn fill) {
    memset(vd, 0, sizeof(*vd));
    vd->w = w;
    vd->h = h;
    uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    vd->Y = malloc((size_t)w * h);
    vd->U = malloc((size_t)cw * ch);
    vd->V = malloc((size_t)cw * ch);
    float* cb = calloc((size_t)cw * ch, sizeof(float));
    float* cr = calloc((size_t)cw * ch, sizeof(float));
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            float rgb[3];
            fill((x + 0.5f) / w, (y + 0.5f) / h, rgb);
            float l = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
            vd->Y[(size_t)y * w + x] = (uint8_t)to8((16.0f + 219.0f * l) / 255.0f);
            cb[(y / 2) * cw + x / 2] += 0.25f * (rgb[2] - l) / 1.8556f;
            cr[(y / 2) * cw + x / 2] += 0.25f * (rgb[0] - l) / 1.5748f;
        }
    for (size_t i = 0; i < (size_t)cw * ch; i++) {
        vd->U[i] = (uint8_t)to8((128.0f + 224.0f * cb[i]) / 255.0f);
        vd->V[i] = (uint8_t)to8((128.0f + 224.0f * cr[i]) / 255.0f);
    }
    free(cb);
    free(cr);
    if (vkh_image(&vk, w, h, VK_FORMAT_R8_UNORM, &vd->y) || vkh_image(&vk, cw, ch, VK_FORMAT_R8_UNORM, &vd->u) ||
        vkh_image(&vk, cw, ch, VK_FORMAT_R8_UNORM, &vd->v))
        return -1;
    vkh_upload(&vk, &vd->y, VK_IMAGE_ASPECT_COLOR_BIT, w, h, 1, vd->Y);
    vkh_upload(&vk, &vd->u, VK_IMAGE_ASPECT_COLOR_BIT, cw, ch, 1, vd->U);
    vkh_upload(&vk, &vd->v, VK_IMAGE_ASPECT_COLOR_BIT, cw, ch, 1, vd->V);
    vd->yuv.kind = MLXR_VIDEO_YUV420P;
    vd->yuv.planes[0] = vd->y.view;
    vd->yuv.planes[1] = vd->u.view;
    vd->yuv.planes[2] = vd->v.view;
    vd->yuv.width = w;
    vd->yuv.height = h;
    vd->yuv.chroma = MLXR_CHROMA_CENTER;
    if (nv12Ok) {
        if (vkh_image(&vk, w, h, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, &vd->nv12)) return -1;
        if (mlxr_passes_nv12_view(P, vd->nv12.image, &vd->nv12View) != VK_SUCCESS) return -1;
        uint8_t* uv = malloc((size_t)cw * ch * 2);
        for (size_t i = 0; i < (size_t)cw * ch; i++) {
            uv[2 * i] = vd->U[i];
            uv[2 * i + 1] = vd->V[i];
        }
        vkh_upload(&vk, &vd->nv12, VK_IMAGE_ASPECT_PLANE_0_BIT, w, h, 1, vd->Y);
        vkh_upload(&vk, &vd->nv12, VK_IMAGE_ASPECT_PLANE_1_BIT, cw, ch, 2, uv);
        free(uv);
        vd->nv = vd->yuv;
        vd->nv.kind = MLXR_VIDEO_NV12;
        vd->nv.planes[0] = vd->nv12View;
        vd->nv.planes[1] = vd->nv.planes[2] = VK_NULL_HANDLE;
    }
    return 0;
}

static void videoFree(Video* vd) {
    vkh_image_free(&vk, &vd->y);
    vkh_image_free(&vk, &vd->u);
    vkh_image_free(&vk, &vd->v);
    if (vd->nv12View) vkDestroyImageView(vk.device, vd->nv12View, NULL);
    vkh_image_free(&vk, &vd->nv12);
    free(vd->Y);
    free(vd->U);
    free(vd->V);
}

// Eight colour bars over the top half, a grey ramp under them
static const float BARS[8][3] = {{0.75f, 0.75f, 0.75f}, {0.75f, 0.75f, 0.0f}, {0.0f, 0.75f, 0.75f},
                                 {0.0f, 0.75f, 0.0f},   {0.75f, 0.0f, 0.75f}, {0.75f, 0.0f, 0.0f},
                                 {0.0f, 0.0f, 0.75f},   {0.1f, 0.1f, 0.1f}};
static void fillBars(float x, float y, float rgb[3]) {
    if (y < 0.5f) {
        memcpy(rgb, BARS[(int)(x * 8.0f) & 7], sizeof(BARS[0]));
    } else {
        rgb[0] = rgb[1] = rgb[2] = x;
    }
}

// Smooth in both axes, colour included, so chroma filtering differences
// stay small
static void fillSmooth(float x, float y, float rgb[3]) {
    rgb[0] = 0.15f + 0.7f * x;
    rgb[1] = 0.2f + 0.6f * (0.5f + 0.5f * sinf(3.0f * x + 2.0f * y));
    rgb[2] = 0.1f + 0.8f * y;
}

static void fillGrey(float x, float y, float rgb[3]) {
    rgb[0] = rgb[1] = rgb[2] = 0.5f;
}

// Fine detail for the box filters: a 3 px period pattern over a gradient
static void fillDetail(float x, float y, float rgb[3]) {
    float s = (((int)(x * 256.0f) + (int)(y * 128.0f)) % 3) * 0.25f;
    rgb[0] = clampf(0.2f + 0.5f * x + s * 0.3f, 0, 1);
    rgb[1] = clampf(0.3f + 0.4f * y + s * 0.2f, 0, 1);
    rgb[2] = clampf(0.6f - 0.4f * x + s * 0.1f, 0, 1);
}

// ---- pass helpers --------------------------------------------------------

typedef struct {
    VkhImage img;
    MlxrTarget t;
} Target;

static int targetMake(Target* t, uint32_t w, uint32_t h, VkFormat fmt) {
    memset(t, 0, sizeof(*t));
    if (vkh_image(&vk, w, h, fmt, &t->img)) return -1;
    t->t.view = t->img.view;
    t->t.format = fmt;
    t->t.rect = (VkRect2D){{0, 0}, {w, h}};
    return 0;
}

static void targetFree(Target* t) {
    vkh_image_free(&vk, &t->img);
}

static uint8_t* readTarget(Target* t) {
    uint8_t* px = malloc((size_t)t->img.width * t->img.height * 4);
    vkh_read(&vk, &t->img, px);
    return px;
}

// Uploads RGBA8 pixels into a fresh sampled image
static int textureMake(VkhImage* img, uint32_t w, uint32_t h, const uint8_t* rgba) {
    if (vkh_image(&vk, w, h, UNORM, img)) return -1;
    return vkh_upload(&vk, img, VK_IMAGE_ASPECT_COLOR_BIT, w, h, 4, rgba);
}

static MlxrWarpParams warpDefaults(const MlxrVideo* v) {
    MlxrWarpParams w;
    memset(&w, 0, sizeof(w));
    w.video = *v;
    w.convergence = 0.5f;
    w.lowResWidth = (float)(v->width / 4);
    w.srcInset = 0.5f / (float)v->width;
    w.edgeFade = 16.0f / (float)v->width;
    w.grade[0] = 0.0f;
    w.grade[1] = 1.0f;
    w.grade[2] = 1.0f;
    w.grade[3] = 1.0f;
    w.tint[0] = w.tint[1] = w.tint[2] = 1.0f;
    return w;
}

static uint8_t* runWarp(Target* t, const MlxrWarpParams* w) {
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t->img, COLOR_ATT);
    mlxr_pass_warp(P, cmd, &t->t, w);
    vkh_layout(&vk, &t->img, SHADER_READ);
    if (vkh_submit(&vk)) {
        check(0, "warp submit");
        return calloc((size_t)t->img.width * t->img.height, 4);
    }
    return readTarget(t);
}

static int maxDiff(const uint8_t* a, const uint8_t* b, size_t n, int stride, size_t* where) {
    int m = 0;
    for (size_t i = 0; i < n; i++)
        for (int c = 0; c < 3; c++) {
            int d = abs((int)a[i * stride + c] - (int)b[i * stride + c]);
            if (d > m) {
                m = d;
                if (where) *where = i;
            }
        }
    return m;
}

static void gradeOf(int bright, int contrast, int gamma, int sat, PictureGrade* g) {
    int units[PICTURE_VALUES];
    units[PICTURE_BRIGHTNESS] = bright;
    units[PICTURE_CONTRAST] = contrast;
    units[PICTURE_GAMMA] = gamma;
    units[PICTURE_SATURATION] = sat;
    *g = pictureGradeFor(units);
}

// Pixels of the bars fixture whose chroma is exact: away from bar edges and
// the seam between the halves
static int barsExact(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    uint32_t bar = w / 8, mx = x % bar;
    if (y + 3 >= h / 2 && y <= h / 2 + 2) return 0;
    if (y < h / 2 && (mx < 3 || mx + 3 >= bar)) return 0;
    return 1;
}

// ---- tests ---------------------------------------------------------------

static void testDevice(void) {
    note("device: %s, api %u.%u, maxPushConstantsSize %u", vk.props.deviceName,
         VK_API_VERSION_MAJOR(vk.props.apiVersion), VK_API_VERSION_MINOR(vk.props.apiVersion),
         vk.props.limits.maxPushConstantsSize);
    if (nv12Ok) {
        VkFilter f;
        VkChromaLocation x, y;
        mlxr_passes_nv12_caps(P, &f, &x, &y);
        note("nv12 conversion: chroma filter %s, x %s, y %s", f == VK_FILTER_LINEAR ? "linear" : "nearest",
             x == VK_CHROMA_LOCATION_COSITED_EVEN ? "cosited" : "midpoint",
             y == VK_CHROMA_LOCATION_COSITED_EVEN ? "cosited" : "midpoint");
    }
    check(nv12Ok, "NV12 path available");
    struct {
        MlxrPassId pass;
        VkFormat color, depth;
    } combos[] = {
        {MLXR_PASS_WARP, SRGB, 0},          {MLXR_PASS_WARP, UNORM, 0},      {MLXR_PASS_WARP, VK_FORMAT_B8G8R8A8_SRGB, 0},
        {MLXR_PASS_UPSAMPLE, UNORM, 0},     {MLXR_PASS_OFFSET, UNORM, 0},    {MLXR_PASS_DOWNSCALE, UNORM, 0},
        {MLXR_PASS_AMBI, UNORM, 0},         {MLXR_PASS_GLOW_EDGE, UNORM, 0}, {MLXR_PASS_GLOW, SRGB, 0},
        {MLXR_PASS_ROOM, SRGB, VK_FORMAT_D32_SFLOAT}, {MLXR_PASS_MODEL, SRGB, VK_FORMAT_D32_SFLOAT},
        {MLXR_PASS_SUPERSAMPLE, SRGB, 0},   {MLXR_PASS_PANEL, SRGB, 0},
    };
    for (size_t i = 0; i < sizeof(combos) / sizeof(combos[0]); i++) {
        check(mlxr_passes_warm(P, combos[i].pass, MLXR_VIDEO_YUV420P, combos[i].color, combos[i].depth) == 0,
              "pipeline %d format %d", combos[i].pass, combos[i].color);
        if (nv12Ok && (combos[i].pass == MLXR_PASS_WARP || combos[i].pass == MLXR_PASS_UPSAMPLE ||
                       combos[i].pass == MLXR_PASS_DOWNSCALE || combos[i].pass == MLXR_PASS_AMBI))
            check(mlxr_passes_warm(P, combos[i].pass, MLXR_VIDEO_NV12, combos[i].color, combos[i].depth) == 0,
                  "nv12 pipeline %d", combos[i].pass);
    }
}

// The colour pass: video sampling and the picture grade against
// pictureGradeApply
static void testGrade(void) {
    const uint32_t W = 256, H = 128;
    Video vd;
    Target t;
    if (videoMake(&vd, W, H, fillBars) || targetMake(&t, W, H, UNORM)) {
        check(0, "fixture");
        return;
    }
    MlxrWarpParams w = warpDefaults(&vd.yuv);
    uint8_t* plain = runWarp(&t, &w);
    ppm("grade_plain", plain, W, H);
    // Ungraded against the CPU conversion
    int worst = 0;
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            if (!barsExact(x, y, W, H)) continue;
            float rgb[3];
            videoCpu(&vd, MLXR_CHROMA_CENTER, (x + 0.5f) / W, (y + 0.5f) / H, rgb);
            for (int c = 0; c < 3; c++) {
                int d = abs(to8(rgb[c]) - plain[((size_t)y * W + x) * 4 + c]);
                if (d > worst) worst = d;
            }
        }
    note("ungraded vs CPU BT.709 limited: max diff %d/255", worst);
    check(worst <= 1, "ungraded video within 1/255 of CPU (%d)", worst);

    // The neutral grade is the picture as streamed
    PictureGrade g;
    gradeOf(PICTURE_BRIGHTNESS_DEFAULT, PICTURE_CONTRAST_DEFAULT, PICTURE_GAMMA_DEFAULT, PICTURE_SATURATION_DEFAULT,
            &g);
    w.gradeOn = 1;
    memcpy(w.grade, &g, sizeof(w.grade));
    uint8_t* neutral = runWarp(&t, &w);
    int d = maxDiff(plain, neutral, (size_t)W * H, 4, NULL);
    note("neutral grade vs off: max diff %d/255", d);
    check(d <= 1, "grade identity within 1/255 (%d)", d);
    free(neutral);

    // Moves against xr_grade's CPU maths
    const int cases[][4] = {{20, 100, 100, 100}, {-30, 100, 100, 100}, {0, 140, 100, 100}, {0, 60, 100, 100},
                            {0, 100, 70, 100},   {0, 100, 180, 100},   {0, 100, 100, 0},   {0, 100, 100, 190},
                            {15, 125, 85, 130},  {-50, 150, 200, 200}};
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        gradeOf(cases[k][0], cases[k][1], cases[k][2], cases[k][3], &g);
        memcpy(w.grade, &g, sizeof(w.grade));
        uint8_t* got = runWarp(&t, &w);
        int m = 0;
        for (uint32_t y = 0; y < H; y++)
            for (uint32_t x = 0; x < W; x++) {
                if (!barsExact(x, y, W, H)) continue;
                float rgb[3], out[3];
                videoCpu(&vd, MLXR_CHROMA_CENTER, (x + 0.5f) / W, (y + 0.5f) / H, rgb);
                pictureGradeApply(&g, rgb, out);
                for (int c = 0; c < 3; c++) {
                    int e = abs(to8(out[c]) - got[((size_t)y * W + x) * 4 + c]);
                    if (e > m) m = e;
                }
            }
        note("grade b%d c%d g%d s%d vs pictureGradeApply: max diff %d/255", cases[k][0], cases[k][1], cases[k][2],
             cases[k][3], m);
        check(m <= 1, "grade case %zu within 1/255 (%d)", k, m);
        if (k == 8) ppm("grade_b15_c125_g85_s130", got, W, H);
        free(got);
    }

    // The tint multiplies after the grade (the eye test's red and blue)
    w.gradeOn = 0;
    w.tint[0] = 1.0f;
    w.tint[1] = 0.2f;
    w.tint[2] = 0.2f;
    uint8_t* tinted = runWarp(&t, &w);
    // Against the CPU's unclamped colour: the conversion can overshoot 1,
    // and the tint scales the overshoot before the write clamps it
    int tm = 0;
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            if (!barsExact(x, y, W, H)) continue;
            float rgb[3];
            videoCpu(&vd, MLXR_CHROMA_CENTER, (x + 0.5f) / W, (y + 0.5f) / H, rgb);
            for (int c = 0; c < 3; c++) {
                int e = abs(tinted[((size_t)y * W + x) * 4 + c] - to8(rgb[c] * w.tint[c]));
                if (e > tm) tm = e;
            }
        }
    check(tm <= 1, "tint within 1/255 (%d)", tm);
    free(tinted);
    free(plain);
    targetFree(&t);
    videoFree(&vd);
}

// Three planes and NV12 through the conversion give the same RGB
static void testNv12(void) {
    if (!nv12Ok) {
        check(0, "no NV12 path");
        return;
    }
    VkChromaLocation cx;
    mlxr_passes_nv12_caps(P, NULL, &cx, NULL);
    MlxrChroma siting = cx == VK_CHROMA_LOCATION_COSITED_EVEN ? MLXR_CHROMA_LEFT : MLXR_CHROMA_CENTER;
    const uint32_t W = 256, H = 128;
    FillFn fills[2] = {fillSmooth, fillBars};
    const char* names[2] = {"smooth", "bars"};
    for (int f = 0; f < 2; f++) {
        Video vd;
        Target t;
        if (videoMake(&vd, W, H, fills[f]) || targetMake(&t, W, H, UNORM)) {
            check(0, "fixture");
            return;
        }
        vd.yuv.chroma = siting;
        MlxrWarpParams w = warpDefaults(&vd.yuv);
        uint8_t* three = runWarp(&t, &w);
        w.video = vd.nv;
        uint8_t* nv = runWarp(&t, &w);
        int all = 0, exact = 0;
        for (uint32_t y = 0; y < H; y++)
            for (uint32_t x = 0; x < W; x++)
                for (int c = 0; c < 3; c++) {
                    size_t i = ((size_t)y * W + x) * 4 + c;
                    int d = abs((int)three[i] - (int)nv[i]);
                    if (d > all) all = d;
                    if ((f == 0 || barsExact(x, y, W, H)) && d > exact) exact = d;
                }
        note("%s: three-plane vs NV12 ycbcr max diff %d/255 (%d/255 away from chroma edges)", names[f], all, exact);
        check(exact <= nv12Tol, "%s: NV12 and three-plane within %d/255 (%d)", names[f], nv12Tol, exact);
        char name[64];
        snprintf(name, sizeof(name), "nv12_%s", names[f]);
        ppm(name, nv, W, H);
        snprintf(name, sizeof(name), "yuv420p_%s", names[f]);
        ppm(name, three, W, H);

        // The other video passes take the same path
        if (f == 0) {
            Target a;
            targetMake(&a, MLXR_AMBI_TEX, MLXR_AMBI_TEX, UNORM);
            MlxrAmbiParams ap = {vd.yuv, {0, 0, 1, 1}, 0, {0, 1, 1, 1}, 1.0f};
            uint8_t* res[2];
            for (int k = 0; k < 2; k++) {
                ap.video = k ? vd.nv : vd.yuv;
                VkCommandBuffer cmd = vkh_begin(&vk);
                vkh_layout(&vk, &a.img, COLOR_ATT);
                mlxr_pass_ambi(P, cmd, &a.t, &ap);
                vkh_submit(&vk);
                res[k] = readTarget(&a);
            }
            int d = maxDiff(res[0], res[1], MLXR_AMBI_TEX * MLXR_AMBI_TEX, 4, NULL);
            note("ambi sample three-plane vs NV12: max diff %d/255", d);
            check(d <= nv12Tol, "ambi NV12 within %d/255 (%d)", nv12Tol, d);
            free(res[0]);
            free(res[1]);
            targetFree(&a);
        }
        free(three);
        free(nv);
        targetFree(&t);
        videoFree(&vd);
    }
}

// An _SRGB view with OUT_LINEAR stores the bytes a _UNORM view stores
static void testSrgb(void) {
    const uint32_t W = 256, H = 128;
    Video vd;
    if (videoMake(&vd, W, H, fillBars)) return;
    Target u, s;
    targetMake(&u, W, H, UNORM);
    memset(&s, 0, sizeof(s));
    vkh_image_mutable(&vk, W, H, UNORM, SRGB, &s.img);
    s.t.view = s.img.view;
    s.t.format = SRGB;
    s.t.rect = (VkRect2D){{0, 0}, {W, H}};
    MlxrWarpParams w = warpDefaults(&vd.yuv);
    uint8_t* a = runWarp(&u, &w);
    uint8_t* b = runWarp(&s, &w);
    int d = maxDiff(a, b, (size_t)W * H, 4, NULL);
    note("warp through an SRGB view vs a UNORM view: max byte diff %d", d);
    check(d <= 1, "SRGB view bytes within 1 of UNORM (%d)", d);
    free(a);
    free(b);

    // The glow is premultiplied: its encoded bytes must survive too
    uint8_t px[MLXR_AMBI_TEX * MLXR_AMBI_TEX * 4];
    for (int i = 0; i < MLXR_AMBI_TEX * MLXR_AMBI_TEX; i++) {
        px[i * 4 + 0] = (uint8_t)(i * 7);
        px[i * 4 + 1] = (uint8_t)(i * 3);
        px[i * 4 + 2] = (uint8_t)(255 - i);
        px[i * 4 + 3] = 255;
    }
    VkhImage tex;
    textureMake(&tex, MLXR_AMBI_TEX, MLXR_AMBI_TEX, px);
    Target gu, gs;
    targetMake(&gu, MLXR_GLOW_TEX, MLXR_GLOW_TEX, UNORM);
    memset(&gs, 0, sizeof(gs));
    vkh_image_mutable(&vk, MLXR_GLOW_TEX, MLXR_GLOW_TEX, UNORM, SRGB, &gs.img);
    gs.t.view = gs.img.view;
    gs.t.format = SRGB;
    gs.t.rect = gu.t.rect;
    MlxrGlowParams gp = {tex.view, 0.8f, 0};
    uint8_t* res[2];
    Target* ts[2] = {&gu, &gs};
    for (int k = 0; k < 2; k++) {
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkh_layout(&vk, &ts[k]->img, COLOR_ATT);
        mlxr_pass_glow(P, cmd, &ts[k]->t, &gp);
        vkh_submit(&vk);
        res[k] = readTarget(ts[k]);
    }
    int dg = 0;
    for (size_t i = 0; i < (size_t)MLXR_GLOW_TEX * MLXR_GLOW_TEX * 4; i++) {
        int e = abs((int)res[0][i] - (int)res[1][i]);
        if (e > dg) dg = e;
    }
    note("glow (premultiplied) through SRGB vs UNORM view: max byte diff %d", dg);
    check(dg <= 1, "glow SRGB bytes within 1 (%d)", dg);
    free(res[0]);
    free(res[1]);
    vkh_image_free(&vk, &tex);
    targetFree(&gu);
    targetFree(&gs);
    targetFree(&u);
    targetFree(&s);
    videoFree(&vd);
}

// Builds the guide+depth map (RGBA8: rgb guide, a depth) for a video by
// averaging its CPU colour over each map texel's footprint
typedef float (*DepthFn)(float x, float y);
static uint8_t* depthMap(const Video* vd, uint32_t mw, uint32_t mh, DepthFn depth) {
    uint8_t* px = malloc((size_t)mw * mh * 4);
    for (uint32_t y = 0; y < mh; y++)
        for (uint32_t x = 0; x < mw; x++) {
            float rgb[3];
            videoCpu(vd, MLXR_CHROMA_CENTER, (x + 0.5f) / mw, (y + 0.5f) / mh, rgb);
            uint8_t* p = px + ((size_t)y * mw + x) * 4;
            for (int c = 0; c < 3; c++) p[c] = (uint8_t)to8(rgb[c]);
            p[3] = (uint8_t)to8(depth((x + 0.5f) / mw, (y + 0.5f) / mh));
        }
    return px;
}

static float depthStep(float x, float y) {
    return x < 0.5f ? 0.2f : 0.9f;
}
static float depthWobble(float x, float y) {
    return 0.5f + 0.4f * sinf(9.0f * x) * cosf(5.0f * y);
}

static float depthHalf(float x, float y) {
    return 128.0f / 255.0f;
}

static void fillStep(float x, float y, float rgb[3]) {
    float v = x < 0.5f ? 0.15f : 0.85f;
    rgb[0] = rgb[1] = rgb[2] = v;
}

static void fillPattern(float x, float y, float rgb[3]) {
    rgb[0] = 0.5f + 0.4f * sinf(40.0f * x);
    rgb[1] = 0.5f + 0.4f * sinf(23.0f * y + 10.0f * x);
    rgb[2] = x < 0.5f ? 0.2f : 0.8f;
}

typedef struct {
    VkhImage map;
    Target up, off;
} Depth3d;

// Upsample and offset search for a map, the way renderVideoFrame chains
// them; leaves both sampled
static void depthChain(Depth3d* d, const Video* vd, uint32_t mw, uint32_t mh, const uint8_t* map, float sep,
                       float conv, float sharp) {
    textureMake(&d->map, mw, mh, map);
    targetMake(&d->up, vd->w / 4, vd->h / 4, UNORM);
    targetMake(&d->off, vd->w / 4, vd->h / 4, UNORM);
    MlxrUpsampleParams up = {vd->yuv, d->map.view, mw, mh, 0.1f, sharp};
    MlxrOffsetParams op = {d->up.img.view, sep * (float)(vd->w / 4), conv};
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &d->up.img, COLOR_ATT);
    vkh_layout(&vk, &d->off.img, COLOR_ATT);
    mlxr_pass_upsample(P, cmd, &d->up.t, &up);
    vkh_layout(&vk, &d->up.img, SHADER_READ);
    mlxr_pass_offset(P, cmd, &d->off.t, &op);
    vkh_layout(&vk, &d->off.img, SHADER_READ);
    check(vkh_submit(&vk) == 0, "depth chain submit");
}

static void depthFree(Depth3d* d) {
    vkh_image_free(&vk, &d->map);
    targetFree(&d->up);
    targetFree(&d->off);
}

// Zero separation is the passthrough, through either warp path
static void testWarpIdentity(void) {
    const uint32_t W = 512, H = 64, MW = 128, MH = 16;
    Video vd;
    Target t;
    if (videoMake(&vd, W, H, fillPattern) || targetMake(&t, W, H, UNORM)) return;
    uint8_t* map = depthMap(&vd, MW, MH, depthWobble);
    Depth3d d;
    depthChain(&d, &vd, MW, MH, map, 0.0f, 0.5f, 1.0f);
    MlxrWarpParams w = warpDefaults(&vd.yuv);
    uint8_t* mono = runWarp(&t, &w);
    w.depth = d.up.img.view;
    w.offsets = d.off.img.view;
    for (int eye = 0; eye < 2; eye++) {
        w.eye = eye;
        w.dispTexels = 0.0f;
        w.disparity = 0.0f;
        w.occlusion = 0;
        uint8_t* plain = runWarp(&t, &w);
        int a = maxDiff(mono, plain, (size_t)W * H, 4, NULL);
        w.occlusion = 1;
        uint8_t* occ = runWarp(&t, &w);
        int b = maxDiff(mono, occ, (size_t)W * H, 4, NULL);
        w.depthCubic = 1;
        uint8_t* cub = runWarp(&t, &w);
        int c = maxDiff(mono, cub, (size_t)W * H, 4, NULL);
        w.depthCubic = 0;
        note("eye %d, separation 0: gather %d/255, occlusion %d/255, occlusion+cubic %d/255 from mono", eye, a, b, c);
        check(a == 0, "zero separation gather is identity (%d)", a);
        check(b <= 1, "zero separation occlusion warp is identity (%d)", b);
        check(c <= 1, "zero separation cubic depth is identity (%d)", c);
        free(plain);
        free(occ);
        free(cub);
    }
    free(mono);
    free(map);
    depthFree(&d);
    targetFree(&t);
    videoFree(&vd);
}

// The inverse warp done the slow way on the CPU from the same depth (the
// upsample's row, read bilinearly as the shader reads it): every source
// position whose shifted landing crosses this pixel, nearest surface wins.
// Returns the source luma per output pixel.
static void warpRowCpu(const float* dRow, int uw, const Video* vd, int W, float disp, float conv, float edgeFade,
                       float* out) {
    for (int x = 0; x < W; x++) {
        float v = (x + 0.5f) / W;
        float edgeW = clampf(fminf(v, 1.0f - v) / edgeFade, 0, 1);
        edgeW = edgeW * edgeW * (3 - 2 * edgeW);
        float dsp = edgeW * disp;
        float bestD = -1, bestTc = v;
        float step = 1.0f / (W * 64.0f);
        float pt = v - 0.08f, pe = 0, pd = 0;
        for (int i = 0; i <= (int)(0.16f / step); i++) {
            float tc = v - 0.08f + i * step;
            float lx = clampf(tc, 0, 1) * uw - 0.5f;
            int x0 = (int)floorf(lx);
            float f = lx - x0;
            float d0 = dRow[x0 < 0 ? 0 : (x0 >= uw ? uw - 1 : x0)];
            float d1 = dRow[x0 + 1 < 0 ? 0 : (x0 + 1 >= uw ? uw - 1 : x0 + 1)];
            float d = d0 + (d1 - d0) * f;
            float e = (tc - v) + dsp * (d - conv);
            if (i > 0 && pe * e <= 0 && pe != e) {
                float t = pe / (pe - e);
                float dd = pd + (d - pd) * t;
                if (dd > bestD) {
                    bestD = dd;
                    bestTc = pt + (tc - pt) * t;
                }
            }
            pt = tc;
            pe = e;
            pd = d;
        }
        float rgb[3];
        bestTc = clampf(bestTc, 0.5f / W, 1.0f - 0.5f / W);
        videoCpu(vd, MLXR_CHROMA_CENTER, bestTc, 0.5f, rgb);
        out[x] = rgb[0];
    }
}

static float edgeOfRow(const float* row, int from, int to) {
    float mid = 0.5f * (0.15f + 0.85f);
    for (int x = from; x + 1 < to; x++)
        if (row[x] < mid && row[x + 1] >= mid) return x + (mid - row[x]) / (row[x + 1] - row[x]) + 0.5f;
    return -1.0f;
}

static float edgeAtMid(const uint8_t* px, uint32_t w, uint32_t h) {
    float row[1024];
    for (uint32_t x = 0; x < w; x++) row[x] = px[((size_t)(h / 2) * w + x) * 4] / 255.0f;
    return edgeOfRow(row, 64, (int)w - 64);
}

// A near object (right half) over a far background. The occlusion aware
// warp matches the slow CPU inversion of the same depth: the edge lands
// where it should in each eye, the left eye's disocclusion is filled from
// the background side, and the right eye's object covers the background.
// The plain gather (depth read at the destination) ghosts the object.
static void testWarpStep(void) {
    const uint32_t W = 512, H = 64, MW = 128, MH = 16, UW = W / 4;
    const float sep = 0.04f, conv = 0.5f;
    Video vd;
    Target t;
    if (videoMake(&vd, W, H, fillStep) || targetMake(&t, W, H, UNORM)) return;
    uint8_t* map = depthMap(&vd, MW, MH, depthStep);
    Depth3d d;
    depthChain(&d, &vd, MW, MH, map, sep, conv, 1.0f);
    uint8_t* up = readTarget(&d.up);
    float dRow[512];
    for (uint32_t x = 0; x < UW; x++) dRow[x] = up[((size_t)(H / 8) * UW + x) * 4 + 3] / 255.0f;
    note("upsampled depth around the edge: %.3f %.3f | %.3f %.3f", dRow[UW / 2 - 2], dRow[UW / 2 - 1], dRow[UW / 2],
         dRow[UW / 2 + 1]);
    MlxrWarpParams w = warpDefaults(&vd.yuv);
    w.depth = d.up.img.view;
    w.offsets = d.off.img.view;
    w.convergence = conv;
    w.dispTexels = sep * (float)UW;
    float near8 = dRow[UW - 20], far8 = dRow[20];
    float objShift = sep * (near8 - conv) * W, bgShift = sep * (conv - far8) * W;
    note("object moves %.2f px, background %.2f px the other way", objShift, bgShift);
    uint8_t* eyes[2];
    float cpuRow[512], gpuRow[512];
    for (int eye = 0; eye < 2; eye++) {
        float s = eye == 0 ? 1.0f : -1.0f;
        w.eye = eye;
        w.disparity = s * sep;
        w.occlusion = 1;
        uint8_t* got = eyes[eye] = runWarp(&t, &w);
        warpRowCpu(dRow, UW, &vd, W, s * sep, conv, w.edgeFade, cpuRow);
        float want = edgeOfRow(cpuRow, 64, W - 64);
        int bad = 0;
        float worst = 0;
        for (uint32_t y = 0; y < H; y++) {
            for (uint32_t x = 0; x < W; x++) gpuRow[x] = got[((size_t)y * W + x) * 4] / 255.0f;
            float e = edgeOfRow(gpuRow, 64, W - 64);
            worst = fmaxf(worst, fabsf(e - want));
            // At a fold (the right eye here) the low resolution search hands
            // the Newton steps a blend of two surfaces' offsets, and the
            // nearer one can reach a pixel past where it truly lands: the
            // fold boundary case tools/xr/warp_lab.py describes. 1.5 px.
            if (fabsf(e - want) > 1.5f) bad++;
        }
        note("eye %d occlusion warp: edge %.2f px from the source edge, CPU inversion %.2f, worst row off by %.2f px",
             eye, (edgeAtMid(got, W, H) - W / 2.0f), want - W / 2.0f, worst);
        check(bad == 0, "eye %d: edge within 1.5 px of the CPU inversion on every row (%d rows off)", eye, bad);
        // Everything away from the edge matches the CPU inversion
        int off = 0;
        for (uint32_t x = 8; x + 8 < W; x++) {
            if (fabsf((float)x - want) < 3.0f) continue;
            if (abs(got[((size_t)(H / 2) * W + x) * 4] - to8(cpuRow[x])) > 3) off++;
        }
        check(off == 0, "eye %d: the rest of the row matches the CPU inversion (%d px)", eye, off);
        // Left eye: between where the background edge went and the object's
        // edge, only background shows
        uint32_t lo = (uint32_t)(W / 2.0f - bgShift) - 2, hi = (uint32_t)(want - 2.0f);
        if (eye == 0) {
            int bright = 0;
            for (uint32_t y = 0; y < H; y++)
                for (uint32_t x = lo; x <= hi; x++)
                    if (got[((size_t)y * W + x) * 4] > 128) bright++;
            note("left eye disocclusion %u..%u: %d object pixels", lo, hi, bright);
            check(bright == 0, "disocclusion filled from the background (%d object pixels)", bright);
        } else {
            // Right eye: the object moved over the background, and its edge
            // is at least the full object shift left of where it was
            check(want <= W / 2.0f - objShift + 2.5f, "right eye: object covers the background (%.2f)", want);
        }
        // The plain gather for the record
        w.occlusion = 0;
        uint8_t* naive = runWarp(&t, &w);
        int ghost = 0;
        for (uint32_t x = lo; x <= hi; x++)
            if (naive[((size_t)(H / 2) * W + x) * 4] > 128) ghost++;
        note("eye %d plain gather: %d object pixels on the middle row of %u..%u", eye, ghost, lo, hi);
        if (eye == 0) {
            check(ghost > 0, "the fixture tells the two warps apart (plain gather ghost %d px)", ghost);
            ppm("warp_step_gather_left", naive, W, H);
        }
        free(naive);
    }
    ppm("warp_step_left", eyes[0], W, H);
    ppm("warp_step_right", eyes[1], W, H);
    free(up);

    // A side by side for looking at: the pattern fixture with a wobbly map
    Video pv;
    videoMake(&pv, W, 128, fillPattern);
    uint8_t* pmap = depthMap(&pv, MW, 32, depthWobble);
    Depth3d pd;
    depthChain(&pd, &pv, MW, 32, pmap, 0.05f, 0.5f, 1.0f);
    Target dbl;
    targetMake(&dbl, 2 * W, 128, UNORM);
    MlxrWarpParams pw = warpDefaults(&pv.yuv);
    pw.depth = pd.up.img.view;
    pw.offsets = pd.off.img.view;
    pw.dispTexels = 0.05f * (W / 4);
    pw.occlusion = 1;
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &dbl.img, COLOR_ATT);
    for (int eye = 0; eye < 2; eye++) {
        pw.eye = eye;
        pw.disparity = eye == 0 ? 0.05f : -0.05f;
        dbl.t.rect = (VkRect2D){{(int32_t)(eye * W), 0}, {W, 128}};
        mlxr_pass_warp(P, cmd, &dbl.t, &pw);
    }
    vkh_submit(&vk);
    uint8_t* sbs = readTarget(&dbl);
    ppm("warp_pattern_sbs", sbs, 2 * W, 128);
    int differ = 0;
    for (uint32_t y = 0; y < 128; y++)
        for (uint32_t x = 0; x < W; x++)
            if (abs(sbs[((size_t)y * 2 * W + x) * 4] - sbs[((size_t)y * 2 * W + W + x) * 4]) > 8) differ++;
    check(differ > 1000, "stereo halves differ (%d px)", differ);
    free(sbs);
    targetFree(&dbl);
    depthFree(&pd);
    free(pmap);
    videoFree(&pv);

    free(eyes[0]);
    free(eyes[1]);
    free(map);
    depthFree(&d);
    targetFree(&t);
    videoFree(&vd);
}

// PLAN 4.3: a host-rendered stereo pair. Left half red and right half blue
// (side by side), or top red and bottom blue (top and bottom): each eye's
// warp reads only its own half, over the whole target and with nothing of
// the other half at the seam; an eye given the other half (what eye_swap
// does) reads the other colour; the depth inputs and a disparity are not
// read; the convergence shift moves the picture the right way.
static const float SBS_RED[3] = {0.8f, 0.1f, 0.1f};
static const float SBS_BLUE[3] = {0.1f, 0.15f, 0.8f};
static void fillSbs(float x, float y, float rgb[3]) {
    memcpy(rgb, x < 0.5f ? SBS_RED : SBS_BLUE, sizeof(SBS_RED));
}
static void fillTb(float x, float y, float rgb[3]) {
    memcpy(rgb, y < 0.5f ? SBS_RED : SBS_BLUE, sizeof(SBS_RED));
}
// The same grey ramp across each half, for the shift
static void fillSbsRamp(float x, float y, float rgb[3]) {
    float u = x < 0.5f ? 2.0f * x : 2.0f * x - 1.0f;
    rgb[0] = rgb[1] = rgb[2] = 0.1f + 0.8f * u;
}

// The largest channel difference of any pixel from rgb (0..1), and where
static int solidDiff(const uint8_t* px, uint32_t w, uint32_t h, const float rgb[3], uint32_t* atX) {
    int m = 0;
    *atX = 0;
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) {
                int d = abs((int)px[((size_t)y * w + x) * 4 + c] - to8(rgb[c]));
                if (d > m) {
                    m = d;
                    *atX = x;
                }
            }
    return m;
}

static float rowMeanLuma(const uint8_t* px, uint32_t w, uint32_t y, uint32_t x0, uint32_t x1) {
    float s = 0.0f;
    for (uint32_t x = x0; x < x1; x++) s += px[((size_t)y * w + x) * 4 + 1];
    return s / (float)(x1 - x0) / 255.0f;
}

static void testWarpSbs(void) {
    const uint32_t W = 256, H = 64;
    const char* layoutName[2] = {"side by side", "top-bottom"};
    for (int layout = 0; layout < 2; layout++) {
        Video vd;
        Target t;
        // A full size eye: half the frame's width (side by side) or height
        uint32_t ew = layout ? W : W / 2, eh = layout ? H / 2 : H;
        if (videoMake(&vd, W, H, layout ? fillTb : fillSbs) || targetMake(&t, ew, eh, UNORM)) {
            check(0, "sbs fixture");
            return;
        }
        // The software decoder's siting, the one that leans across the seam
        vd.yuv.chroma = MLXR_CHROMA_LEFT;
        uint8_t* ramp = malloc((size_t)W * H * 4);
        for (size_t i = 0; i < (size_t)W * H * 4; i++) ramp[i] = (uint8_t)(i * 37 % 251);
        VkhImage noise;
        textureMake(&noise, W / 4, H / 4, ramp);
        free(ramp);
        for (int k = 0; k < (nv12Ok ? 2 : 1); k++) {
            const MlxrVideo* v = k ? &vd.nv : &vd.yuv;
            int tol = k ? nv12Tol + 1 : 2;
            for (int half = 0; half < 2; half++) {
                const float* want = half == 0 ? SBS_RED : SBS_BLUE;
                MlxrWarpParams w = warpDefaults(v);
                w.split = layout ? MLXR_SPLIT_TB : MLXR_SPLIT_SBS;
                w.half = half;
                w.eye = half;
                uint32_t at;
                uint8_t* px = runWarp(&t, &w);
                int d = solidDiff(px, ew, eh, want, &at);
                note("%s %s, eye %d reads half %d: max diff %d/255 from %s (column %u)", layoutName[layout],
                     k ? "NV12" : "three-plane", half, half, d, half ? "blue" : "red", at);
                check(d <= tol, "%s %s: eye %d is all its own half's colour, seam included (%d/255 at column %u)",
                      layoutName[layout], k ? "NV12" : "three-plane", half, d, at);
                char name[64];
                snprintf(name, sizeof(name), "sbs_%s_%s_eye%d", layout ? "tb" : "lr", k ? "nv12" : "yuv", half);
                ppm(name, px, ew, eh);
                free(px);
                // eye_swap: the left eye shown the right half
                w.eye = 1 - half;
                px = runWarp(&t, &w);
                d = solidDiff(px, ew, eh, want, &at);
                check(d <= tol, "%s %s: swapped, eye %d reads half %d (%d/255)", layoutName[layout],
                      k ? "NV12" : "three-plane", 1 - half, half, d);
                free(px);
                // Depth and disparity are not read with a split
                w.eye = half;
                w.depth = noise.view;
                w.offsets = noise.view;
                w.disparity = half ? -0.02f : 0.02f;
                w.dispTexels = 4.0f;
                w.occlusion = 1;
                px = runWarp(&t, &w);
                d = solidDiff(px, ew, eh, want, &at);
                check(d <= tol, "%s %s: eye %d ignores the depth and the disparity (%d/255)", layoutName[layout],
                      k ? "NV12" : "three-plane", half, d);
                free(px);
            }
        }
        vkh_image_free(&vk, &noise);
        targetFree(&t);
        videoFree(&vd);
    }

    // The shift: each half the same ramp, dark to light left to right. A
    // positive shift (convergence over 0.5, the picture behind the screen)
    // makes the left eye read further right (lighter) and the right eye
    // further left (darker), by the shift's share of the ramp.
    Video vd;
    Target t;
    if (videoMake(&vd, W, H, fillSbsRamp) || targetMake(&t, W / 2, H, UNORM)) {
        check(0, "sbs ramp fixture");
        return;
    }
    const float s = 0.1f;
    uint32_t x0 = W / 8, x1 = W / 2 - W / 8;
    for (int eye = 0; eye < 2; eye++) {
        MlxrWarpParams w = warpDefaults(&vd.yuv);
        w.split = MLXR_SPLIT_SBS;
        w.half = eye;
        w.eye = eye;
        uint8_t* flat = runWarp(&t, &w);
        w.shift = eye == 0 ? s : -s;
        uint8_t* moved = runWarp(&t, &w);
        float a = rowMeanLuma(flat, W / 2, H / 2, x0, x1), b = rowMeanLuma(moved, W / 2, H / 2, x0, x1);
        float want = (eye == 0 ? 1.0f : -1.0f) * 0.8f * s;
        note("eye %d: ramp mean %.3f, shifted by %+.2f: %.3f (%+.3f, want %+.3f)", eye, a, w.shift, b, b - a, want);
        check(fabsf((b - a) - want) < 0.02f, "eye %d: the shift moves its half the right way (%+.3f, want %+.3f)", eye,
              b - a, want);
        free(flat);
        free(moved);
    }
    targetFree(&t);
    videoFree(&vd);
}

static void testSharpen(void) {
    const uint32_t W = 256, H = 64;
    Video flat, step;
    Target t;
    videoMake(&flat, W, H, fillGrey);
    videoMake(&step, W, H, fillStep);
    targetMake(&t, W, H, UNORM);
    MlxrWarpParams w = warpDefaults(&flat.yuv);
    uint8_t* a = runWarp(&t, &w);
    w.sharpen = 1.0f;
    uint8_t* b = runWarp(&t, &w);
    int d = maxDiff(a, b, (size_t)W * H, 4, NULL);
    check(d == 0, "sharpen leaves a flat picture alone (%d)", d);
    free(a);
    free(b);
    w = warpDefaults(&step.yuv);
    a = runWarp(&t, &w);
    w.sharpen = 1.0f;
    b = runWarp(&t, &w);
    const uint8_t* ra = a + (size_t)(H / 2) * W * 4;
    const uint8_t* rb = b + (size_t)(H / 2) * W * 4;
    int x0 = W / 2 - 1, x1 = W / 2;
    note("step edge row: off %d %d, sharpened %d %d", ra[x0 * 4], ra[x1 * 4], rb[x0 * 4], rb[x1 * 4]);
    check(rb[x0 * 4] < ra[x0 * 4] && rb[x1 * 4] > ra[x1 * 4], "sharpen steepens the edge");
    int far = 0;
    for (uint32_t x = 0; x < W; x++)
        if (abs((int)x - (int)W / 2) > 3 && ra[x * 4] != rb[x * 4]) far++;
    check(far == 0, "sharpen touches only the edge (%d px)", far);
    ppm("sharpen_step", b, W, H);
    free(a);
    free(b);
    targetFree(&t);
    videoFree(&flat);
    videoFree(&step);
}

// CPU copy of UPSAMPLE_FRAGMENT_SRC
static float upsampleCpu(const Video* vd, const uint8_t* map, int mw, int mh, float u, float v, float sigmaR,
                         float sharp) {
    float hi[3];
    videoCpu(vd, MLXR_CHROMA_CENTER, u, v, hi);
    float lpx = u * mw - 0.5f, lpy = v * mh - 0.5f;
    int bx = (int)floorf(lpx), by = (int)floorf(lpy);
    float num = 0, den = 0, dlo = 1, dhi = 0;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int qx = bx + dx, qy = by + dy;
            qx = qx < 0 ? 0 : (qx > mw - 1 ? mw - 1 : qx);
            qy = qy < 0 ? 0 : (qy > mh - 1 ? mh - 1 : qy);
            const uint8_t* s = map + ((size_t)qy * mw + qx) * 4;
            float ox = qx - lpx, oy = qy - lpy;
            float ws = expf(-(ox * ox + oy * oy) / (2.0f * 1.5f * 1.5f));
            float cd = 0;
            for (int c = 0; c < 3; c++) {
                float e = hi[c] - s[c] / 255.0f;
                cd += e * e;
            }
            float wr = expf(-cd / (2.0f * sigmaR * sigmaR));
            float sa = s[3] / 255.0f;
            num += ws * wr * sa;
            den += ws * wr;
            dlo = fminf(dlo, sa);
            dhi = fmaxf(dhi, sa);
        }
    float d = num / fmaxf(den, 1e-6f);
    float span = dhi - dlo;
    if (sharp > 0 && span >= 0.05f) {
        float t = clampf((d - dlo) / fmaxf(span, 1e-6f), 0, 1);
        float snapped = dlo + span / (1.0f + expf(-24.0f * (t - 0.5f)));
        d = d + (snapped - d) * sharp;
    }
    return d;
}

static void testUpsample(void) {
    const uint32_t W = 512, H = 64, MW = 128, MH = 16;
    struct {
        FillFn fill;
        DepthFn depth;
        float sharp;
        const char* name;
    } cases[] = {{fillGrey, depthWobble, 0.0f, "flat guide, wobble"},
                 {fillStep, depthStep, 0.0f, "step guide, step"},
                 {fillStep, depthStep, 1.0f, "step guide, step, sharp"},
                 {fillPattern, depthWobble, 0.5f, "pattern guide, wobble, sharp 0.5"}};
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); k++) {
        Video vd;
        videoMake(&vd, W, H, cases[k].fill);
        uint8_t* map = depthMap(&vd, MW, MH, cases[k].depth);
        Depth3d d;
        depthChain(&d, &vd, MW, MH, map, 0.0f, 0.5f, cases[k].sharp);
        uint8_t* got = readTarget(&d.up);
        int m = 0;
        uint32_t uw = W / 4, uh = H / 4;
        for (uint32_t y = 0; y < uh; y++)
            for (uint32_t x = 0; x < uw; x++) {
                float e = upsampleCpu(&vd, map, MW, MH, (x + 0.5f) / uw, (y + 0.5f) / uh, 0.1f, cases[k].sharp);
                int dd = abs(to8(e) - got[((size_t)y * uw + x) * 4 + 3]);
                if (dd > m) m = dd;
            }
        note("upsample %s: max diff from the CPU copy %d/255", cases[k].name, m);
        check(m <= 2, "upsample %s within 2/255 (%d)", cases[k].name, m);
        free(got);
        free(map);
        depthFree(&d);
        videoFree(&vd);
    }
    // A flat map stays flat, whatever the picture, as long as the guide is
    // the picture the map came from. (A guide that matches nothing drives
    // every range weight under the 1e-6 floor on den and the depth toward
    // 0, far: upstream's maths, reproduced by the CPU copy above.)
    Video vd;
    videoMake(&vd, W, H, fillPattern);
    uint8_t* map = depthMap(&vd, MW, MH, depthHalf);
    Depth3d d;
    depthChain(&d, &vd, MW, MH, map, 0.0f, 0.5f, 1.0f);
    uint8_t* got = readTarget(&d.up);
    int off = 0;
    for (uint32_t i = 0; i < (W / 4) * (H / 4); i++)
        if (abs(got[i * 4 + 3] - 128) > 1) off++;
    check(off == 0, "flat depth stays flat through the upsample (%d)", off);
    free(got);
    free(map);
    depthFree(&d);
    videoFree(&vd);
}

// CPU copy of OFFSET_FRAGMENT_SRC for one eye
static float offsetCpu(const uint8_t* up, int w, int x, int y, float dispTexels, float conv, int eye) {
    int reach = (int)ceilf(fabsf(dispTexels) * fmaxf(conv, 1.0f - conv)) + 2;
    float disp = eye == 0 ? dispTexels : -dispTexels;
#define D(xx) (up[((size_t)y * w + ((xx) < 0 ? 0 : ((xx) > w - 1 ? w - 1 : (xx)))) * 4 + 3] / 255.0f)
    float here = D(x);
    float bestD = -1.0f, bestOff = -disp * (here - conv);
    float pd = D(x - reach);
    float pe = (float)(-reach) + disp * (pd - conv);
    for (int t = -reach + 1; t <= reach; t++) {
        float cd = D(x + t);
        float ce = (float)t + disp * (cd - conv);
        float span = ce - pe;
        if (pe * ce <= 0.0f && fabsf(span) > 1e-6f) {
            float f = clampf(-pe / span, 0, 1);
            float rd = pd + f * (cd - pd);
            if (rd > bestD) {
                bestD = rd;
                bestOff = (float)(t - 1) + f;
            }
        }
        pd = cd;
        pe = ce;
    }
#undef D
    return bestOff / (2.0f * reach) + 0.5f;
}

static void testOffset(void) {
    const uint32_t W = 512, H = 64, MW = 128, MH = 16;
    const float seps[] = {0.0f, 0.02f, 0.05f};
    for (int k = 0; k < 3; k++) {
        Video vd;
        videoMake(&vd, W, H, fillStep);
        uint8_t* map = depthMap(&vd, MW, MH, k == 1 ? depthWobble : depthStep);
        Depth3d d;
        float disp = seps[k] * (W / 4);
        depthChain(&d, &vd, MW, MH, map, seps[k], 0.5f, 1.0f);
        uint8_t* up = readTarget(&d.up);
        uint8_t* got = readTarget(&d.off);
        int m = 0;
        for (uint32_t y = 0; y < H / 4; y++)
            for (uint32_t x = 0; x < W / 4; x++)
                for (int eye = 0; eye < 2; eye++) {
                    float e = offsetCpu(up, W / 4, x, y, disp, 0.5f, eye);
                    int dd = abs(to8(e) - got[((size_t)y * (W / 4) + x) * 4 + eye]);
                    if (dd > m) m = dd;
                }
        note("offset search, separation %.2f (%.2f texels): max diff from the CPU copy %d/255", seps[k], disp, m);
        check(m <= 1, "offset search within 1/255 (%d)", m);
        if (k == 0) {
            int notZero = 0;
            for (uint32_t i = 0; i < (W / 4) * (H / 4); i++)
                if (abs(got[i * 4] - 128) > 1 || abs(got[i * 4 + 1] - 128) > 1) notZero++;
            check(notZero == 0, "zero separation encodes zero offsets (%d)", notZero);
        }
        free(up);
        free(got);
        free(map);
        depthFree(&d);
        videoFree(&vd);
    }
}

static void testDownscale(void) {
    const uint32_t W = 256, H = 128, DW = 64, DH = 32;
    Video vd;
    Target t;
    videoMake(&vd, W, H, fillDetail);
    targetMake(&t, DW, DH, UNORM);
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    mlxr_pass_downscale(P, cmd, &t.t, &vd.yuv);
    vkh_submit(&vk);
    uint8_t* got = readTarget(&t);
    int m = 0;
    for (uint32_t y = 0; y < DH; y++)
        for (uint32_t x = 0; x < DW; x++) {
            float sum[3] = {0, 0, 0};
            for (int j = 0; j < 4; j++)
                for (int i = 0; i < 4; i++) {
                    float rgb[3];
                    float u = (x + 0.5f) / DW + (i - 1.5f) * 0.25f / DW;
                    float v = (y + 0.5f) / DH + (j - 1.5f) * 0.25f / DH;
                    videoCpu(&vd, MLXR_CHROMA_CENTER, u, v, rgb);
                    for (int c = 0; c < 3; c++) sum[c] += rgb[c];
                }
            for (int c = 0; c < 3; c++) {
                int dd = abs(to8(sum[c] / 16.0f) - got[((size_t)y * DW + x) * 4 + c]);
                if (dd > m) m = dd;
            }
        }
    note("downscale 256x128 to 64x32: max diff from the CPU box %d/255", m);
    check(m <= 2, "downscale within 2/255 (%d)", m);
    free(got);
    targetFree(&t);
    videoFree(&vd);
}

static void ambiCpu(const Video* vd, const float crop[4], const PictureGrade* g, int x, int y, float out[3]) {
    float bu = crop[0] + (x + 0.5f) / 32.0f * crop[2], bv = crop[1] + (y + 0.5f) / 32.0f * crop[3];
    out[0] = out[1] = out[2] = 0;
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 4; i++) {
            float rgb[3], gr[3];
            videoCpu(vd, MLXR_CHROMA_CENTER, bu + (i - 1.5f) * (0.25f / 32.0f) * crop[2],
                     bv + (j - 1.5f) * (0.25f / 32.0f) * crop[3], rgb);
            if (g)
                pictureGradeApply(g, rgb, gr);
            else
                memcpy(gr, rgb, sizeof(gr));
            for (int c = 0; c < 3; c++) out[c] += gr[c] / 16.0f;
        }
}

static void testAmbi(void) {
    const uint32_t W = 256, H = 128;
    Video a, b;
    Target t;
    videoMake(&a, W, H, fillBars);
    videoMake(&b, W, H, fillSmooth);
    targetMake(&t, MLXR_AMBI_TEX, MLXR_AMBI_TEX, UNORM);
    PictureGrade g;
    gradeOf(10, 120, 90, 120, &g);
    const float crops[2][4] = {{0, 0, 1, 1}, {0.25f, 0.125f, 0.5f, 0.75f}};
    for (int k = 0; k < 2; k++) {
        MlxrAmbiParams ap = {a.yuv, {0}, 1, {g.offset, g.contrast, g.exponent, g.saturation}, 1.0f};
        memcpy(ap.crop, crops[k], sizeof(ap.crop));
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkh_layout(&vk, &t.img, COLOR_ATT);
        mlxr_pass_ambi(P, cmd, &t.t, &ap);
        vkh_submit(&vk);
        uint8_t* got = readTarget(&t);
        int m = 0;
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < 32; x++) {
                float e[3];
                ambiCpu(&a, crops[k], &g, x, y, e);
                for (int c = 0; c < 3; c++) {
                    int dd = abs(to8(e[c]) - got[((size_t)y * 32 + x) * 4 + c]);
                    if (dd > m) m = dd;
                }
            }
        note("ambi crop %d, graded: max diff from the CPU copy %d/255", k, m);
        check(m <= 2, "ambi crop %d within 2/255 (%d)", k, m);
        free(got);
    }
    // Smoothing: a quarter of the new sample over three quarters of the old
    MlxrAmbiParams first = {a.yuv, {0, 0, 1, 1}, 0, {0, 1, 1, 1}, 1.0f};
    MlxrAmbiParams next = {b.yuv, {0, 0, 1, 1}, 0, {0, 1, 1, 1}, 0.25f};
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    mlxr_pass_ambi(P, cmd, &t.t, &first);
    vkh_submit(&vk);
    uint8_t* old = readTarget(&t);
    cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    mlxr_pass_ambi(P, cmd, &t.t, &next);
    vkh_submit(&vk);
    uint8_t* mixed = readTarget(&t);
    int m = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            float e[3];
            ambiCpu(&b, crops[0], NULL, x, y, e);
            for (int c = 0; c < 3; c++) {
                float want = 0.25f * clampf(e[c], 0, 1) + 0.75f * old[((size_t)y * 32 + x) * 4 + c] / 255.0f;
                int dd = abs(to8(want) - mixed[((size_t)y * 32 + x) * 4 + c]);
                if (dd > m) m = dd;
            }
        }
    note("ambi constant alpha smoothing 0.25: max diff %d/255", m);
    check(m <= 2, "ambi smoothing within 2/255 (%d)", m);
    free(old);
    free(mixed);
    targetFree(&t);
    videoFree(&a);
    videoFree(&b);
}

// CPU copy of GLOW_EDGE_FRAGMENT_SRC built from xr_glow.c's pieces
static void glowEdgeCpu(const uint8_t* in, int px, int py, float out[3]) {
    const int N = AMBI_SAMPLE_TEX, RING = GLOW_RING;
#define LIFT(X, Y, O)                                                                       \
    do {                                                                                    \
        float c_[3];                                                                        \
        for (int k_ = 0; k_ < 3; k_++) c_[k_] = in[((size_t)(Y) * N + (X)) * 4 + k_] / 255.0f; \
        glowNormalise(c_, O);                                                               \
    } while (0)
    if (px > 0 && py > 0 && px < N - 1 && py < N - 1) {
        LIFT(px, py, out);
        return;
    }
    float reach = glowReachTexels();
    int steps = glowInwardSteps();
    int j = glowRingIndex(px, py);
    int r = (int)reach < RING / 2 - 1 ? (int)reach : RING / 2 - 1;
    float own[3] = {0, 0, 0}, sum[3] = {0, 0, 0}, weight = 0, carried = 0;
    for (int k = -r; k <= r; k++) {
        int x, y;
        glowRingTexel(((j + k) % RING + RING) % RING, &x, &y);
        int ix = x == 0 ? 1 : (x == N - 1 ? -1 : 0), iy = y == 0 ? 1 : (y == N - 1 ? -1 : 0);
        float c[3];
        LIFT(x, y, c);
        for (int s = 1; s <= steps; s++) {
            float deeper[3];
            LIFT(x + ix * s, y + iy * s, deeper);
            float l = glowLit(c);
            for (int q = 0; q < 3; q++) c[q] = deeper[q] + (c[q] - deeper[q]) * l;
        }
        float t = k / reach;
        float wgt = (1.0f - t * t) * glowLit(c);
        for (int q = 0; q < 3; q++) sum[q] += c[q] * wgt;
        weight += wgt;
        if (wgt > carried) carried = wgt;
        if (k == 0) memcpy(own, c, sizeof(own));
    }
    float lo = glowLit(own);
    for (int q = 0; q < 3; q++) out[q] = clampf(own[q] + sum[q] / fmaxf(weight, 1e-4f) * (carried * (1.0f - lo)), 0, 1);
#undef LIFT
}

static void glowSample(uint8_t* px) {
    // Colour patches with a dark band along part of the ring and a dark
    // border on one side, which is what the roll off exists for
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            uint8_t* p = px + ((size_t)y * 32 + x) * 4;
            float r = 0.5f + 0.5f * sinf(x * 0.4f), g = 0.5f + 0.5f * cosf(y * 0.3f), b = (x + y) / 62.0f;
            float dark = (y == 0 && x > 8 && x < 20) || x == 31 || (y > 20 && x < 2) ? 0.02f : 1.0f;
            p[0] = (uint8_t)to8(r * dark);
            p[1] = (uint8_t)to8(g * dark);
            p[2] = (uint8_t)to8(b * dark);
            p[3] = 255;
        }
}

static void testGlowEdge(void) {
    uint8_t px[32 * 32 * 4];
    glowSample(px);
    VkhImage tex;
    textureMake(&tex, 32, 32, px);
    Target t;
    targetMake(&t, 32, 32, UNORM);
    MlxrGlowEdgeParams gp = {tex.view, {GLOW_LUMA_TARGET, GLOW_LUMA_FLOOR, GLOW_LUMA_FULL},
                             {GLOW_LIT_LO, GLOW_LIT_HI}, glowReachTexels(), glowInwardSteps()};
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    mlxr_pass_glow_edge(P, cmd, &t.t, &gp);
    vkh_submit(&vk);
    uint8_t* got = readTarget(&t);
    int m = 0, mRing = 0;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            float e[3];
            glowEdgeCpu(px, x, y, e);
            for (int c = 0; c < 3; c++) {
                int dd = abs(to8(e[c]) - got[((size_t)y * 32 + x) * 4 + c]);
                if (dd > m) m = dd;
                if (glowRingIndex(x, y) >= 0 && dd > mRing) mRing = dd;
            }
        }
    note("glow edge vs xr_glow.c CPU sums: max diff %d/255 (ring %d/255), reach %.2f, steps %d", m, mRing,
         glowReachTexels(), glowInwardSteps());
    check(m <= 1, "glow edge within 1/255 (%d)", m);
    free(got);
    targetFree(&t);
    vkh_image_free(&vk, &tex);
}

// CPU copies of GLOW_FRAGMENT_SRC's spline and blurred spline
static void tex32(const uint8_t* px, float u, float v, float out[3]) {
    for (int c = 0; c < 3; c++) out[c] = bilin8(px, 32, 32, 4, c, u, v);
}

static void splineCpu(const uint8_t* px, float fu, float fv, float out[3]) {
    float tc[2] = {fu * 32 - 0.5f, fv * 32 - 0.5f}, h0[2], h1[2], g0[2];
    for (int a = 0; a < 2; a++) {
        float base = floorf(tc[a]), f = tc[a] - base, f2 = f * f, f3 = f2 * f;
        float w0 = (1 - 3 * f + 3 * f2 - f3) / 6, w1 = (4 - 6 * f2 + 3 * f3) / 6;
        float w2 = (1 + 3 * f + 3 * f2 - 3 * f3) / 6, w3 = f3 / 6;
        g0[a] = w0 + w1;
        float g1 = w2 + w3;
        h0[a] = (base - 0.5f + w1 / g0[a]) / 32;
        h1[a] = (base + 1.5f + w3 / g1) / 32;
    }
    float c00[3], c10[3], c01[3], c11[3];
    tex32(px, h0[0], h0[1], c00);
    tex32(px, h1[0], h0[1], c10);
    tex32(px, h0[0], h1[1], c01);
    tex32(px, h1[0], h1[1], c11);
    for (int c = 0; c < 3; c++) {
        float top = c11[c] + (c01[c] - c11[c]) * g0[0];
        float bot = c10[c] + (c00[c] - c10[c]) * g0[0];
        out[c] = top + (bot - top) * g0[1];
    }
}

// The true cubic B spline over clamped texels, 16 taps, to check the
// four fetch fold itself
static void splineDirect(const uint8_t* px, float fu, float fv, float out[3]) {
    float tx = fu * 32 - 0.5f, ty = fv * 32 - 0.5f;
    float bx = floorf(tx), by = floorf(ty), fx = tx - bx, fy = ty - by;
    float wx[4], wy[4];
    float* ws[2] = {wx, wy};
    float fs[2] = {fx, fy};
    for (int a = 0; a < 2; a++) {
        float f = fs[a], f2 = f * f, f3 = f2 * f;
        ws[a][0] = (1 - 3 * f + 3 * f2 - f3) / 6;
        ws[a][1] = (4 - 6 * f2 + 3 * f3) / 6;
        ws[a][2] = (1 + 3 * f + 3 * f2 - 3 * f3) / 6;
        ws[a][3] = f3 / 6;
    }
    for (int c = 0; c < 3; c++) {
        out[c] = 0;
        for (int j = 0; j < 4; j++)
            for (int i = 0; i < 4; i++)
                out[c] += wx[i] * wy[j] * texel8(px, 32, 32, 4, c, (int)bx - 1 + i, (int)by - 1 + j);
    }
}

static void blurredCpu(const uint8_t* px, float fu, float fv, float out[3]) {
    float tc[2] = {fu * 32 - 0.5f, fv * 32 - 0.5f}, h[3][2], g[3][2];
    for (int a = 0; a < 2; a++) {
        float base = floorf(tc[a]), f = tc[a] - base, f2 = f * f, f3 = f2 * f;
        float b0 = (1 - 3 * f + 3 * f2 - f3) / 6, b1 = (4 - 6 * f2 + 3 * f3) / 6;
        float b2 = (1 + 3 * f + 3 * f2 - 3 * f3) / 6, b3 = f3 / 6;
        float wb = (b0 + b1) / 3, wd = (b1 + b2 + b3) / 3, wf = b3 / 3;
        g[0][a] = (2 * b0 + b1) / 3;
        g[1][a] = (b0 + 2 * b1 + 2 * b2 + b3) / 3;
        g[2][a] = (b2 + 2 * b3) / 3;
        h[0][a] = (base - 1.5f + wb / g[0][a]) / 32;
        h[1][a] = (base + 0.5f + wd / g[1][a]) / 32;
        h[2][a] = (base + 2.5f + wf / g[2][a]) / 32;
    }
    out[0] = out[1] = out[2] = 0;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            float s[3];
            tex32(px, h[i][0], h[j][1], s);
            for (int c = 0; c < 3; c++) out[c] += s[c] * g[i][0] * g[j][1];
        }
}

static void testGlow(void) {
    uint8_t px[32 * 32 * 4];
    glowSample(px);
    VkhImage tex;
    textureMake(&tex, 32, 32, px);
    Target t;
    targetMake(&t, MLXR_GLOW_TEX, MLXR_GLOW_TEX, UNORM);
    // The fold against the plain 16 tap spline, CPU only
    float worstFold = 0;
    for (int i = 0; i < 400; i++) {
        float u = (i % 20) / 19.0f * 1.4f - 0.2f, v = (i / 20) / 19.0f * 1.4f - 0.2f, a[3], b[3];
        splineCpu(px, u, v, a);
        splineDirect(px, u, v, b);
        for (int c = 0; c < 3; c++) worstFold = fmaxf(worstFold, fabsf(a[c] - b[c]));
    }
    check(worstFold < 1e-4f, "four fetch spline equals the 16 tap B spline (%g)", worstFold);
    for (int blur = 0; blur < 2; blur++) {
        const float intensity = 0.7f;
        MlxrGlowParams gp = {tex.view, intensity, blur};
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkh_layout(&vk, &t.img, COLOR_ATT);
        mlxr_pass_glow(P, cmd, &t.t, &gp);
        vkh_submit(&vk);
        uint8_t* got = readTarget(&t);
        int m = 0, ma = 0;
        for (int y = 0; y < MLXR_GLOW_TEX; y++)
            for (int x = 0; x < MLXR_GLOW_TEX; x++) {
                float u = (x + 0.5f) / MLXR_GLOW_TEX, v = (y + 0.5f) / MLXR_GLOW_TEX;
                float fu = (u - 0.5f) * 1.7f + 0.5f, fv = (v - 0.5f) * 1.7f + 0.5f, col[3];
                if (blur)
                    blurredCpu(px, fu, fv, col);
                else
                    splineCpu(px, fu, fv, col);
                float dx = fmaxf(fabsf(u - 0.5f) - 0.5f / 1.7f, 0) / (0.5f - 0.5f / 1.7f);
                float dy = fmaxf(fabsf(v - 0.5f) - 0.5f / 1.7f, 0) / (0.5f - 0.5f / 1.7f);
                float tt = fminf(sqrtf(dx * dx + dy * dy), 1.0f), s = tt * tt * (3 - 2 * tt);
                float a = (1 - s) * (1 - s) * intensity;
                const uint8_t* g = got + ((size_t)y * MLXR_GLOW_TEX + x) * 4;
                for (int c = 0; c < 3; c++) m = abs(to8(col[c] * a) - g[c]) > m ? abs(to8(col[c] * a) - g[c]) : m;
                ma = abs(to8(a) - g[3]) > ma ? abs(to8(a) - g[3]) : ma;
            }
        note("glow %s: colour max diff %d/255, alpha %d/255 from the CPU copy", blur ? "blurred" : "spline", m, ma);
        check(m <= 2 && ma <= 1, "glow blur %d within 2/255 (colour %d, alpha %d)", blur, m, ma);
        const uint8_t* corner = got;
        check(corner[3] == 0, "glow rim is clear (alpha %d)", corner[3]);
        ppm(blur ? "glow_blurred" : "glow_spline", got, MLXR_GLOW_TEX, MLXR_GLOW_TEX);
        free(got);
    }
    targetFree(&t);
    vkh_image_free(&vk, &tex);
}

static void quadVerts(float* v, int first, float x0, float y0, float x1, float y1, float z, const float rgb[3],
                      float spill) {
    float corners[4][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}};
    for (int i = 0; i < 4; i++) {
        float* o = v + (size_t)(first + i) * MLXR_ROOM_VERTEX_FLOATS;
        o[0] = corners[i][0];
        o[1] = corners[i][1];
        o[2] = z;
        o[3] = rgb[0];
        o[4] = rgb[1];
        o[5] = rgb[2];
        o[6] = spill;
        o[7] = i & 1 ? 1.0f : 0.0f;
        o[8] = i & 2 ? 1.0f : 0.0f;
        o[9] = 0.0f;
    }
}

static void eyeViewProj(float* out, float eyeX) {
    XrFovf fov = {-0.785398f, 0.785398f, 0.785398f, -0.785398f};
    XrPosef pose = {{0, 0, 0, 1}, {eyeX, 0, 0}};
    float proj[16], view[16];
    projectionFromFov(proj, fov, 0.05f, 100.0f);
    viewFromPose(view, pose);
    matMul(out, proj, view);
}

static void testRoom(void) {
    const uint32_t EW = 128, EH = 128;
    Target t;
    VkhImage depth;
    targetMake(&t, 2 * EW, EH, UNORM);
    vkh_image(&vk, 2 * EW, EH, VK_FORMAT_D32_SFLOAT, &depth);
    // Far quad filling the view, red, unlit; near quad in the upper middle,
    // textured, lit; a painted quad drawn after it but behind it
    VkhBuffer vb, ib;
    vkh_buffer(&vk, 12 * MLXR_ROOM_VERTEX_FLOATS * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vb);
    vkh_buffer(&vk, 18 * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &ib);
    const float red[3] = {0.8f, 0.1f, 0.1f}, grey[3] = {0.5f, 0.5f, 0.5f}, blue[3] = {0.1f, 0.1f, 0.9f};
    quadVerts(vb.map, 0, -20, -20, 20, 20, -10, red, 0.0f);
    quadVerts(vb.map, 4, -0.3f, 0.1f, 0.3f, 0.6f, -1, grey, 1.0f);
    quadVerts(vb.map, 8, -0.3f, 0.1f, 0.3f, 0.6f, -2, blue, 0.0f);
    uint32_t* idx = ib.map;
    for (int q = 0; q < 3; q++) {
        uint32_t b = q * 4, quad[6] = {b, b + 1, b + 2, b + 2, b + 1, b + 3};
        memcpy(idx + q * 6, quad, sizeof(quad));
    }
    // The atlas: 2x2 green shades; the colour sample: flat
    uint8_t atlasPx[16] = {0, 200, 0, 255, 0, 200, 0, 255, 0, 200, 0, 255, 0, 200, 0, 255};
    VkhImage atlas, ambi;
    textureMake(&atlas, 2, 2, atlasPx);
    uint8_t ambiPx[32 * 32 * 4];
    for (int i = 0; i < 32 * 32; i++) ambiPx[i * 4] = 51, ambiPx[i * 4 + 1] = 102, ambiPx[i * 4 + 2] = 153, ambiPx[i * 4 + 3] = 255;
    textureMake(&ambi, 32, 32, ambiPx);
    MlxrRoomPart parts[3] = {{0, 6, VK_NULL_HANDLE, 0.0f}, {6, 6, atlas.view, 1.0f}, {12, 6, VK_NULL_HANDLE, 0.0f}};
    MlxrRoomParams rp;
    memset(&rp, 0, sizeof(rp));
    rp.ambi = ambi.view;
    rp.vertices = vb.buffer;
    rp.indices = ib.buffer;
    rp.parts = parts;
    rp.partCount = 3;
    rp.eyes = 2;
    eyeViewProj(rp.viewProj[0], -0.03f);
    eyeViewProj(rp.viewProj[1], 0.03f);
    rp.eyeRect[0] = (VkRect2D){{0, 0}, {EW, EH}};
    rp.eyeRect[1] = (VkRect2D){{(int32_t)EW, 0}, {EW, EH}};
    rp.spillGain = 0.5f;
    rp.dim = 0.8f;
    rp.depth = depth.view;
    rp.depthFormat = VK_FORMAT_D32_SFLOAT;
    t.t.clearColor[2] = 1.0f;
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    vkh_layout(&vk, &depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    mlxr_pass_room(P, cmd, &t.t, &rp);
    check(vkh_submit(&vk) == 0, "room submit");
    uint8_t* got = readTarget(&t);
    ppm("room", got, 2 * EW, EH);
    for (int eye = 0; eye < 2; eye++) {
        // The near quad spans y 0.1..0.6 at z -1: rows above the centre
        const uint8_t* nearPx = got + ((size_t)(EH / 2 - 25) * 2 * EW + eye * EW + EW / 2) * 4;
        const uint8_t* farPx = got + ((size_t)(EH / 2 + 30) * 2 * EW + eye * EW + EW / 2) * 4;
        float washR = 0.2f * 0.5f, washG = 0.4f * 0.5f, washB = 0.6f * 0.5f;
        int wantNear[3] = {to8(0.0f * 0.8f + washR), to8(200 / 255.0f * 0.8f + washG), to8(0.0f + washB)};
        int wantFar[3] = {to8(0.8f * 0.8f), to8(0.1f * 0.8f), to8(0.1f * 0.8f)};
        int dn = 0, df = 0;
        for (int c = 0; c < 3; c++) {
            dn = abs(nearPx[c] - wantNear[c]) > dn ? abs(nearPx[c] - wantNear[c]) : dn;
            df = abs(farPx[c] - wantFar[c]) > df ? abs(farPx[c] - wantFar[c]) : df;
        }
        note("room eye %d: near %d %d %d (want %d %d %d), far %d %d %d (want %d %d %d)", eye, nearPx[0], nearPx[1],
             nearPx[2], wantNear[0], wantNear[1], wantNear[2], farPx[0], farPx[1], farPx[2], wantFar[0], wantFar[1],
             wantFar[2]);
        check(dn <= 1, "room eye %d: atlas * dim + wash in the upper middle, in front (%d)", eye, dn);
        check(df <= 1, "room eye %d: painted * dim below, unlit (%d)", eye, df);
    }
    // Parallax: the near quad's left edge sits further right in the left eye
    int edge[2];
    for (int eye = 0; eye < 2; eye++) {
        const uint8_t* row = got + ((size_t)(EH / 2 - 25) * 2 * EW + eye * EW) * 4;
        edge[eye] = -1;
        for (uint32_t x = 0; x < EW; x++)
            if (row[x * 4 + 1] > 100) {
                edge[eye] = (int)x;
                break;
            }
    }
    note("near quad left edge: left eye column %d, right eye %d", edge[0], edge[1]);
    check(edge[0] > edge[1] && edge[1] >= 0, "the eyes see the near quad with crossed parallax");
    free(got);
    vkh_buffer_free(&vk, &vb);
    vkh_buffer_free(&vk, &ib);
    vkh_image_free(&vk, &atlas);
    vkh_image_free(&vk, &ambi);
    vkh_image_free(&vk, &depth);
    targetFree(&t);
}

static void testModel(void) {
    const uint32_t EW = 64, EH = 64;
    Target t;
    VkhImage depth;
    targetMake(&t, 2 * EW, EH, UNORM);
    vkh_image(&vk, 2 * EW, EH, VK_FORMAT_D32_SFLOAT, &depth);
    // Two quads facing the eye: the left one's normal along the light, the
    // right one's away from it
    VkhBuffer vb, ib;
    vkh_buffer(&vk, 8 * MLXR_MODEL_VERTEX_FLOATS * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vb);
    vkh_buffer(&vk, 12 * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &ib);
    float* v = vb.map;
    const float light[3] = {-0.303f, 0.808f, 0.505f};
    for (int q = 0; q < 2; q++)
        for (int i = 0; i < 4; i++) {
            float* o = v + (size_t)(q * 4 + i) * MLXR_MODEL_VERTEX_FLOATS;
            float x0 = q == 0 ? -0.4f : 0.05f;
            o[0] = x0 + (i & 1 ? 0.35f : 0.0f);
            o[1] = i & 2 ? 0.2f : -0.2f;
            o[2] = -1.0f;
            for (int c = 0; c < 3; c++) o[3 + c] = q == 0 ? light[c] : -light[c];
            o[6] = o[7] = 0.0f;
            o[8] = 0.9f;
            o[9] = 0.6f;
            o[10] = 0.3f;
        }
    uint32_t idx[12] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    memcpy(ib.map, idx, sizeof(idx));
    MlxrModelParams mp;
    memset(&mp, 0, sizeof(mp));
    mp.vertices = vb.buffer;
    mp.indices = ib.buffer;
    mp.indexCount = 12;
    mp.eyes = 2;
    eyeViewProj(mp.viewProj[0], 0.0f);
    eyeViewProj(mp.viewProj[1], 0.0f);
    mp.eyeRect[0] = (VkRect2D){{0, 0}, {EW, EH}};
    mp.eyeRect[1] = (VkRect2D){{(int32_t)EW, 0}, {EW, EH}};
    mp.shown[0] = 1;
    for (int i = 0; i < 16; i++) mp.model[0][i] = i % 5 == 0 ? 1.0f : 0.0f;
    mp.depth = depth.view;
    mp.depthFormat = VK_FORMAT_D32_SFLOAT;
    check(mlxr_passes_warm(P, MLXR_PASS_MODEL, 0, UNORM, VK_FORMAT_D32_SFLOAT) == 0, "model pipeline");
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    vkh_layout(&vk, &depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    mlxr_pass_model(P, cmd, &t.t, &mp);
    check(vkh_submit(&vk) == 0, "model submit");
    uint8_t* got = readTarget(&t);
    ppm("model", got, 2 * EW, EH);
    float len = sqrtf(light[0] * light[0] + light[1] * light[1] + light[2] * light[2]);
    float litOn = 0.45f + 0.55f * len, litOff = 0.45f;
    const uint8_t* on = got + ((size_t)(EH / 2) * 2 * EW + EW / 2 - 8) * 4;
    const uint8_t* off = got + ((size_t)(EH / 2) * 2 * EW + EW / 2 + 8) * 4;
    const uint8_t* empty = got + ((size_t)2 * 2 * EW + 2) * 4;
    int d1 = abs(on[0] - to8(0.9f * litOn)) + abs(on[1] - to8(0.6f * litOn)) + abs(on[2] - to8(0.3f * litOn));
    int d2 = abs(off[0] - to8(0.9f * litOff)) + abs(off[1] - to8(0.6f * litOff)) + abs(off[2] - to8(0.3f * litOff));
    note("model: lit side %d %d %d, unlit side %d %d %d, empty alpha %d", on[0], on[1], on[2], off[0], off[1], off[2],
         empty[3]);
    check(d1 <= 2, "model facing the light is colour * %.3f (%d)", litOn, d1);
    check(d2 <= 2, "model facing away is colour * 0.45 (%d)", d2);
    check(on[3] == 255 && empty[3] == 0 && empty[0] == 0, "model clears to nothing and writes alpha 1");
    free(got);
    vkh_buffer_free(&vk, &vb);
    vkh_buffer_free(&vk, &ib);
    vkh_image_free(&vk, &depth);
    targetFree(&t);
}

static void testSupersample(void) {
    const uint32_t W = 256, H = 128;
    uint8_t* px = malloc((size_t)2 * W * H * 4);
    // Left half a one pixel checkerboard, right half solid blue
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < 2 * W; x++) {
            uint8_t* p = px + ((size_t)y * 2 * W + x) * 4;
            if (x < W) {
                uint8_t c = ((x + y) & 1) ? 255 : 0;
                p[0] = p[1] = p[2] = c;
            } else {
                p[0] = p[1] = 0;
                p[2] = 255;
            }
            p[3] = 255;
        }
    VkhImage src;
    textureMake(&src, 2 * W, H, px);
    for (int taps = 2; taps <= 4; taps += 2) {
        Target t;
        targetMake(&t, W / 2, H / 2, UNORM);
        MlxrSupersampleParams sp = {src.view, {0.0f, 0.0f, 0.5f, 1.0f}, taps};
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkh_layout(&vk, &t.img, COLOR_ATT);
        mlxr_pass_supersample(P, cmd, &t.t, &sp);
        vkh_submit(&vk);
        uint8_t* got = readTarget(&t);
        int worst = 0, blue = 0;
        for (uint32_t i = 0; i < (W / 2) * (H / 2); i++) {
            uint32_t x = i % (W / 2), y = i / (W / 2);
            if (got[i * 4 + 2] > got[i * 4] + 2) blue++;
            // The outermost ring is held half a texel inside the region, so
            // its outer taps double up on the edge texel by design
            if (x == 0 || y == 0 || x == W / 2 - 1 || y == H / 2 - 1) continue;
            for (int c = 0; c < 3; c++) worst = abs(got[i * 4 + c] - 128) > worst ? abs(got[i * 4 + c] - 128) : worst;
        }
        note("supersample 2:1, %d taps, checkerboard: max distance from grey %d/255, seam leaks %d", taps, worst, blue);
        check(worst <= 1, "checkerboard averages to grey with %d taps (%d)", taps, worst);
        check(blue == 0, "the left eye never reads the right across the seam (%d)", blue);
        free(got);
        targetFree(&t);
    }
    // A 3:1 ratio of a smooth ramp keeps the ramp
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < 2 * W; x++) {
            uint8_t* p = px + ((size_t)y * 2 * W + x) * 4;
            p[0] = p[1] = p[2] = (uint8_t)(x / 2);
        }
    vkh_upload(&vk, &src, VK_IMAGE_ASPECT_COLOR_BIT, 2 * W, H, 4, px);
    Target t;
    targetMake(&t, 2 * W / 3, H / 3, UNORM);
    MlxrSupersampleParams sp = {src.view, {0, 0, 1, 1}, 4};
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    mlxr_pass_supersample(P, cmd, &t.t, &sp);
    vkh_submit(&vk);
    uint8_t* got = readTarget(&t);
    int worst = 0;
    uint32_t tw = 2 * W / 3;
    for (uint32_t x = 2; x + 2 < tw; x++) {
        float centre = (x + 0.5f) / tw * 2 * W - 0.5f; // in source texels
        int want = (int)floorf(centre / 2.0f + 0.5f);
        int g = got[((size_t)(H / 6) * tw + x) * 4];
        worst = abs(g - want) > worst ? abs(g - want) : worst;
    }
    check(worst <= 1, "a ramp survives a 3:1 supersample (%d)", worst);
    free(got);
    targetFree(&t);
    free(px);
    vkh_image_free(&vk, &src);
}

static void testPanel(void) {
    const uint32_t W = 128, H = 64;
    // Top half orange, bottom half teal, both at alpha 0.5 premultiplied
    uint8_t px[8 * 8 * 4];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            uint8_t* p = px + (y * 8 + x) * 4;
            if (y < 4)
                p[0] = 128, p[1] = 64, p[2] = 0;
            else
                p[0] = 0, p[1] = 100, p[2] = 100;
            p[3] = 128;
        }
    VkhImage tex;
    textureMake(&tex, 8, 8, px);
    Target t;
    targetMake(&t, W, H, UNORM);
    t.t.clear = 1;
    t.t.clearColor[2] = 1.0f;
    t.t.clearColor[3] = 1.0f;
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = tex.view;
    pp.eyes = 1;
    for (int i = 0; i < 16; i++) pp.mvp[0][i] = i % 5 == 0 ? 1.0f : 0.0f;
    pp.eyeRect[0] = t.t.rect;
    pp.uvRect[0][2] = pp.uvRect[0][3] = 1.0f;
    pp.width = 2.0f;
    pp.height = 2.0f;
    pp.segments = 1;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    mlxr_pass_panel(P, cmd, &t.t, &pp);
    vkh_submit(&vk);
    uint8_t* got = readTarget(&t);
    const uint8_t* top = got + ((size_t)4 * W + W / 2) * 4;
    const uint8_t* bot = got + ((size_t)(H - 5) * W + W / 2) * 4;
    // src + dst * (1 - a): orange over blue, teal over blue
    float ia = 1.0f - 128 / 255.0f;
    int dt = abs(top[0] - 128) + abs(top[1] - 64) + abs(top[2] - to8(ia));
    int db = abs(bot[0] - 0) + abs(bot[1] - 100) + abs(bot[2] - (100 + to8(ia)));
    note("panel: top %d %d %d, bottom %d %d %d", top[0], top[1], top[2], bot[0], bot[1], bot[2]);
    check(dt <= 2, "panel: the texture's top row lands at the top, blended premultiplied (%d)", dt);
    check(db <= 2, "panel: bottom half blended (%d)", db);
    free(got);
    // Curved: radius 1 and 2 m wide is a radian each side, so it reaches
    // sin(1) of the way across rather than all of it
    pp.radius = 1.0f;
    pp.segments = 32;
    cmd = vkh_begin(&vk);
    vkh_layout(&vk, &t.img, COLOR_ATT);
    mlxr_pass_panel(P, cmd, &t.t, &pp);
    vkh_submit(&vk);
    got = readTarget(&t);
    ppm("panel_curved", got, W, H);
    const uint8_t* row = got + (size_t)4 * W * 4;
    int first = -1, last = -1;
    for (uint32_t x = 0; x < W; x++)
        if (row[x * 4] > 60) {
            if (first < 0) first = (int)x;
            last = (int)x;
        }
    float want = sinf(1.0f) * W / 2;
    note("curved panel spans columns %d..%d, expected %.1f..%.1f", first, last, W / 2 - want, W / 2 + want - 1);
    check(fabsf(first - (W / 2 - want)) <= 1.5f && fabsf(last - (W / 2 + want - 1)) <= 1.5f,
          "curved panel's projected width is sin(angle) of the flat one");
    free(got);
    targetFree(&t);
    vkh_image_free(&vk, &tex);
}

// ---- bench (not a ctest): GPU time of each pass at stream sizes ----------

static int cmpf(const void* a, const void* b) {
    float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}

typedef void (*RecordFn)(VkCommandBuffer cmd, void* user);

// Median GPU milliseconds of `rec` over n submissions
static float timeIt(RecordFn rec, void* user, int n) {
    VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2;
    VkQueryPool pool;
    vkCreateQueryPool(vk.device, &qi, NULL, &pool);
    float ms[64];
    if (n > 64) n = 64;
    for (int i = 0; i < n; i++) {
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkCmdResetQueryPool(cmd, pool, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, 0);
        rec(cmd, user);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, 1);
        vkh_submit(&vk);
        uint64_t ts[2];
        vkGetQueryPoolResults(vk.device, pool, 0, 2, sizeof(ts), ts, sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        ms[i] = (float)((ts[1] - ts[0]) * vk.props.limits.timestampPeriod / 1e6);
    }
    vkDestroyQueryPool(vk.device, pool, NULL);
    qsort(ms, (size_t)n, sizeof(float), cmpf);
    return ms[n / 2];
}

typedef struct {
    Video* vd;
    int nv12;
    Target *eye, *up, *off, *ambi, *glowEdge, *glow, *small, *down;
    VkhImage* map;
    int occlusion, sharpen, cubic, eyes;
} Bench;

static MlxrVideo benchVideo(Bench* b) {
    return b->nv12 ? b->vd->nv : b->vd->yuv;
}

static void recWarp(VkCommandBuffer cmd, void* u) {
    Bench* b = u;
    MlxrVideo v = benchVideo(b);
    MlxrWarpParams w = warpDefaults(&v);
    w.depth = b->up->img.view;
    w.offsets = b->off->img.view;
    w.dispTexels = 0.05f * (v.width / 4);
    w.occlusion = b->occlusion;
    w.depthCubic = b->cubic;
    w.sharpen = b->sharpen ? 0.5f : 0.0f;
    w.gradeOn = 1;
    PictureGrade g;
    gradeOf(10, 110, 90, 110, &g);
    memcpy(w.grade, &g, sizeof(w.grade));
    for (int e = 0; e < b->eyes; e++) {
        w.eye = e;
        w.disparity = b->eyes == 2 ? (e ? -0.05f : 0.05f) : 0.0f;
        MlxrTarget t = b->eye->t;
        t.rect = (VkRect2D){{(int32_t)(e * v.width), 0}, {v.width, v.height}};
        mlxr_pass_warp(P, cmd, &t, &w);
    }
}

static void recUpsample(VkCommandBuffer cmd, void* u) {
    Bench* b = u;
    MlxrUpsampleParams up = {benchVideo(b), b->map->view, b->map->width, b->map->height, 0.1f, 1.0f};
    mlxr_pass_upsample(P, cmd, &b->up->t, &up);
}

static void recOffset(VkCommandBuffer cmd, void* u) {
    Bench* b = u;
    MlxrOffsetParams op = {b->up->img.view, 0.05f * b->up->img.width, 0.5f};
    mlxr_pass_offset(P, cmd, &b->off->t, &op);
}

static void recDownscale(VkCommandBuffer cmd, void* u) {
    Bench* b = u;
    MlxrVideo v = benchVideo(b);
    mlxr_pass_downscale(P, cmd, &b->down->t, &v);
}

static void recAmbiGlow(VkCommandBuffer cmd, void* u) {
    Bench* b = u;
    MlxrAmbiParams ap = {benchVideo(b), {0, 0, 1, 1}, 1, {0, 1, 1, 1}, 1.0f};
    mlxr_pass_ambi(P, cmd, &b->ambi->t, &ap);
    mlxr_barrier(cmd, b->ambi->img.image, VK_IMAGE_ASPECT_COLOR_BIT, COLOR_ATT, SHADER_READ);
    MlxrGlowEdgeParams ge = {b->ambi->img.view, {GLOW_LUMA_TARGET, GLOW_LUMA_FLOOR, GLOW_LUMA_FULL},
                             {GLOW_LIT_LO, GLOW_LIT_HI}, glowReachTexels(), glowInwardSteps()};
    mlxr_pass_glow_edge(P, cmd, &b->glowEdge->t, &ge);
    mlxr_barrier(cmd, b->glowEdge->img.image, VK_IMAGE_ASPECT_COLOR_BIT, COLOR_ATT, SHADER_READ);
    MlxrGlowParams gp = {b->glowEdge->img.view, 0.7f, 1};
    mlxr_pass_glow(P, cmd, &b->glow->t, &gp);
    mlxr_barrier(cmd, b->ambi->img.image, VK_IMAGE_ASPECT_COLOR_BIT, SHADER_READ, COLOR_ATT);
    mlxr_barrier(cmd, b->glowEdge->img.image, VK_IMAGE_ASPECT_COLOR_BIT, SHADER_READ, COLOR_ATT);
}

static void recSupersample(VkCommandBuffer cmd, void* u) {
    Bench* b = u;
    MlxrSupersampleParams sp = {b->eye->img.view, {0, 0, 0.5f, 1}, 4};
    mlxr_pass_supersample(P, cmd, &b->small->t, &sp);
}

static void testBench(void) {
    const uint32_t sizes[2][2] = {{1920, 1080}, {3840, 2160}};
    for (int s = 0; s < 2; s++) {
        uint32_t W = sizes[s][0], H = sizes[s][1];
        Video vd;
        if (videoMake(&vd, W, H, fillSmooth)) return;
        Target eye, up, off, ambi, glowEdge, glow, small, down;
        VkhImage map;
        uint8_t* mpx = depthMap(&vd, 512, 288, depthWobble);
        textureMake(&map, 512, 288, mpx);
        free(mpx);
        targetMake(&eye, 2 * W, H, SRGB);
        targetMake(&up, W / 4, H / 4, UNORM);
        targetMake(&off, W / 4, H / 4, UNORM);
        targetMake(&ambi, 32, 32, UNORM);
        targetMake(&glowEdge, 32, 32, UNORM);
        targetMake(&glow, MLXR_GLOW_TEX, MLXR_GLOW_TEX, SRGB);
        targetMake(&small, W * 2 / 3, H * 2 / 3, SRGB);
        targetMake(&down, 512, 288, UNORM);
        Target* all[] = {&eye, &up, &off, &ambi, &glowEdge, &glow, &small, &down};
        VkCommandBuffer cmd = vkh_begin(&vk);
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) vkh_layout(&vk, &all[i]->img, COLOR_ATT);
        vkh_submit(&vk);
        Bench b = {&vd, 0, &eye, &up, &off, &ambi, &glowEdge, &glow, &small, &down, &map, 0, 0, 0, 1};
        // The depth chain's outputs are sampled by the warp: run it once and
        // leave them readable
        cmd = vkh_begin(&vk);
        recUpsample(cmd, &b);
        vkh_layout(&vk, &up.img, SHADER_READ);
        recOffset(cmd, &b);
        vkh_layout(&vk, &off.img, SHADER_READ);
        vkh_submit(&vk);
        // Clocks ramp up under load: a second of throwaway work first
        b.eyes = 2, b.occlusion = 1;
        for (int i = 0; i < 8; i++) timeIt(recWarp, &b, 21);
        for (int nv = 0; nv < 2; nv++) {
            if (nv && !nv12Ok) break;
            b.nv12 = nv;
            const char* k = nv ? "nv12" : "yuv420p";
            b.eyes = 1, b.occlusion = 0, b.sharpen = 0, b.cubic = 0;
            note("%ux%u %s warp mono (colour pass, graded): %.3f ms", W, H, k, timeIt(recWarp, &b, 21));
            b.sharpen = 1;
            note("%ux%u %s warp mono + sharpen: %.3f ms", W, H, k, timeIt(recWarp, &b, 21));
            b.eyes = 2, b.sharpen = 0, b.occlusion = 1;
            note("%ux%u %s warp stereo occlusion (both eyes): %.3f ms", W, H, k, timeIt(recWarp, &b, 21));
            b.cubic = 1;
            note("%ux%u %s warp stereo occlusion + cubic depth: %.3f ms", W, H, k, timeIt(recWarp, &b, 21));
            b.cubic = 0;
            // Upsample writes what the warp reads: give it its own layout
            cmd = vkh_begin(&vk);
            vkh_layout(&vk, &up.img, COLOR_ATT);
            vkh_submit(&vk);
            note("%ux%u %s upsample to %ux%u: %.3f ms", W, H, k, W / 4, H / 4, timeIt(recUpsample, &b, 21));
            cmd = vkh_begin(&vk);
            vkh_layout(&vk, &up.img, SHADER_READ);
            vkh_submit(&vk);
            note("%ux%u %s downscale to 512x288: %.3f ms", W, H, k, timeIt(recDownscale, &b, 21));
            note("%ux%u %s ambi + glow edge + glow: %.3f ms", W, H, k, timeIt(recAmbiGlow, &b, 21));
        }
        cmd = vkh_begin(&vk);
        vkh_layout(&vk, &off.img, COLOR_ATT);
        vkh_submit(&vk);
        note("%ux%u offset search at %ux%u: %.3f ms", W, H, W / 4, H / 4, timeIt(recOffset, &b, 21));
        cmd = vkh_begin(&vk);
        vkh_layout(&vk, &eye.img, SHADER_READ);
        vkh_submit(&vk);
        note("%ux%u supersample one eye to %ux%u, 4 taps: %.3f ms", W, H, W * 2 / 3, H * 2 / 3,
             timeIt(recSupersample, &b, 21));
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) targetFree(all[i]);
        vkh_image_free(&vk, &map);
        videoFree(&vd);
    }
    check(1, "bench ran");
}

int main(int argc, char** argv) {
    const char* group = argc > 1 ? argv[1] : "all";
    if (argc > 2) outDir = argv[2];
    int dev = vkh_init(&vk);
    if (dev != 0) {
        fprintf(stderr, "no Vulkan 1.2 device\n");
        // MLXR_SOFT_GPU=1 (CI) promises lavapipe: a missing driver there is a
        // broken setup, not a skip
        if (softGpu()) return 1;
        return dev == VKH_NO_DEVICE ? 77 : 1;
    }
    if (softGpu()) nv12Tol = 3;
    MlxrPassesInfo info = {vk.phys, vk.device, VK_NULL_HANDLE, 1, MLXR_CHROMA_LEFT, vk.anisotropy ? 8.0f : 0.0f};
    if (mlxr_passes_create(&info, &P)) {
        info.nv12 = 0;
        if (mlxr_passes_create(&info, &P)) {
            fprintf(stderr, "mlxr_passes_create failed\n");
            return 1;
        }
    }
    nv12Ok = info.nv12;
    VkCommandBuffer cmd = vkh_begin(&vk);
    mlxr_passes_prepare(P, cmd);
    vkh_submit(&vk);

    struct {
        const char* name;
        void (*fn)(void);
    } tests[] = {{"device", testDevice},
                 {"grade", testGrade},
                 {"nv12", testNv12},
                 {"srgb", testSrgb},
                 {"warp_identity", testWarpIdentity},
                 {"warp_step", testWarpStep},
                 {"warp_sbs", testWarpSbs},
                 {"sharpen", testSharpen},
                 {"upsample", testUpsample},
                 {"offset", testOffset},
                 {"downscale", testDownscale},
                 {"ambi", testAmbi},
                 {"glow_edge", testGlowEdge},
                 {"glow", testGlow},
                 {"room", testRoom},
                 {"model", testModel},
                 {"supersample", testSupersample},
                 {"panel", testPanel},
                 {"bench", testBench}};
    int ran = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        // The bench only runs when asked for by name
        if (strcmp(group, tests[i].name) && (strcmp(group, "all") || !strcmp(tests[i].name, "bench"))) continue;
        int c0 = checks, f0 = failed;
        printf("%s\n", tests[i].name);
        tests[i].fn();
        printf("%s: %d checks, %d failed\n", tests[i].name, checks - c0, failed - f0);
        ran++;
    }
    vkDeviceWaitIdle(vk.device);
    mlxr_passes_destroy(P);
    vkh_destroy(&vk);
    if (!ran) {
        fprintf(stderr, "no test group %s\n", group);
        return 2;
    }
    return failed ? 1 : 0;
}
