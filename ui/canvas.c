// See canvas.h. GPLv3.
#include "canvas.h"

#include <math.h>
#include <string.h>

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

void ui_cv_init(UiCanvas* cv, uint8_t* px, int w, int h) {
    cv->px = px;
    cv->w = w;
    cv->h = h;
    cv->stride = w * 4;
    cv->cx0 = 0;
    cv->cy0 = 0;
    cv->cx1 = w;
    cv->cy1 = h;
}

void ui_cv_clear(UiCanvas* cv) {
    for (int y = 0; y < cv->h; y++) {
        memset(cv->px + (size_t)y * cv->stride, 0, (size_t)cv->w * 4);
    }
}

void ui_cv_set_clip(UiCanvas* cv, float x0, float y0, float x1, float y1) {
    cv->cx0 = imax(0, (int)floorf(x0));
    cv->cy0 = imax(0, (int)floorf(y0));
    cv->cx1 = imin(cv->w, (int)ceilf(x1));
    cv->cy1 = imin(cv->h, (int)ceilf(y1));
}

void ui_cv_reset_clip(UiCanvas* cv) {
    cv->cx0 = 0;
    cv->cy0 = 0;
    cv->cx1 = cv->w;
    cv->cy1 = cv->h;
}

uint32_t ui_color_alpha(uint32_t argb, float f) {
    float a = (float)(argb >> 24) * clampf(f, 0.0f, 1.0f);
    return (argb & 0x00FFFFFFu) | ((uint32_t)(a + 0.5f) << 24);
}

uint32_t ui_color_mix(uint32_t a, uint32_t b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float x = (float)((a >> sh) & 0xFF);
        float y = (float)((b >> sh) & 0xFF);
        out |= (uint32_t)(x + (y - x) * t + 0.5f) << sh;
    }
    return out;
}

void ui_cv_over(UiCanvas* cv, int x, int y, uint32_t argb, float cov) {
    if (x < cv->cx0 || x >= cv->cx1 || y < cv->cy0 || y >= cv->cy1 || cov <= 0.0f) {
        return;
    }
    float a = ((float)(argb >> 24) * (1.0f / 255.0f)) * (cov > 1.0f ? 1.0f : cov);
    if (a <= 0.0f) {
        return;
    }
    uint8_t* d = cv->px + (size_t)y * cv->stride + (size_t)x * 4;
    float inv = 1.0f - a;
    float r = (float)((argb >> 16) & 0xFF) * a;
    float g = (float)((argb >> 8) & 0xFF) * a;
    float b = (float)(argb & 0xFF) * a;
    float na = a * 255.0f;
    int ro = (int)(r + (float)d[0] * inv + 0.5f);
    int go = (int)(g + (float)d[1] * inv + 0.5f);
    int bo = (int)(b + (float)d[2] * inv + 0.5f);
    int ao = (int)(na + (float)d[3] * inv + 0.5f);
    d[0] = (uint8_t)imin(ro, 255);
    d[1] = (uint8_t)imin(go, 255);
    d[2] = (uint8_t)imin(bo, 255);
    d[3] = (uint8_t)imin(ao, 255);
}

// Visits every pixel of a box (padded for the AA fringe) inside the clip
#define BOX_LOOP(x0, y0, x1, y1, PADV)                                            \
    int bx0 = imax(cv->cx0, (int)floorf((x0) - (PADV)));                          \
    int by0 = imax(cv->cy0, (int)floorf((y0) - (PADV)));                          \
    int bx1 = imin(cv->cx1, (int)ceilf((x1) + (PADV)));                           \
    int by1 = imin(cv->cy1, (int)ceilf((y1) + (PADV)));                           \
    for (int py = by0; py < by1; py++)                                            \
        for (int px = bx0; px < bx1; px++)

