// Moonlight XR Native, tests/m5: mlxr-world-snapshot. The M4 snapshot tool's
// scene (the ported layers composed from one eye on the CPU) with what M5
// draws on the GPU put in: each room drawn by xrRoomRecord from the seat with
// the picture where applyRoomPlacement hangs it, the glow from the colour
// sample of the picture (xrWorldVideoFrame), the controller models
// (xrModelRecord) at two grips, and the curved picture with its glow drawn
// into its layer. Writes PNGs into the directory given (ui-snapshots runs it
// next to mlxr-ui-snapshot). Native (lane eye-swapchains): every pass draws
// into an image per eye, as the session's swapchains are now; the left eye's
// image is composed. GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "m5_gpu.h"
#include "scene.h"
#include "xr_native.h"

#define W 1280
#define H 720
#define HFOV 96.0f

#define FAKE_ROOM ((XrSwapchain)(uintptr_t)0x7001)
#define FAKE_MODEL ((XrSwapchain)(uintptr_t)0x7002)
#define FAKE_GLOW ((XrSwapchain)(uintptr_t)0x7003)
#define FAKE_CURVE ((XrSwapchain)(uintptr_t)0x7004)
// Native (lane eye-swapchains): the right eye's own swapchains
#define FAKE_ROOM_RIGHT ((XrSwapchain)(uintptr_t)0x7011)
#define FAKE_MODEL_RIGHT ((XrSwapchain)(uintptr_t)0x7012)
#define FAKE_CURVE_RIGHT ((XrSwapchain)(uintptr_t)0x7014)

static M5Gpu g;
static M5Video video;

// The picture: a sunset over water, with a bright title bar, so the glow and
// the room's light have colours to take
static void sunset(float u, float v, float out[3]) {
    float sky = v / 0.6f;
    if (v < 0.6f) {
        out[0] = 0.95f - 0.35f * sky;
        out[1] = 0.55f - 0.40f * sky;
        out[2] = 0.30f + 0.25f * sky;
        float dx = (u - 0.62f) * 1.78f, dy = v - 0.45f;
        if (dx * dx + dy * dy < 0.012f) {
            out[0] = 1.0f;
            out[1] = 0.85f;
            out[2] = 0.45f;
        }
    }
    else {
        float w = (v - 0.6f) / 0.4f;
        out[0] = 0.05f + 0.25f * (1.0f - w);
        out[1] = 0.25f - 0.10f * w;
        out[2] = 0.35f - 0.10f * w;
        if (fmodf(u * 40.0f + v * 13.0f, 1.0f) < 0.06f && w < 0.5f) out[0] = out[1] = out[2] = 0.9f;
    }
    if (v > 0.08f && v < 0.13f && u > 0.06f && u < 0.42f) out[0] = out[1] = out[2] = 0.95f;
}

typedef struct {
    uint8_t* room;    // W x H RGBA, the room from the seat
    uint8_t* models;  // premultiplied
    uint8_t* curve;
    uint8_t* glow;    // GLOW_TEX square, premultiplied
} Images;

static Images im;

static int quadTexel(void* user, XrSwapchain sc, float u, float v, float out[4]) {
    (void)user;
    if (sc == SCENE_VIDEO_SWAPCHAIN) {
        sunset(u, v, out);
        out[3] = 1.0f;
        return 1;
    }
    if (sc == FAKE_GLOW && im.glow) {
        int x = (int)(u * (GLOW_TEX - 1) + 0.5f), y = (int)(v * (GLOW_TEX - 1) + 0.5f);
        const uint8_t* p = im.glow + ((size_t)y * GLOW_TEX + x) * 4;
        for (int k = 0; k < 4; k++) out[k] = p[k] / 255.0f;
        return 1;
    }
    return 0;
}

static int projTexel(void* user, const XrCompositionLayerProjection* layer, int x, int y, float out[4]) {
    (void)user;
    if (layer->views == NULL) return 0;
    XrSwapchain sc = layer->views[0].subImage.swapchain;
    const uint8_t* src = sc == FAKE_ROOM ? im.room : sc == FAKE_MODEL ? im.models : sc == FAKE_CURVE ? im.curve : NULL;
    if (src == NULL) return 0;
    // The left eye's own image
    const uint8_t* p = src + ((size_t)y * W + x) * 4;
    for (int k = 0; k < 4; k++) out[k] = p[k] / 255.0f;
    return 1;
}

