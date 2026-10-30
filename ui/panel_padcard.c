// The pad card (PLAN 2b.6): a flat schematic of the physical gamepad in use, drawn from
// UiView.pad (xr_cards.h's PadLive), with whatever is pressed, pulled or touched lit. One
// schematic per style (Xbox, PlayStation, Nintendo, Steam, generic); the layouts are drawn
// in code, not a model, and no maker's artwork is used. Native, no upstream counterpart.
// GPLv3.
#include <math.h>
#include <string.h>

#include "panels_int.h"

#define COL_DIM 0x99FFFFFFu
#define COL_FAINT 0x55FFFFFFu
#define COL_BODY 0x16FFFFFFu
#define COL_LIT 0xFF4FC3F7u
#define COL_INK 0xFF101014u

const char* ui_pad_start_name(int style) {
    switch (style) {
    case PADSTYLE_PLAYSTATION: return ui_str(UIS_PADCARD_START_OPTIONS);
    case PADSTYLE_NINTENDO: return ui_str(UIS_PADCARD_START_PLUS);
    case PADSTYLE_XBOX:
    case PADSTYLE_STEAM: return ui_str(UIS_PADCARD_START_MENU);
    default: return ui_str(UIS_PADCARD_START_GENERIC);
    }
}

const char* ui_pad_back_name(int style) {
    switch (style) {
    case PADSTYLE_PLAYSTATION: return ui_str(UIS_PADCARD_BACK_CREATE);
    case PADSTYLE_NINTENDO: return ui_str(UIS_PADCARD_BACK_MINUS);
    case PADSTYLE_XBOX:
    case PADSTYLE_STEAM: return ui_str(UIS_PADCARD_BACK_VIEW);
    default: return ui_str(UIS_PADCARD_BACK_GENERIC);
    }
}

static const char* style_name(int style) {
    switch (style) {
    case PADSTYLE_PLAYSTATION: return ui_str(UIS_PADCARD_PLAYSTATION);
    case PADSTYLE_NINTENDO: return ui_str(UIS_PADCARD_NINTENDO);
    case PADSTYLE_STEAM: return ui_str(UIS_PADCARD_STEAM);
    case PADSTYLE_XBOX: return ui_str(UIS_PADCARD_XBOX);
    default: return ui_str(UIS_PADCARD_GENERIC);
    }
}

// A round part: filled when lit, an outline when not
static void round_part(Dc* dc, float cx, float cy, float r, int lit) {
    if (lit) {
        dc_fill_circle(dc, cx, cy, r, COL_LIT);
    } else {
        dc_stroke_circle(dc, cx, cy, r, 2.5f, COL_DIM);
    }
}

// A thumbstick: the ring lights when it is pulled or clicked, the cap follows the stick
static void stick(Dc* dc, float cx, float cy, float r, int x, int y, int clicked) {
    int pulled = padStickLit(x, y);
    dc_stroke_circle(dc, cx, cy, r, 2.5f, pulled || clicked ? COL_LIT : COL_DIM);
    float k = r * 0.55f / 32767.0f;
    float dx = (float)x * k, dy = -(float)y * k;
    float m = sqrtf(dx * dx + dy * dy);
    float lim = r * 0.55f;
    if (m > lim) {
        dx *= lim / m;
        dy *= lim / m;
    }
    dc_fill_circle(dc, cx + dx, cy + dy, r * 0.5f, clicked ? COL_LIT : (pulled ? 0xCC4FC3F7u : COL_FAINT));
    dc_stroke_circle(dc, cx + dx, cy + dy, r * 0.5f, 2.0f, pulled || clicked ? COL_LIT : COL_DIM);
}

static void dpad(Dc* dc, float cx, float cy, float s, int buttons) {
    struct { int bit; float x0, y0, x1, y1; } arm[4] = {
        { PAD_UP, cx - s * 0.32f, cy - s, cx + s * 0.32f, cy - s * 0.12f },
        { PAD_DOWN, cx - s * 0.32f, cy + s * 0.12f, cx + s * 0.32f, cy + s },
        { PAD_LEFT, cx - s, cy - s * 0.32f, cx - s * 0.12f, cy + s * 0.32f },
        { PAD_RIGHT, cx + s * 0.12f, cy - s * 0.32f, cx + s, cy + s * 0.32f },
    };
    for (int i = 0; i < 4; i++) {
        if (buttons & arm[i].bit) {
            dc_fill_rrect(dc, arm[i].x0, arm[i].y0, arm[i].x1, arm[i].y1, 6.0f, COL_LIT);
        } else {
            dc_stroke_rrect(dc, arm[i].x0, arm[i].y0, arm[i].x1, arm[i].y1, 6.0f, 2.5f, COL_DIM);
        }
    }
}

