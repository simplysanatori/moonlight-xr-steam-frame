// The bar's button faces, drawn as simple vector shapes in the 128 px cells
// upstream uses (XrPanels.java build*Button). GPLv3.
#include <math.h>

#include "panels_int.h"

#define INK_ON 0xEEFFFFFFu
#define INK_OFF 0x80FFFFFFu

typedef struct { Dc* dc; float ox, oy, s; } Face;

static void f_line(Face* f, float x0, float y0, float x1, float y1, float w, uint32_t c) {
    dc_line(f->dc, f->ox + x0 * f->s, f->oy + y0 * f->s, f->ox + x1 * f->s, f->oy + y1 * f->s, w * f->s, c);
}
static void f_rr(Face* f, float x0, float y0, float x1, float y1, float r, uint32_t c) {
    dc_fill_rrect(f->dc, f->ox + x0 * f->s, f->oy + y0 * f->s, f->ox + x1 * f->s, f->oy + y1 * f->s, r * f->s, c);
}
static void f_srr(Face* f, float x0, float y0, float x1, float y1, float r, float w, uint32_t c) {
    dc_stroke_rrect(f->dc, f->ox + x0 * f->s, f->oy + y0 * f->s, f->ox + x1 * f->s, f->oy + y1 * f->s, r * f->s, w * f->s, c);
}
static void f_fc(Face* f, float cx, float cy, float r, uint32_t c) {
    dc_fill_circle(f->dc, f->ox + cx * f->s, f->oy + cy * f->s, r * f->s, c);
}
static void f_sc(Face* f, float cx, float cy, float r, float w, uint32_t c) {
    dc_stroke_circle(f->dc, f->ox + cx * f->s, f->oy + cy * f->s, r * f->s, w * f->s, c);
}

// The frame the switches and the environment button share
static void frame(Face* f, uint32_t ink) { f_srr(f, 14, 14, 114, 114, 22, 6, ink); }

static void strike(Face* f, float x0, float y0, float x1, float y1) {
    f_line(f, x0, y0, x1, y1, 7, INK_ON);
}

static void face_env(Face* f) {
    frame(f, INK_ON);
    f_fc(f, 46, 46, 9, INK_ON);
    float hills[10] = { 26, 100, 54, 58, 73, 84, 84, 70, 102, 100 };
    float t[10];
    for (int i = 0; i < 5; i++) { t[2 * i] = f->ox + hills[2 * i] * f->s; t[2 * i + 1] = f->oy + hills[2 * i + 1] * f->s; }
    dc_fill_poly(f->dc, t, 5, INK_ON);
}

static void face_kb(Face* f) {
    f_srr(f, 12, 28, 116, 100, 14, 6, INK_ON);
    for (int row = 0; row < 2; row++) {
        float y = 42.0f + (float)row * 16.0f;
        for (int key = 0; key < 4; key++) {
            float x = 26.0f + (float)key * 20.0f;
            f_rr(f, x, y, x + 14, y + 12, 3, INK_ON);
        }
    }
    f_rr(f, 44, 74, 84, 86, 3, INK_ON);
}

static void face_exit(Face* f) {
    const float mid = 64.0f, radius = 36.0f;
    dc_arc(f->dc, f->ox + mid * f->s, f->oy + mid * f->s, radius * f->s, -60.0f, 300.0f, 9.0f * f->s, INK_ON, 1);
    f_line(f, mid, mid - radius - 10, mid, mid - 2, 9, INK_ON);
}

static void face_cog(Face* f) {
    const UiImage* img = ui_asset_image("images/settings_icon.png");
    if (img) {
        float x0 = f->ox * f->dc->kx, y0 = f->oy * f->dc->ky;
        float x1 = (f->ox + 128.0f * f->s) * f->dc->kx, y1 = (f->oy + 128.0f * f->s) * f->dc->ky;
        ui_image_cover(f->dc->cv, img->rgba, img->w, img->h, x0, y0, x1, y1, 0.0f);
        return;
    }
    // the fallback gear of the Java side
    const float mid = 64.0f;
    f_sc(f, mid, mid, 34, 10, INK_ON);
    for (int tooth = 0; tooth < 8; tooth++) {
        double a = tooth * 3.14159265358979 / 4.0;
        float dx = (float)cos(a), dy = (float)sin(a);
        f_line(f, mid + dx * 34, mid + dy * 34, mid + dx * 48, mid + dy * 48, 12, INK_ON);
    }
    f_sc(f, mid, mid, 14, 8, INK_ON);
}

