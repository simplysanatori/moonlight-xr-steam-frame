// Native (lane radial-dwell): the dwell click's radial menu on the XR side, and the dwell's art.
//
// The input pass's updateDwell hands each frame of the head or eye pointer's dwell here
// (xrDwellMenuStep). The state machine is xr/core/xr_radial.c's; this keeps where the dwell
// started on the picture, holds the host's cursor there while the menu is up and while the click
// it chose plays (so every click lands on the dwell's start, wherever the dot went to pick), plays
// the choices through the headset button's player (xrAssistActionAt, on the picture), and keeps a
// drag the menu started: the left button held until the next dwell (anywhere) or a press of the
// headset button lets it go; a dwell back on the drag's start lets it go there, so nothing moves.
//
// The art is drawn once into the UI atlas with the rest of the code drawn art: every step of the
// dwell's circle (a thin arc in the dot's colour sweeping clockwise from the top while its radius
// shrinks onto the dot, the dot inside it, so the two are one quad), the circle fading when a dwell
// is dropped, the hollow dot of a held drag and the mark at its start, and the menu (three segments,
// left, down and right, with an icon each, a gap above) at each step of its fade in and of a lit
// segment's hold. A frame then only picks a picture and moves its quad. GPLv3.
#include "xr_renderer.h"
#include "xr_native.h"

// How long the host's cursor stays on the dwell's start after a click's presses end (and after a
// drag's button goes down), so the host has seen them there before the cursor moves on
#define DWELL_HOLD_AFTER_NS 50000000LL

static int pointing(const XrCtx* ctx) {
    return ctx->pointerSource != POINTER_SOURCE_CONTROLLER && !ctx->padMode;
}

static void holdAt(XrCtx* ctx, float u, float v, int64_t until) {
    ctx->dwellHoldU = u;
    ctx->dwellHoldV = v;
    if (until > ctx->dwellHoldUntil) {
        ctx->dwellHoldUntil = until;
    }
}

static void letGo(XrCtx* ctx, int64_t now, const char* why) {
    if (!ctx->dwellDrag) {
        return;
    }
    ctx->dwellDrag = 0;
    assistPlayStart(&ctx->assistPlay, ASSIST_DRAG_UP, now);
    LOGEV("dwell menu: drag let go (%s)", why);
}

void xrDwellMenuReset(XrCtx* ctx) {
    radialReset(&ctx->radial);
    memset(&ctx->radialView, 0, sizeof(ctx->radialView));
    ctx->radialView.fraction = -1.0f;
    ctx->radialView.sector = RADIAL_NONE;
    ctx->dwellRingShown = 0;
    ctx->dwellFadeNs = 0;
    ctx->dwellHoldUntil = 0;
    letGo(ctx, ctx->dwellArtNs, "the dwell stopped");
}

void xrDwellMenuWatch(XrCtx* ctx, int64_t now, int focused, int streaming) {
    if (ctx->dwellDrag && (!focused || !streaming || !pointing(ctx))) {
        letGo(ctx, now, !focused ? "the session lost focus" : !streaming ? "no stream" : "the pointer changed");
    }
}

int xrDwellDragRelease(XrCtx* ctx, int64_t now) {
    if (!ctx->dwellDrag) {
        return 0;
    }
    letGo(ctx, now, "the headset button");
    return 1;
}

// A choice of the menu, on the dwell's start
static void act(XrCtx* ctx, int action, int64_t now) {
    float u = ctx->dwellAnchorU, v = ctx->dwellAnchorV;
    switch (action) {
    case ASSIST_LEFT:
    case ASSIST_DOUBLE:
    case ASSIST_RIGHT:
        xrAssistActionAt(ctx, action, now, 0);
        holdAt(ctx, u, v, now + (action == ASSIST_DOUBLE ? 3 : 1) * ASSIST_PRESS_NS + DWELL_HOLD_AFTER_NS);
        break;
    case ASSIST_DRAG_DOWN:
        xrAssistActionAt(ctx, ASSIST_DRAG_DOWN, now, 0);
        holdAt(ctx, u, v, now + ASSIST_PRESS_NS + DWELL_HOLD_AFTER_NS);
        ctx->dwellDrag = 1;
        ctx->dwellDragX = ctx->radial.ax;
        ctx->dwellDragY = ctx->radial.ay;
        ctx->dwellDragU = u;
        ctx->dwellDragV = v;
        ctx->dwellDragAt = ctx->dwellAnchorAt;
        break;
    default:
        return;
    }
    ctx->dwellClicks++;
    ctx->dwellMenuPicks++;
    LOGI("dwell menu: %s on the picture", assistActionName(action));
}