// The four face buttons around (cx, cy) at distance d: bottom, right, left, top, with what
// the pad prints on them (shapes on a PlayStation pad)
static void face(Dc* dc, float cx, float cy, float d, float r, int buttons, int style) {
    static const char* const TEXT[2][4] = {
        { "A", "B", "X", "Y" },   // Xbox, Steam, generic
        { "B", "A", "Y", "X" },   // Nintendo
    };
    const int bit[4] = { PAD_A, PAD_B, PAD_X, PAD_Y };
    const float px[4] = { cx, cx + d, cx - d, cx };
    const float py[4] = { cy + d, cy, cy, cy - d };
    for (int i = 0; i < 4; i++) {
        int lit = (buttons & bit[i]) != 0;
        round_part(dc, px[i], py[i], r, lit);
        uint32_t ink = lit ? COL_INK : COL_DIM;
        if (style == PADSTYLE_PLAYSTATION) {
            float q = r * 0.48f;
            if (i == 0) {
                dc_line(dc, px[i] - q, py[i] - q, px[i] + q, py[i] + q, 2.5f, ink);
                dc_line(dc, px[i] - q, py[i] + q, px[i] + q, py[i] - q, 2.5f, ink);
            } else if (i == 1) {
                dc_stroke_circle(dc, px[i], py[i], q, 2.5f, ink);
            } else if (i == 2) {
                dc_stroke_rrect(dc, px[i] - q, py[i] - q, px[i] + q, py[i] + q, 2.0f, 2.5f, ink);
            } else {
                float tri[6] = { px[i], py[i] - q * 1.1f, px[i] + q * 1.05f, py[i] + q * 0.8f,
                                 px[i] - q * 1.05f, py[i] + q * 0.8f };
                dc_polyline(dc, tri, 3, 2.5f, ink, 1);
            }
        } else {
            dc_text_center(dc, UI_WEIGHT_MEDIUM, r * 1.15f, px[i], py[i], ink, TEXT[style == PADSTYLE_NINTENDO][i]);
        }
    }
}

static void pill(Dc* dc, float cx, float cy, float w, float h, int lit) {
    if (lit) {
        dc_fill_rrect(dc, cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, h * 0.5f, COL_LIT);
    } else {
        dc_stroke_rrect(dc, cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f, h * 0.5f, 2.0f, COL_DIM);
    }
}

// A trigger: an outline with a bar filling from the left as far as it is pulled
static void trigger(Dc* dc, float x0, float y0, float x1, float y1, int v) {
    int lit = padTriggerLit(v);
    dc_stroke_rrect(dc, x0, y0, x1, y1, 8.0f, 2.5f, lit ? COL_LIT : COL_DIM);
    if (lit) {
        float w = (x1 - x0) * (float)(v > 255 ? 255 : v) / 255.0f;
        dc_fill_rrect(dc, x0, y0, x0 + w, y1, 8.0f, COL_LIT);
    }
}

static int anything_lit(const PadLive* p) {
    return p->buttons != 0 || padTriggerLit(p->lt) || padTriggerLit(p->rt) || padStickLit(p->lx, p->ly) ||
           padStickLit(p->rx, p->ry) || p->touching;
}

