// Shared helpers, hit lists, the view, assets and the panel dispatcher. GPLv3.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "panels_int.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_FD 0
#include "third_party/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// ---- sizes and names -------------------------------------------------------------------

void ui_panel_native_size(UiPanelId id, int* w, int* h) {
    static const int SZ[UI_PANEL_COUNT][2] = {
        { COG_TEX_W, COG_TEX_H },       // cog
        { PICKER_TEX_W, PICKER_TEX_H }, // picker
        { KB_TEX_W, KB_TEX_H },         // keyboard
        { 1792, 128 },                  // bar strip
        { BTN_ATLAS_W, BTN_ATLAS_H },   // button atlas
        { EXIT_TEX_W, EXIT_TEX_H },
        { HINT_TEX_W, HINT_TEX_H },
        { KOFI_TEX_W, KOFI_TEX_H },
        { REPORT_TEX_W, REPORT_TEX_H },
        { TOAST_TEX_W, TOAST_TEX_H },
        { COG_CLOCK_TEX_W, COG_CLOCK_TEX_H },
        { 1024, 640 },                  // pc list
        { 1024, 640 },                  // app list
        { XR_HOST_TEX_W, XR_HOST_TEX_H }, // host sheet
        { FIRSTRUN_TEX_W, FIRSTRUN_TEX_H }, // first-run card
        { PADCARD_TEX_W, PADCARD_TEX_H },   // pad card
    };
    if ((unsigned)id >= UI_PANEL_COUNT) {
        *w = *h = 0;
        return;
    }
    *w = SZ[id][0];
    *h = SZ[id][1];
}

const char* ui_panel_name(UiPanelId id) {
    static const char* const N[UI_PANEL_COUNT] = { "cog", "picker", "keyboard", "bar",
        "button_atlas", "exit", "hint", "kofi", "report", "toast", "clock", "pc_list", "app_list", "host",
        "firstrun", "padcard" };
    return (unsigned)id < UI_PANEL_COUNT ? N[id] : "?";
}

// Shelves, widest first where it matters: A cog | report, B pc list | app list,
// C kofi | hint, D host sheet | exit, clock, E picker, then the toast, the bar
// strip and the button strip, one to a row, and last the keyboard (its media
// row made it taller than the shelf)
// Native (lane wave3-cards): under the keyboard, the first-run card and the pad card side by side.
UiRectPx ui_atlas_rect(UiPanelId id) {
    static const UiRectPx R[UI_PANEL_COUNT] = {
        [UI_PANEL_COG] = { 0, 0, COG_TEX_W, COG_TEX_H },
        [UI_PANEL_REPORT] = { 768, 0, REPORT_TEX_W, REPORT_TEX_H },
        [UI_PANEL_PC_LIST] = { 0, 640, 1024, 640 },
        [UI_PANEL_APP_LIST] = { 1024, 640, 1024, 640 },
        [UI_PANEL_KOFI] = { 0, 1280, KOFI_TEX_W, KOFI_TEX_H },
        [UI_PANEL_HINT] = { 1024, 1280, HINT_TEX_W, HINT_TEX_H },
        [UI_PANEL_HOST] = { 0, 1792, XR_HOST_TEX_W, XR_HOST_TEX_H },
        [UI_PANEL_KEYBOARD] = { 0, 2960, KB_TEX_W, KB_TEX_H },
        [UI_PANEL_EXIT] = { 1120, 1792, EXIT_TEX_W, EXIT_TEX_H },
        [UI_PANEL_CLOCK] = { 1632, 1792, COG_CLOCK_TEX_W, COG_CLOCK_TEX_H },
        [UI_PANEL_PICKER] = { 0, 2240, PICKER_TEX_W, PICKER_TEX_H },
        [UI_PANEL_TOAST] = { 0, 2536, TOAST_TEX_W, TOAST_TEX_H },
        [UI_PANEL_BAR] = { 0, 2696, 1792, 128 },
        [UI_PANEL_BUTTON_ATLAS] = { 0, 2824, BTN_ATLAS_W, BTN_ATLAS_H },
        [UI_PANEL_FIRSTRUN] = { 0, 3472, FIRSTRUN_TEX_W, FIRSTRUN_TEX_H },
        [UI_PANEL_PADCARD] = { 1024, 3472, PADCARD_TEX_W, PADCARD_TEX_H },
    };
    UiRectPx none = { 0, 0, 0, 0 };
    return (unsigned)id < UI_PANEL_COUNT ? R[id] : none;
}