static XrView views[2];
// Native (lane eye-swapchains): an image per eye, as the session's swapchains
static M5Image eyeImg[2], depth;

// Both eyes' images and the depth moved to attachment, as the targets of a pass
static XrEyeTargets eyesNow(VkCommandBuffer cmd) {
    VkImage im2[2] = { eyeImg[0].img.image, eyeImg[1].img.image };
    VkImageView v2[2] = { eyeImg[0].img.view, eyeImg[1].img.view };
    for (int e = 0; e < 2; e++) {
        mlxr_barrier(cmd, im2[e], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    mlxr_barrier(cmd, depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    XrEyeTargets t = xrEyesPerImage(im2, v2, VK_FORMAT_R8G8B8A8_SRGB, W, H);
    t.depthImage = depth.img.image;
    t.depthView = depth.img.view;
    return t;
}

static void roomBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)user;
    XrEyeTargets t = eyesNow(cmd);
    xrRoomRecord(ctx, cmd, &t, views);
}

static void modelBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)user;
    XrEyeTargets t = eyesNow(cmd);
    xrModelRecord(ctx, cmd, &t, views);
}

static void glowBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)cmd;
    (void)user;
    xrWorldVideoFrame(ctx, &video.video);
}

static M5Image curvePic;

// The picture on its surface over what the targets hold, as curveRecord draws it
static void pictureInto(XrCtx* ctx, VkCommandBuffer cmd, const XrEyeTargets* tg) {
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = curvePic.img.view;
    xrCurveShape(ctx, &pp.width, &pp.height, &pp.radius);
    pp.segments = 96;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    for (int e = 0; e < 2; e++) {
        xrCurveMvp(views[e].fov, views[e].pose, ctx->screenPose, pp.mvp[e]);
        pp.uvRect[e][2] = pp.uvRect[e][3] = 1.0f;
    }
    xrEyesPanel(ctx, cmd, tg, &pp, NULL);
}

static void curveBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)user;
    XrEyeTargets tg = eyesNow(cmd);
    tg.clear = 1;
    // curveRecord's order: the glow, then the picture over it
    xrWorldCurvedGlow(ctx, &tg, views);
    tg.clear = 0;
    xrEyesBarrier(cmd, &tg);
    pictureInto(ctx, cmd, &tg);
}

// Native: one projection layer a frame (xrWorldPlanLayers). The room, the
// flat 3D picture with its glow and the models drawn into the picture's
// images the way curveRecord draws them: room first (clearing), glow and
// picture over it, models last.
static void oneLayerBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)user;
    XrEyeTargets tg = eyesNow(cmd);
    tg.depthImage = VK_NULL_HANDLE; // the room and the models take the world's depth, as in the session
    tg.depthView = VK_NULL_HANDLE;
    tg.clear = 1;
    if (xrRoomRecordInto(ctx, cmd, &tg, views)) {
        xrEyesBarrier(cmd, &tg);
        tg.clear = 0;
    }
    if (xrWorldGlowShown(ctx)) {
        xrWorldFlatGlowInto(ctx, &tg, views);
        xrEyesBarrier(cmd, &tg);
        tg.clear = 0;
    }
    pictureInto(ctx, cmd, &tg);
    xrModelRecordOver(ctx, cmd, &tg, views);
}

static int oneLayerFailures;

static uint8_t* grab(void) {
    uint8_t* px = malloc((size_t)W * H * 4);
    m5_readback(&g, eyeImg[0].img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, W, H, px);
    return px;
}

