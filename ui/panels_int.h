// Shared drawing context and helpers for the panel rasterisers (internal).
// Everything here takes native texture pixels, the units upstream's Java
// draws in, and scales them to the canvas, so the panel code reads like
// XrPanels.java. GPLv3.
#ifndef UI_PANELS_INT_H
#define UI_PANELS_INT_H

#include "panels.h"

typedef struct {
    UiCanvas* cv;
    UiFont* font;
    UiHits* hits;
    const UiView* v;
    float nw, nh;      // native size of the panel
    float kx, ky;      // canvas / native
    float k;           // size scale (kx)
    // native (lane wave6-i18n): the settings panel's row label waiting to be drawn once its row
    // knows how much room it has (panel_cog.c label)
    const char* label_text;
    float label_y;
    int label_live;
} Dc;

void dc_init(Dc* dc, UiCanvas* cv, UiHits* hits, const UiView* v, int native_w, int native_h);

// Hit region from native pixel coordinates
void dc_hit(Dc* dc, int kind, int a, int b, float x0, float y0, float x1, float y1, int enabled);
// Hit region from panel UV
void dc_hit_uv(Dc* dc, int kind, int a, int b, float u0, float v0, float u1, float v1, int enabled);
int dc_hover(const Dc* dc, int kind, int a, int b);   // is this the hovered hit

// Shapes in native pixels
void dc_fill_rrect(Dc* dc, float x0, float y0, float x1, float y1, float r, uint32_t c);
void dc_stroke_rrect(Dc* dc, float x0, float y0, float x1, float y1, float r, float w, uint32_t c);
void dc_fill_circle(Dc* dc, float cx, float cy, float r, uint32_t c);
void dc_stroke_circle(Dc* dc, float cx, float cy, float r, float w, uint32_t c);
void dc_line(Dc* dc, float x0, float y0, float x1, float y1, float w, uint32_t c);
void dc_polyline(Dc* dc, const float* xy, int n, float w, uint32_t c, int closed);
void dc_fill_poly(Dc* dc, const float* xy, int n, uint32_t c);
void dc_arc(Dc* dc, float cx, float cy, float r, float start, float sweep, float w, uint32_t c, int round_caps);

// Text in native pixels. baseline form, and centred on a point.
float dc_text(Dc* dc, UiWeight w, float size, float x, float baseline, UiAlign a, uint32_t c, const char* s);
void dc_text_center(Dc* dc, UiWeight w, float size, float cx, float cy, uint32_t c, const char* s);
// Left aligned, vertically centred on cy
void dc_text_left_mid(Dc* dc, UiWeight w, float size, float x, float cy, uint32_t c, const char* s);
float dc_text_width(Dc* dc, UiWeight w, float size, const char* s);   // native px
// Ellipsized to max_w native px, drawn centred / left; returns nothing
void dc_text_fit_center(Dc* dc, UiWeight w, float size, float cx, float cy, float max_w, uint32_t c,
                        const char* s);
void dc_text_fit_left(Dc* dc, UiWeight w, float size, float x, float baseline, float max_w,
                      uint32_t c, const char* s);

// The dark sheet every panel sits on: rounded rect inset one pixel, r 32
void dc_sheet(Dc* dc, float radius, uint32_t c);

// Rounded outline button (stroke), optionally with hover wash; label centred
void dc_button(Dc* dc, float x0, float y0, float x1, float y1, float r, float stroke, uint32_t ink,
               const char* label, float label_size, int hot);

// Images (decoded once, cached): NULL if the asset is missing
typedef struct { uint8_t* rgba; int w, h; } UiImage;
const UiImage* ui_asset_image(const char* relpath);

// Button faces (icons.c): one BUTTON_TEX square face drawn with its origin at
// (ox, oy) native pixels of the dc. cell is a BTN_CELL_ value (+1 for a
// switch's on face).
void dc_button_face(Dc* dc, int cell, float ox, float oy, float size_px_native);

const char* ui_basename_short(const char* path, char* out, size_t cap);

// Panels (one file each)
void panel_cog(Dc* dc);
void panel_picker(Dc* dc);
void panel_keyboard(Dc* dc);
void panel_bar(Dc* dc);
void panel_button_atlas(Dc* dc);
void panel_exit(Dc* dc);
void panel_hint(Dc* dc);
void panel_kofi(Dc* dc);
void panel_host(Dc* dc);

#endif