// ---- hits -----------------------------------------------------------------------------------

void ui_hits_clear(UiHits* h) { h->count = 0; }

int ui_hits_at(const UiHits* h, float u, float v) {
    for (int i = h->count - 1; i >= 0; i--) {
        const UiHit* t = &h->hit[i];
        if (u >= t->u0 && u <= t->u1 && v >= t->v0 && v <= t->v1) return i;
    }
    return -1;
}

int ui_hits_find(const UiHits* h, int kind, int a, int b) {
    for (int i = 0; i < h->count; i++) {
        if (h->hit[i].kind == kind && h->hit[i].a == a && (b < 0 || h->hit[i].b == b)) return i;
    }
    return -1;
}

void dc_hit_uv(Dc* dc, int kind, int a, int b, float u0, float v0, float u1, float v1, int enabled) {
    if (!dc->hits || dc->hits->count >= UI_MAX_HITS) return;
    UiHit* h = &dc->hits->hit[dc->hits->count++];
    h->kind = kind;
    h->a = a;
    h->b = b;
    h->u0 = u0 < 0 ? 0 : u0;
    h->v0 = v0 < 0 ? 0 : v0;
    h->u1 = u1 > 1 ? 1 : u1;
    h->v1 = v1 > 1 ? 1 : v1;
    h->enabled = enabled;
}

void dc_hit(Dc* dc, int kind, int a, int b, float x0, float y0, float x1, float y1, int enabled) {
    dc_hit_uv(dc, kind, a, b, x0 / dc->nw, y0 / dc->nh, x1 / dc->nw, y1 / dc->nh, enabled);
}

int dc_hover(const Dc* dc, int kind, int a, int b) {
    const UiHover* h = &dc->v->hover;
    return h->kind == kind && h->a == a && (b < 0 || h->b == b);
}

// ---- drawing context -----------------------------------------------------------------------------

void dc_init(Dc* dc, UiCanvas* cv, UiHits* hits, const UiView* v, int nw, int nh) {
    dc->cv = cv;
    dc->font = ui_font_default();
    dc->hits = hits;
    dc->v = v;
    dc->nw = (float)nw;
    dc->nh = (float)nh;
    dc->kx = (float)cv->w / (float)nw;
    dc->ky = (float)cv->h / (float)nh;
    dc->k = dc->kx;
    if (hits) hits->count = 0;
}

void dc_fill_rrect(Dc* d, float x0, float y0, float x1, float y1, float r, uint32_t c) {
    ui_fill_rrect(d->cv, x0 * d->kx, y0 * d->ky, x1 * d->kx, y1 * d->ky, r * d->k, c);
}
void dc_stroke_rrect(Dc* d, float x0, float y0, float x1, float y1, float r, float w, uint32_t c) {
    ui_stroke_rrect(d->cv, x0 * d->kx, y0 * d->ky, x1 * d->kx, y1 * d->ky, r * d->k, w * d->k, c);
}
void dc_fill_circle(Dc* d, float cx, float cy, float r, uint32_t c) {
    ui_fill_circle(d->cv, cx * d->kx, cy * d->ky, r * d->k, c);
}
void dc_stroke_circle(Dc* d, float cx, float cy, float r, float w, uint32_t c) {
    ui_stroke_circle(d->cv, cx * d->kx, cy * d->ky, r * d->k, w * d->k, c);
}
void dc_line(Dc* d, float x0, float y0, float x1, float y1, float w, uint32_t c) {
    ui_line(d->cv, x0 * d->kx, y0 * d->ky, x1 * d->kx, y1 * d->ky, w * d->k, c);
}
void dc_polyline(Dc* d, const float* xy, int n, float w, uint32_t c, int closed) {
    float tmp[64];
    if (n > 32) n = 32;
    for (int i = 0; i < n; i++) { tmp[2 * i] = xy[2 * i] * d->kx; tmp[2 * i + 1] = xy[2 * i + 1] * d->ky; }
    ui_polyline(d->cv, tmp, n, w * d->k, c, closed);
}
void dc_fill_poly(Dc* d, const float* xy, int n, uint32_t c) {
    float tmp[64];
    if (n > 32) n = 32;
    for (int i = 0; i < n; i++) { tmp[2 * i] = xy[2 * i] * d->kx; tmp[2 * i + 1] = xy[2 * i + 1] * d->ky; }
    ui_fill_poly(d->cv, tmp, n, c);
}
void dc_arc(Dc* d, float cx, float cy, float r, float start, float sweep, float w, uint32_t c, int rc) {
    ui_arc(d->cv, cx * d->kx, cy * d->ky, r * d->k, start, sweep, w * d->k, c, rc);
}

