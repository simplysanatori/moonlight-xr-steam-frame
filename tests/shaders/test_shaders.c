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

static int videoUpload(Video* vd);

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
    return videoUpload(vd);
}

// The images of vd's Y, U and V planes (already filled), three plane and NV12
static int videoUpload(Video* vd) {
    uint32_t w = vd->w, h = vd->h;
    uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
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
static void depthChainEx(Depth3d* d, const Video* vd, uint32_t mw, uint32_t mh, const uint8_t* map, float sep,
                         float conv, float sharp, float sigmaR, float hole) {
    textureMake(&d->map, mw, mh, map);
    targetMake(&d->up, vd->w / 4, vd->h / 4, UNORM);
    targetMake(&d->off, vd->w / 4, vd->h / 4, UNORM);
    MlxrUpsampleParams up = {vd->yuv, d->map.view, mw, mh, sigmaR, sharp};
    MlxrOffsetParams op = {d->up.img.view, sep * (float)(vd->w / 4), conv, hole};
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &d->up.img, COLOR_ATT);
    vkh_layout(&vk, &d->off.img, COLOR_ATT);
    mlxr_pass_upsample(P, cmd, &d->up.t, &up);
    vkh_layout(&vk, &d->up.img, SHADER_READ);
    mlxr_pass_offset(P, cmd, &d->off.t, &op);
    vkh_layout(&vk, &d->off.img, SHADER_READ);
    check(vkh_submit(&vk) == 0, "depth chain submit");
}

// The chain as the older groups run it: a tight colour match, no hole fill
static void depthChain(Depth3d* d, const Video* vd, uint32_t mw, uint32_t mh, const uint8_t* map, float sep,
                       float conv, float sharp) {
    depthChainEx(d, vd, mw, mh, map, sep, conv, sharp, 0.1f, 0.0f);
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

// ---- zoom and pan (PLAN 4b.6) ---------------------------------------------------------------
// A part of the picture fills the target: x0, y0 and the side in picture uv.
// A: flat, a ramp in both axes, so each output pixel has a known colour from
// where it lands in the zoomed part. B: inside the eye's half of a host-rendered
// pair, the zoom narrowing the half and nothing of the other half reading in.
// C: with a disparity, the depth is read at the zoomed position, so the zoomed
// picture is the crop of the unzoomed one.

static void fillRamp2(float x, float y, float rgb[3]) {
    rgb[0] = 0.1f + 0.8f * x;
    rgb[1] = 0.1f + 0.8f * y;
    rgb[2] = 0.5f;
}
// Each half holds the same ramp over its own extent; the blue channel says which half
static void fillSbsRamp2(float x, float y, float rgb[3]) {
    float u = x < 0.5f ? 2.0f * x : 2.0f * x - 1.0f;
    rgb[0] = 0.1f + 0.8f * u;
    rgb[1] = 0.1f + 0.8f * y;
    rgb[2] = x < 0.5f ? 0.2f : 0.8f;
}
static void fillTbRamp2(float x, float y, float rgb[3]) {
    float v = y < 0.5f ? 2.0f * y : 2.0f * y - 1.0f;
    rgb[0] = 0.1f + 0.8f * x;
    rgb[1] = 0.1f + 0.8f * v;
    rgb[2] = y < 0.5f ? 0.2f : 0.8f;
}

// The worst channel difference of a render against the ramp it should show:
// the pixel at (a, b) of the target is at (x0 + a s, y0 + b s) of the picture, and the
// picture is the whole frame or one half of it
static int rampDiff(const uint8_t* px, uint32_t w, uint32_t h, float x0, float y0, float s, float blue, int split) {
    int m = 0;
    for (int i = 1; i < 10; i++) {
        for (int j = 1; j < 10; j++) {
            float a = i / 10.0f, b = j / 10.0f;
            float u = x0 + a * s, v = y0 + b * s;
            const uint8_t* p = px + ((size_t)(uint32_t)(b * h) * w + (uint32_t)(a * w)) * 4;
            float want[3] = {0.1f + 0.8f * u, 0.1f + 0.8f * v, blue};
            (void)split;
            for (int c = 0; c < 3; c++) {
                int d = abs((int)p[c] - to8(want[c]));
                if (d > m) m = d;
            }
        }
    }
    return m;
}

static void testWarpZoom(void) {
    const uint32_t W = 256, H = 128, TW = 128, TH = 64;
    Video vd;
    Target t;
    // A: flat
    if (videoMake(&vd, W, H, fillRamp2) || targetMake(&t, TW, TH, UNORM)) {
        check(0, "zoom fixture");
        return;
    }
    vd.yuv.chroma = MLXR_CHROMA_LEFT;
    const struct { float x0, y0, s; const char* name; } cases[] = {
        {0.0f, 0.0f, 1.0f, "whole"}, {0.25f, 0.25f, 0.5f, "200% about the middle"},
        {0.75f, 0.75f, 0.25f, "400% in the bottom right corner"}, {0.0f, 0.0f, 0.25f, "400% in the top left corner"},
        {0.05f, 0.45f, 0.5f, "200% about 30,70"},
    };
    for (int k = 0; k < (nv12Ok ? 2 : 1); k++) {
        const MlxrVideo* v = k ? &vd.nv : &vd.yuv;
        int tol = k ? nv12Tol + 3 : 6;   // the arm64 runner's rounding is a notch wider than lavapipe on x86
        for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            MlxrWarpParams w = warpDefaults(v);
            w.zoomX = cases[c].x0;
            w.zoomY = cases[c].y0;
            w.zoomSize = cases[c].s;
            uint8_t* px = runWarp(&t, &w);
            int d = rampDiff(px, TW, TH, cases[c].x0, cases[c].y0, cases[c].s, 0.5f, 0);
            note("%s, %s: ramp off by at most %d/255", k ? "NV12" : "three-plane", cases[c].name, d);
            check(d <= tol, "%s zoom, %s: every sampled pixel is where the zoomed part puts it (%d/255)",
                  k ? "NV12" : "three-plane", cases[c].name, d);
            if (k == 0) {
                char name[64];
                snprintf(name, sizeof(name), "zoom_flat_%zu", c);
                ppm(name, px, TW, TH);
            }
            free(px);
        }
        // Not zoomed is as it was: a size of 0 (the params' zeros) and the whole picture are the same draw
        MlxrWarpParams w = warpDefaults(v);
        uint8_t* a = runWarp(&t, &w);
        w.zoomSize = 1.0f;
        uint8_t* b = runWarp(&t, &w);
        check(maxDiff(a, b, (size_t)TW * TH, 4, NULL) == 0, "%s: zoom size 0 and 1 draw the same", k ? "NV12" : "three-plane");
        w.zoomSize = 0.5f;
        w.zoomX = 0.3f;
        w.zoomY = 0.2f;
        w.showDepth = 0;
        uint8_t* z = runWarp(&t, &w);
        check(maxDiff(a, z, (size_t)TW * TH, 4, NULL) > 20, "%s: a zoom does change the picture", k ? "NV12" : "three-plane");
        free(a);
        free(b);
        free(z);
    }
    targetFree(&t);
    videoFree(&vd);

    // B: inside the eye's half
    for (int layout = 0; layout < 2; layout++) {
        Video pv;
        Target pt;
        uint32_t ew = layout ? W : W / 2, eh = layout ? H / 2 : H;
        if (videoMake(&pv, W, H, layout ? fillTbRamp2 : fillSbsRamp2) || targetMake(&pt, ew / 2, eh / 2, UNORM)) {
            check(0, "zoom pair fixture");
            return;
        }
        pv.yuv.chroma = MLXR_CHROMA_LEFT;
        const struct { float x0, y0, s; } parts[] = {{0.0f, 0.0f, 1.0f}, {0.25f, 0.25f, 0.5f}, {0.75f, 0.75f, 0.25f},
                                                      {0.0f, 0.5f, 0.5f}, {0.5f, 0.0f, 0.5f}};
        for (int half = 0; half < 2; half++) {
            for (size_t c = 0; c < sizeof(parts) / sizeof(parts[0]); c++) {
                MlxrWarpParams w = warpDefaults(&pv.yuv);
                w.split = layout ? MLXR_SPLIT_TB : MLXR_SPLIT_SBS;
                w.half = half;
                w.eye = half;
                w.zoomX = parts[c].x0;
                w.zoomY = parts[c].y0;
                w.zoomSize = parts[c].s;
                uint8_t* px = runWarp(&pt, &w);
                int d = rampDiff(px, ew / 2, eh / 2, parts[c].x0, parts[c].y0, parts[c].s, half ? 0.8f : 0.2f, 1);
                note("%s eye %d, part %.2f,%.2f side %.2f: off by at most %d/255", layout ? "top-bottom" : "side by side",
                     half, parts[c].x0, parts[c].y0, parts[c].s, d);
                check(d <= 6, "%s eye %d zoomed %.2f,%.2f/%.2f: the part of its own half, none of the other (%d/255)",
                      layout ? "top-bottom" : "side by side", half, parts[c].x0, parts[c].y0, parts[c].s, d);
                if (c == 2) {
                    char name[64];
                    snprintf(name, sizeof(name), "zoom_%s_eye%d", layout ? "tb" : "lr", half);
                    ppm(name, px, ew / 2, eh / 2);
                }
                free(px);
            }
        }
        targetFree(&pt);
        videoFree(&pv);
    }

    // C: the depth read at the zoomed position
    {
        const uint32_t N = 256;
        Video cv;
        Target full, zoomed;
        if (videoMake(&cv, N, 64, fillRamp2) || targetMake(&full, N, 32, UNORM) || targetMake(&zoomed, N, 32, UNORM)) {
            check(0, "zoom depth fixture");
            return;
        }
        // Depth by the pixel's x alone, a ramp from near to far, in the alpha
        uint8_t* dm = malloc((size_t)64 * 8 * 4);
        for (uint32_t y = 0; y < 8; y++)
            for (uint32_t x = 0; x < 64; x++) {
                uint8_t* p = dm + ((size_t)y * 64 + x) * 4;
                p[0] = p[1] = p[2] = 128;
                p[3] = (uint8_t)to8(0.05f + 0.9f * (x + 0.5f) / 64.0f);
            }
        VkhImage dtex;
        textureMake(&dtex, 64, 8, dm);
        free(dm);
        MlxrWarpParams w = warpDefaults(&cv.yuv);
        w.depth = dtex.view;
        w.offsets = dtex.view;
        w.eye = 0;
        w.disparity = 0.2f;
        w.convergence = 0.5f;
        w.srcInset = -1.0f;
        w.edgeFade = 1e-6f;
        uint8_t* a = runWarp(&full, &w);
        const struct { float x0, s; } zs[] = {{0.25f, 0.5f}, {0.5f, 0.5f}, {0.0f, 0.5f}, {0.375f, 0.25f}};
        for (size_t k = 0; k < sizeof(zs) / sizeof(zs[0]); k++) {
            w.zoomX = zs[k].x0;
            w.zoomY = 0.25f;
            w.zoomSize = zs[k].s;
            uint8_t* b = runWarp(&zoomed, &w);
            // The zoomed row against the unzoomed one read between its pixels, where
            // the zoomed pixel lands; red is the ramp of x so a shift moves it
            int worst = 0;
            for (uint32_t i = 8; i < N - 8; i++) {
                float pos = (zs[k].x0 + (i + 0.5f) / N * zs[k].s) * N - 0.5f;
                int p0 = (int)floorf(pos);
                float f = pos - (float)p0;
                if (p0 < 8 || p0 + 1 >= (int)N - 8) continue;
                float ref = a[((size_t)16 * N + p0) * 4] * (1.0f - f) + a[((size_t)16 * N + p0 + 1) * 4] * f;
                int d = (int)fabsf(ref - (float)b[((size_t)16 * N + i) * 4]);
                if (d > worst) worst = d;
            }
            note("depth warp zoomed %.3f..%.3f: the crop of the unzoomed one within %d/255", zs[k].x0, zs[k].x0 + zs[k].s, worst);
            check(worst <= 5, "zoomed %.3f side %.2f with a disparity is the crop of the unzoomed picture (%d/255)", zs[k].x0,
                  zs[k].s, worst);
            if (k == 0) ppm("zoom_depth", b, N, 32);
            free(b);
        }
        // And the shift is there at all (else the check above proves nothing): the same row with and without a disparity
        w.zoomSize = 0.0f;
        w.disparity = 0.0f;
        uint8_t* flat = runWarp(&zoomed, &w);
        check(maxDiff(a, flat, (size_t)N * 32, 4, NULL) > 12, "the disparity moves the picture the test reads");
        free(flat);
        free(a);
        vkh_image_free(&vk, &dtex);
        targetFree(&full);
        targetFree(&zoomed);
        videoFree(&cv);
    }
}

