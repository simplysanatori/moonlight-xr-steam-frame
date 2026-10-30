// Moonlight XR Native, tests/m4: the headset-free scene (see scene.h). GPLv3.
#include "scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

Scene* scene_new(void) {
    Scene* s = calloc(1, sizeof(Scene));
    s->ctx = calloc(1, sizeof(XrCtx));
    settings_defaults(&s->settings);
    XrCtx* ctx = s->ctx;
    xrCtxDefaults(ctx, 2560, 1440, 90);
    xrApplySettings(ctx, &s->settings);
    if (!xrAtlasCreateCpu(ctx) || !xrPanelsCreate(ctx)) {
        fprintf(stderr, "scene: no atlas\n");
        exit(1);
    }
    ctx->sessionState = XR_SESSION_STATE_FOCUSED;
    ctx->sessionRunning = 1;
    ctx->shouldRender = 1;
    ctx->everRendered = 1;
    ctx->videoShown = 1;
    ctx->streaming = 1;
    ctx->swapchain = SCENE_VIDEO_SWAPCHAIN;
    // as the session sets it: the curve is drawn by the host
    ctx->cylinderSupported = 1;
    scene_reset(s);
    return s;
}

void scene_free(Scene* s) {
    xrPanelsDestroy(s->ctx);
    xrAtlasDestroy(s->ctx);
    free(s->ctx);
    free(s);
}

void scene_reset(Scene* s) {
    XrCtx* c = s->ctx;
    c->cogOpen = c->kbOpen = c->exitConfirmOpen = c->pickerOpen = c->reportOpen = c->hintOpen = c->kofiOpen = c->hostOpen = 0;
    c->hostConfirm = -1;
    c->hostHoverZone = XR_HOST_ZONE_NONE;
    c->hostPage = XR_HOST_PAGE_MAIN;
    c->hostActKind = XR_HOSTACT_NONE;
    memset(&c->hostCompose, 0, sizeof(c->hostCompose));
    for (int i = 0; i < FADE_PANELS; i++) {
        c->panelFades[i].level = 0.0f;
    }
    c->hoverKind = HOVER_NONE;
    c->beamVisible = 0;
    c->launcher = LAUNCHER_NONE;
    c->cogHoverSlider = -1;
    c->cogHoverCell = -1;
    c->cogHoverStep = 0;
    c->kbHoverKey = -1;
    c->pickerHover = -1;
    c->exitHoverZone = EXIT_ZONE_NONE;
    c->envButtonHot = c->cogButtonHot = c->kbButtonHot = c->exitButtonHot = c->rayButtonHot = c->aimButtonHot = 0;
    memset(&s->launcher, 0, sizeof(s->launcher));
}

void scene_ray_to(Scene* s, Vec3 hand, Vec3 target) {
    XrCtx* c = s->ctx;
    c->beamVisible = 1;
    c->beamFree = 0;
    c->beamGaze = 0;
    c->beamStart = (XrVector3f){ hand.x, hand.y, hand.z };
    c->beamEnd = (XrVector3f){ target.x, target.y, target.z };
    c->headPos = (XrVector3f){ 0, 0, 0 };
}

void scene_frame(Scene* s, const char* stats) {
    XrCtx* ctx = s->ctx;
    s->live = s->settings;
    xrSettingsFromCtx(ctx, &s->live);
    s->live.head_locked = s->settings.head_locked;
    xrPanelsUpdate(ctx, &s->live, &s->launcher, stats, "10:42", nowNs());
    XrFrameIn in;
    memset(&in, 0, sizeof(in));
    xrDefaultPlacement(&in.distance, &in.quadWidth, &in.curvature);
    in.headLocked = s->settings.head_locked;
    in.passthrough = ctx->passthrough;
    in.curved = s->curved;
    memset(&s->out, 0, sizeof(s->out));
    xrBuildLayers(ctx, &in, &s->out);
    // as the session does right after: the splash's layer, only while there is no picture (or it is fading out)
    xrSplashLayers(ctx, &s->out, nowNs());
}

// The stand in for a frame of video: a dark gradient with a grid, so the
// panels read against something
static void videoTexel(float u, float v, float out[4]) {
    float g = 0.12f + 0.25f * u;
    float b = 0.18f + 0.30f * (1.0f - v);
    int grid = (fmodf(u * 32.0f, 1.0f) < 0.03f) || (fmodf(v * 18.0f, 1.0f) < 0.05f);
    out[0] = grid ? 0.45f : 0.10f;
    out[1] = grid ? 0.45f : g;
    out[2] = grid ? 0.50f : b;
    out[3] = 1.0f;
}