float dc_text(Dc* d, UiWeight w, float size, float x, float baseline, UiAlign a, uint32_t c, const char* s) {
    if (!d->font || !s) return 0;
    return ui_text_draw(d->cv, d->font, w, size * d->k, x * d->kx, baseline * d->ky, a, c, s, -1) / d->k;
}

float dc_text_width(Dc* d, UiWeight w, float size, const char* s) {
    if (!d->font || !s) return 0;
    return ui_text_width(d->font, w, size * d->k, s, -1) / d->k;
}

// A line centred vertically on cy, by cap height so it sits level with its box
static float centred_baseline(Dc* d, UiWeight w, float size, float cy) {
    UiFontMetrics m;
    ui_font_metrics(d->font, w, size * d->k, &m);
    return cy * d->ky + m.cap_height * 0.5f;
}

void dc_text_center(Dc* d, UiWeight w, float size, float cx, float cy, uint32_t c, const char* s) {
    if (!d->font || !s) return;
    ui_text_draw(d->cv, d->font, w, size * d->k, cx * d->kx, centred_baseline(d, w, size, cy),
                 UI_ALIGN_CENTER, c, s, -1);
}

void dc_text_left_mid(Dc* d, UiWeight w, float size, float x, float cy, uint32_t c, const char* s) {
    if (!d->font || !s) return;
    ui_text_draw(d->cv, d->font, w, size * d->k, x * d->kx, centred_baseline(d, w, size, cy),
                 UI_ALIGN_LEFT, c, s, -1);
}

void dc_text_fit_center(Dc* d, UiWeight w, float size, float cx, float cy, float max_w, uint32_t c,
                        const char* s) {
    if (!d->font || !s) return;
    char buf[512];
    ui_text_ellipsize(d->font, w, size * d->k, s, max_w * d->k, UI_ELLIPSIZE_END, buf, sizeof buf, NULL);
    dc_text_center(d, w, size, cx, cy, c, buf);
}

void dc_text_fit_left(Dc* d, UiWeight w, float size, float x, float baseline, float max_w,
                      uint32_t c, const char* s) {
    if (!d->font || !s) return;
    char buf[512];
    ui_text_ellipsize(d->font, w, size * d->k, s, max_w * d->k, UI_ELLIPSIZE_END, buf, sizeof buf, NULL);
    dc_text(d, w, size, x, baseline, UI_ALIGN_LEFT, c, buf);
}

void dc_sheet(Dc* d, float radius, uint32_t c) {
    dc_fill_rrect(d, 1.0f, 1.0f, d->nw - 1.0f, d->nh - 1.0f, radius, c);
}

void dc_button(Dc* d, float x0, float y0, float x1, float y1, float r, float stroke, uint32_t ink,
               const char* label, float label_size, int hot) {
    if (hot) dc_fill_rrect(d, x0, y0, x1, y1, r, (ink & 0x00FFFFFFu) | 0x38000000u);
    dc_stroke_rrect(d, x0, y0, x1, y1, r, hot ? stroke + 2.0f : stroke, ink);
    dc_text_fit_center(d, UI_WEIGHT_REGULAR, label_size, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f,
                       (x1 - x0) - 24.0f, ink, label);
}

// ---- assets ---------------------------------------------------------------------------------------------

static char g_assets_dir[900];
#define IMG_CACHE 8
static struct { char path[96]; UiImage img; int tried; } g_img[IMG_CACHE];

void ui_panels_set_assets_dir(const char* dir) {
    ui_panels_shutdown();
    snprintf(g_assets_dir, sizeof g_assets_dir, "%s", dir ? dir : "");
}

