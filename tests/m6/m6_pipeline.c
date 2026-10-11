// Moonlight XR Native, tests/m6: the real 3D pipeline headless, the same
// code the session runs (xr/xr_depth.c), on a clip:
//   file -> V4L2 Iris hardware decoder -> cached dma-buf import (NV12)
//   -> downscale + readback -> stage thread -> LiteRT (ZipDepth) on the
//   depth thread -> depth texture -> upsample -> offset search -> warp, each
//   eye into an image of its own (native, lane eye-swapchains: as the
//   session's curved and 3D picture)
// paced like a 90 Hz frame loop for --seconds. Writes left.png, right.png,
// depth.png (the map the warp used, 1 = near = white), upsample.png and
// anaglyph.png into --out, and prints maps/s, model ms, capture to display
// age, GPU ms per pass, the frame loop's own time and the CPU the process
// used. With --analyse (tests/m6/gen_scene.sh's clip: a red ball over a far
// wall and a floor) it checks the ball shifts between the eyes the way a
// near object does and the wall less, and that the map says the ball is
// nearer than the wall. GPLv3.
#define _GNU_SOURCE
#include <libavutil/frame.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "file_source.h"
#include "frame_slot.h"
#include "hw_video.h"
#include "m6_gpu.h"
#include "settings.h"
#include "vk_import.h"
#include "xr_native.h"

static M5Gpu g;

typedef struct {
    mlxr_vkimp* imp;
    struct { uint32_t gen, index; VkImageView view; } views[32];
} Import;

