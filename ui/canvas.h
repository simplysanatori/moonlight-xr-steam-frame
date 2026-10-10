// A small software canvas: RGBA8, premultiplied alpha, top-left origin, the
// drawing calls the panels need (anti-aliased rounded rectangles, circles,
// strokes, arcs, polygons, images). Colours are 0xAARRGGBB with straight
// alpha, which is how the upstream Java panels write them, and are composited
// source-over in sRGB space as Android's Canvas does. No dependencies beyond
// libm. GPLv3.
#ifndef UI_CANVAS_H
#define UI_CANVAS_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* px;   // w*h*4 bytes, R G B A, premultiplied
    int w, h;
    int stride;    // bytes per row
    int cx0, cy0, cx1, cy1;   // clip rectangle in pixels, [x0,x1) [y0,y1)
} UiCanvas;

#define UI_ARGB(a, r, g, b) ((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define UI_WHITE 0xFFFFFFFFu
#define UI_BLACK 0xFF000000u

// Wraps a caller's buffer of w*h*4 bytes. Does not clear it.
void ui_cv_init(UiCanvas* cv, uint8_t* px, int w, int h);
// A canvas over a w*h rectangle at (x,y) of a larger buffer that is stride_px
// pixels wide (an atlas): drawing is clipped to the rectangle.
void ui_cv_sub(UiCanvas* cv, uint8_t* base, int stride_px, int x, int y, int w, int h);
void ui_cv_clear(UiCanvas* cv);            // all transparent (ignores the clip)
void ui_cv_set_clip(UiCanvas* cv, float x0, float y0, float x1, float y1);
void ui_cv_reset_clip(UiCanvas* cv);

// colour with its alpha scaled by f (0..1)
uint32_t ui_color_alpha(uint32_t argb, float f);
uint32_t ui_color_mix(uint32_t a, uint32_t b, float t);

// One pixel, covered by cov (0..1), in colour: source-over. Clipped.
void ui_cv_over(UiCanvas* cv, int x, int y, uint32_t argb, float cov);

void ui_fill_rect(UiCanvas* cv, float x0, float y0, float x1, float y1, uint32_t argb);
void ui_fill_rrect(UiCanvas* cv, float x0, float y0, float x1, float y1, float r, uint32_t argb);
// Stroke centred on the edge, like Paint.Style.STROKE
void ui_stroke_rrect(UiCanvas* cv, float x0, float y0, float x1, float y1, float r, float width,
                     uint32_t argb);
void ui_fill_circle(UiCanvas* cv, float cx, float cy, float r, uint32_t argb);
void ui_stroke_circle(UiCanvas* cv, float cx, float cy, float r, float width, uint32_t argb);
// Round capped line
void ui_line(UiCanvas* cv, float x0, float y0, float x1, float y1, float width, uint32_t argb);
// Open polyline of n points (x,y pairs) with round caps and joins, one
// composite so overlapping segments do not double up
void ui_polyline(UiCanvas* cv, const float* xy, int n, float width, uint32_t argb, int closed);
// Arc stroke: start angle in degrees clockwise from three o'clock (y down),
// sweep in degrees, round or butt caps
void ui_arc(UiCanvas* cv, float cx, float cy, float r, float start_deg, float sweep_deg,
            float width, uint32_t argb, int round_caps);
// Filled polygon (even-odd not needed: simple polygons), 4x4 supersampled
void ui_fill_poly(UiCanvas* cv, const float* xy, int n, uint32_t argb);
// Straight-alpha RGBA8 image scaled to cover the destination rectangle
// (centred crop), through a rounded-rectangle mask of radius r.
void ui_image_cover(UiCanvas* cv, const uint8_t* rgba, int iw, int ih, float x0, float y0,
                    float x1, float y1, float r);
// Straight-alpha RGBA8 image stretched to the rectangle, unfiltered (nearest),
// for the QR code
void ui_image_nearest(UiCanvas* cv, const uint8_t* rgba, int iw, int ih, float x0, float y0,
                      float x1, float y1);
// Radial gradient disc: colour c0 at the centre fading linearly through
// (stop, c1) to transparent c2 at the rim; used by the splash glow
void ui_fill_radial(UiCanvas* cv, float cx, float cy, float r, uint32_t c0, uint32_t c1,
                    float stop, uint32_t c2);

#endif
