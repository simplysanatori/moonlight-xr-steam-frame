// Moonlight XR Native (lane wave6-splash): see xr_splash.h. GPLv3.
#include "xr_native.h"
#include "xr_splash.h"

#include "panels_int.h"
#include "text.h"
#include "ui_strings.h"

// The quad hangs off the eyes, straight ahead and a little above the middle
// of the view, 0.9 m wide at 1.5 m: the logo about a third of a metre across
#define SPLASH_DISTANCE_M 1.5f
#define SPLASH_RISE_M 0.04f
#define SPLASH_W_M 0.9f

// ---- the state --------------------------------------------------------------------------

float xrSplashStep(XrSplashState* s, int streaming, int launcherUp, int pictureShown, int64_t nowNs) {
    if (!streaming) {
        // No stream: nothing to wait for, and the next one starts again at "Connecting"
        s->wasStreaming = 0;
        s->seenFrame = 0;
        s->up = 0;
        s->fading = 0;
        s->level = 0.0f;
        return 0.0f;
    }
    s->wasStreaming = 1;
    if (pictureShown) {
        s->seenFrame = 1;
    }
    if (launcherUp) {
        // The list is in the picture's place, and says what it needs to
        s->up = 0;
        s->fading = 0;
        s->level = 0.0f;
        return 0.0f;
    }
    if (!pictureShown) {
        s->up = 1;
        s->fading = 0;
        s->reconnect = s->seenFrame;
        s->level = 1.0f;
        return 1.0f;
    }
    // The picture is here. If the splash was up, it fades out over the next 300 ms
    if (s->up) {
        s->up = 0;
        s->fading = 1;
        s->fadeFromNs = nowNs;
    }
    if (s->fading) {
        float level = 1.0f - (float)(nowNs - s->fadeFromNs) / (float)SPLASH_FADE_NS;
        if (level <= 0.0f) {
            s->fading = 0;
            level = 0.0f;
        }
        s->level = level;
        return level;
    }
    s->level = 0.0f;
    return 0.0f;
}

void xrSplashLine(int reconnect, const char* host, char* out, int cap) {
    if (reconnect) {
        snprintf(out, (size_t)cap, "%s", ui_str(UIS_NATIVE_SPLASH_RECONNECTING));
    }
    else if (host != NULL && host[0] != '\0') {
        snprintf(out, (size_t)cap, ui_str(UIS_NATIVE_SPLASH_CONNECTING), host);
    }
    else {
        snprintf(out, (size_t)cap, "%s", ui_str(UIS_NATIVE_SPLASH_CONNECTING_NOPC));
    }
}

int xrSplashShowing(const XrCtx* ctx) {
    return ctx->splashUi.up || ctx->splashUi.fading;
}

// ---- the art ------------------------------------------------------------------------------

// The logo is the app's own icon art (assets/branding/, made by
// tools/branding/make-art.py), found through the same assets directory as the
// settings icon and the Ko-fi code
#define SPLASH_ICON "branding/icon-256.png"
#define SPLASH_DISC_PX 232.0f
#define SPLASH_TEXT_PX 36.0f
#define SPLASH_PILL_H 64.0f
#define SPLASH_PILL_TOP 270.0f