int xrDwellMenuStep(XrCtx* ctx, int valid, int aimed, float x, float y, int64_t now, int onPicture, float u,
                    float v, XrVector3f at) {
    // the radial style, on the picture, with no drag held: a dwell can open the menu
    int menu = ctx->assistDwellStyle == DWELL_STYLE_RADIAL && onPicture && !ctx->dwellDrag;
    // the dwell's angles have yaw to the left; the menu's right is right
    float right = -x, up = y;
    // the start of the dwell under way, before this step moves it (a drag's release asks)
    float wasX = ctx->dwell.x, wasY = ctx->dwell.y;
    int action = ASSIST_NONE;
    RadialView view;
    int ev = radialStep(&ctx->radial, &ctx->dwell, valid, aimed, right, up, now, ctx->headDwellMs,
                        DWELL_TOLERANCE_DEG, menu, &action, &view);
    if (view.fresh) {
        ctx->dwellAnchorValid = onPicture;
        ctx->dwellAnchorU = u;
        ctx->dwellAnchorV = v;
        ctx->dwellAnchorAt = at;
    }
    ctx->dwellArtNs = now;
    // the circle; one dropped before its click fades out where it had got to
    float g = radialRingProgress(view.fraction);
    if (g < 0.0f && ctx->dwellRingShown && !view.open && ev != RADIAL_EV_CLICK && ev != RADIAL_EV_ACTION) {
        ctx->dwellFadeFrom = ctx->dwellRingWas;
        ctx->dwellFadeNs = now;
    }
    if (g >= 0.0f || ev == RADIAL_EV_CLICK || ev == RADIAL_EV_ACTION) {
        ctx->dwellFadeNs = 0;
    }
    ctx->dwellRingShown = g >= 0.0f;
    ctx->dwellRingWas = g;
    ctx->dwellFraction = view.fraction;
    ctx->radialView = view;
    if (view.open && ctx->dwellAnchorValid) {
        holdAt(ctx, ctx->dwellAnchorU, ctx->dwellAnchorV, now);
    }
    switch (ev) {
    case RADIAL_EV_ACTION:
        if (ctx->dwellAnchorValid) {
            act(ctx, action, now);
        }
        return DWELL_NONE;
    case RADIAL_EV_CANCEL:
        LOGEV("dwell menu: cancelled");
        return DWELL_NONE;
    case RADIAL_EV_CLICK:
        if (!ctx->dwellDrag) {
            return DWELL_CLICK;
        }
        // a drag held: the dwell lets it go where the dot is, or on the drag's start when it is back
        // there (the dead zone round it), so nothing moves
        {
            float dx = wasX - ctx->dwellDragX, dy = wasY - ctx->dwellDragY;
            if (dx * dx + dy * dy <= RADIAL_DEAD_DEG * RADIAL_DEAD_DEG) {
                holdAt(ctx, ctx->dwellDragU, ctx->dwellDragV, now + ASSIST_PRESS_NS + DWELL_HOLD_AFTER_NS);
            }
        }
        letGo(ctx, now, "a dwell");
        ctx->dwellClicks++;
        return DWELL_NONE;
    default:
        return view.fraction >= 0.0f ? DWELL_CHARGING : DWELL_NONE;
    }
}

int xrDwellMenuHostPoint(const XrCtx* ctx, int64_t now, float* u, float* v) {
    if (!pointing(ctx)) {
        return 0;
    }
    if (ctx->radialView.open && ctx->dwellAnchorValid) {
        *u = ctx->dwellAnchorU;
        *v = ctx->dwellAnchorV;
        return 1;
    }
    if (now <= ctx->dwellHoldUntil) {
        *u = ctx->dwellHoldU;
        *v = ctx->dwellHoldV;
        return 1;
    }
    return 0;
}

void xrDwellMenuSendHeld(XrCtx* ctx, int hand, int64_t now, float* out) {
    float u, v;
    if (hand >= 0 && xrDwellMenuHostPoint(ctx, now, &u, &v)) {
        xrPointerToHost(ctx, hand, u, v, out);
    }
}