static VkImageView importView(Import* im, const mlxr_vkimp_image* img) {
    int slot = -1;
    for (int i = 0; i < 32; i++) {
        if (im->views[i].view && im->views[i].gen == img->generation && im->views[i].index == img->index)
            return im->views[i].view;
        if (!im->views[i].view && slot < 0) slot = i;
    }
    if (slot < 0) return VK_NULL_HANDLE;
    VkImageView v = VK_NULL_HANDLE;
    if (mlxr_passes_nv12_view(g.passes, img->image, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
    im->views[slot].gen = img->generation;
    im->views[slot].index = img->index;
    im->views[slot].view = v;
    return v;
}

static void releaseCb(void* user, const mlxr_vframe* f) {
    hw_video_release((HwVideo*)user, f);
}

typedef struct {
    MlxrVideo video;
    const mlxr_vkimp_image* img;
    Import* im;
    uint64_t frameId;
    int64_t frameNs;
    M5Image* target; // an image per eye
    int eyes;
} Body;

static void body(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    Body* b = user;
    mlxr_vkimp_cmd_acquire(b->im->imp, cmd, b->img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    XrDepthDraw dd;
    xrDepthPicture(ctx, &b->video, 1, b->frameId, b->frameNs, &dd);
    xrGpuTimeBegin(ctx, GPU_WARP);
    MlxrTarget t[2];
    memset(t, 0, sizeof(t));
    for (int e = 0; e < 2; e++) {
        xrVkImageTo(ctx, &b->target[e].img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        t[e].view = b->target[e].img.view;
        t[e].format = b->target[e].img.format;
        t[e].rect.extent = (VkExtent2D){ b->video.width, b->video.height };
    }
    MlxrWarpParams w;
    memset(&w, 0, sizeof(w));
    w.video = b->video;
    w.tint[0] = w.tint[1] = w.tint[2] = 1.0f;
    xrDepthRecordWarpEyes(ctx, cmd, t, &w, &dd);
    for (int e = 0; e < 2; e++) {
        xrVkImageTo(ctx, &b->target[e].img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    xrGpuTimeEnd(ctx, GPU_WARP);
    mlxr_vkimp_cmd_release(b->im->imp, cmd, b->img, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    ctx->drawnEyes = dd.eyes;
    b->eyes = dd.eyes;
}

static int cmpD(const void* a, const void* b) {
    double x = *(const double*)a, y = *(const double*)b;
    return x < y ? -1 : x > y;
}

static uint64_t parseMask(const char* s) {
    uint64_t m = 0;
    for (const char* p = s; *p;) {
        char* e;
        long a = strtol(p, &e, 10);
        if (e == p) break;
        long b = a;
        p = e;
        if (*p == '-') {
            b = strtol(p + 1, &e, 10);
            p = e;
        }
        for (long c = a; c <= b; c++) m |= 1ull << c;
        if (*p == ',') p++;
    }
    return m;
}

// The red ball's pixels (the clip's only saturated red) in one eye: centroid x
static int ballCentroid(const uint8_t* rgba, int stride, int x0, int w, int h, double* cx, double* cy) {
    double sx = 0, sy = 0;
    long n = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* p = rgba + ((size_t)y * stride + x0 + x) * 4;
            if (p[0] > 110 && p[1] < 70 && p[2] < 70 && p[0] > 2 * p[1] + 30) {
                sx += x;
                sy += y;
                n++;
            }
        }
    }
    if (n < 500) return 0;
    *cx = sx / n;
    *cy = sy / n;
    return 1;
}

// The horizontal shift (px) of the right eye's rows y0..y1 against the left
// eye's that matches best, in -range..range: where left content sits
// relative to right (positive: further right in the left eye)
static int rowShift(const uint8_t* rgba, int stride, int w, int y0, int y1, int range) {
    double best = 1e30;
    int bestS = 0;
    for (int s = -range; s <= range; s++) {
        double err = 0;
        long n = 0;
        for (int y = y0; y < y1; y += 2) {
            for (int x = range + 40; x < w - range - 40; x += 2) {
                const uint8_t* l = rgba + ((size_t)y * stride + x) * 4;
                const uint8_t* r = rgba + ((size_t)y * stride + w + x - s) * 4;
                int d = (int)l[0] + l[1] + l[2] - r[0] - r[1] - r[2];
                err += (double)d * d;
                n++;
            }
        }
        err /= n;
        if (err < best) {
            best = err;
            bestS = s;
        }
    }
    return bestS;
}

int main(int argc, char** argv) {
    const char* clip = NULL;
    const char* out = "m6-out";
    const char* assets = NULL;
    const char* frameCpus = NULL;
    double seconds = 10.0;
    int analyse = 0, source = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--clip") && i + 1 < argc) clip = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc) assets = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--frame-cpus") && i + 1 < argc) frameCpus = argv[++i];
        else if (!strcmp(argv[i], "--analyse")) analyse = 1;
        else if (!strcmp(argv[i], "--midas")) source = 1;
        else if (!strcmp(argv[i], "--int8")) source = 8;
    }
    if (clip == NULL || access(clip, R_OK) != 0) {
        fprintf(stderr, "no clip (--clip): skipped\n");
        return 77;
    }
    mkdir(out, 0755);
    if (m6_gpu_init(&g, 1) != 0) {
        fprintf(stderr, "no Vulkan device\n");
        return 77;
    }
    Settings s;
    settings_defaults(&s);
    s.depth_source = source;
    s.stereo = 1;
    XrCtx* ctx = m6_ctx(&g, xrDepthRealOps(), assets ? assets : MLXR_ASSETS_DIR, &s);
    if (ctx->stereoMode != DEPTH_MODE_MODEL) {
        fprintf(stderr, "no depth model: %s\n", ctx->depthReason);
        return 1;
    }

    FrameSlot slot;
    frame_slot_init(&slot);
    FileSource* src = file_source_start(clip, &slot, 1);
    HwVideo* hv = NULL;
    for (int i = 0; i < 200 && (hv = hw_video_current()) == NULL; i++) usleep(10000);
    if (src == NULL || hv == NULL) {
        fprintf(stderr, "hardware decoder did not start\n");
        return 1;
    }
    Import im;
    memset(&im, 0, sizeof(im));
    mlxr_vkimp_config ic;
    memset(&ic, 0, sizeof(ic));
    ic.phys = g.vk.phys;
    ic.device = g.vk.device;
    ic.queue_family = g.vk.queueFamily;
    ic.release = releaseCb;
    ic.release_user = hv;
    ic.linear_filter = true;
    if (mlxr_vkimp_create(&ic, &im.imp) != 0) {
        fprintf(stderr, "importer failed\n");
        return 1;
    }

    M5Image target[2];
    memset(target, 0, sizeof(target));
    int W = 0, H = 0;
    const int64_t period = 1000000000LL / 90;
    int64_t start = nowNs(), next = start;
    double* frameMs = calloc(100000, sizeof(double));
    long nFrame = 0, videoFrames = 0, late = 0, twoEye = 0;
    struct rusage ru0, ru1;
    getrusage(RUSAGE_SELF, &ru0);
    int pinned = 0;
    uint64_t fid = 0;
    int64_t measureFrom = 0;
    while (nowNs() - start < (int64_t)(seconds * 1e9)) {
        int64_t t0 = nowNs();
        mlxr_vkimp_poll(im.imp);
        mlxr_vframe hf;
        if (hw_video_take(hv, &hf)) {
            const mlxr_vkimp_image* img = mlxr_vkimp_get(im.imp, &hf);
            VkImageView view = img ? importView(&im, img) : VK_NULL_HANDLE;
            if (view == VK_NULL_HANDLE) {
                hw_video_release(hv, &hf);
            }
            else {
                if (target[0].img.image == NULL) {
                    W = (int)hf.width;
                    H = (int)hf.height;
                    for (int e = 0; e < 2; e++) {
                        m5_image(&g, ctx, &target[e], W, H, VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
                                         | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 VK_IMAGE_ASPECT_COLOR_BIT);
                    }
                }
                Body b;
                memset(&b, 0, sizeof(b));
                b.video.kind = MLXR_VIDEO_NV12;
                b.video.planes[0] = view;
                b.video.width = hf.width;
                b.video.height = hf.height;
                b.video.texWidth = hf.coded_width;
                b.video.texHeight = hf.coded_height;
                b.img = img;
                b.im = &im;
                b.frameId = fid++;
                b.frameNs = t0;
                b.target = target;
                m6_frame(&g, ctx, 0, body, &b);
                mlxr_vkimp_frame_done_timeline(im.imp, &hf, g.timeline, g.serial);
                videoFrames++;
                twoEye += b.eyes == 2;
            }
        }
        else {
            m6_frame(&g, ctx, 0, NULL, NULL);
        }
        int64_t t1 = nowNs();
        XrDepthInfo info;
        xrDepthGetInfo(ctx, &info);
        if (info.engineState == MLXR_DEPTH_READY && measureFrom == 0) {
            measureFrom = t1;
            if (frameCpus != NULL) {
                cpu_set_t set;
                CPU_ZERO(&set);
                uint64_t m = parseMask(frameCpus);
                for (int c = 0; c < 64; c++)
                    if (m & (1ull << c)) CPU_SET(c, &set);
                pinned = pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
            }
        }
        if (measureFrom && nFrame < 100000) frameMs[nFrame++] = (t1 - t0) / 1e6;
        next += period;
        int64_t now = nowNs();
        if (now < next) {
            struct timespec ts = { 0, (long)(next - now) };
            nanosleep(&ts, NULL);
        }
        else {
            late++;
            next = now;
        }
    }
    getrusage(RUSAGE_SELF, &ru1);
    m6_idle(&g, ctx);
    double wall = (nowNs() - start) / 1e9;
    double cpu = (ru1.ru_utime.tv_sec - ru0.ru_utime.tv_sec) + (ru1.ru_utime.tv_usec - ru0.ru_utime.tv_usec) / 1e6
            + (ru1.ru_stime.tv_sec - ru0.ru_stime.tv_sec) + (ru1.ru_stime.tv_usec - ru0.ru_stime.tv_usec) / 1e6;

    XrDepthInfo info;
    xrDepthGetInfo(ctx, &info);
    int gs[GPU_SECTIONS];
    double gms[GPU_SECTIONS];
    for (int i = 0; i < GPU_SECTIONS; i++) gms[i] = xrGpuTimeAvg(ctx, i, &gs[i]);
    char line[700];
    xrDepthStatsLine(ctx, line, sizeof(line));
    qsort(frameMs, (size_t)nFrame, sizeof(double), cmpD);
    double p50 = nFrame ? frameMs[nFrame / 2] : 0, p99 = nFrame ? frameMs[(long)(nFrame * 0.99)] : 0;
    double fmax = nFrame ? frameMs[nFrame - 1] : 0;
    printf("clip %s %dx%d, %.1f s, %ld video frames drawn (%ld in 3D), %ld loop frames measured\n", clip, W, H, wall,
           videoFrames, twoEye, nFrame);
    printf("depth: %s (%d threads, cpus %s), load %.0f ms, warm up %.0f ms\n", ctx->depth ? "ready" : "-",
           info.threads, getenv("MLXR_DEPTH_CPUS") ? getenv("MLXR_DEPTH_CPUS") : "any", info.loadMs, info.warmupMs);
    printf("depth: %llu maps, %.1f maps/s (target %d), model %.1f ms avg (p95 %.1f), submit to map %.1f ms, "
           "capture to display %.1f ms avg (%.1f frames)\n",
           (unsigned long long)info.maps, info.mapsPerSecond, ctx->depthGov.target, info.modelMsAvg, info.modelMsP95,
           info.latencyMsAvg, info.ageMsAvg, info.ageFramesAvg);
    printf("depth: %llu captures, %llu submitted, %llu skipped (no buffer), %llu replaced, stage wait %.2f ms, "
           "submit %.2f ms, %llu uploads, %llu blocked\n",
           (unsigned long long)info.captures, (unsigned long long)info.submitted,
           (unsigned long long)info.skipsNoPair, (unsigned long long)info.replaced, info.stageWaitMsAvg,
           info.submitMsAvg, (unsigned long long)info.uploads, (unsigned long long)info.uploadBlocked);
    static const char* const NAMES[GPU_SECTIONS] = { "warp (2 eyes)", "glow", "room", "models", "curve",
                                                      "downscale+readback", "upsample", "offset" };
    printf("gpu:");
    for (int i = 0; i < GPU_SECTIONS; i++)
        if (gs[i]) printf(" %s %.3f ms (%d);", NAMES[i], gms[i], gs[i]);
    printf("\nframe loop: own work p50 %.2f ms, p99 %.2f ms, max %.2f ms, %ld late of 90 Hz%s; process CPU %.2f "
           "cores (%.1f s over %.1f s)\n",
           p50, p99, fmax, late, pinned ? " (pinned)" : "", cpu / wall, cpu, wall);
    printf("stats line: %s\n", line);

    CHECK(info.maps > 0);
    CHECK(info.uploads > 0);
    CHECK(twoEye > 0);
    CHECK(info.pairReuseBad == 0);
    CHECK(info.frameIdBackwards == 0);
    CHECK(info.submitFailed == 0);

    // The pictures
    if (target[0].img.image != NULL && info.uploads > 0) {
        // Each eye's own image into its half of one side by side buffer
        uint8_t* eyes = malloc((size_t)2 * W * H * 4);
        uint8_t* one = malloc((size_t)W * H * 4);
        for (int e = 0; e < 2; e++) {
            m5_readback(&g, target[e].img.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, W, H, one);
            for (int y = 0; y < H; y++) memcpy(eyes + ((size_t)y * 2 * W + e * W) * 4, one + (size_t)y * W * 4, (size_t)W * 4);
        }
        char path[512];
        for (int e = 0; e < 2; e++) {
            for (int y = 0; y < H; y++) memcpy(one + (size_t)y * W * 4, eyes + ((size_t)y * 2 * W + e * W) * 4, (size_t)W * 4);
            snprintf(path, sizeof(path), "%s/%s.png", out, e == 0 ? "left" : "right");
            m6_png(path, one, W, H, 1);
        }
        // red-cyan: the left eye's red, the right eye's green and blue
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t* o = one + ((size_t)y * W + x) * 4;
                const uint8_t* l = eyes + ((size_t)y * 2 * W + x) * 4;
                const uint8_t* r = eyes + ((size_t)y * 2 * W + W + x) * 4;
                o[0] = (uint8_t)((l[0] * 30 + l[1] * 59 + l[2] * 11) / 100);
                o[1] = o[2] = (uint8_t)((r[0] * 30 + r[1] * 59 + r[2] * 11) / 100);
                o[3] = 255;
            }
        snprintf(path, sizeof(path), "%s/anaglyph.png", out);
        m6_png(path, one, W, H, 1);
        int mw = info.mapW, mh = info.mapH;
        uint8_t* map = malloc((size_t)mw * mh * 4);
        m5_readback(&g, info.depthImage[info.readIndex], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mw, mh, map);
        uint8_t* grey = malloc((size_t)mw * mh * 4);
        for (int i = 0; i < mw * mh; i++) {
            grey[i * 4] = grey[i * 4 + 1] = grey[i * 4 + 2] = map[i * 4 + 3];
            grey[i * 4 + 3] = 255;
        }
        snprintf(path, sizeof(path), "%s/depth.png", out);
        m6_png(path, grey, mw, mh, 1);
        snprintf(path, sizeof(path), "%s/guide.png", out);
        m6_png(path, map, mw, mh, 1);
        if (info.upsampleImage != NULL) {
            int uw = ctx->upsampleWidth, uh = ctx->upsampleHeight;
            uint8_t* up = malloc((size_t)uw * uh * 4);
            m5_readback(&g, info.upsampleImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uw, uh, up);
            for (int i = 0; i < uw * uh; i++) {
                up[i * 4] = up[i * 4 + 1] = up[i * 4 + 2] = up[i * 4 + 3];
                up[i * 4 + 3] = 255;
            }
            snprintf(path, sizeof(path), "%s/upsample.png", out);
            m6_png(path, up, uw, uh, 1);
            free(up);
        }
        printf("wrote %s/{left,right,anaglyph,depth,guide,upsample}.png\n", out);

        if (analyse) {
            double lx, ly, rx, ry;
            int lok = ballCentroid(eyes, 2 * W, 0, W, H, &lx, &ly);
            int rok = ballCentroid(eyes, 2 * W, W, W, H, &rx, &ry);
            CHECK(lok && rok);
            double ballShift = lok && rok ? lx - rx : 0.0;
            // The far wall: rows of bricks well above the floor's horizon
            int wallShift = rowShift(eyes, 2 * W, W, H * 8 / 108, H * 32 / 108, 24);
            double sep = ctx->separationCurrent;
            printf("analyse: ball centroid left %.1f, right %.1f: shift %+.2f px (left minus right); far wall "
                   "shift %+d px; separation %.3f of %d px = %.1f px at full near-far\n",
                   lx, rx, ballShift, wallShift, sep, W, sep * W);
            // Near: crossed, the left eye's ball right of the right eye's
            CHECK(ballShift > 1.0);
            // The wall less than the ball (behind it: the other way or less)
            CHECK(wallShift < ballShift);
            // The map: the ball nearer (whiter) than the wall
            double cx = (lx + rx) / 2.0 * mw / W, cy = (ly + ry) / 2.0 * mh / H;
            double rad = 120.0 * mw / W;
            double ball = 0, wall = 0;
            long nb = 0, nw = 0;
            for (int y = 0; y < mh; y++)
                for (int x = 0; x < mw; x++) {
                    double dx = x - cx, dy = y - cy;
                    int a = map[((size_t)y * mw + x) * 4 + 3];
                    if (dx * dx + dy * dy < rad * rad) {
                        ball += a;
                        nb++;
                    }
                    else if (y < mh * 30 / 108) {
                        wall += a;
                        nw++;
                    }
                }
            ball = nb ? ball / nb : 0;
            wall = nw ? wall / nw : 0;
            printf("analyse: map depth (255 near) ball %.1f, wall %.1f\n", ball, wall);
            CHECK(ball > wall + 20);
        }
        free(map);
        free(grey);
        free(one);
        free(eyes);
    }

    vkDeviceWaitIdle(g.vk.device);
    mlxr_vkimp_drain(im.imp, 1000000000ull);
    for (int i = 0; i < 32; i++)
        if (im.views[i].view) vkDestroyImageView(g.vk.device, im.views[i].view, NULL);
    mlxr_vkimp_reset(im.imp);
    mlxr_vkimp_destroy(im.imp);
    m5_image_free(ctx, &target[0]);
    m5_image_free(ctx, &target[1]);
    m6_ctx_free(ctx);
    file_source_stop(src);
    frame_slot_destroy(&slot);
    m5_gpu_free(&g);
    free(frameMs);
    return checksDone("m6_pipeline");
}
