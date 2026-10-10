// Moonlight XR Native, tests/m5: mlxr-world-snapshot. The M4 snapshot tool's
// scene (the ported layers composed from one eye on the CPU) with what M5
// draws on the GPU put in: each room drawn by xrRoomRecord from the seat with
// the picture where applyRoomPlacement hangs it, the glow from the colour
// sample of the picture (xrWorldVideoFrame), the controller models
// (xrModelRecord) at two grips, and the curved picture with its glow drawn
// into its layer. Writes PNGs into the directory given (ui-snapshots runs it
// next to mlxr-ui-snapshot). GPLv3.
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
    // The left eye's half of a double wide image
    const uint8_t* p = src + ((size_t)y * (2 * W) + x) * 4;
    for (int k = 0; k < 4; k++) out[k] = p[k] / 255.0f;
    return 1;
}

static XrView views[2];
static M5Image colour, depth;

static void clearTargets(VkCommandBuffer cmd) {
    mlxr_barrier(cmd, colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

static void roomBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)user;
    clearTargets(cmd);
    xrRoomRecord(ctx, cmd, colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, depth.img.view, W, H, views);
}

static void modelBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)user;
    clearTargets(cmd);
    xrModelRecord(ctx, cmd, colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, depth.img.view, W, H, views);
}

static void glowBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)cmd;
    (void)user;
    xrWorldVideoFrame(ctx, &video.video);
}

static M5Image curvePic;

static void curveBody(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    (void)user;
    clearTargets(cmd);
    MlxrTarget tg;
    memset(&tg, 0, sizeof(tg));
    tg.view = colour.img.view;
    tg.format = VK_FORMAT_R8G8B8A8_SRGB;
    tg.rect.extent = (VkExtent2D){ 2 * W, H };
    tg.clear = 1;
    // curveRecord's order: the glow, then the picture over it
    xrWorldCurvedGlow(ctx, &tg, views, W, H);
    tg.clear = 0;
    mlxr_barrier(cmd, colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrPanelParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.src = curvePic.img.view;
    pp.eyes = 2;
    xrCurveShape(ctx, &pp.width, &pp.height, &pp.radius);
    pp.segments = 96;
    pp.color[0] = pp.color[1] = pp.color[2] = pp.color[3] = 1.0f;
    for (int e = 0; e < 2; e++) {
        xrCurveMvp(views[e].fov, views[e].pose, ctx->screenPose, pp.mvp[e]);
        pp.eyeRect[e].offset.x = e * W;
        pp.eyeRect[e].extent = (VkExtent2D){ W, H };
        pp.uvRect[e][2] = pp.uvRect[e][3] = 1.0f;
    }
    mlxr_pass_panel(ctx->passes, cmd, &tg, &pp);
}

static uint8_t* grab(void) {
    uint8_t* px = malloc((size_t)2 * W * H * 4);
    m5_readback(&g, colour.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2 * W, H, px);
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
    if (m5_gpu_init(&g) != 0) {
        fprintf(stderr, "no Vulkan device\n");
        return 1;
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
    m5_image(&g, ctx, &colour, 2 * W, H, VK_FORMAT_R8G8B8A8_SRGB,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, ctx, &depth, 2 * W, H, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    // One eye at the seat for both halves (only the left is composed)
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
    ctx->modelSwapchain = FAKE_MODEL;
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
        cv[0].subImage.swapchain = cv[1].subImage.swapchain = FAKE_CURVE;
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
        m5_image_free(ctx, &curvePic);
    }

    // Each room from the seat, lit by the picture, with the picture hung
    // where the room puts it, the glow where the room has it on, the models
    const char* names[4] = { "", "theater", "cinema", "synthwave" };
    ctx->roomSwapchain = FAKE_ROOM;
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

    m5_image_free(ctx, &colour);
    m5_image_free(ctx, &depth);
    m5_video_free(ctx, &video);
    ctx->glowSwapchain = ctx->modelSwapchain = ctx->roomSwapchain = XR_NULL_HANDLE;
    xrWorldDestroy(ctx);
    scene_free(s);
    m5_gpu_free(&g);
    free(im.room);
    free(im.models);
    free(im.curve);
    free(im.glow);
    return 0;
}