// Signed distance to a rounded rectangle (negative inside)
static float sd_rrect(float px, float py, float x0, float y0, float x1, float y1, float r) {
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    float hx = (x1 - x0) * 0.5f, hy = (y1 - y0) * 0.5f;
    if (r > hx) r = hx;
    if (r > hy) r = hy;
    if (r < 0.0f) r = 0.0f;
    float qx = fabsf(px - cx) - (hx - r);
    float qy = fabsf(py - cy) - (hy - r);
    float ox = qx > 0.0f ? qx : 0.0f, oy = qy > 0.0f ? qy : 0.0f;
    float inside = qx > qy ? qx : qy;
    if (inside > 0.0f) inside = 0.0f;
    return sqrtf(ox * ox + oy * oy) + inside - r;
}

void ui_fill_rect(UiCanvas* cv, float x0, float y0, float x1, float y1, uint32_t argb) {
    ui_fill_rrect(cv, x0, y0, x1, y1, 0.0f, argb);
}

void ui_fill_rrect(UiCanvas* cv, float x0, float y0, float x1, float y1, float r, uint32_t argb) {
    if (x1 <= x0 || y1 <= y0) return;
    BOX_LOOP(x0, y0, x1, y1, 1.0f) {
        float d = sd_rrect((float)px + 0.5f, (float)py + 0.5f, x0, y0, x1, y1, r);
        ui_cv_over(cv, px, py, argb, clampf(0.5f - d, 0.0f, 1.0f));
    }
}

void ui_stroke_rrect(UiCanvas* cv, float x0, float y0, float x1, float y1, float r, float width,
                     uint32_t argb) {
    if (x1 <= x0 || y1 <= y0) return;
    float hw = width * 0.5f;
    BOX_LOOP(x0, y0, x1, y1, hw + 1.0f) {
        float d = sd_rrect((float)px + 0.5f, (float)py + 0.5f, x0, y0, x1, y1, r);
        ui_cv_over(cv, px, py, argb, clampf(0.5f - (fabsf(d) - hw), 0.0f, 1.0f));
    }
}

void ui_fill_circle(UiCanvas* cv, float cx, float cy, float r, uint32_t argb) {
    BOX_LOOP(cx - r, cy - r, cx + r, cy + r, 1.0f) {
        float d = hypotf((float)px + 0.5f - cx, (float)py + 0.5f - cy) - r;
        ui_cv_over(cv, px, py, argb, clampf(0.5f - d, 0.0f, 1.0f));
    }
}

void ui_stroke_circle(UiCanvas* cv, float cx, float cy, float r, float width, uint32_t argb) {
    float hw = width * 0.5f;
    BOX_LOOP(cx - r - hw, cy - r - hw, cx + r + hw, cy + r + hw, 1.0f) {
        float d = fabsf(hypotf((float)px + 0.5f - cx, (float)py + 0.5f - cy) - r) - hw;
        ui_cv_over(cv, px, py, argb, clampf(0.5f - d, 0.0f, 1.0f));
    }
}

static float dist_seg(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float l2 = dx * dx + dy * dy;
    float t = l2 > 0.0f ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0.0f;
    t = clampf(t, 0.0f, 1.0f);
    return hypotf(px - (ax + dx * t), py - (ay + dy * t));
}

void ui_polyline(UiCanvas* cv, const float* xy, int n, float width, uint32_t argb, int closed) {
    if (n < 1) return;
    float hw = width * 0.5f;
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
    for (int i = 1; i < n; i++) {
        minx = fminf(minx, xy[2 * i]);
        maxx = fmaxf(maxx, xy[2 * i]);
        miny = fminf(miny, xy[2 * i + 1]);
        maxy = fmaxf(maxy, xy[2 * i + 1]);
    }
    BOX_LOOP(minx - hw, miny - hw, maxx + hw, maxy + hw, 1.0f) {
        float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
        float best = 1e30f;
        if (n == 1) {
            best = hypotf(fx - xy[0], fy - xy[1]);
        }
        for (int i = 0; i + 1 < n; i++) {
            float d = dist_seg(fx, fy, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3]);
            if (d < best) best = d;
        }
        if (closed && n > 2) {
            float d = dist_seg(fx, fy, xy[2 * (n - 1)], xy[2 * (n - 1) + 1], xy[0], xy[1]);
            if (d < best) best = d;
        }
        ui_cv_over(cv, px, py, argb, clampf(0.5f - (best - hw), 0.0f, 1.0f));
    }
}