// ---- the art's places -----------------------------------------------------------------------------

static UiRectPx cell(int slot, int cols, int px, int i) {
    UiRectPx s = xrAtlasRect(slot);
    UiRectPx r = { s.x + (i % cols) * px, s.y + (i / cols) * px, px, px };
    return r;
}

UiRectPx xrDwellRingRect(int frame) {
    UiRectPx none = { 0, 0, 0, 0 };
    return frame >= 0 && frame < RADIAL_RING_FRAMES ? cell(ATLAS_DWELL_RING, 16, DWELL_ART_PX, frame) : none;
}

UiRectPx xrDwellHollowRect(int frame) {
    UiRectPx none = { 0, 0, 0, 0 };
    return frame >= 0 && frame < RADIAL_HOLLOW_STEPS ? cell(ATLAS_DWELL_HOLLOW, 4, DWELL_ART_PX, frame) : none;
}

UiRectPx xrDwellMenuRect(int frame) {
    UiRectPx none = { 0, 0, 0, 0 };
    if (frame < 0 || frame >= RADIAL_MENU_FRAMES) {
        return none;
    }
    if (frame < 4) {
        return cell(ATLAS_DWELL_MENU_A, 1, DWELL_MENU_PX, frame);
    }
    if (frame < 7) {
        return cell(ATLAS_DWELL_MENU_B, 3, DWELL_MENU_PX, frame - 4);
    }
    return cell(ATLAS_DWELL_MENU_C, 4, DWELL_MENU_PX, frame - 7);
}

int xrDwellMenuArt(const XrCtx* ctx, XrDwellArt* art) {
    memset(art, 0, sizeof(*art));
    if (!pointing(ctx)) {
        return 0;
    }
    float g = radialRingProgress(ctx->dwellFraction);
    int64_t faded = ctx->dwellArtNs - ctx->dwellFadeNs;
    if (g >= 0.0f) {
        art->dot = ctx->dwellDrag ? xrDwellHollowRect(radialHollowFrame(g)) : xrDwellRingRect(radialRingFrame(g));
    }
    else if (ctx->dwellDrag) {
        art->dot = xrDwellRingRect(RADIAL_FRAME_HOLLOW_DOT);
    }
    else if (ctx->dwellFadeNs != 0 && faded < RADIAL_RING_FADE_NS) {
        art->dot = xrDwellRingRect(radialFadeFrame(ctx->dwellFadeFrom, faded));
    }
    art->dotSide = RADIAL_ART_SIDE;
    int menu = radialMenuFrame(&ctx->radialView);
    if (menu >= 0 && ctx->dwellAnchorValid) {
        art->mark = xrDwellMenuRect(menu);
        art->markAt = ctx->dwellAnchorAt;
        art->menuHalfDeg = RADIAL_MENU_HALF_DEG * radialMenuScale(&ctx->radialView);
    }
    else if (ctx->dwellDrag) {
        art->mark = xrDwellRingRect(RADIAL_FRAME_MARK);
        art->markAt = ctx->dwellDragAt;
        art->markSide = RADIAL_ART_SIDE;
    }
    return art->dot.w > 0 || art->mark.w > 0;
}

// ---- the art ----------------------------------------------------------------------------------------
// Premultiplied grey and alpha, built up layer over layer: white is the dot's colour, black the
// soft rim that keeps it seen on a bright picture

typedef struct {
    float c, a;
} Px;

static void over(Px* p, float lit, float alpha) {
    // a layer of grey lit (0 black .. 1 white) at alpha over what is there
    if (alpha <= 0.0f) {
        return;
    }
    if (alpha > 1.0f) alpha = 1.0f;
    p->c = lit * alpha + p->c * (1.0f - alpha);
    p->a = alpha + p->a * (1.0f - alpha);
}

static void put(uint8_t* atlas, UiRectPx r, int x, int y, Px p, float level) {
    uint8_t* q = atlas + (((size_t)(r.y + y) * UI_ATLAS_W) + (size_t)(r.x + x)) * 4;
    float c = p.c * level * 255.0f + 0.5f, a = p.a * level * 255.0f + 0.5f;
    q[0] = q[1] = q[2] = (uint8_t)(c > 255.0f ? 255.0f : c);
    q[3] = (uint8_t)(a > 255.0f ? 255.0f : a);
}