// ---- the fur (#36, #46): a thin dark bar on a bright page ---------------------------

// Native (lane wave5-depth, PLAN 5.2). 1024 wide, the bar from pixel 497 to
// 504, its edges anti-aliased over two pixels as text and hair are
#define FUR_W 1024
#define FUR_BAR0 497.0f
#define FUR_BAR1 504.0f
#define FUR_SOFT 2.0f

static void fillBar(float x, float y, float rgb[3]) {
    float px = x * FUR_W;
    float cover = clampf((px - FUR_BAR0) / FUR_SOFT + 0.5f, 0.0f, 1.0f)
                  * clampf((FUR_BAR1 - px) / FUR_SOFT + 0.5f, 0.0f, 1.0f);
    rgb[0] = rgb[1] = rgb[2] = 0.9f - 0.8f * cover;
}

// The bar near (0.9) over a far page (0.2), a step at the map's texels made
// as soft as a depth model makes it (each texel the mean of itself and the
// one to its left), the guide the picture under each texel
static uint8_t* furMap(const Video* vd, uint32_t mw, uint32_t mh) {
    float* d = malloc(sizeof(float) * mw);
    float* s = malloc(sizeof(float) * mw);
    for (uint32_t x = 0; x < mw; x++) {
        float px = (x + 0.5f) / mw * FUR_W;
        d[x] = px >= FUR_BAR0 && px < FUR_BAR1 ? 0.9f : 0.2f;
    }
    for (uint32_t x = 0; x < mw; x++) s[x] = (d[x == 0 ? 0 : x - 1] + d[x]) / 2.0f;
    uint8_t* out = malloc((size_t)mw * mh * 4);
    for (uint32_t y = 0; y < mh; y++)
        for (uint32_t x = 0; x < mw; x++) {
            float rgb[3];
            videoCpu(vd, MLXR_CHROMA_CENTER, (x + 0.5f) / mw, (y + 0.5f) / mh, rgb);
            uint8_t* p = out + ((size_t)y * mw + x) * 4;
            for (int c = 0; c < 3; c++) p[c] = (uint8_t)to8(rgb[c]);
            p[3] = (uint8_t)to8(s[x]);
        }
    free(d);
    free(s);
    return out;
}

typedef struct {
    int left, right;   // pixels of smear at the bar's left and right edges
    int dark;          // pixels of the bar's own dark
} FurEdges;

// On a row: the pixels strictly between 10 and 90 percent of the way from the
// bar's dark to the page's bright, next to the dark run on each side
static FurEdges furEdges(const uint8_t* row, uint32_t w, float dark, float bright) {
    FurEdges e = {99, 99, 0};
    float lo = dark + 0.1f * (bright - dark), hi = bright - 0.1f * (bright - dark);
    int first = -1, last = -1;
    for (int x = 300; x < 720 && x < (int)w; x++) {
        float v = row[x * 4] / 255.0f;
        if (v <= lo) {
            if (first < 0) first = x;
            last = x;
            e.dark++;
        }
    }
    if (first < 0) return e;
    int i = first;
    while (i > 0 && row[(i - 1) * 4] / 255.0f < hi) i--;
    e.left = first - i;
    int j = last;
    while (j + 1 < (int)w && row[(j + 1) * 4] / 255.0f < hi) j++;
    e.right = j - last;
    return e;
}

// The left eye uncovers the page beside the bar's left edge and the right eye
// beside its right edge. Without the fill the uncovered side is the bar's edge
// stretched across the gap, a smear several pixels wide in one eye where the
// other eye's edge is sharp: the reported "left eye fuzzy, right eye sharp".
// With it the uncovered side in each eye is the picture's own edge, the same
// in both eyes, the covered side stays upstream's, and the bar keeps most of
// its width. The separation puts as many depth texels between the eyes as
// the Frame's default 0.6 percent does on a 1920 wide stream (2.9).
static void testWarpFur(void) {
    const uint32_t W = FUR_W, H = 16, MW = W / 4, MH = H / 4;
    const float sep = 0.012f, conv = 0.5f;
    Video vd;
    Target t;
    if (videoMake(&vd, W, H, fillBar) || targetMake(&t, W, H, UNORM)) return;
    uint8_t* map = furMap(&vd, MW, MH);
    MlxrWarpParams w = warpDefaults(&vd.yuv);
    uint8_t* flat = runWarp(&t, &w);
    const uint8_t* flatRow = flat + (size_t)(H / 2) * W * 4;
    float bright = flatRow[100 * 4] / 255.0f, dark = 1.0f;
    for (uint32_t x = 490; x < 520; x++) dark = fminf(dark, flatRow[x * 4] / 255.0f);
    FurEdges src = furEdges(flatRow, W, dark, bright);
    note("source: page %.3f, bar %.3f, bar %d px, edges %d | %d px", bright, dark, src.dark, src.left, src.right);
    FurEdges e[2][2];   // [fill off, on][eye]
    memset(e, 0, sizeof(e));
    for (int fill = 0; fill < 2; fill++) {
        Depth3d d;
        depthChainEx(&d, &vd, MW, MH, map, sep, conv, 0.0f, 0.25f, fill ? MLXR_HOLE_STRETCH : 0.0f);
        w.depth = d.up.img.view;
        w.offsets = d.off.img.view;
        w.occlusion = 1;
        w.convergence = conv;
        w.dispTexels = sep * (float)MW;
        w.lowResWidth = (float)MW;
        for (int eye = 0; eye < 2; eye++) {
            w.eye = eye;
            w.disparity = eye == 0 ? sep : -sep;
            uint8_t* got = runWarp(&t, &w);
            // Every row is the same picture; the worst row counts
            FurEdges worst = {0, 0, 1 << 20};
            for (uint32_t y = 0; y < H; y++) {
                FurEdges r = furEdges(got + (size_t)y * W * 4, W, dark, bright);
                if (r.left > worst.left) worst.left = r.left;
                if (r.right > worst.right) worst.right = r.right;
                if (r.dark < worst.dark) worst.dark = r.dark;
            }
            e[fill][eye] = worst;
            note("fill %s, %s eye: smear %d px left of the bar, %d px right, bar %d px", fill ? "on " : "off",
                 eye == 0 ? "left " : "right", worst.left, worst.right, worst.dark);
            char name[64];
            snprintf(name, sizeof(name), "warp_fur_%s_%s", fill ? "fill" : "nofill", eye == 0 ? "left" : "right");
            ppm(name, got, W, H);
            free(got);
        }
        depthFree(&d);
    }
    // The fixture shows the bug: without the fill the uncovered edge smears
    // in the eye that uncovers it, three pixels or more past the other eye's
    check(e[0][0].left >= e[0][1].left + 3, "fill off: the left eye's left edge smears (%d px, right eye %d)",
          e[0][0].left, e[0][1].left);
    check(e[0][1].right >= e[0][0].right + 3, "fill off: the right eye's right edge smears (%d px, left eye %d)",
          e[0][1].right, e[0][0].right);
    // With it the edge each eye uncovers is as wide as the one the other eye
    // uncovers (the left eye's left edge against the right eye's right)...
    check(abs(e[1][0].left - e[1][1].right) <= 1, "fill on: uncovered edges %d px left eye, %d right eye",
          e[1][0].left, e[1][1].right);
    // ...the side each eye covers is upstream's, untouched by the fill (its
    // occlusion cut is harder than the picture's own edge, one pixel landing
    // either side of the 90 percent mark between lavapipe builds, so it is
    // held to upstream exactly rather than to the uncovered side's width)...
    check(e[1][0].right == e[0][0].right && e[1][1].left == e[0][1].left,
          "fill on: the covered edges as without it (left eye %d from %d, right eye %d from %d)", e[1][0].right,
          e[0][0].right, e[1][1].left, e[0][1].left);
    // ...narrower than the smear was by two pixels or more...
    check(e[1][0].left <= e[0][0].left - 2 && e[1][1].right <= e[0][1].right - 2,
          "fill on: the uncovered edges narrower (left eye %d from %d, right eye %d from %d)", e[1][0].left,
          e[0][0].left, e[1][1].right, e[0][1].right);
    for (int eye = 0; eye < 2; eye++) {
        // ...no wider than the picture's own anti-aliased edge and a pixel...
        int own = src.left > src.right ? src.left : src.right;
        check(e[1][eye].left <= own + 1 && e[1][eye].right <= own + 1,
              "fill on, eye %d: edges as the picture's own (%d | %d px, the picture's %d)", eye, e[1][eye].left,
              e[1][eye].right, own);
        // ...and the bar is still there, as much of it as without the fill
        check(e[1][eye].dark >= e[0][eye].dark - 2 && e[1][eye].dark >= 2,
              "fill on, eye %d: the bar keeps its dark (%d px, %d without the fill)", eye, e[1][eye].dark,
              e[0][eye].dark);
    }
    free(flat);
    free(map);
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

// CPU copy of OFFSET_FRAGMENT_SRC for one eye: the encoded offset, and in
// *isHole whether the pixel is a hole the shift opened (hole stretch > 0)
static float offsetCpu(const uint8_t* up, int w, int x, int y, float dispTexels, float conv, int eye, float hole,
                       int* isHole) {
    int reach = (int)ceilf(fabsf(dispTexels) * fmaxf(conv, 1.0f - conv)) + 2;
    float disp = eye == 0 ? dispTexels : -dispTexels;
#define D(xx) (up[((size_t)y * w + ((xx) < 0 ? 0 : ((xx) > w - 1 ? w - 1 : (xx)))) * 4 + 3] / 255.0f)
#define E(tt) ((float)(tt) + disp * (D(x + (tt)) - conv))
    float here = D(x);
    float bestD = -1.0f, bestOff = -disp * (here - conv), bestSpan = 1.0f;
    int bestT = 0;
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
                bestSpan = span;
                bestT = t;
            }
        }
        pd = cd;
        pe = ce;
    }
    *isHole = 0;
    if (hole > 0.0f && bestD >= 0.0f && bestSpan > hole) {
        int dir = D(x + bestT) < D(x + bestT - 1) ? 1 : -1;
        int tFar = dir > 0 ? bestT : bestT - 1;
        int tNear = dir > 0 ? bestT - 1 : bestT;
        for (int k = 0; k < 2 * reach && (E(tFar + dir) - E(tFar)) * (float)dir > hole; k++) tFar += dir;
        for (int k = 0; k < 2 * reach && (E(tNear) - E(tNear - dir)) * (float)dir > hole; k++) tNear -= dir;
        float kept = -disp * (D(x + tNear) - conv);
        if ((kept - (float)tNear) * (float)dir >= 0.0f && ((float)tFar - kept) * (float)dir >= 0.0f)
            bestOff = kept;
        else
            bestOff = (float)(tFar + dir);
        bestOff = clampf(bestOff, -(float)reach, (float)reach);
        *isHole = 1;
    }