static void frameWith(Scene* s, const XrCompositionLayerProjection* curved) {
    XrCtx* ctx = s->ctx;
    s->live = s->settings;
    xrSettingsFromCtx(ctx, &s->live);
    s->live.head_locked = s->settings.head_locked;
    xrPanelsUpdate(ctx, &s->live, &s->launcher, "Display 90 Hz, 90 fps\nVideo 2560x1440, 60 fps", "21:07", nowNs());
    XrFrameIn in;
    memset(&in, 0, sizeof(in));
    xrDefaultPlacement(&in.distance, &in.quadWidth, &in.curvature);
    in.headLocked = s->settings.head_locked;
    in.passthrough = ctx->passthrough;
    in.curved = curved;
    memset(&s->out, 0, sizeof(s->out));
    xrBuildLayers(ctx, &in, &s->out);
}

static void shot(Scene* s, const char* dir, const char* name, const XrCompositionLayerProjection* curved) {
    char path[512];
    frameWith(s, curved);
    frameWith(s, curved); // fades and placement settle
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    int n = scene_render(s, W, H, HFOV, path);
    printf("%-34s %2u layers, %d drawn\n", name, s->out.count, n);
}

static void glowNow(XrCtx* ctx) {
    for (int i = 0; i < 12; i++) m5_frame(&g, ctx, glowBody, NULL);
    free(im.glow);
    im.glow = malloc(GLOW_TEX * GLOW_TEX * 4);
    m5_readback(&g, ctx->glowTexture.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, GLOW_TEX, GLOW_TEX, im.glow);
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "world-snapshots";
    mkdir(dir, 0755);
    int gpu = m5_gpu_init(&g);
    if (gpu != 0) {
        fprintf(stderr, "no Vulkan device\n");
        return gpu == M5_GPU_NO_DEVICE ? 77 : 1;
    }
    Scene* s = scene_new();
    XrCtx* ctx = s->ctx;
    m5_ctx_gpu(&g, ctx);
    s->hookUser = NULL;
    s->quadTexel = quadTexel;
    s->projTexel = projTexel;
    ctx->overlayVisible = 0;
    m5_frame(&g, ctx, NULL, NULL);
    m5_video(&g, ctx, &video, 1920, 1080, sunset);
    for (int e = 0; e < 2; e++) {
        m5_image(&g, ctx, &eyeImg[e], W, H, VK_FORMAT_R8G8B8A8_SRGB,
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    m5_image(&g, ctx, &depth, W, H, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    // One eye at the seat for both images (only the left is composed)
    views[0] = m5_view(HFOV, W, H, 0.0f);
    views[1] = views[0];

    // The glow and the models, as the session would have them up
    ctx->ambilightOn = 1;
    ctx->ambiIntensity = 0.6f;
    ctx->glowSwapchain = FAKE_GLOW;
    ctx->glowSwapchainFilled = 1;
    ctx->modelOn = 1;
    m5_frame(&g, ctx, NULL, NULL); // the model loads
    ctx->modelShown[HAND_LEFT] = ctx->modelShown[HAND_RIGHT] = 1;
    Vec3 ax = { 1, 0, 0 };
    ctx->modelGrip[HAND_LEFT] = (XrPosef){ axisAngleQuat(ax, -0.45f), { -0.20f, -0.28f, -0.42f } };
    ctx->modelGrip[HAND_RIGHT] = (XrPosef){ axisAngleQuat(ax, -0.60f), { 0.22f, -0.25f, -0.48f } };
    ctx->modelsShowing = 1;
    ctx->modelRendered = 1;
    ctx->modelSwapchain[0] = FAKE_MODEL;
    ctx->modelSwapchain[1] = FAKE_MODEL_RIGHT;
    ctx->modelEyeWidth = W;
    ctx->modelEyeHeight = H;
    for (int e = 0; e < 2; e++) ctx->modelViews[e] = views[e];
    m5_frame(&g, ctx, modelBody, NULL);
    im.models = grab();

    // The void: the picture, its glow, the models, the ray on the picture
    glowNow(ctx);
    Vec3 hand = { 0.22f, -0.25f, -0.48f };
    Vec3 at = { ctx->screenPose.position.x, ctx->screenPose.position.y, ctx->screenPose.position.z };
    frameWith(s, NULL);
    at = (Vec3){ ctx->screenPose.position.x + 0.3f, ctx->screenPose.position.y + 0.2f, ctx->screenPose.position.z };
    scene_ray_to(s, hand, at);
    shot(s, dir, "m5_void_glow_models", NULL);

    // The curved picture with its glow in the same layer
    {
        ctx->panelCurve = 0.7f;
        frameWith(s, NULL);
        m5_image(&g, ctx, &curvePic, 1920, 1080, VK_FORMAT_R8G8B8A8_UNORM,
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        VkCommandBuffer cmd = m5_begin(&g);
        mlxr_barrier(cmd, curvePic.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        MlxrTarget t;
        memset(&t, 0, sizeof(t));
        t.view = curvePic.img.view;
        t.format = VK_FORMAT_R8G8B8A8_UNORM;
        t.rect.extent = (VkExtent2D){ 1920, 1080 };
        MlxrWarpParams wp;
        memset(&wp, 0, sizeof(wp));
        wp.video = video.video;
        wp.convergence = 0.5f;
        wp.srcInset = 0.5f / 1920.0f;
        wp.edgeFade = 16.0f / 1920.0f;
        wp.tint[0] = wp.tint[1] = wp.tint[2] = 1.0f;
        mlxr_pass_warp(g.passes, cmd, &t, &wp);
        mlxr_barrier(cmd, curvePic.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m5_submit(&g, cmd, NULL, 0);
        m5_frame(&g, ctx, curveBody, NULL);
        im.curve = grab();
        XrCompositionLayerProjectionView cv[2];
        memset(cv, 0, sizeof(cv));
        cv[0].subImage.swapchain = FAKE_CURVE;
        cv[1].subImage.swapchain = FAKE_CURVE_RIGHT;
        XrCompositionLayerProjection curved;
        memset(&curved, 0, sizeof(curved));
        curved.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        curved.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        curved.viewCount = 2;
        curved.views = cv;
        s->out.count = 0;
        ctx->beamVisible = 0;
        shot(s, dir, "m5_curved_glow", &curved);
        ctx->panelCurve = -1.0f;
    }

    // Each room from the seat, lit by the picture, with the picture hung
    // where the room puts it, the glow where the room has it on, the models
    const char* names[4] = { "", "theater", "cinema", "synthwave" };
    ctx->roomSwapchain[0] = FAKE_ROOM;
    ctx->roomSwapchain[1] = FAKE_ROOM_RIGHT;
    ctx->roomViewsValid = 1;
    ctx->roomRendered = 1;
    ctx->roomEyeWidth = W;
    ctx->roomEyeHeight = H;
    for (int e = 0; e < 2; e++) ctx->roomViews[e] = views[e];
    for (int st = ROOM_STYLE_FIRST; st <= ROOM_STYLE_LAST; st++) {
        xrSetEnvironment(ctx, roomCellForStyle(st));
        ctx->passthrough = 0;
        for (int f = 0; f < 30 && !(ctx->roomBuiltStyle == st && xrRoomResident(ctx) == st); f++) {
            m5_frame(&g, ctx, NULL, NULL);
            xrRoomWaitLoader(ctx);
        }
        glowNow(ctx);
        ctx->beamVisible = 0;
        frameWith(s, NULL); // the room hangs the picture
        m5_frame(&g, ctx, roomBody, NULL);
        free(im.room);
        im.room = grab();
        char name[64];
        snprintf(name, sizeof(name), "m5_room_%s", names[st]);
        shot(s, dir, name, NULL);
        // The bar under the room's picture, the ray on it
        at = (Vec3){ ctx->screenPose.position.x, ctx->screenPose.position.y, ctx->screenPose.position.z };
        scene_ray_to(s, hand, at);
        ctx->hoverKind = HOVER_BAR;
        snprintf(name, sizeof(name), "m5_room_%s_bar", names[st]);
        shot(s, dir, name, NULL);
        ctx->hoverKind = HOVER_NONE;
        // Native: the 3D picture and the models with the room, all in the
        // picture's one projection layer (xrWorldPlanLayers)
        {
            ctx->beamVisible = 0;
            int stereo = ctx->stereoMode;
            ctx->stereoMode = DEPTH_MODE_MODEL;
            ctx->modelsShowing = 1;
            xrWorldPlanLayers(ctx, 1);
            m5_frame(&g, ctx, oneLayerBody, NULL);
            uint8_t* one = grab();
            // Opaque everywhere, the room behind the picture, the models over it
            int opaque = 1, modelsOver = 0, modelsSeen = 0, pictureSeen = 0;
            for (int y = 0; y < H; y += 4) {
                for (int x = 0; x < W; x += 4) {
                    size_t i = ((size_t)y * W + x) * 4;
                    opaque &= one[i + 3] == 255;
                    if (im.models[i + 3] == 255) {
                        modelsSeen++;
                        modelsOver += abs(one[i] - im.models[i]) <= 2 && abs(one[i + 1] - im.models[i + 1]) <= 2;
                    }
                    pictureSeen += abs(one[i] - im.room[i]) > 24 || abs(one[i + 2] - im.room[i + 2]) > 24;
                }
            }
            int ok = opaque && modelsSeen > 0 && modelsOver >= modelsSeen * 95 / 100 && pictureSeen > 200
                    && ctx->roomInPicture && ctx->modelsInPicture;
            printf("%-34s opaque %d, models over %d of %d, picture texels %d: %s\n", "one layer", opaque, modelsOver,
                   modelsSeen, pictureSeen, ok ? "ok" : "FAILED");
            oneLayerFailures += !ok;
            free(im.curve);
            im.curve = one;
            XrCompositionLayerProjectionView ov[2];
            memset(ov, 0, sizeof(ov));
            ov[0].subImage.swapchain = FAKE_CURVE;
            ov[1].subImage.swapchain = FAKE_CURVE_RIGHT;
            XrCompositionLayerProjection oneLayer;
            memset(&oneLayer, 0, sizeof(oneLayer));
            oneLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            oneLayer.viewCount = 2;
            oneLayer.views = ov;
            snprintf(name, sizeof(name), "m5_room_%s_3d_models_one_layer", names[st]);
            shot(s, dir, name, &oneLayer);
            int projections = 0;
            for (uint32_t i = 0; i < s->out.count; i++) {
                projections += s->out.order[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            }
            if (projections != 1) {
                printf("  %u projection layers, not 1: FAILED\n", projections);
                oneLayerFailures++;
            }
            ctx->stereoMode = stereo;
            xrWorldPlanLayers(ctx, 0);
        }
        // The Room tab
        ctx->cogTab = COG_TAB_SCREEN;
        ctx->cogOpen = 1;
        ctx->cogPose = cogPanelPose(ctx, &ctx->cogW, &ctx->cogH);
        snprintf(name, sizeof(name), "m5_room_%s_roomtab", names[st]);
        shot(s, dir, name, NULL);
        ctx->cogOpen = 0;
        ctx->panelFades[FADE_COG].level = 0.0f;
        ctx->beamVisible = 0;
    }
    // The picker with every cell live, from the theater
    ctx->pickerOpen = 1;
    ctx->pickerHover = ENV_CELL_SYNTHWAVE;
    shot(s, dir, "m5_picker_rooms", NULL);
    ctx->pickerOpen = 0;
    ctx->panelFades[FADE_PICKER].level = 0.0f;
    // Display tab with its M5 rows live
    xrSetEnvironment(ctx, ENV_CELL_VOID);
    ctx->cogTab = COG_TAB_DISPLAY;
    ctx->cogOpen = 1;
    ctx->cogPose = cogPanelPose(ctx, &ctx->cogW, &ctx->cogH);
    shot(s, dir, "m5_display_tab", NULL);
    ctx->cogOpen = 0;

    m5_image_free(ctx, &curvePic);
    m5_image_free(ctx, &eyeImg[0]);
    m5_image_free(ctx, &eyeImg[1]);
    m5_image_free(ctx, &depth);
    m5_video_free(ctx, &video);
    ctx->glowSwapchain = XR_NULL_HANDLE;
    for (int e = 0; e < 2; e++) ctx->modelSwapchain[e] = ctx->roomSwapchain[e] = XR_NULL_HANDLE;
    xrWorldDestroy(ctx);
    scene_free(s);
    m5_gpu_free(&g);
    free(im.room);
    free(im.models);
    free(im.curve);
    free(im.glow);
    return oneLayerFailures ? 1 : 0;
}