// Coverage of a stroke halfWidth either side of a line at distance d, a pixel of smoothing
static float stroke(float d, float halfWidth) {
    float t = halfWidth - fabsf(d) + 0.5f;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// Coverage of a filled shape whose signed distance (negative inside) is d
static float fill(float d) {
    float t = 0.5f - d;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

static float segDist(float px, float py, float ax, float ay, float bx, float by) {
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float l2 = vx * vx + vy * vy;
    float t = l2 > 0.0f ? (wx * vx + wy * vy) / l2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float dx = wx - vx * t, dy = wy - vy * t;
    return sqrtf(dx * dx + dy * dy);
}

static const float PI_F = 3.14159265f;

// Angle of (dx, dy) clockwise from straight up, 0 .. 2 pi (image rows go down)
static float clockAngle(float dx, float dy) {
    float a = atan2f(dx, -dy);
    return a < 0.0f ? a + 2.0f * PI_F : a;
}

// Distance to an arc of radius r round (0, 0), from clock angle a0 clockwise through sweep, with
// round ends
static float arcDist(float x, float y, float r, float a0, float sweep) {
    float rho = sqrtf(x * x + y * y);
    float t = clockAngle(x, y) - a0;
    while (t < 0.0f) t += 2.0f * PI_F;
    if (t <= sweep) {
        return fabsf(rho - r);
    }
    float sx = r * sinf(a0), sy = -r * cosf(a0);
    float ex = r * sinf(a0 + sweep), ey = -r * cosf(a0 + sweep);
    float ds = hypotf(x - sx, y - sy), de = hypotf(x - ex, y - ey);
    return ds < de ? ds : de;
}

// The dot as ATLAS_DOT draws it, at rd dot half sides from its middle
static void dotOver(Px* p, float rd) {
    float a = rd < 0.45f ? 1.0f : (rd < 0.75f ? (0.75f - rd) / 0.30f : 0.0f);
    float shade = rd < 0.35f ? 1.0f : 0.25f;
    over(p, shade, a);
}

// The hollow dot of a held drag: a ring where the dot's edge is, with the same dark rim
static void hollowOver(Px* p, float rd, float pxPerDot) {
    over(p, 0.0f, 0.55f * stroke((rd - 0.5f) * pxPerDot, 2.4f));
    over(p, 1.0f, stroke((rd - 0.5f) * pxPerDot, 1.3f));
}

// One frame of the dot's quad: the circle at progress g (no circle below 0) at level, round a
// filled or a hollow dot
static void ringFrame(uint8_t* atlas, UiRectPx r, float g, float level, int hollow) {
    const float half = DWELL_ART_PX * 0.5f;
    const float pxPerDot = half / RADIAL_ART_SIDE;   // pixels to a dot half side
    float radius = (RADIAL_RING_R_END + (RADIAL_RING_R_START - RADIAL_RING_R_END) * (1.0f - g)) * half;
    float sweep = g * 2.0f * PI_F;
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            float dx = x + 0.5f - half, dy = y + 0.5f - half;
            float rho = sqrtf(dx * dx + dy * dy);
            Px p = { 0.0f, 0.0f };
            if (g >= 0.0f) {
                float track = rho - radius;
                float arc = arcDist(dx, dy, radius, 0.0f, sweep);
                // a soft dark rim under it all, the whole circle faint, the swept arc bright
                Px ring = { 0.0f, 0.0f };
                over(&ring, 0.0f, 0.38f * stroke(track, 2.6f));
                over(&ring, 1.0f, 0.30f * stroke(track, 0.9f));
                over(&ring, 0.0f, 0.45f * stroke(arc, 2.9f));
                over(&ring, 1.0f, stroke(arc, 1.35f));
                over(&p, ring.a > 0.0f ? ring.c / ring.a : 0.0f, ring.a * level);
            }
            float rd = rho / pxPerDot;
            if (hollow) {
                hollowOver(&p, rd, pxPerDot);
            }
            else {
                dotOver(&p, rd);
            }
            put(atlas, r, x, y, p, 1.0f);
        }
    }
}