void ui_panels_shutdown(void) {
    for (int i = 0; i < IMG_CACHE; i++) {
        if (g_img[i].img.rgba) stbi_image_free(g_img[i].img.rgba);
        memset(&g_img[i], 0, sizeof g_img[i]);
    }
}

const UiImage* ui_asset_image(const char* rel) {
    int free_slot = -1;
    for (int i = 0; i < IMG_CACHE; i++) {
        if (g_img[i].tried && strcmp(g_img[i].path, rel) == 0) return g_img[i].img.rgba ? &g_img[i].img : NULL;
        if (!g_img[i].tried && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) return NULL;
    const char* dir = g_assets_dir[0] ? g_assets_dir : getenv("MLXR_ASSETS_DIR");
#ifdef MLXR_ASSETS_DIR
    if (!dir || !*dir) dir = MLXR_ASSETS_DIR;
#endif
    g_img[free_slot].tried = 1;
    snprintf(g_img[free_slot].path, sizeof g_img[free_slot].path, "%s", rel);
    if (!dir || !*dir) return NULL;
    char full[1100];
    snprintf(full, sizeof full, "%s/%s", dir, rel);
    int w, h, n;
    unsigned char* px = stbi_load(full, &w, &h, &n, 4);
    if (!px) return NULL;
    g_img[free_slot].img.rgba = px;
    g_img[free_slot].img.w = w;
    g_img[free_slot].img.h = h;
    return &g_img[free_slot].img;
}

const char* ui_basename_short(const char* path, char* out, size_t cap) {
    const char* home = getenv("HOME");
    if (home && *home && strncmp(path, home, strlen(home)) == 0 && path[strlen(home)] == '/') {
        snprintf(out, cap, "~%s", path + strlen(home));
    } else {
        snprintf(out, cap, "%s", path);
    }
    return out;
}

// ---- the view -------------------------------------------------------------------------------------------

void ui_view_init(UiView* v, const Settings* s) {
    memset(v, 0, sizeof(*v));
    v->settings = s;
    session_from_settings(s, &v->session);
    v->cog_tab = COG_TAB_SCREEN;
    v->room_env = -1;
    v->curve_ok = 1;
    // native (PLAN 4.3): a host-rendered pair needs no depth model
    v->stereo_source = s->stereo_source;
    v->stereo_ok = s->depth_source != 2 || s->stereo_source != STEREO_SOURCE_DEPTH;
    v->env_selected = -1;
    v->bar_show_aim = s->head_locked;
    v->bar_show_stereo = v->stereo_ok;
    v->hover.kind = UI_HIT_NONE;
    v->kb_sheet = KB_STATE_LOWER;
    v->kofi_url = "https://ko-fi.com/moonlightxr";
    v->env_live_cells = ENV_CELL_COUNT;
    v->host_confirm = -1;
}

int ui_cog_face(const UiView* v) {
    return v->cog_tab == COG_TAB_SCREEN && v->room_env >= 0 && settings_room_index(v->room_env) >= 0
               ? COG_FACE_ROOM : v->cog_tab;
}

int ui_cog_row_count(int face) {
    if (face == COG_TAB_SCREEN) return COG_SCREEN_ROW_COUNT;
    if (face == COG_TAB_3D) return COG_ROW3D_COUNT;
    if (face == COG_FACE_ROOM) return COG_ROOM_ROW_COUNT;
    if (face == COG_TAB_PICTURE) return PICTURE_TAB_ROWS;
    if (face == COG_TAB_ABOUT) return 0;
    return COG_DISPLAY_SLIDER_ROW + 1;
}

int ui_cog_row_is_track(int face, int row) {
    if (face == COG_TAB_SCREEN) return row < COG_SCREEN_TRACK_ROWS;
    if (face == COG_TAB_DISPLAY) return row == COG_DISPLAY_SLIDER_ROW;
    if (face == COG_FACE_ROOM) return row != COG_ROOM_ROW_GLOW && row != COG_ROOM_ROW_LIGHT;
    if (face == COG_TAB_3D) return row == COG_ROW3D_SEPARATION || row == COG_ROW3D_CONVERGENCE;
    return 1;
}

static int option_cells(int option) {
    static const int N[COG_OPTION_COUNT] = { COG_SHARPEN_CELLS, COG_SUPERSAMPLE_CELLS,
        COG_STATS_CELLS, COG_HEAD_LOCK_CELLS, COG_HEAD_AIM_CELLS, COG_GAMEPAD_CELLS,
        COG_CONTROLLERS_CELLS, COG_RAY_CELLS, COG_POINTER_SLEEP_CELLS, COG_CLICK_STEADY_CELLS, COG_CLICK_SOUND_CELLS,
        COG_AMBI_CELLS, COG_ROOM_LIGHT_CELLS, COG_NOTICES_CELLS };
    return option >= 0 && option < COG_OPTION_COUNT ? N[option] : 0;
}

int ui_cog_row_cells(int face, int row) {
    if (face == COG_TAB_SCREEN) {
        return row == COG_SCREEN_ROW_PRESET ? COG_SCREEN_PRESET_CELLS
             : row == COG_SCREEN_ROW_PER_APP ? COG_SCREEN_PER_APP_CELLS
             : row == COG_SCREEN_ROW_LOCK ? COG_SCREEN_LOCK_CELLS : 0;
    }
    if (face == COG_FACE_ROOM) return COG_ROOM_SWITCH_CELLS;
    if (face == COG_TAB_3D) {
        return row == COG_ROW3D_PRESET ? COG_PRESET_CELLS : row == COG_ROW3D_SOURCE ? COG_SOURCE_CELLS : COG_STEREO_CELLS;
    }
    return option_cells(row);
}

int ui_cog_row_live(const UiView* v, int face, int row) {
    int in_room = v->room_env >= 0 && settings_room_index(v->room_env) >= 0;
    int head_locked = v->settings->head_locked;
    if (face == COG_TAB_SCREEN && row == COG_SLIDER_CURVE) return v->curve_ok;
    if (face == COG_TAB_SCREEN &&
        (row == COG_SLIDER_AIM_SENSITIVITY || row == COG_SLIDER_AIM_DEADZONE)) return head_locked;
    if (face == COG_TAB_DISPLAY && row >= 0 && row < 32 && (v->display_later & (1u << row))) return 0;
    if (face == COG_TAB_DISPLAY && row == COG_OPTION_HEAD_AIM) return head_locked && !in_room;
    if (face == COG_TAB_3D) return cog3dRowLive(v->stereo_ok, v->stereo_source, row);
    if (face == COG_FACE_ROOM && row == COG_ROOM_ROW_SIZE) return settings_room_resizable(v->room_env);
    return 1;
}

void ui_presets_for(const Settings* s, int presets[COG_PRESET_CELLS]) {
    int def = settings_model_default_separation(s);
    if (def < 0) def = 0;
    if (def > COG_SEP_STEPS) def = COG_SEP_STEPS;
    int lo = def - COG_PRESET_STEPS, hi = def + COG_PRESET_STEPS;
    presets[COG_PRESET_COMFORT] = lo < 0 ? 0 : lo;
    presets[COG_PRESET_BALANCED] = def;
    presets[COG_PRESET_STRONG] = hi > COG_SEP_STEPS ? COG_SEP_STEPS : hi;
}

// ---- dispatcher ----------------------------------------------------------------------------------------

void ui_draw_panel(UiPanelId id, UiCanvas* cv, const UiView* v, UiHits* hits) {
    int nw, nh;
    ui_panel_native_size(id, &nw, &nh);
    if (!nw) return;
    Dc dc;
    dc_init(&dc, cv, hits, v, nw, nh);
    if (!dc.font) return;
    switch (id) {
    case UI_PANEL_COG: panel_cog(&dc); break;
    case UI_PANEL_PICKER: panel_picker(&dc); break;
    case UI_PANEL_KEYBOARD: panel_keyboard(&dc); break;
    case UI_PANEL_BAR: panel_bar(&dc); break;
    case UI_PANEL_BUTTON_ATLAS: panel_button_atlas(&dc); break;
    case UI_PANEL_EXIT: panel_exit(&dc); break;
    case UI_PANEL_HINT: panel_hint(&dc); break;
    case UI_PANEL_KOFI: panel_kofi(&dc); break;
    case UI_PANEL_HOST: panel_host(&dc); break;
    case UI_PANEL_FIRSTRUN: panel_firstrun(&dc); break;
    case UI_PANEL_PADCARD: panel_padcard(&dc); break;
    default: break;
    }
}