#undef E
#undef D
    return bestOff / (2.0f * reach) + 0.5f;
}

static void testOffset(void) {
    const uint32_t W = 512, H = 64, MW = 128, MH = 16;
    const float seps[] = {0.0f, 0.02f, 0.05f};
    // Native (lane wave5-depth): each with the hole fill off (upstream's
    // search) and on, as the 3D runs it
    for (int run = 0; run < 6; run++) {
        int k = run % 3;
        float hole = run < 3 ? 0.0f : MLXR_HOLE_STRETCH;
        Video vd;
        videoMake(&vd, W, H, fillStep);
        uint8_t* map = depthMap(&vd, MW, MH, k == 1 ? depthWobble : depthStep);
        Depth3d d;
        float disp = seps[k] * (W / 4);
        depthChainEx(&d, &vd, MW, MH, map, seps[k], 0.5f, 1.0f, 0.1f, hole);
        uint8_t* up = readTarget(&d.up);
        uint8_t* got = readTarget(&d.off);
        int m = 0, holes = 0, flagOff = 0;
        for (uint32_t y = 0; y < H / 4; y++)
            for (uint32_t x = 0; x < W / 4; x++)
                for (int eye = 0; eye < 2; eye++) {
                    int isHole = 0;
                    float e = offsetCpu(up, W / 4, x, y, disp, 0.5f, eye, hole, &isHole);
                    const uint8_t* px = got + ((size_t)y * (W / 4) + x) * 4;
                    int gotHole = px[2 + eye] > 127;
                    holes += isHole;
                    // A segment whose spread sits right on the threshold may
                    // round either way; the offsets are compared where the
                    // two agree
                    if (gotHole != isHole) {
                        flagOff++;
                        continue;
                    }
                    int dd = abs(to8(e) - px[eye]);
                    if (dd > m) m = dd;
                }
        note("offset search, separation %.2f (%.2f texels), hole stretch %.1f: max diff from the CPU copy %d/255, "
             "%d hole texels, %d flagged differently",
             seps[k], disp, hole, m, holes, flagOff);
        check(m <= 1, "offset search within 1/255 (%d)", m);
        check(flagOff <= 2, "holes flagged as the CPU copy flags them (%d off)", flagOff);
        if (hole == 0.0f) check(holes == 0, "no holes with the fill off (%d)", holes);
        // The step at 0.05 (6.4 texels) opens a gap in the left eye on every row
        if (hole > 0.0f && k == 2) check(holes >= 2 * (int)(H / 4), "the step's gap is flagged (%d)", holes);
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

// 3D Eye Balance (native, lane wave3-visual): the offset search with the eyes
// given unequal shares of the shift. dispTexels is the larger eye's; each
// eye's own is that times (1 +- b) / (1 + |b|).
static float offsetCpuBal(const uint8_t* up, int w, int x, int y, float dispTexels, float conv, int eye, float b) {
    float top = 1.0f + fabsf(b);
    float share = eye == 0 ? (1.0f + b) / top : (1.0f - b) / top;
    int reach = (int)ceilf(fabsf(dispTexels) * fmaxf(conv, 1.0f - conv)) + 2;
    float disp = (eye == 0 ? dispTexels : -dispTexels) * share;
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

static void testOffsetBalance(void) {
    const uint32_t W = 512, H = 64, MW = 128, MH = 16;
    const float sep = 0.05f;
    const float balances[] = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f};
    for (int k = 0; k < 5; k++) {
        float b = balances[k];
        Video vd;
        videoMake(&vd, W, H, fillStep);
        uint8_t* map = depthMap(&vd, MW, MH, depthStep);
        Depth3d d;
        depthChain(&d, &vd, MW, MH, map, sep, 0.5f, 1.0f);
        // again with the balance: dispTexels sized for the larger eye
        float disp = sep * (float)(W / 4) * (1.0f + fabsf(b));
        MlxrOffsetParams op = {.depth = d.up.img.view, .dispTexels = disp, .convergence = 0.5f, .balance = b};
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkh_layout(&vk, &d.off.img, COLOR_ATT);
        mlxr_pass_offset(P, cmd, &d.off.t, &op);
        vkh_layout(&vk, &d.off.img, SHADER_READ);
        check(vkh_submit(&vk) == 0, "balanced offset submit");
        uint8_t* up = readTarget(&d.up);
        uint8_t* got = readTarget(&d.off);
        int m = 0, moved[2] = {0, 0};
        for (uint32_t y = 0; y < H / 4; y++)
            for (uint32_t x = 0; x < W / 4; x++)
                for (int eye = 0; eye < 2; eye++) {
                    float e = offsetCpuBal(up, W / 4, x, y, disp, 0.5f, eye, b);
                    int g = got[((size_t)y * (W / 4) + x) * 4 + eye];
                    int dd = abs(to8(e) - g);
                    if (dd > m) m = dd;
                    if (abs(g - 128) > 1) moved[eye]++;
                }
        note("offset search, balance %+.1f: max diff from the CPU copy %d/255, texels moved %d (left) %d (right)", b, m,
             moved[0], moved[1]);
        check(m <= 1, "balanced offset search within 1/255 (%d) at %+.1f", m, b);
        if (b == 0.0f) check(moved[0] > 0 && moved[1] > 0, "an even balance moves both eyes (%d, %d)", moved[0], moved[1]);
        if (b == 1.0f)
            check(moved[1] == 0 && moved[0] > 0, "all in the left eye: the right is not moved (%d), the left is (%d)", moved[1],
                  moved[0]);
        if (b == -1.0f)
            check(moved[0] == 0 && moved[1] > 0, "all in the right eye: the left is not moved (%d), the right is (%d)", moved[0],
                  moved[1]);
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

static float smooth01(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// The Picture tab's Glow edge (native, lane wave3-visual): the glow image with
// u_feather set. Off it is what it always was (testGlow); on, the middle of the
// image, which is behind or over the picture, goes clear, and a band along the
// picture's edge is the glow's colour taken from the border texels, opaque on
// the edge and easing out a feather's depth in. Outside the picture nothing
// moves.
static void testGlowFeather(void) {
    uint8_t px[32 * 32 * 4];
    glowSample(px);
    VkhImage tex;
    textureMake(&tex, 32, 32, px);
    Target t;
    targetMake(&t, MLXR_GLOW_TEX, MLXR_GLOW_TEX, UNORM);
    const float intensity = 0.7f;
    const float fracs[3] = {0.0f, GLOW_FEATHER_SOFT_FRAC, GLOW_FEATHER_WIDE_FRAC};
    const int N = MLXR_GLOW_TEX;
    for (int blur = 0; blur < 2; blur++) {
        uint8_t* img[3];
        for (int k = 0; k < 3; k++) {
            MlxrGlowParams gp = {tex.view, intensity, blur, fracs[k]};
            VkCommandBuffer cmd = vkh_begin(&vk);
            vkh_layout(&vk, &t.img, COLOR_ATT);
            mlxr_pass_glow(P, cmd, &t.t, &gp);
            vkh_submit(&vk);
            img[k] = readTarget(&t);
        }
        // The middle: at the glow's level behind the picture when off, clear when on
        const uint8_t* mid[3];
        for (int k = 0; k < 3; k++) mid[k] = img[k] + ((size_t)(N / 2) * N + N / 2) * 4;
        check(abs(mid[0][3] - to8(intensity)) <= 1, "blur %d: feather off leaves the middle at the glow level (%d)", blur,
              mid[0][3]);
        for (int k = 1; k < 3; k++)
            check(mid[k][3] == 0 && mid[k][0] == 0 && mid[k][1] == 0 && mid[k][2] == 0,
                  "blur %d: feather %.2f clears the middle (%d %d %d %d)", blur, fracs[k], mid[k][0], mid[k][1], mid[k][2],
                  mid[k][3]);
        // Against a CPU copy of the shader inside the picture, and the image
        // outside the picture untouched
        for (int k = 1; k < 3; k++) {
            const float f = fracs[k];
            int m = 0, ma = 0, outsideDiff = 0, bandLit = 0, bandPixels = 0;
            for (int y = 0; y < N; y++)
                for (int x = 0; x < N; x++) {
                    float u = (x + 0.5f) / N, v = (y + 0.5f) / N;
                    float fu = (u - 0.5f) * 1.7f + 0.5f, fv = (v - 0.5f) * 1.7f + 0.5f;
                    float dx = fminf(fu, 1.0f - fu), dy = fminf(fv, 1.0f - fv);
                    const uint8_t* g = img[k] + ((size_t)y * N + x) * 4;
                    const uint8_t* o = img[0] + ((size_t)y * N + x) * 4;
                    if (fminf(dx, dy) < -1e-4f) {
                        for (int c = 0; c < 4; c++)
                            if (abs(g[c] - o[c]) > outsideDiff) outsideDiff = abs(g[c] - o[c]);
                        continue;
                    }
                    if (fminf(dx, dy) < 1e-4f) continue;   // on the edge itself: either side of it
                    float hx = 1.0f - smooth01(0.0f, f, dx), hy = 1.0f - smooth01(0.0f, f, dy);
                    float sx = fu + ((fu < 0.5f ? 0.0f : 1.0f) - fu) * hx;
                    float sy = fv + ((fv < 0.5f ? 0.0f : 1.0f) - fv) * hy;
                    float col[3];
                    if (blur)
                        blurredCpu(px, sx, sy, col);
                    else
                        splineCpu(px, sx, sy, col);
                    float ft = clampf(fminf(dx, dy) / f, 0.0f, 1.0f);
                    float ramp = 1.0f - ft * ft * (3.0f - 2.0f * ft);
                    for (int c = 0; c < 3; c++) {
                        int dd = abs(to8(col[c] * intensity * ramp) - g[c]);
                        if (dd > m) m = dd;
                    }
                    int da = abs(to8(ramp) - g[3]);
                    if (da > ma) ma = da;
                    if (fminf(dx, dy) < f * 0.9f) {
                        bandPixels++;
                        if (g[3] > 0) bandLit++;
                    }
                }
            note("glow feather %.2f blur %d: colour max diff %d/255, alpha %d/255; %d of %d band pixels lit", f, blur, m, ma,
                 bandLit, bandPixels);
            check(m <= 3 && ma <= 2, "glow feather %.2f blur %d within 3/255 of the CPU copy (colour %d, alpha %d)", f, blur,
                  m, ma);
            check(outsideDiff == 0, "glow feather %.2f blur %d leaves outside the picture alone (%d)", f, blur, outsideDiff);
            check(bandPixels > 0 && bandLit == bandPixels, "the feather band is lit (%d of %d)", bandLit, bandPixels);
        }
        // Next to the left edge, half way down: nearly opaque, in the border
        // texels' colour at the glow level. A little further in the soft feather
        // has cleared while the wide one is still fading.
        int row = N / 2;
        float fvRow = ((row + 0.5f) / N - 0.5f) * 1.7f + 0.5f;
        int xe = (int)ceilf((0.5f - 0.5f / 1.7f) * N) + 1;   // the first column inside the picture
        const uint8_t* edgePx = img[2] + ((size_t)row * N + xe) * 4;
        float border[3];
        if (blur)
            blurredCpu(px, 0.0f, fvRow, border);
        else
            splineCpu(px, 0.0f, fvRow, border);
        check(edgePx[3] > 235, "blur %d: the wide feather is nearly opaque on the picture's edge (%d)", blur, edgePx[3]);
        int worst = 0;
        for (int c = 0; c < 3; c++) {
            int want = to8(border[c] * intensity * (edgePx[3] / 255.0f));
            if (abs(edgePx[c] - want) > worst) worst = abs(edgePx[c] - want);
        }
        check(worst <= 12, "blur %d: and in the colour of the border texels there at the glow level (%d off)", blur, worst);
        int xin = xe + (int)(GLOW_FEATHER_SOFT_FRAC * 1.5f * N / 1.7f);
        const uint8_t* soft = img[1] + ((size_t)row * N + xin) * 4;
        const uint8_t* wide = img[2] + ((size_t)row * N + xin) * 4;
        check(soft[3] == 0 && wide[3] > 0 && wide[3] < edgePx[3], "blur %d: soft clears (%d) where wide is still fading (%d of %d)",
              blur, soft[3], wide[3], edgePx[3]);
        if (blur == 0) {
            ppm("glow_feather_soft", img[1], N, N);
            ppm("glow_feather_wide", img[2], N, N);
        }
        for (int k = 0; k < 3; k++) free(img[k]);
    }
    targetFree(&t);
    vkh_image_free(&vk, &tex);
}

// The Picture tab's Background dim on the room (native, lane wave3-visual):
// u_keep scales the whole of what the room shows, the wash of the picture's
// light included, to black at the far end
static void testRoomDim(void) {
    const uint32_t EW = 64, EH = 64;
    Target t;
    VkhImage depth;
    targetMake(&t, 2 * EW, EH, UNORM);
    vkh_image(&vk, 2 * EW, EH, VK_FORMAT_D32_SFLOAT, &depth);
    VkhBuffer vb, ib;
    vkh_buffer(&vk, 8 * MLXR_ROOM_VERTEX_FLOATS * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vb);
    vkh_buffer(&vk, 12 * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &ib);
    const float red[3] = {0.8f, 0.3f, 0.2f}, grey[3] = {0.5f, 0.5f, 0.5f};
    quadVerts(vb.map, 0, -20, -20, 20, 20, -10, red, 0.0f);          // fills the view, unlit
    quadVerts(vb.map, 4, -0.5f, -0.5f, 0.5f, 0.5f, -1, grey, 1.0f);   // lit by the picture's light
    uint32_t* idx = ib.map;
    for (int q = 0; q < 2; q++) {
        uint32_t b = q * 4, quad[6] = {b, b + 1, b + 2, b + 2, b + 1, b + 3};
        memcpy(idx + q * 6, quad, sizeof(quad));
    }
    VkhImage ambi;
    uint8_t ambiPx[32 * 32 * 4];
    for (int i = 0; i < 32 * 32; i++) {
        ambiPx[i * 4] = 204;
        ambiPx[i * 4 + 1] = 153;
        ambiPx[i * 4 + 2] = 102;
        ambiPx[i * 4 + 3] = 255;
    }
    textureMake(&ambi, 32, 32, ambiPx);
    MlxrRoomPart parts[2] = {{0, 6, VK_NULL_HANDLE, 0.0f}, {6, 6, VK_NULL_HANDLE, 0.0f}};
    const float dims[] = {0.0f, 0.25f, 0.5f, 0.8f, 1.0f};
    uint8_t* base = NULL;
    double baseMean = 0;
    size_t n = (size_t)2 * EW * EH;
    for (int k = 0; k < 5; k++) {
        MlxrRoomParams rp;
        memset(&rp, 0, sizeof(rp));
        rp.ambi = ambi.view;
        rp.vertices = vb.buffer;
        rp.indices = ib.buffer;
        rp.parts = parts;
        rp.partCount = 2;
        rp.eyes = 2;
        eyeViewProj(rp.viewProj[0], -0.03f);
        eyeViewProj(rp.viewProj[1], 0.03f);
        rp.eyeRect[0] = (VkRect2D){{0, 0}, {EW, EH}};
        rp.eyeRect[1] = (VkRect2D){{(int32_t)EW, 0}, {EW, EH}};
        rp.spillGain = 0.5f;
        rp.dim = 0.8f;
        rp.bgDim = dims[k];
        rp.depth = depth.view;
        rp.depthFormat = VK_FORMAT_D32_SFLOAT;
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkh_layout(&vk, &t.img, COLOR_ATT);
        vkh_layout(&vk, &depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        mlxr_pass_room(P, cmd, &t.t, &rp);
        check(vkh_submit(&vk) == 0, "room dim submit");
        uint8_t* got = readTarget(&t);
        double mean = 0;
        int alphaBad = 0;
        for (size_t i = 0; i < n; i++) {
            for (int c = 0; c < 3; c++) mean += got[i * 4 + c];
            if (got[i * 4 + 3] != 255) alphaBad++;
        }
        mean /= (double)(n * 3);
        check(alphaBad == 0, "room dim %.2f keeps the room opaque (%d)", dims[k], alphaBad);
        if (k == 0) {
            base = got;
            baseMean = mean;
            check(baseMean > 20.0, "the undimmed room is lit (mean %.1f)", baseMean);
            continue;
        }
        double want = baseMean * (1.0 - dims[k]);
        note("room dim %.2f: mean luminance %.2f, %.2f of the undimmed %.2f", dims[k], mean, mean / baseMean, baseMean);
        check(fabs(mean - want) < 1.0, "room dim %.2f scales the mean luminance (%.2f, wanted %.2f)", dims[k], mean, want);
        int worst = 0, lit = 0;
        for (size_t i = 0; i < n; i++)
            for (int c = 0; c < 3; c++) {
                int w = to8(base[i * 4 + c] / 255.0f * (1.0f - dims[k]));
                if (abs(w - got[i * 4 + c]) > worst) worst = abs(w - got[i * 4 + c]);
                if (got[i * 4 + c] > 0) lit++;
            }
        check(worst <= 1, "room dim %.2f scales every pixel (worst %d/255)", dims[k], worst);
        if (dims[k] >= 1.0f) check(lit == 0, "all the way down is black (%d lit)", lit);
        if (k == 3) ppm("room_dim_80", got, 2 * EW, EH);
        free(got);
    }
    ppm("room_dim_0", base, 2 * EW, EH);
    free(base);
    vkh_buffer_free(&vk, &vb);
    vkh_buffer_free(&vk, &ib);
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

// ---- text clarity (lane text-clarity) ------------------------------------
// The owner, after a headset run: small text "could still be a little bit
// better". This renders a synthetic desktop (text_scene.h: Noto Sans at 12 to
// 16 px em, grey and ClearType antialiasing, a dark theme, syntax colours, a
// slanted edge, 1 px line grids, a zone plate) the way a stream reaches the
// picture: 2560x1440 encoded to 4:2:0 (BT.709 limited, chroma left sited),
// the warp into the curved picture's own image, its mip chain
// (mlxr_record_mips, the session's), and the panel pass into one eye's
// projection image at the Frame's 3408x3408, from the eye in front of the
// screen as the Screen tab places it. Then it scores what came out against
// the picture itself:
//   MTF50     slanted edge, cycles per desktop pixel, horizontal and vertical
//   overshoot the slanted edge's ring past either side, % of the step
//   lines     the 1 px line grids' modulation (fitted fundamental, out / in)
//             at periods 3, 4 and 6 px
//   contrast  text tiles' luma spread against the ideal's (1 is the ideal:
//             the area average of the desktop under each eye pixel)
//   text RMS  grey and ClearType text against that ideal, luma, /255
//   chroma    syntax coloured and red / blue text, chroma RMS against it, /255
//   alias     the zone plate's left over modulation beyond the eye image's
//             Nyquist, RMS /255 (the ideal's own figure beside it)
// and the same after a model of SteamVR's own resample of the layer (bilinear,
// 1:1, a 0.3 px phase: at the lens centre the Frame's 3408 per eye is about
// one panel pixel a pixel). The eye's field is taken as 100 degrees square.
#include "text_scene.h"

#define TX_EYE 3408
#define TX_TAN 1.19175359f // tan(50 degrees)
#define TX_COMP 0.3f       // the compositor model's phase, eye pixels
#define TX_MARGIN 14.0f    // desktop pixels kept clear of a tile's sides

typedef struct {
    const char* name;
    float width, dist, curve; // the Screen tab: metres, metres, 0..1
} TxGeom;

static const TxGeom TX_GEOMS[] = {
    {"3 m wide at 3 m, curve 50 (the default size and distance)", 3.0f, 3.0f, 0.5f},
    {"4 m wide at 2.5 m, curve 50 (big and near)", 4.0f, 2.5f, 0.5f},
    {"2 m wide at 4 m, curve 50 (small and far)", 2.0f, 4.0f, 0.5f},
};
#define TX_GEOM_COUNT ((int)(sizeof(TX_GEOMS) / sizeof(TX_GEOMS[0])))

typedef struct {
    const char* name;
    float warpSharpen;  // the warp's CAS (the Display tab's Sharpen before: 0.4 normal, 0.8 quality)
    int pictureFilter;  // the panel pass's footprint filter (0: one trilinear read, as before)
    float sharpen;      // the panel pass's eye scale sharpen (0.5 normal, 1 quality)
    int chroma;         // chroma from luma in the warp
    int video;          // 0 three planes 4:2:0, 1 NV12, 2 three planes 4:4:4 (a bound)
    int everywhere;     // 1: at every placement; 0: the default placement only
    int crawl;          // 1: the crawl run too
} TxCfg;

typedef struct {
    float W, H, R, D; // the drawn surface: width, height, radius (0 flat), distance
    float shift;      // the surface moved sideways, metres (the phase runs)
} TxSurf;

typedef struct {
    float mtf50[2];   // horizontal, vertical; cycles per desktop pixel
    float overshoot;  // % of the step, the worse side of either edge
    float lines[3];   // periods 3, 4, 6
    float contrast;   // text luma spread over the ideal's
    float textRms;    // /255
    float chromaRms;  // /255
    float alias;      // /255
    float aliasIdeal; // /255
    float textStd;    // the text tiles' luma spread, /255 (the phase runs)
} TxScore;

static float* txSrc; // the desktop, RGB, gamma encoded

static float txLuma(const float* c) {
    return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
}

static void txChroma(const float* c, float* cb, float* cr) {
    float y = txLuma(c);
    *cb = (c[2] - y) / 1.8556f;
    *cr = (c[0] - y) / 1.5748f;
}

// The desktop at continuous pixel coordinates (pixel k spans k..k+1), bilinear
static void txSrcAt(float sx, float sy, float out[3]) {
    float x = sx - 0.5f, y = sy - 0.5f;
    float fx = floorf(x), fy = floorf(y);
    float ax = x - fx, ay = y - fy;
    int x0 = (int)fx, y0 = (int)fy;
    for (int c = 0; c < 3; c++) out[c] = 0.0f;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int X = x0 + i, Y = y0 + j;
            X = X < 0 ? 0 : (X >= TXS_W ? TXS_W - 1 : X);
            Y = Y < 0 ? 0 : (Y >= TXS_H ? TXS_H - 1 : Y);
            float wgt = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
            const float* p = txSrc + ((size_t)Y * TXS_W + X) * 3;
            for (int c = 0; c < 3; c++) out[c] += wgt * p[c];
        }
}

static TxSurf txSurf(const TxGeom* g) {
    TxSurf s;
    float aspect = (float)TXS_H / TXS_W;
    s.D = g->dist;
    if (g->curve > 0.01f) {
        // xr_input.c: radius = distance (1 + 3 (1 - curvature)); xrCurveShape
        s.R = g->dist * (1.0f + 3.0f * (1.0f - g->curve));
        float fit = cylinderFit(g->width, s.R);
        s.W = g->width * fit;
        s.H = g->width * aspect * fit;
    } else {
        s.R = 0.0f;
        s.W = g->width;
        s.H = g->width * aspect;
    }
    return s;
}

// An eye image position (continuous pixels, row 0 the top) to the desktop's
// continuous pixel coordinates: the eye at the origin looking down -z, the
// surface as panel.vert draws it, centred on (0, 0, -D)
static int txMap(const TxSurf* s, float fx, float fy, float* sx, float* sy) {
    float dx = (fx / TX_EYE * 2.0f - 1.0f) * TX_TAN, dy = (1.0f - fy / TX_EYE * 2.0f) * TX_TAN;
    float u, v;
    if (s->R <= 0.0f) {
        u = (dx * s->D - s->shift) / s->W + 0.5f;
        v = 0.5f - dy * s->D / s->H;
    } else {
        float zc = s->R - s->D; // the axis
        float a = dx * dx + 1.0f, b = 2.0f * zc, c = zc * zc - s->R * s->R;
        float disc = b * b - 4.0f * a * c;
        if (disc < 0.0f) return 0;
        float t = (-b + sqrtf(disc)) / (2.0f * a);
        float zc2 = zc;
        // the axis moved with the surface: solve about (shift, zc)
        if (s->shift != 0.0f) {
            float bx = -2.0f * dx * s->shift + 2.0f * zc2;
            float cx = s->shift * s->shift + zc2 * zc2 - s->R * s->R;
            float dd = bx * bx - 4.0f * a * cx;
            if (dd < 0.0f) return 0;
            t = (-bx + sqrtf(dd)) / (2.0f * a);
        }
        u = atan2f(t * dx - s->shift, zc + t) * s->R / s->W + 0.5f;
        v = 0.5f - t * dy / s->H;
    }
    *sx = u * TXS_W;
    *sy = v * TXS_H;
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

// The eye image box the picture covers (a little generous)
static void txBox(const TxSurf* s, int* x0, int* y0, int* x1, int* y1) {
    *x0 = TX_EYE;
    *y0 = TX_EYE;
    *x1 = 0;
    *y1 = 0;
    for (int y = 0; y < TX_EYE; y += 4)
        for (int x = 0; x < TX_EYE; x += 4) {
            float sx, sy;
            if (!txMap(s, x + 0.5f, y + 0.5f, &sx, &sy)) continue;
            if (x < *x0) *x0 = x;
            if (y < *y0) *y0 = y;
            if (x > *x1) *x1 = x;
            if (y > *y1) *y1 = y;
        }
    *x0 = *x0 > 4 ? *x0 - 4 : 0;
    *y0 = *y0 > 4 ? *y0 - 4 : 0;
    *x1 = *x1 + 5 < TX_EYE ? *x1 + 5 : TX_EYE;
    *y1 = *y1 + 5 < TX_EYE ? *y1 + 5 : TX_EYE;
}

// The ideal eye pixel at continuous position (fx, fy) (its centre): the area
// average of the desktop under the pixel's footprint, 6 x 6 points
static void txIdeal(const TxSurf* s, float fx, float fy, float out[3]) {
    out[0] = out[1] = out[2] = 0.0f;
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 6; i++) {
            float sx, sy, c[3];
            txMap(s, fx - 0.5f + (i + 0.5f) / 6.0f, fy - 0.5f + (j + 0.5f) / 6.0f, &sx, &sy);
            txSrcAt(sx, sy, c);
            for (int k = 0; k < 3; k++) out[k] += c[k] / 36.0f;
        }
}

// The eye image (RGBA8) at continuous position (fx, fy), bilinear
static void txLayerAt(const uint8_t* img, float fx, float fy, float out[3]) {
    float x = fx - 0.5f, y = fy - 0.5f;
    float bx = floorf(x), by = floorf(y);
    float ax = x - bx, ay = y - by;
    int x0 = (int)bx, y0 = (int)by;
    out[0] = out[1] = out[2] = 0.0f;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int X = x0 + i, Y = y0 + j;
            X = X < 0 ? 0 : (X >= TX_EYE ? TX_EYE - 1 : X);
            Y = Y < 0 ? 0 : (Y >= TX_EYE ? TX_EYE - 1 : Y);
            float wgt = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
            const uint8_t* p = img + ((size_t)Y * TX_EYE + X) * 4;
            for (int c = 0; c < 3; c++) out[c] += wgt * p[c] / 255.0f;
        }
}

#define TX_ESF_BINS 240
#define TX_ESF_BIN 0.1f
#define TX_ESF_HALF 12.0f

// MTF50 (cycles per desktop pixel) and the worse overshoot of a binned edge
// spread function
static float txMtf50(const double* sum, const int* cnt, float* overshoot) {
    float esf[TX_ESF_BINS];
    int last = -1;
    for (int i = 0; i < TX_ESF_BINS; i++) {
        if (cnt[i] == 0) continue;
        esf[i] = (float)(sum[i] / cnt[i]);
        if (last >= 0 && i - last > 1)
            for (int k = last + 1; k < i; k++) esf[k] = esf[last] + (esf[i] - esf[last]) * (k - last) / (i - last);
        else if (last < 0)
            for (int k = 0; k < i; k++) esf[k] = esf[i];
        last = i;
    }
    if (last < 0) return 0.0f;
    for (int k = last + 1; k < TX_ESF_BINS; k++) esf[k] = esf[last];
    double lo = 0, hi = 0;
    int nl = 0, nh = 0;
    float mx = -1e9f, mn = 1e9f;
    for (int i = 0; i < TX_ESF_BINS; i++) {
        float d = -TX_ESF_HALF + (i + 0.5f) * TX_ESF_BIN;
        if (d < -7.0f) lo += esf[i], nl++;
        if (d > 7.0f) hi += esf[i], nh++;
        if (esf[i] > mx) mx = esf[i];
        if (esf[i] < mn) mn = esf[i];
    }
    lo /= nl;
    hi /= nh;
    float step = (float)(hi - lo);
    float over = fmaxf(mx - (float)hi, (float)lo - mn) / step;
    *overshoot = over > 0.0f ? 100.0f * over : 0.0f;
    float lsf[TX_ESF_BINS];
    for (int i = 0; i < TX_ESF_BINS; i++) {
        int a = i > 0 ? i - 1 : i, b = i + 1 < TX_ESF_BINS ? i + 1 : i;
        float d = -TX_ESF_HALF + (i + 0.5f) * TX_ESF_BIN;
        float hann = 0.5f + 0.5f * cosf((float)M_PI * d / TX_ESF_HALF);
        lsf[i] = (esf[b] - esf[a]) / ((b - a) * TX_ESF_BIN) * hann;
    }
    double dc = 0;
    for (int i = 0; i < TX_ESF_BINS; i++) dc += lsf[i];
    float prev = 1.0f;
    for (float f = 0.002f; f < 1.0f; f += 0.002f) {
        double re = 0, im = 0;
        for (int i = 0; i < TX_ESF_BINS; i++) {
            float d = -TX_ESF_HALF + (i + 0.5f) * TX_ESF_BIN;
            re += lsf[i] * cos(2 * M_PI * f * d);
            im -= lsf[i] * sin(2 * M_PI * f * d);
        }
        float m = (float)(sqrt(re * re + im * im) / dc);
        if (m < 0.5f) return f - 0.002f * (0.5f - m) / (prev - m);
        prev = m;
    }
    return 1.0f;
}

// Least squares fit of v = a + b cos(2 pi p / P) + c sin(...), as normal
// equations; the fundamental's amplitude
typedef struct {
    double m[3][3], r[3];
} TxFit;

static void txFitAdd(TxFit* f, float pos, float period, float v) {
    double bas[3] = {1.0, cos(2 * M_PI * pos / period), sin(2 * M_PI * pos / period)};
    for (int i = 0; i < 3; i++) {
        f->r[i] += bas[i] * v;
        for (int j = 0; j < 3; j++) f->m[i][j] += bas[i] * bas[j];
    }
}

static float txFitAmp(const TxFit* f) {
    double a[3][4];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) a[i][j] = f->m[i][j];
        a[i][3] = f->r[i];
    }
    for (int c = 0; c < 3; c++) {
        int p = c;
        for (int i = c + 1; i < 3; i++)
            if (fabs(a[i][c]) > fabs(a[p][c])) p = i;
        for (int j = 0; j < 4; j++) {
            double t = a[c][j];
            a[c][j] = a[p][j];
            a[p][j] = t;
        }
        if (fabs(a[c][c]) < 1e-12) return 0.0f;
        for (int i = 0; i < 3; i++) {
            if (i == c) continue;
            double k = a[i][c] / a[c][c];
            for (int j = c; j < 4; j++) a[i][j] -= k * a[c][j];
        }
    }
    double b = a[1][3] / a[1][1], cc = a[2][3] / a[2][2];
    return (float)sqrt(b * b + cc * cc);
}

static const int TX_LINE_PERIODS[3] = {3, 4, 6};

// The source's own fundamental for 1 px lines every P pixels
static float txLinesSourceAmp(int period) {
    TxFit f;
    memset(&f, 0, sizeof(f));
    for (int k = 0; k < period * 64; k++) txFitAdd(&f, k + 0.5f, (float)period, k % period == 0 ? 0.0f : 1.0f);
    return txFitAmp(&f);
}

static int txIn(int tile, float sx, float sy, float margin, float* lx, float* ly) {
    int x0, y0;
    txs_tile(tile, &x0, &y0);
    *lx = sx - x0;
    *ly = sy - y0;
    return *lx >= margin && *ly >= margin && *lx <= TXS_TILE_W - margin && *ly <= TXS_TILE_H - margin;
}

static int txTextTile(int t) {
    return t == TXS_TEXT_BW || t == TXS_TEXT_WD || t == TXS_TEXT_CT || t == TXS_TEXT_CT_WD || t >= TXS_TEXT_BW2;
}

// Per placement and read offset: each eye pixel's desktop position and, where
// a score needs it, its ideal colour (worked out once, used by every run)
typedef struct {
    int x0, y0, w, h;
    float off;
    float* sxy;   // 2 a pixel; NAN outside the picture
    float* ideal; // 3 a pixel; NAN where no score needs it
} TxCache;

static void txCacheMake(TxCache* k, const TxSurf* s, float off, int noIdeal) {
    int x1, y1;
    txBox(s, &k->x0, &k->y0, &x1, &y1);
    k->w = x1 - k->x0;
    k->h = y1 - k->y0;
    k->off = off;
    k->sxy = malloc(sizeof(float) * 2 * k->w * k->h);
    k->ideal = malloc(sizeof(float) * 3 * k->w * k->h);
    for (int y = 0; y < k->h; y++)
        for (int x = 0; x < k->w; x++) {
            size_t i = (size_t)y * k->w + x;
            float fx = k->x0 + x + 0.5f + off, fy = k->y0 + y + 0.5f + off, sx, sy;
            k->ideal[i * 3] = NAN;
            if (!txMap(s, fx, fy, &sx, &sy)) {
                k->sxy[i * 2] = NAN;
                continue;
            }
            k->sxy[i * 2] = sx;
            k->sxy[i * 2 + 1] = sy;
            int tile = (int)(sx / TXS_TILE_W) + 4 * (int)(sy / TXS_TILE_H);
            float lx, ly;
            if (noIdeal || tile < 0 || tile >= TXS_TILES || !txIn(tile, sx, sy, TX_MARGIN, &lx, &ly)) continue;
            txIdeal(s, fx, fy, &k->ideal[i * 3]);
        }
}

static void txCacheFree(TxCache* k) {
    free(k->sxy);
    free(k->ideal);
}

// Scores an eye image read at the cache's offset (0: the layer as drawn;
// TX_COMP: the compositor model). img NULL: the ideal itself.
static TxScore txScore(const TxSurf* s, const TxCache* k, const uint8_t* img) {
    TxScore sc;
    memset(&sc, 0, sizeof(sc));
    float off = k->off;
    int ideal = img == NULL;
    int bx0 = k->x0, by0 = k->y0, bx1 = k->x0 + k->w, by1 = k->y0 + k->h;
    static double esfSum[2][TX_ESF_BINS];
    static int esfCnt[2][TX_ESF_BINS];
    memset(esfSum, 0, sizeof(esfSum));
    memset(esfCnt, 0, sizeof(esfCnt));
    TxFit fits[2][3];
    memset(fits, 0, sizeof(fits));
    double tSq = 0, cSq = 0, aSq = 0;
    long tN = 0, cN = 0, aN = 0;
    // text spread: sums of luma and squares, out and ideal
    double so = 0, so2 = 0, si = 0, si2 = 0;
    long sn = 0;
    // the eye image's Nyquist in desktop cycles a pixel, from the centre
    float ax0, ay0, ax1, ay1;
    txMap(s, TX_EYE / 2.0f, TX_EYE / 2.0f, &ax0, &ay0);
    txMap(s, TX_EYE / 2.0f + 1.0f, TX_EYE / 2.0f, &ax1, &ay1);
    float ratio = fabsf(ax1 - ax0);
    float nyq = 0.5f / ratio;
    float cosT = 1.0f / sqrtf(1.0f + TXS_EDGE_TAN * TXS_EDGE_TAN);
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            float fx = x + 0.5f + off, fy = y + 0.5f + off;
            size_t ki = (size_t)(y - by0) * k->w + (x - bx0);
            float sx = k->sxy[ki * 2], sy = k->sxy[ki * 2 + 1];
            if (isnan(sx)) continue;
            int tile = (int)(sx / TXS_TILE_W) + 4 * (int)(sy / TXS_TILE_H);
            if (tile < 0 || tile >= TXS_TILES) continue;
            float lx, ly;
            if (!txIn(tile, sx, sy, TX_MARGIN, &lx, &ly)) continue;
            float c[3];
            const float* id = &k->ideal[ki * 3];
            if (ideal)
                memcpy(c, id, sizeof(c));
            else if (off != 0.0f)
                txLayerAt(img, fx, fy, c);
            else
                for (int k = 0; k < 3; k++) c[k] = img[((size_t)y * TX_EYE + x) * 4 + k] / 255.0f;
            float l = txLuma(c);
            if (tile == TXS_EDGE_V || tile == TXS_EDGE_H) {
                float d = tile == TXS_EDGE_V ? (lx - (TXS_TILE_W / 2.0f + (ly - TXS_TILE_H / 2.0f) * TXS_EDGE_TAN))
                                             : (ly - (TXS_TILE_H / 2.0f - (lx - TXS_TILE_W / 2.0f) * TXS_EDGE_TAN));
                d *= cosT;
                int b = (int)floorf((d + TX_ESF_HALF) / TX_ESF_BIN);
                if (b >= 0 && b < TX_ESF_BINS) {
                    esfSum[tile == TXS_EDGE_H][b] += l;
                    esfCnt[tile == TXS_EDGE_H][b]++;
                }
            } else if (tile == TXS_LINES_V || tile == TXS_LINES_H) {
                int vert = tile == TXS_LINES_V;
                float along = vert ? lx : ly, bandW = vert ? TXS_TILE_W / 4.0f : TXS_TILE_H / 4.0f;
                int band = (int)(along / bandW);
                float inBand = along - band * bandW;
                if (inBand < 6.0f || inBand > bandW - 6.0f || band < 1) continue;
                txFitAdd(&fits[!vert][band - 1], along, (float)txs_period(band), l);
            } else if (tile == TXS_ZONE) {
                float dx = lx - TXS_TILE_W / 2.0f, dy = ly - TXS_TILE_H / 2.0f;
                float f = sqrtf(dx * dx + dy * dy) / 360.0f;
                if (f > 1.15f * nyq && f < 0.5f && f * 360.0f < TXS_TILE_H / 2.0f - 4.0f) {
                    aSq += (l - 0.5) * (l - 0.5);
                    aN++;
                }
            } else if ((tile == TXS_TEXT_CODE || tile == TXS_TEXT_RED) && !isnan(id[0])) {
                float cb, cr, ib, ir;
                txChroma(c, &cb, &cr);
                txChroma(id, &ib, &ir);
                cSq += (cb - ib) * (cb - ib) + (cr - ir) * (cr - ir);
                cN += 2;
            } else if (txTextTile(tile)) {
                float il = isnan(id[0]) ? 0.0f : txLuma(id);
                tSq += (l - il) * (l - il);
                tN++;
                so += l;
                so2 += l * l;
                si += il;
                si2 += il * il;
                sn++;
            }
        }
    for (int e = 0; e < 2; e++) {
        float o;
        sc.mtf50[e] = txMtf50(esfSum[e], esfCnt[e], &o);
        if (o > sc.overshoot) sc.overshoot = o;
    }
    for (int p = 0; p < 3; p++) {
        float src = txLinesSourceAmp(TX_LINE_PERIODS[p]);
        sc.lines[p] = 0.5f * (txFitAmp(&fits[0][p]) + txFitAmp(&fits[1][p])) / src;
    }
    double vo = so2 / sn - (so / sn) * (so / sn), vi = si2 / sn - (si / sn) * (si / sn);
    sc.contrast = (float)sqrt(vo / vi);
    sc.textStd = (float)(255.0 * sqrt(vo));
    sc.textRms = tN ? (float)(255.0 * sqrt(tSq / tN)) : 0.0f;
    sc.chromaRms = cN ? (float)(255.0 * sqrt(cSq / cN)) : 0.0f;
    sc.alias = aN ? (float)(255.0 * sqrt(aSq / aN)) : -1.0f;
    return sc;
}

static void txPrint(const char* label, const TxScore* s) {
    note("%-34s MTF50 %.3f %.3f  over %4.1f%%  lines %.2f %.2f %.2f  contrast %.2f  text %5.1f  chroma %4.1f  "
         "alias %5.1f",
         label, s->mtf50[0], s->mtf50[1], s->overshoot, s->lines[0], s->lines[1], s->lines[2], s->contrast, s->textRms,
         s->chromaRms, s->alias);
}

// The stream's frame: 4:2:0, BT.709 limited, chroma left sited (even columns,
// 1 2 1 across, between the two rows), as a hardware encoder hands it on
static int txVideo(Video* vd) {
    memset(vd, 0, sizeof(*vd));
    uint32_t w = TXS_W, h = TXS_H, cw = w / 2, ch = h / 2;
    vd->w = w;
    vd->h = h;
    vd->Y = malloc((size_t)w * h);
    vd->U = malloc((size_t)cw * ch);
    vd->V = malloc((size_t)cw * ch);
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++) {
            const float* p = txSrc + ((size_t)y * w + x) * 3;
            vd->Y[(size_t)y * w + x] = (uint8_t)to8((16.0f + 219.0f * txLuma(p)) / 255.0f);
        }
    for (uint32_t j = 0; j < ch; j++)
        for (uint32_t i = 0; i < cw; i++) {
            float cb = 0, cr = 0;
            for (int r = 0; r < 2; r++)
                for (int k = -1; k <= 1; k++) {
                    int X = (int)(2 * i) + k;
                    X = X < 0 ? 0 : X;
                    float wgt = (k == 0 ? 0.5f : 0.25f) * 0.5f, b, rr;
                    txChroma(txSrc + ((size_t)(2 * j + r) * w + X) * 3, &b, &rr);
                    cb += wgt * b;
                    cr += wgt * rr;
                }
            vd->U[(size_t)j * cw + i] = (uint8_t)to8((128.0f + 224.0f * cb) / 255.0f);
            vd->V[(size_t)j * cw + i] = (uint8_t)to8((128.0f + 224.0f * cr) / 255.0f);
        }
    if (videoUpload(vd)) return -1;
    vd->yuv.chroma = MLXR_CHROMA_LEFT;
    vd->nv.chroma = MLXR_CHROMA_LEFT;
    return 0;
}

// The same frame with chroma at full resolution (three planes): what a
// perfect chroma upsample could give back, a bound
static int txVideo444(Video* vd) {
    memset(vd, 0, sizeof(*vd));
    uint32_t w = TXS_W, h = TXS_H;
    vd->w = w;
    vd->h = h;
    vd->Y = malloc((size_t)w * h);
    vd->U = malloc((size_t)w * h);
    vd->V = malloc((size_t)w * h);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        float cb, cr;
        txChroma(txSrc + i * 3, &cb, &cr);
        vd->Y[i] = (uint8_t)to8((16.0f + 219.0f * txLuma(txSrc + i * 3)) / 255.0f);
        vd->U[i] = (uint8_t)to8((128.0f + 224.0f * cb) / 255.0f);
        vd->V[i] = (uint8_t)to8((128.0f + 224.0f * cr) / 255.0f);
    }
    if (vkh_image(&vk, w, h, VK_FORMAT_R8_UNORM, &vd->y) || vkh_image(&vk, w, h, VK_FORMAT_R8_UNORM, &vd->u) ||
        vkh_image(&vk, w, h, VK_FORMAT_R8_UNORM, &vd->v))
        return -1;
    vkh_upload(&vk, &vd->y, VK_IMAGE_ASPECT_COLOR_BIT, w, h, 1, vd->Y);
    vkh_upload(&vk, &vd->u, VK_IMAGE_ASPECT_COLOR_BIT, w, h, 1, vd->U);
    vkh_upload(&vk, &vd->v, VK_IMAGE_ASPECT_COLOR_BIT, w, h, 1, vd->V);
    vd->yuv.kind = MLXR_VIDEO_YUV420P;
    vd->yuv.planes[0] = vd->y.view;
    vd->yuv.planes[1] = vd->u.view;
    vd->yuv.planes[2] = vd->v.view;
    vd->yuv.width = w;
    vd->yuv.height = h;
    vd->yuv.chroma = MLXR_CHROMA_CENTER;
    return 0;
}