void ui_line(UiCanvas* cv, float x0, float y0, float x1, float y1, float width, uint32_t argb) {
    float p[4] = { x0, y0, x1, y1 };
    ui_polyline(cv, p, 2, width, argb, 0);
}

void ui_arc(UiCanvas* cv, float cx, float cy, float r, float start_deg, float sweep_deg,
            float width, uint32_t argb, int round_caps) {
    float hw = width * 0.5f;
    const float rad = 3.14159265358979f / 180.0f;
    float a0 = start_deg * rad;
    float sw = sweep_deg * rad;
    float ex0 = cx + cosf(a0) * r, ey0 = cy + sinf(a0) * r;
    float ex1 = cx + cosf(a0 + sw) * r, ey1 = cy + sinf(a0 + sw) * r;
    BOX_LOOP(cx - r - hw, cy - r - hw, cx + r + hw, cy + r + hw, 1.0f) {
        float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
        float ang = atan2f(fy - cy, fx - cx) - a0;
        // fold into [0, 2pi) for a positive sweep, (-2pi, 0] for negative
        float tau = 6.28318530717959f;
        if (sw >= 0.0f) {
            ang = fmodf(ang, tau);
            if (ang < 0.0f) ang += tau;
        } else {
            ang = fmodf(ang, tau);
            if (ang > 0.0f) ang -= tau;
        }
        int within = fabsf(ang) <= fabsf(sw);
        float d;
        if (within) {
            d = fabsf(hypotf(fx - cx, fy - cy) - r);
        } else if (round_caps) {
            d = fminf(hypotf(fx - ex0, fy - ey0), hypotf(fx - ex1, fy - ey1));
        } else {
            // butt: beyond the ends there is nothing
            continue;
        }
        ui_cv_over(cv, px, py, argb, clampf(0.5f - (d - hw), 0.0f, 1.0f));
    }
}

void ui_fill_poly(UiCanvas* cv, const float* xy, int n, uint32_t argb) {
    if (n < 3) return;
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
    for (int i = 1; i < n; i++) {
        minx = fminf(minx, xy[2 * i]);
        maxx = fmaxf(maxx, xy[2 * i]);
        miny = fminf(miny, xy[2 * i + 1]);
        maxy = fmaxf(maxy, xy[2 * i + 1]);
    }
    enum { SS = 4 };
    BOX_LOOP(minx, miny, maxx, maxy, 1.0f) {
        int hits = 0;
        for (int sy = 0; sy < SS; sy++) {
            float fy = (float)py + ((float)sy + 0.5f) / SS;
            for (int sx = 0; sx < SS; sx++) {
                float fx = (float)px + ((float)sx + 0.5f) / SS;
                int inside = 0;
                for (int i = 0, j = n - 1; i < n; j = i++) {
                    float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
                    if (((yi > fy) != (yj > fy)) && (fx < (xj - xi) * (fy - yi) / (yj - yi) + xi)) {
                        inside = !inside;
                    }
                }
                hits += inside;
            }
        }
        if (hits) ui_cv_over(cv, px, py, argb, (float)hits / (SS * SS));
    }
}