static void atlasTexel(XrCtx* ctx, const XrRect2Di* r, float u, float v, float out[4]) {
    const uint8_t* px = xrAtlasPixels(ctx);
    float fx = r->offset.x + u * r->extent.width - 0.5f;
    float fy = r->offset.y + v * r->extent.height - 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - x0, ay = fy - y0;
    for (int c = 0; c < 4; c++) out[c] = 0.0f;
    for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
            int x = x0 + dx, y = y0 + dy;
            // clamp to the rect, as the compositor's sampler does at an image's edge
            if (x < r->offset.x) x = r->offset.x;
            if (y < r->offset.y) y = r->offset.y;
            if (x > r->offset.x + r->extent.width - 1) x = r->offset.x + r->extent.width - 1;
            if (y > r->offset.y + r->extent.height - 1) y = r->offset.y + r->extent.height - 1;
            float w = (dx ? ax : 1 - ax) * (dy ? ay : 1 - ay);
            const uint8_t* p = px + ((size_t)y * UI_ATLAS_W + (size_t)x) * 4;
            for (int c = 0; c < 4; c++) out[c] += w * p[c] / 255.0f;
        }
    }
}

int scene_render(Scene* s, int w, int h, float hfovDeg, const char* path) {
    XrCtx* ctx = s->ctx;
    float* acc = calloc((size_t)w * h * 3, sizeof(float));
    // the void
    for (int i = 0; i < w * h * 3; i++) acc[i] = 0.02f;
    float tx = tanf(hfovDeg * (float)M_PI / 360.0f);
    float ty = tx * (float)h / (float)w;
    int drawn = 0;
    for (uint32_t li = 0; li < s->out.count; li++) {
        const XrCompositionLayerBaseHeader* base = s->out.order[li];
        if (base->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION && s->projTexel != NULL) {
            const XrCompositionLayerProjection* pl = (const XrCompositionLayerProjection*)base;
            int blendP = (pl->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
            drawn++;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < w; x++) {
                    float texel[4];
                    if (!s->projTexel(s->hookUser, pl, x, y, texel)) continue;
                    float* o = &acc[((size_t)y * w + x) * 3];
                    float keep = blendP ? 1.0f - texel[3] : 0.0f;
                    for (int k = 0; k < 3; k++) o[k] = texel[k] + o[k] * keep;
                }
            }
            continue;
        }
        if (base->type != XR_TYPE_COMPOSITION_LAYER_QUAD) {
            continue;
        }
        const XrCompositionLayerQuad* q = (const XrCompositionLayerQuad*)base;
        drawn++;
        Vec3 back = { 0, 0, 1 };
        Vec3 n = quatRotate(q->pose.orientation, back);
        Vec3 c = { q->pose.position.x, q->pose.position.y, q->pose.position.z };
        int blend = (q->layerFlags & XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT) != 0;
        XrQuaternionf inv = quatConj(q->pose.orientation);
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Vec3 d = { ((x + 0.5f) / w * 2 - 1) * tx, (1 - (y + 0.5f) / h * 2) * ty, -1 };
                float dn = vecDot(d, n);
                if (fabsf(dn) < 1e-6f) continue;
                float t = vecDot(c, n) / dn;
                if (t <= 0) continue;
                Vec3 p = { d.x * t - c.x, d.y * t - c.y, d.z * t - c.z };
                Vec3 l = quatRotate(inv, p);
                float u = l.x / q->size.width + 0.5f, v = 0.5f - l.y / q->size.height;
                if (u < 0 || u > 1 || v < 0 || v > 1) continue;
                float texel[4];
                if (s->quadTexel != NULL && s->quadTexel(s->hookUser, q->subImage.swapchain, u, v, texel)) {
                    // the hook's image
                }
                else if (q->subImage.swapchain == SCENE_VIDEO_SWAPCHAIN) {
                    videoTexel(u, v, texel);
                }
                else {
                    atlasTexel(ctx, &q->subImage.imageRect, u, v, texel);
                }
                float* o = &acc[((size_t)y * w + x) * 3];
                float keep = blend ? 1.0f - texel[3] : 0.0f;
                for (int k = 0; k < 3; k++) o[k] = texel[k] + o[k] * keep;
            }
        }
    }
    if (path != NULL) {
        uint8_t* png = malloc((size_t)w * h * 3);
        for (int i = 0; i < w * h * 3; i++) {
            float v = acc[i];
            png[i] = (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255.0f + 0.5f);
        }
        stbi_write_png(path, w, h, 3, png, w * 3);
        free(png);
    }
    free(acc);
    return drawn;
}