void panel_padcard(Dc* dc) {
    const PadLive* p = &dc->v->pad;
    int style = p->style >= 0 && p->style < PADSTYLE_COUNT ? p->style : PADSTYLE_GENERIC;
    dc_sheet(dc, 28.0f, 0xE6141416u);

    // header: what the pad is, and whether gyro is reaching the PC
    dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 30.0f, 36.0f, 56.0f, 440.0f, 0xFFFFFFFFu, style_name(style));
    const char* gyro = ui_str(UIS_PADCARD_GYRO);
    float gw = dc_text_width(dc, UI_WEIGHT_REGULAR, 22.0f, gyro);
    dc_text(dc, UI_WEIGHT_REGULAR, 22.0f, dc->nw - 36.0f, 54.0f, UI_ALIGN_RIGHT, p->gyro ? COL_LIT : COL_FAINT, gyro);
    round_part(dc, dc->nw - 36.0f - gw - 18.0f, 46.0f, 8.0f, p->gyro);

    // the body: two grips under a wide shell
    dc_fill_rrect(dc, 128.0f, 244.0f, 252.0f, 396.0f, 56.0f, COL_BODY);
    dc_fill_rrect(dc, 516.0f, 244.0f, 640.0f, 396.0f, 56.0f, COL_BODY);
    dc_fill_rrect(dc, 108.0f, 128.0f, 660.0f, 322.0f, 88.0f, COL_BODY);
    dc_stroke_rrect(dc, 108.0f, 128.0f, 660.0f, 322.0f, 88.0f, 2.5f, COL_DIM);

    // shoulder and trigger
    dc_stroke_rrect(dc, 140.0f, 112.0f, 262.0f, 126.0f, 7.0f, 2.5f, COL_DIM);
    dc_stroke_rrect(dc, 506.0f, 112.0f, 628.0f, 126.0f, 7.0f, 2.5f, COL_DIM);
    if (p->buttons & PAD_LB) dc_fill_rrect(dc, 140.0f, 112.0f, 262.0f, 126.0f, 7.0f, COL_LIT);
    if (p->buttons & PAD_RB) dc_fill_rrect(dc, 506.0f, 112.0f, 628.0f, 126.0f, 7.0f, COL_LIT);
    trigger(dc, 150.0f, 82.0f, 252.0f, 104.0f, p->lt);
    trigger(dc, 516.0f, 82.0f, 618.0f, 104.0f, p->rt);

    int lclick = (p->buttons & PAD_LS_CLICK) != 0, rclick = (p->buttons & PAD_RS_CLICK) != 0;
    int back = (p->buttons & PAD_BACK) != 0, start = (p->buttons & PAD_START) != 0;
    if (style == PADSTYLE_PLAYSTATION) {
        dpad(dc, 222.0f, 196.0f, 30.0f, p->buttons);
        stick(dc, 306.0f, 282.0f, 34.0f, p->lx, p->ly, lclick);
        stick(dc, 462.0f, 282.0f, 34.0f, p->rx, p->ry, rclick);
        face(dc, 546.0f, 196.0f, 36.0f, 17.0f, p->buttons, style);
        // the touchpad, with a dot where a finger is
        float tl = 318.0f, tt = 150.0f, tr = 450.0f, tb = 222.0f;
        dc_stroke_rrect(dc, tl, tt, tr, tb, 14.0f, 2.5f, p->touching ? COL_LIT : COL_DIM);
        if (p->touching) {
            float u = p->touchX < 0 ? 0 : (p->touchX > 1 ? 1 : p->touchX);
            float v = p->touchY < 0 ? 0 : (p->touchY > 1 ? 1 : p->touchY);
            dc_fill_circle(dc, tl + 14.0f + u * (tr - tl - 28.0f), tt + 12.0f + v * (tb - tt - 24.0f), 9.0f, COL_LIT);
        }
        pill(dc, 298.0f, 240.0f, 30.0f, 12.0f, back);
        pill(dc, 470.0f, 240.0f, 30.0f, 12.0f, start);
        round_part(dc, 384.0f, 262.0f, 12.0f, 0);
    } else if (style == PADSTYLE_STEAM) {
        stick(dc, 222.0f, 188.0f, 30.0f, p->lx, p->ly, lclick);
        face(dc, 546.0f, 188.0f, 34.0f, 16.0f, p->buttons, style);
        // the two trackpads: the left one carries the d-pad, a finger shows on the right
        dc_stroke_circle(dc, 262.0f, 286.0f, 46.0f, 2.5f, COL_DIM);
        dpad(dc, 262.0f, 286.0f, 24.0f, p->buttons);
        dc_stroke_circle(dc, 506.0f, 286.0f, 46.0f, 2.5f, p->touching ? COL_LIT : COL_DIM);
        if (p->touching) {
            float u = p->touchX < 0 ? 0 : (p->touchX > 1 ? 1 : p->touchX);
            float v = p->touchY < 0 ? 0 : (p->touchY > 1 ? 1 : p->touchY);
            dc_fill_circle(dc, 506.0f + (u - 0.5f) * 64.0f, 286.0f + (v - 0.5f) * 64.0f, 9.0f, COL_LIT);
        }
        stick(dc, 384.0f, 268.0f, 26.0f, p->rx, p->ry, rclick);
        pill(dc, 334.0f, 176.0f, 34.0f, 12.0f, back);
        pill(dc, 434.0f, 176.0f, 34.0f, 12.0f, start);
    } else {
        // Xbox, Nintendo and generic: left stick over the d-pad, face buttons over the right stick
        stick(dc, 222.0f, 196.0f, 34.0f, p->lx, p->ly, lclick);
        dpad(dc, 306.0f, 282.0f, 30.0f, p->buttons);
        face(dc, 546.0f, 196.0f, 36.0f, 17.0f, p->buttons, style);
        stick(dc, 462.0f, 282.0f, 34.0f, p->rx, p->ry, rclick);
        pill(dc, 336.0f, 196.0f, 34.0f, 12.0f, back);
        pill(dc, 432.0f, 196.0f, 34.0f, 12.0f, start);
        if (style != PADSTYLE_GENERIC) round_part(dc, 384.0f, 158.0f, 14.0f, 0);
    }

    // touchpad word, for the pads that have one
    if (style == PADSTYLE_PLAYSTATION || style == PADSTYLE_STEAM) {
        dc_text_fit_left(dc, UI_WEIGHT_REGULAR, 18.0f, 36.0f, 418.0f, 200.0f, p->touching ? COL_LIT : COL_FAINT,
                         ui_str(UIS_PADCARD_TOUCHPAD));
    }
    if (!anything_lit(p)) {
        dc_text_fit_center(dc, UI_WEIGHT_REGULAR, 20.0f, dc->nw * 0.5f, 412.0f, dc->nw - 80.0f, COL_FAINT,
                           ui_str(UIS_PADCARD_HINT));
    }
}