// Not thread safe against other drawings of the same font: the panel worker only
void xrSplashDraw(UiCanvas* cv, const char* line, int alpha) {
    const UiImage* logo = ui_asset_image(SPLASH_ICON);
    float w = (float)cv->w;
    float x0 = (w - SPLASH_DISC_PX) * 0.5f, y0 = 8.0f;
    if (logo != NULL) {
        ui_image_cover(cv, logo->rgba, logo->w, logo->h, x0, y0, x0 + SPLASH_DISC_PX, y0 + SPLASH_DISC_PX, 0.0f);
    }
    else {
        // No assets directory: a plain disc in the logo's blue, so the line still has something over it
        ui_fill_circle(cv, w * 0.5f, y0 + SPLASH_DISC_PX * 0.5f, SPLASH_DISC_PX * 0.5f, 0xFF2E5FA8u);
    }
    UiFont* font = ui_font_default();
    if (font != NULL && line != NULL && line[0] != '\0') {
        char cut[200];
        float room = w - 96.0f;
        ui_text_ellipsize(font, UI_WEIGHT_REGULAR, SPLASH_TEXT_PX, line, room, UI_ELLIPSIZE_END, cut, sizeof(cut),
                          NULL);
        float tw = ui_text_width(font, UI_WEIGHT_REGULAR, SPLASH_TEXT_PX, cut, -1);
        float pw = tw + 72.0f;
        if (pw > w - 8.0f) {
            pw = w - 8.0f;
        }
        float px0 = (w - pw) * 0.5f;
        ui_fill_rrect(cv, px0, SPLASH_PILL_TOP, px0 + pw, SPLASH_PILL_TOP + SPLASH_PILL_H, SPLASH_PILL_H * 0.5f,
                      0xE0141416u);
        float base = ui_text_baseline_centered(font, UI_WEIGHT_REGULAR, SPLASH_TEXT_PX,
                                               SPLASH_PILL_TOP + SPLASH_PILL_H * 0.5f);
        ui_text_draw(cv, font, UI_WEIGHT_REGULAR, SPLASH_TEXT_PX, w * 0.5f, base, UI_ALIGN_CENTER, 0xFFFFFFFFu, cut,
                     -1);
    }
    if (alpha < 255) {
        // Premultiplied: every channel scales with the alpha
        int a = alpha < 0 ? 0 : alpha;
        for (int y = 0; y < cv->h; y++) {
            uint8_t* row = cv->px + (size_t)y * (size_t)cv->stride;
            for (int x = 0; x < cv->w * 4; x++) {
                row[x] = (uint8_t)((row[x] * a + 127) / 255);
            }
        }
    }
}

// ---- the layer ----------------------------------------------------------------------------

static unsigned hashLine(const char* s) {
    unsigned h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    return h | 1u;
}

int xrSplashLayers(XrCtx* ctx, XrFrameOut* out, int64_t nowNs) {
    XrSplashState* s = &ctx->splashUi;
    int wasShowing = xrSplashShowing(ctx);
    float level = xrSplashStep(s, ctx->streaming, ctx->launcher != LAUNCHER_NONE, ctx->videoShown, nowNs);
    if (level <= 0.0f) {
        if (wasShowing) {
            LOGI("splash: gone");
        }
        return 0;
    }
    char host[UI_STR_MAX];
    xrStreamIdentityHost(host, sizeof(host));
    xrSplashLine(s->reconnect, host, s->line, (int)sizeof(s->line));
    unsigned key = hashLine(s->line);
    int step = (int)ceilf(level * SPLASH_STEPS);
    if (step < 1) {
        step = 1;
    }
    if (step > SPLASH_STEPS) {
        step = SPLASH_STEPS;
    }
    if (!wasShowing) {
        LOGI("splash: %s (%s)", s->line, s->reconnect ? "the stream dropped" : "no picture yet");
    }
    // The art is asked for again when its words or the fade's step change; the
    // worker draws only the newest ask, and the frame loop never waits for it
    if (key != s->askedKey || step != s->askedStep) {
        s->askedKey = key;
        s->askedStep = step;
        xrPanelsSplash(ctx, s->line, key, step, step * 255 / SPLASH_STEPS);
    }
    // Shown once the art of these words is in the atlas at least as bright as
    // this frame's step (a fade step still to land shows the brighter one)
    if (!s->artReady || s->drawnKey != key || s->drawnStep < step) {
        return 0;
    }
    if (!ctx->shouldRender) {
        return 0;
    }
    // Over the limit it is the first thing to go, and never a reason to drop another layer
    if (out->count >= (uint32_t)ctx->maxLayerCount || out->count >= XR_FRAME_MAX_LAYERS) {
        return 0;
    }
    UiRectPx r = xrAtlasRect(ATLAS_SPLASH);
    XrCompositionLayerQuad* q = &s->quad;
    memset(q, 0, sizeof(*q));
    q->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    q->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    q->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q->subImage.swapchain = xrAtlasSwapchain(ctx);
    q->subImage.imageRect.offset.x = r.x;
    q->subImage.imageRect.offset.y = r.y;
    q->subImage.imageRect.extent.width = r.w;
    q->subImage.imageRect.extent.height = r.h;
    q->space = ctx->viewSpace;
    q->pose.orientation.w = 1.0f;
    q->pose.position.y = SPLASH_RISE_M;
    q->pose.position.z = -SPLASH_DISTANCE_M;
    q->size.width = SPLASH_W_M;
    q->size.height = SPLASH_W_M * (float)r.h / (float)r.w;
    out->order[out->count++] = (const XrCompositionLayerBaseHeader*)q;
    return 1;
}