typedef struct {
    Video* vd;
    Video* vd444;
    VkhImage pic;        // the curved picture's own image, with its chain
    VkImageView picL0;
    uint32_t mips;
    Target eye;          // one eye of the projection layer
    const TxCfg* cfg;
    TxSurf surf;
    float mvp[16];
} TxRun;

static void txRecWarp(VkCommandBuffer cmd, void* u) {
    TxRun* r = u;
    MlxrVideo v = r->cfg->video == 2 ? r->vd444->yuv : (nv12Ok && r->cfg->video == 1) ? r->vd->nv : r->vd->yuv;
    MlxrWarpParams w = warpDefaults(&v);
    w.srcInset = 0.5f / TXS_W;
    w.sharpen = r->cfg->warpSharpen;
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = r->picL0;
    t.format = UNORM;
    t.rect.extent = (VkExtent2D){TXS_W, TXS_H};
    mlxr_barrier(cmd, r->pic.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, COLOR_ATT);
    mlxr_pass_warp(P, cmd, &t, &w);
    mlxr_record_mips(cmd, r->pic.image, TXS_W, TXS_H, r->mips);
}

static void txRecPanel(VkCommandBuffer cmd, void* u) {
    TxRun* r = u;
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = r->pic.view;
    pp.eyes = 1;
    memcpy(pp.mvp[0], r->mvp, sizeof(r->mvp));
    pp.eyeRect[0] = r->eye.t.rect;
    pp.uvRect[0][2] = pp.uvRect[0][3] = 1.0f;
    pp.width = r->surf.W;
    pp.height = r->surf.H;
    pp.radius = r->surf.R;
    pp.segments = 96;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    MlxrTarget t = r->eye.t;
    t.clear = 1;
    mlxr_pass_panel(P, cmd, &t, &pp);
}