static void face_stereo(Face* f, int on) {
    uint32_t ink = on ? INK_ON : INK_OFF;
    frame(f, ink);
    dc_text_center(f->dc, UI_WEIGHT_MEDIUM, 48.0f * f->s, f->ox + 64.0f * f->s, f->oy + 64.0f * f->s, ink, "3D");
    if (!on) strike(f, 30, 98, 98, 30);
}

// Native (PLAN 4.3): the 3D button's on face with a host-rendered pair, the
// pair's layout in place of "3D" (the off face is the struck 3D as ever)
static void face_stereo_host(Face* f, const char* text) {
    frame(f, INK_ON);
    Dc* dc = f->dc;
    // shrunk to sit inside the frame rather than cut
    float size = ui_text_fit_size(dc->font, UI_WEIGHT_MEDIUM, 48.0f * f->s * dc->k, 24.0f * f->s * dc->k, text,
                                  84.0f * f->s * dc->k) / dc->k;
    dc_text_center(dc, UI_WEIGHT_MEDIUM, size, f->ox + 64.0f * f->s, f->oy + 64.0f * f->s, INK_ON, text);
}

static void face_ray(Face* f, int on) {
    uint32_t ink = on ? INK_ON : INK_OFF;
    frame(f, ink);
    f_line(f, 34, 94, 70, 58, 7, ink);
    f_fc(f, 84, 44, 11, ink);
    if (!on) strike(f, 30, 30, 98, 98);
}

static void face_pad(Face* f, int on) {
    uint32_t ink = on ? INK_ON : INK_OFF;
    frame(f, ink);
    f_srr(f, 28, 46, 100, 84, 18, 5, ink);
    f_line(f, 38, 65, 54, 65, 5, ink);
    f_line(f, 46, 57, 46, 73, 5, ink);
    f_fc(f, 76, 69, 4.5f, ink);
    f_fc(f, 86, 60, 4.5f, ink);
    if (!on) strike(f, 30, 30, 98, 98);
}

static void face_aim(Face* f, int on) {
    uint32_t ink = on ? INK_ON : INK_OFF;
    frame(f, ink);
    const float mid = 64.0f;
    f_sc(f, mid, mid, 18, 6, ink);
    f_line(f, mid, mid - 38, mid, mid - 25, 6, ink);
    f_line(f, mid, mid + 25, mid, mid + 38, 6, ink);
    f_line(f, mid - 38, mid, mid - 25, mid, 6, ink);
    f_line(f, mid + 25, mid, mid + 38, mid, 6, ink);
    f_fc(f, mid, mid, 5, ink);
    if (!on) strike(f, 30, 30, 98, 98);
}

// Native (lane wave2-bind-wire): the Host sheet's, a PC screen on its stand
// with two keys on it
static void face_host(Face* f) {
    f_srr(f, 16, 22, 112, 88, 10, 6, INK_ON);
    f_line(f, 64, 88, 64, 102, 6, INK_ON);
    f_line(f, 42, 106, 86, 106, 6, INK_ON);
    f_rr(f, 34, 44, 60, 66, 4, INK_ON);
    f_rr(f, 68, 44, 94, 66, 4, INK_ON);
}

void dc_button_face(Dc* dc, int cell, float ox, float oy, float size) {
    Face f = { dc, ox, oy, size / (float)BUTTON_TEX };
    switch (cell) {
    case BTN_CELL_KB: face_kb(&f); break;
    case BTN_CELL_COG: face_cog(&f); break;
    case BTN_CELL_ENV: face_env(&f); break;
    case BTN_CELL_EXIT: face_exit(&f); break;
    case BTN_CELL_STEREO: face_stereo(&f, 0); break;
    case BTN_CELL_STEREO + 1: face_stereo(&f, 1); break;
    case BTN_CELL_STEREO_SBS: face_stereo_host(&f, "SBS"); break;
    case BTN_CELL_STEREO_TB: face_stereo_host(&f, "TB"); break;
    case BTN_CELL_RAY: face_ray(&f, 0); break;
    case BTN_CELL_RAY + 1: face_ray(&f, 1); break;
    case BTN_CELL_AIM: face_aim(&f, 0); break;
    case BTN_CELL_AIM + 1: face_aim(&f, 1); break;
    case BTN_CELL_PAD: face_pad(&f, 0); break;
    case BTN_CELL_PAD + 1: face_pad(&f, 1); break;
    case BTN_CELL_HOST: face_host(&f); break;
    default: break;
    }
}