// The mark at a drag's start: a small dot in a thin ring, a little fainter than the pointer's
static void markFrame(uint8_t* atlas, UiRectPx r) {
    const float half = DWELL_ART_PX * 0.5f;
    const float pxPerDot = half / RADIAL_ART_SIDE;
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            float dx = x + 0.5f - half, dy = y + 0.5f - half;
            float rd = sqrtf(dx * dx + dy * dy) / pxPerDot;
            Px p = { 0.0f, 0.0f };
            over(&p, 0.0f, 0.45f * stroke((rd - 1.05f) * pxPerDot, 2.3f));
            over(&p, 1.0f, 0.85f * stroke((rd - 1.05f) * pxPerDot, 1.0f));
            over(&p, 0.0f, 0.45f * fill((rd - 0.42f) * pxPerDot));
            over(&p, 1.0f, 0.9f * fill((rd - 0.3f) * pxPerDot));
            put(atlas, r, x, y, p, 1.0f);
        }
    }
}

// ---- the menu's icons, about 22 pixels, round (0, 0), rows down -----------------------------------

// A mouse, its middle at (cx, 0): the body's outline, the buttons' split, one button filled
// (side -1 left, 1 right). Returns the coverage.
static float mouseIcon(float x, float y, float cx, int side) {
    const float hw = 6.6f, hh = 10.0f, rad = 6.6f, split = -2.2f, w = 0.85f;
    float qx = fabsf(x - cx) - (hw - rad), qy = fabsf(y) - (hh - rad);
    float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
    float inside = qx > qy ? qx : qy;
    float body = sqrtf(ox * ox + oy * oy) + (inside < 0.0f ? inside : 0.0f) - rad;   // signed
    float c = stroke(body, w);
    // the buttons: a line across, and one down the middle above it
    c = fmaxf(c, stroke(segDist(x, y, cx - hw, split, cx + hw, split), w) * fill(body));
    c = fmaxf(c, stroke(segDist(x, y, cx, -hh, cx, split), w));
    // the pressed button, inside the body and above the line, on its side
    float sideD = side < 0 ? (x - cx) : (cx - x);
    float button = fmaxf(fmaxf(body + 1.6f, y - (split - 1.6f)), sideD + 1.6f);
    return fmaxf(c, fill(button));
}

// A "2", its box round (cx, 0), 10 pixels tall
static float twoIcon(float x, float y, float cx) {
    const float w = 0.95f;
    float c = stroke(arcDist(x - cx, y + 2.4f, 2.9f, 1.5f * PI_F, 1.5f * PI_F * 0.78f), w);
    // where the arc ends (clock angle 3/2 pi plus its sweep) down to the foot, then the foot
    float a = 1.5f * PI_F + 1.5f * PI_F * 0.78f;
    float ex = cx + 2.9f * sinf(a), ey = -2.4f - 2.9f * cosf(a);
    c = fmaxf(c, stroke(segDist(x, y, ex, ey, cx - 3.0f, 5.2f), w));
    c = fmaxf(c, stroke(segDist(x, y, cx - 3.0f, 5.2f, cx + 3.2f, 5.2f), w));
    return c;
}

// Four arrows out from the middle: to move
static float dragIcon(float x, float y) {
    const float len = 10.0f, head = 3.6f, w = 0.95f;
    float c = fmaxf(stroke(segDist(x, y, -len, 0, len, 0), w), stroke(segDist(x, y, 0, -len, 0, len), w));
    static const float D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    for (int i = 0; i < 4; i++) {
        float tx = D[i][0] * len, ty = D[i][1] * len;
        // the two barbs, back from the tip at 45 degrees
        float bx = -D[i][0] * head, by = -D[i][1] * head, px = -D[i][1] * head, py = D[i][0] * head;
        c = fmaxf(c, stroke(segDist(x, y, tx, ty, tx + bx + px, ty + by + py), w));
        c = fmaxf(c, stroke(segDist(x, y, tx, ty, tx + bx - px, ty + by - py), w));
    }
    return c;
}

static float iconAt(int sector, float x, float y) {
    switch (sector) {
    case RADIAL_LEFT: return fmaxf(mouseIcon(x, y, 4.6f, -1), twoIcon(x, y, -8.0f));
    case RADIAL_DOWN: return dragIcon(x, y);
    case RADIAL_RIGHT: return mouseIcon(x, y, 0.0f, 1);
    default: return 0.0f;
    }
}

