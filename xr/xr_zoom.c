// Zoom and pan (PLAN 4b.6): a part of the picture fills the screen. The state
// is the context's zoomPct, zoomX and zoomY (config.ini's [display] zoom,
// zoom_x and zoom_y, remembered per PC and app with the layout); the arithmetic
// is xr_layout.c's. The warp pass draws the part (warp.frag), the pointer is
// mapped back through it (xrDepthPointerUv) and the glow samples it
// (xr_ambilight.c). GPLv3.
#include <string.h>

#include "xr_native.h"

static int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void xrZoomRect(const XrCtx* ctx, float rect[4]) {
    if (ctx->zoomPct < ZOOM_MIN_PCT) {
        // Never set (a context nothing applied the settings to): the whole picture
        rect[0] = rect[1] = 0.0f;
        rect[2] = rect[3] = 1.0f;
        return;
    }
    zoomRect(ctx->zoomPct, ctx->zoomX, ctx->zoomY, rect);
}

void xrZoomPointer(const XrCtx* ctx, float* u, float* v) {
    float rect[4];
    xrZoomRect(ctx, rect);
    zoomPointerUv(rect, u, v);
}

void xrZoomFromSettings(XrCtx* ctx, const Settings* s) {
    ctx->zoomPct = clampInt(s->zoom, ZOOM_MIN_PCT, ZOOM_MAX_PCT);
    ctx->zoomX = clampInt(s->zoom_x, ZOOM_PAN_MIN, ZOOM_PAN_MAX);
    ctx->zoomY = clampInt(s->zoom_y, ZOOM_PAN_MIN, ZOOM_PAN_MAX);
}

void xrZoomToSettings(const XrCtx* ctx, Settings* s) {
    if (ctx->zoomPct < ZOOM_MIN_PCT) {
        return;
    }
    s->zoom = ctx->zoomPct;
    s->zoom_x = ctx->zoomX;
    s->zoom_y = ctx->zoomY;
}

int xrZoomSync(XrCtx* ctx, Settings* s) {
    if (ctx->zoomPct < ZOOM_MIN_PCT) {
        return 0;
    }
    if (s->zoom == ctx->zoomPct && s->zoom_x == ctx->zoomX && s->zoom_y == ctx->zoomY) {
        return 0;
    }
    xrZoomToSettings(ctx, s);
    return 1;
}

int xrZoomStep(XrCtx* ctx, int dir) {
    if (ctx == NULL) {
        return 0;
    }
    int was = ctx->zoomPct < ZOOM_MIN_PCT ? ZOOM_MIN_PCT : ctx->zoomPct;
    int wasX = ctx->zoomPct < ZOOM_MIN_PCT ? ZOOM_PAN_DEFAULT : ctx->zoomX;
    int wasY = ctx->zoomPct < ZOOM_MIN_PCT ? ZOOM_PAN_DEFAULT : ctx->zoomY;
    int pct = zoomStepPct(was, dir);
    int cx = ZOOM_PAN_DEFAULT, cy = ZOOM_PAN_DEFAULT;
    if (dir != 0) {
        // Aimed: the picture point under the pointer stays under it. With the
        // pointer not on the screen, the middle of the view.
        float hu = ctx->zoomHitValid ? ctx->zoomHitU : 0.5f;
        float hv = ctx->zoomHitValid ? ctx->zoomHitV : 0.5f;
        hu = hu < 0.0f ? 0.0f : (hu > 1.0f ? 1.0f : hu);
        hv = hv < 0.0f ? 0.0f : (hv > 1.0f ? 1.0f : hv);
        zoomAnchorCentre(was, wasX, wasY, pct, hu, hv, &cx, &cy);
    }
    if (pct == ctx->zoomPct && cx == ctx->zoomX && cy == ctx->zoomY) {
        return 0;
    }
    ctx->zoomPct = pct;
    ctx->zoomX = cx;
    ctx->zoomY = cy;
    LOGEV("zoom %d%% centred %d,%d%s", pct, cx, cy, dir == 0 ? " (reset)" : "");
    return 1;
}