static void txMvp(const TxSurf* s, float* mvp) {
    float proj[16], view[16], model[16], vp[16];
    XrFovf fov = {-atanf(TX_TAN), atanf(TX_TAN), atanf(TX_TAN), -atanf(TX_TAN)};
    XrPosef eye;
    memset(&eye, 0, sizeof(eye));
    eye.orientation.w = 1.0f;
    projectionFromFov(proj, fov, 0.05f, 100.0f);
    viewFromPose(view, eye);
    for (int i = 0; i < 16; i++) model[i] = i % 5 == 0 ? 1.0f : 0.0f;
    model[12] = s->shift;
    model[14] = -s->D;
    matMul(vp, proj, view);
    matMul(mvp, vp, model);
}

// Renders one configuration at one placement; out is the eye image
static void txRender(TxRun* r, uint8_t* out) {
    txRecWarp(vkh_begin(&vk), r);
    vkh_submit(&vk);
    r->pic.layout = SHADER_READ;
    VkCommandBuffer cmd = vkh_begin(&vk);
    vkh_layout(&vk, &r->eye.img, COLOR_ATT);
    txRecPanel(cmd, r);
    vkh_submit(&vk);
    vkh_read(&vk, &r->eye.img, out);
}

static void txCrop(const char* name, const TxSurf* s, const uint8_t* eye) {
    int x0, y0, x1, y1;
    txBox(s, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    uint8_t* c = malloc((size_t)w * h * 4);
    for (int y = 0; y < h; y++) memcpy(c + (size_t)y * w * 4, eye + ((size_t)(y + y0) * TX_EYE + x0) * 4, (size_t)w * 4);
    ppm(name, c, (uint32_t)w, (uint32_t)h);
    free(c);
}

static void testText(void) {
    txSrc = malloc(sizeof(float) * TXS_W * TXS_H * 3);
    if (txs_build(txSrc, MLXR_TEXT_FONT) != 0) {
        check(0, "text: the font %s cannot be read", MLXR_TEXT_FONT);
        free(txSrc);
        return;
    }
    {
        uint8_t* px = malloc((size_t)TXS_W * TXS_H * 4);
        for (size_t i = 0; i < (size_t)TXS_W * TXS_H; i++) {
            for (int c = 0; c < 3; c++) px[i * 4 + c] = (uint8_t)to8(txSrc[i * 3 + c]);
            px[i * 4 + 3] = 255;
        }
        ppm("text_desktop", px, TXS_W, TXS_H);
        free(px);
    }
    Video vd;
    check(txVideo(&vd) == 0, "text: the 4:2:0 frame uploads");
    Video vd444;
    check(txVideo444(&vd444) == 0, "text: the 4:4:4 frame uploads");
    TxRun r;
    memset(&r, 0, sizeof(r));
    r.vd = &vd;
    r.vd444 = &vd444;
    r.mips = 1;
    for (uint32_t m = TXS_W; m > 1; m >>= 1) r.mips++;
    vkh_image_mips(&vk, TXS_W, TXS_H, UNORM, r.mips, &r.pic, &r.picL0);
    targetMake(&r.eye, TX_EYE, TX_EYE, UNORM);
    uint8_t* eye = malloc((size_t)TX_EYE * TX_EYE * 4);
    uint8_t* moved = malloc((size_t)TX_EYE * TX_EYE * 4);
    enum { BEFORE, BEFORE_CAS, ROWS };
    static const TxCfg cfgs[ROWS] = {
        [BEFORE] = {"before: trilinear, bilinear chroma", 0.0f, 0, 0.0f, 0, 0, 1, 1},
        [BEFORE_CAS] = {"before, Sharpen quality (warp CAS)", 0.8f, 0, 0.0f, 0, 0, 0, 0},
    };
    TxScore at[TX_GEOM_COUNT][ROWS][2];
    float crawl[TX_GEOM_COUNT][ROWS + 1];
    memset(at, 0, sizeof(at));
    memset(crawl, 0, sizeof(crawl));
    note("three plane video (chroma left sited, midway between rows: the Frame's NV12 conversion; lavapipe's NV12 "
         "conversion offers only cosited rows); eye %dx%d, field 100 degrees; ideal = the area average of the desktop "
         "under each eye pixel",
         TX_EYE, TX_EYE);
    for (int g = 0; g < TX_GEOM_COUNT; g++) {
        r.surf = txSurf(&TX_GEOMS[g]);
        txMvp(&r.surf, r.mvp);
        float ax0, ay0, ax1, ay1;
        txMap(&r.surf, TX_EYE / 2.0f, TX_EYE / 2.0f, &ax0, &ay0);
        txMap(&r.surf, TX_EYE / 2.0f + 1.0f, TX_EYE / 2.0f, &ax1, &ay1);
        note("");
        note("%s: %.2f desktop pixels an eye pixel at the centre", TX_GEOMS[g].name, fabsf(ax1 - ax0));
        TxCache k0, kc, km;
        txCacheMake(&k0, &r.surf, 0.0f, 0);
        txCacheMake(&kc, &r.surf, TX_COMP, 0);
        TxScore id = txScore(&r.surf, &k0, NULL);
        txPrint("ideal", &id);
        for (int k = 0; k < ROWS; k++) {
            if (g > 0 && !cfgs[k].everywhere) continue;
            r.cfg = &cfgs[k];
            txRender(&r, eye);
            at[g][k][0] = txScore(&r.surf, &k0, eye);
            at[g][k][1] = txScore(&r.surf, &kc, eye);
            txPrint(cfgs[k].name, &at[g][k][0]);
            txPrint("  + compositor resample", &at[g][k][1]);
            if (g == 0) {
                char name[64];
                snprintf(name, sizeof(name), "text_eye_row%d", k);
                txCrop(name, &r.surf, eye);
            }
        }
        // Crawl: the screen moved one eye pixel sideways (1.79 desktop
        // pixels at the default placement, so the desktop lands on the eye's
        // pixels at a new phase), and the new image compared with the first
        // one moved by that pixel. A filter that treated every phase alike
        // would give the same picture; the difference is the text crawling
        // and changing weight as the head moves. RMS over the text tiles,
        // /255; the ideal's own (the area average is not phase free either)
        // for scale.
        TxSurf ms = r.surf;
        ms.shift = r.surf.D * 2.0f * TX_TAN / TX_EYE;
        txCacheMake(&km, &ms, 0.0f, 0);
        for (int k = -1; k < ROWS; k++) {
            if (k >= 0 && (!cfgs[k].crawl || (g > 0 && !cfgs[k].everywhere))) continue;
            if (k >= 0) {
                r.cfg = &cfgs[k];
                txRender(&r, eye);
                TxRun rp = r;
                rp.surf = ms;
                txMvp(&ms, rp.mvp);
                txRender(&rp, moved);
            }
            double sq = 0;
            long n = 0;
            for (int y = k0.y0; y < k0.y0 + k0.h; y++)
                for (int x = k0.x0; x + 1 < k0.x0 + k0.w; x++) {
                    size_t ki = (size_t)(y - k0.y0) * k0.w + (x - k0.x0);
                    float sx = k0.sxy[ki * 2], sy = k0.sxy[ki * 2 + 1], lx, ly;
                    if (isnan(sx)) continue;
                    int tile = (int)(sx / TXS_TILE_W) + 4 * (int)(sy / TXS_TILE_H);
                    if (tile < 0 || tile >= TXS_TILES || !txTextTile(tile) || !txIn(tile, sx, sy, TX_MARGIN, &lx, &ly))
                        continue;
                    float l0, l1;
                    if (k < 0) {
                        int mx = x + 1 - km.x0, my = y - km.y0;
                        if (mx < 0 || my < 0 || mx >= km.w || my >= km.h) continue;
                        const float* i1 = &km.ideal[((size_t)my * km.w + mx) * 3];
                        if (isnan(i1[0]) || isnan(k0.ideal[ki * 3])) continue;
                        l0 = txLuma(&k0.ideal[ki * 3]);
                        l1 = txLuma(i1);
                    } else {
                        const uint8_t* p0 = eye + ((size_t)y * TX_EYE + x) * 4;
                        const uint8_t* p1 = moved + ((size_t)y * TX_EYE + x + 1) * 4;
                        float c0[3] = {p0[0] / 255.0f, p0[1] / 255.0f, p0[2] / 255.0f};
                        float c1[3] = {p1[0] / 255.0f, p1[1] / 255.0f, p1[2] / 255.0f};
                        l0 = txLuma(c0);
                        l1 = txLuma(c1);
                    }
                    sq += (l1 - l0) * (l1 - l0);
                    n++;
                }
            crawl[g][k + 1] = (float)(255.0 * sqrt(sq / (n ? n : 1)));
            note("crawl, %-34s %5.2f", k < 0 ? "ideal" : cfgs[k].name, crawl[g][k + 1]);
        }
        txCacheFree(&km);
        txCacheFree(&k0);
        txCacheFree(&kc);
    }
    // GPU time on this device (report only: lavapipe's says nothing about the
    // Frame's; docs/dev/reports/text-clarity.md has the estimate)
    {
        r.surf = txSurf(&TX_GEOMS[0]);
        txMvp(&r.surf, r.mvp);
        VkCommandBuffer cmd = vkh_begin(&vk);
        vkh_layout(&vk, &r.eye.img, COLOR_ATT);
        vkh_submit(&vk);
        const int timed[] = {BEFORE};
        for (size_t i = 0; i < sizeof(timed) / sizeof(timed[0]); i++) {
            r.cfg = &cfgs[timed[i]];
            float wm = timeIt(txRecWarp, &r, 5);
            cmd = vkh_begin(&vk);
            mlxr_barrier(cmd, r.pic.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, SHADER_READ);
            vkh_submit(&vk);
            float pm = timeIt(txRecPanel, &r, 5);
            note("time (report only, %s): warp + mips %.2f ms, panel into one eye %.2f ms", cfgs[timed[i]].name, wm, pm);
        }
    }
    free(moved);
    free(eye);
    targetFree(&r.eye);
    vkDestroyImageView(vk.device, r.picL0, NULL);
    vkh_image_free(&vk, &r.pic);
    videoFree(&vd);
    videoFree(&vd444);
    free(txSrc);
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
                 {"warp_zoom", testWarpZoom},
                 {"warp_fur", testWarpFur},
                 {"sharpen", testSharpen},
                 {"upsample", testUpsample},
                 {"offset", testOffset},
                 {"offset_balance", testOffsetBalance},
                 {"downscale", testDownscale},
                 {"ambi", testAmbi},
                 {"glow_edge", testGlowEdge},
                 {"glow", testGlow},
                 {"glow_feather", testGlowFeather},
                 {"room", testRoom},
                 {"room_dim", testRoomDim},
                 {"model", testModel},
                 {"supersample", testSupersample},
                 {"panel", testPanel},
                 {"text", testText},
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