// One frame of the menu: the segments at level (its fade in), lit the segment lit (-1 none) with its
// hold at part
static void menuFrame(uint8_t* atlas, UiRectPx r, float level, int lit, float part) {
    const float half = DWELL_MENU_PX * 0.5f;
    const float pxDeg = half / RADIAL_MENU_HALF_DEG;
    const float inner = RADIAL_DEAD_DEG * pxDeg, outer = RADIAL_RING_DEG * pxDeg;
    const float iconR = (inner + outer) * 0.5f;
    // left, down, right as clock angles; each segment its quarter less a small gap either side
    static const float MID[RADIAL_SECTORS] = { 1.5f * PI_F, PI_F, 0.5f * PI_F };
    const float spread = PI_F * 0.25f - 0.045f;
    for (int y = 0; y < r.h; y++) {
        for (int x = 0; x < r.w; x++) {
            float dx = x + 0.5f - half, dy = y + 0.5f - half;
            float rho = sqrtf(dx * dx + dy * dy);
            float ang = clockAngle(dx, dy);
            Px p = { 0.0f, 0.0f };
            for (int s = 0; s < RADIAL_SECTORS; s++) {
                float t = fabsf(ang - MID[s]);
                if (t > PI_F) t = 2.0f * PI_F - t;
                float dAng = (t - spread) * rho;   // past the segment's sides, in pixels
                float dRad = fmaxf(inner - rho, rho - outer);
                float d = fmaxf(dAng, dRad);
                int on = s == lit;
                // the segment: dark glass, a thin edge in the dot's colour, lit as its hold runs
                over(&p, 0.0f, 0.42f * fill(d));
                if (on) {
                    float filled = fmaxf(d, rho - (inner + (outer - inner) * part));
                    over(&p, 1.0f, 0.34f * fill(filled));
                }
                over(&p, 1.0f, (on ? 1.0f : 0.55f) * stroke(d, on ? 0.95f : 0.6f));
                float ix = dx - iconR * sinf(MID[s]), iy = dy + iconR * cosf(MID[s]);
                if (fabsf(ix) < 16.0f && fabsf(iy) < 16.0f) {
                    over(&p, 0.0f, 0.35f * iconAt(s, ix * 0.92f, iy * 0.92f));
                    over(&p, 1.0f, (on ? 1.0f : 0.9f) * iconAt(s, ix, iy));
                }
            }
            put(atlas, r, x, y, p, level);
        }
    }
}

void xrDrawDwellArt(uint8_t* atlas) {
    for (int i = 0; i < RADIAL_RING_STEPS; i++) {
        ringFrame(atlas, xrDwellRingRect(i), (float)i / (float)(RADIAL_RING_STEPS - 1), 1.0f, 0);
    }
    for (int i = 0; i < RADIAL_HOLLOW_STEPS; i++) {
        ringFrame(atlas, xrDwellHollowRect(i), (float)i / (float)(RADIAL_HOLLOW_STEPS - 1), 1.0f, 1);
    }
    for (int b = 0; b < RADIAL_FADE_BUCKETS; b++) {
        for (int l = 0; l < RADIAL_FADE_LEVELS; l++) {
            ringFrame(atlas, xrDwellRingRect(RADIAL_FRAME_FADE + b * RADIAL_FADE_LEVELS + l),
                      ((float)b + 0.5f) / (float)RADIAL_FADE_BUCKETS, radialFadeLevel(l), 0);
        }
    }
    ringFrame(atlas, xrDwellRingRect(RADIAL_FRAME_HOLLOW_DOT), -1.0f, 1.0f, 1);
    markFrame(atlas, xrDwellRingRect(RADIAL_FRAME_MARK));
    for (int f = 0; f < RADIAL_MENU_FADE_STEPS; f++) {
        menuFrame(atlas, xrDwellMenuRect(f), (float)(f + 1) / (float)RADIAL_MENU_FADE_STEPS, -1, 0.0f);
    }
    for (int s = 0; s < RADIAL_SECTORS; s++) {
        for (int k = 0; k < RADIAL_HOLD_STEPS; k++) {
            menuFrame(atlas, xrDwellMenuRect(RADIAL_MENU_FADE_STEPS + s * RADIAL_HOLD_STEPS + k), 1.0f, s,
                      (float)(k + 1) / (float)RADIAL_HOLD_STEPS);
        }
    }
}