static void sample_bilinear(const uint8_t* rgba, int iw, int ih, float u, float v, float out[4]) {
    // u,v in pixels (centre of texel i is i + 0.5); alpha-weighted
    float x = clampf(u - 0.5f, 0.0f, (float)(iw - 1));
    float y = clampf(v - 0.5f, 0.0f, (float)(ih - 1));
    int x0 = (int)x, y0 = (int)y;
    int x1 = imin(x0 + 1, iw - 1), y1 = imin(y0 + 1, ih - 1);
    float fx = x - (float)x0, fy = y - (float)y0;
    float acc[4] = { 0, 0, 0, 0 };
    const int xs[4] = { x0, x1, x0, x1 };
    const int ys[4] = { y0, y0, y1, y1 };
    const float ws[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
    for (int i = 0; i < 4; i++) {
        const uint8_t* p = rgba + ((size_t)ys[i] * iw + xs[i]) * 4;
        float a = (float)p[3] * (1.0f / 255.0f);
        acc[0] += ws[i] * (float)p[0] * a;
        acc[1] += ws[i] * (float)p[1] * a;
        acc[2] += ws[i] * (float)p[2] * a;
        acc[3] += ws[i] * a;
    }
    if (acc[3] > 0.0f) {
        out[0] = acc[0] / acc[3];
        out[1] = acc[1] / acc[3];
        out[2] = acc[2] / acc[3];
    } else {
        out[0] = out[1] = out[2] = 0.0f;
    }
    out[3] = acc[3];
}

void ui_image_cover(UiCanvas* cv, const uint8_t* rgba, int iw, int ih, float x0, float y0,
                    float x1, float y1, float r) {
    if (!rgba || iw < 1 || ih < 1 || x1 <= x0 || y1 <= y0) return;
    float dw = x1 - x0, dh = y1 - y0;
    float scale = fmaxf(dw / (float)iw, dh / (float)ih);
    float ox = (x0 + x1) * 0.5f - (float)iw * scale * 0.5f;
    float oy = (y0 + y1) * 0.5f - (float)ih * scale * 0.5f;
    BOX_LOOP(x0, y0, x1, y1, 1.0f) {
        float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
        float cov = clampf(0.5f - sd_rrect(fx, fy, x0, y0, x1, y1, r), 0.0f, 1.0f);
        if (cov <= 0.0f) continue;
        float s[4];
        sample_bilinear(rgba, iw, ih, (fx - ox) / scale, (fy - oy) / scale, s);
        uint32_t c = UI_ARGB(0, (int)(s[0] + 0.5f), (int)(s[1] + 0.5f), (int)(s[2] + 0.5f));
        ui_cv_over(cv, px, py, c | ((uint32_t)(s[3] * 255.0f + 0.5f) << 24), cov);
    }
}

void ui_image_nearest(UiCanvas* cv, const uint8_t* rgba, int iw, int ih, float x0, float y0,
                      float x1, float y1) {
    if (!rgba || iw < 1 || ih < 1 || x1 <= x0 || y1 <= y0) return;
    BOX_LOOP(x0, y0, x1, y1, 0.0f) {
        float fx = (float)px + 0.5f, fy = (float)py + 0.5f;
        if (fx < x0 || fx >= x1 || fy < y0 || fy >= y1) continue;
        int ix = imin((int)((fx - x0) / (x1 - x0) * (float)iw), iw - 1);
        int iy = imin((int)((fy - y0) / (y1 - y0) * (float)ih), ih - 1);
        const uint8_t* p = rgba + ((size_t)iy * iw + ix) * 4;
        ui_cv_over(cv, px, py, UI_ARGB(p[3], p[0], p[1], p[2]), 1.0f);
    }
}

void ui_fill_radial(UiCanvas* cv, float cx, float cy, float r, uint32_t c0, uint32_t c1,
                    float stop, uint32_t c2) {
    BOX_LOOP(cx - r, cy - r, cx + r, cy + r, 1.0f) {
        float d = hypotf((float)px + 0.5f - cx, (float)py + 0.5f - cy) / r;
        if (d >= 1.0f) continue;
        uint32_t c = d < stop ? ui_color_mix(c0, c1, d / stop)
                              : ui_color_mix(c1, c2, (d - stop) / (1.0f - stop));
        ui_cv_over(cv, px, py, c, 1.0f);
    }
}
