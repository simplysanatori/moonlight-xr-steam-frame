// The ambilight glow's colour arithmetic and its shape around a curved
// picture. No GL and no context, so the host tests reach all of it.
// GLOW_EDGE_FRAGMENT_SRC does the same colour sums on the GPU.
#include <math.h>

#include "xr_glow.h"
#include "xr_shared.h"

static float smooth(float edge0, float edge1, float x) {
    float t = (x - edge0) / (edge1 - edge0);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

float glowLuma(const float rgb[3]) {
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

void glowNormalise(const float in[3], float out[3]) {
    float l = glowLuma(in);
    float s = smooth(GLOW_LUMA_FLOOR, GLOW_LUMA_FULL, l);
    // The luma it lands on, then the gain that takes it there. Every channel
    // moves by the same gain, so the hue is the picture's.
    float lifted = l + (GLOW_LUMA_TARGET - l) * s;
    float gain = lifted / fmaxf(l, 1e-5f);
    for (int i = 0; i < 3; i++) {
        float c = in[i] * gain;
        out[i] = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
    }
}

float glowLit(const float rgb[3]) {
    float m = fmaxf(rgb[0], fmaxf(rgb[1], rgb[2]));
    return smooth(GLOW_LIT_LO, GLOW_LIT_HI, m);
}

float glowReachTexels(void) {
    return fmaxf(GLOW_ROLLOFF_REACH * AMBI_SAMPLE_TEX, 1.0f);
}

int glowInwardSteps(void) {
    return (int)(GLOW_INWARD_DEPTH * AMBI_SAMPLE_TEX - 0.5f);
}

float glowRolloffWeight(int k, float reach) {
    if (reach <= 0.0f) {
        return k == 0 ? 1.0f : 0.0f;
    }
    float t = k / reach;
    float w = 1.0f - t * t;
    return w > 0.0f ? w : 0.0f;
}

void glowRingTexel(int i, int* x, int* y) {
    const int n = AMBI_SAMPLE_TEX;
    i = ((i % GLOW_RING) + GLOW_RING) % GLOW_RING;
    if (i < n - 1) {
        *x = i;
        *y = 0;
    }
    else if (i < 2 * (n - 1)) {
        *x = n - 1;
        *y = i - (n - 1);
    }
    else if (i < 3 * (n - 1)) {
        *x = 3 * (n - 1) - i;
        *y = n - 1;
    }
    else {
        *x = 0;
        *y = GLOW_RING - i;
    }
}

int glowRingIndex(int x, int y) {
    const int n = AMBI_SAMPLE_TEX;
    if (x < 0 || y < 0 || x >= n || y >= n) {
        return -1;
    }
    if (y == 0) {
        return x;
    }
    if (x == n - 1) {
        return (n - 1) + y;
    }
    if (y == n - 1) {
        return 3 * (n - 1) - x;
    }
    if (x == 0) {
        return GLOW_RING - y;
    }
    return -1;
}

int glowCylinderFor(float screenWidth, float screenHeight, float screenRadius,
                    GlowCylinder* out) {
    return glowCylinderForScale(screenWidth, screenHeight, screenRadius, GLOW_SCALE, out);
}

int glowCylinderForScale(float screenWidth, float screenHeight, float screenRadius, float scale,
                         GlowCylinder* out) {
    return glowCylinderForScales(screenWidth, screenHeight, screenRadius, scale, scale, GLOW_PROUD_M, out);
}

int glowCylinderForScales(float screenWidth, float screenHeight, float screenRadius, float scaleX, float scaleY,
                          float proud, GlowCylinder* out) {
    if (screenRadius <= 1e-3f || screenWidth <= 0.0f || screenHeight <= 0.0f || scaleX <= 0.0f || scaleY <= 0.0f) {
        return 0;
    }
    float wanted = scaleX * screenWidth / screenRadius;
    int columns = GLOW_TEX;
    if (wanted > GLOW_MAX_ANGLE) {
        // Even, so the columns left out are the same either side
        columns = (int)(GLOW_TEX * GLOW_MAX_ANGLE / wanted) & ~1;
        if (columns < 2) {
            columns = 2;
        }
    }
    out->rectWidth = columns;
    out->rectX = (GLOW_TEX - columns) / 2;
    // From the whole columns, so a cut glow is exactly as wide as what it shows
    out->centralAngle = wanted * columns / GLOW_TEX;
    out->radius = screenRadius > 2.0f * proud ? screenRadius - proud : screenRadius;
    out->aspectRatio = out->radius * out->centralAngle / (scaleY * screenHeight);
    return 1;
}

// ---- native (issue #7): the Backlight (xr_glow.h) ------------------------------------------

int glowZonesAcross(float picW, float picH) {
    if (picW <= 0.0f || picH <= 0.0f) {
        return 20;
    }
    int across = (int)(0.5f * GLOW_ZONES * picW / (picW + picH) + 0.25f);
    return across < 4 ? 4 : (across > 28 ? 28 : across);
}

float backlightEase(float dt) {
    return dt <= 0.0f ? 0.0f : 1.0f - expf(-dt / BACKLIGHT_EASE_SECS);
}

void backlightShapeFor(float picW, float picH, int spread, int bezel, int inRoom, BacklightShape* out) {
    float aspect = picW > 0.0f && picH > 0.0f ? picH / picW : 9.0f / 16.0f;
    float longSide = aspect > 1.0f ? aspect : 1.0f;
    float shortSide = aspect > 1.0f ? 1.0f : aspect;
    int s = spread < GLOW_SPREAD_MIN ? GLOW_SPREAD_MIN : (spread > GLOW_SPREAD_MAX ? GLOW_SPREAD_MAX : spread);
    int b = bezel < GLOW_BEZEL_MIN ? GLOW_BEZEL_MIN : (bezel > GLOW_BEZEL_MAX ? GLOW_BEZEL_MAX : bezel);
    out->aspect = aspect;
    out->reach = BACKLIGHT_MAX_REACH * (float)s / 100.0f * longSide;
    out->bezel = (float)b / 100.0f * longSide;
    float most = BACKLIGHT_ROOM_REACH * shortSide - out->bezel;
    if (inRoom && out->reach > most) {
        out->reach = most > 0.5f * BACKLIGHT_ROOM_REACH * shortSide ? most : 0.5f * BACKLIGHT_ROOM_REACH * shortSide;
    }
    out->scaleX = (1.0f + 2.0f * (out->reach + out->bezel)) / (1.0f - BACKLIGHT_PAD);
    out->scaleY = (1.0f + 2.0f * (out->reach + out->bezel) / aspect) / (1.0f - BACKLIGHT_PAD);
    out->zonesAcross = glowZonesAcross(1.0f, aspect);
}

int backlightShapeSame(const BacklightShape* a, const BacklightShape* b) {
    return a->scaleX == b->scaleX && a->scaleY == b->scaleY && a->aspect == b->aspect && a->reach == b->reach
            && a->bezel == b->bezel && a->zonesAcross == b->zonesAcross;
}

int glowHoleFor(float picW, float picH, float radius, float fadeFrac, float glowSpanU, float glowH, float proud,
                const float eyes[][3], int eyeCount, float* holeU, float* holeV) {
    int curved = radius > 1e-3f;
    float f = fadeFrac + GLOW_HOLE_FILTER_FRAC;
    if (picW <= 0.0f || picH <= 0.0f || glowSpanU <= 0.0f || glowH <= 0.0f || f >= 0.45f || eyeCount < 1) {
        return 0;
    }
    // The middle's half extents: an angle (curved) or metres (flat) across, metres up
    float halfU = (curved ? picW / radius : picW) * (0.5f - f);
    float halfY = picH * (0.5f - f);
    float mh = 0.0f, mv = 0.0f;
    if (proud > 0.0f) {
        // Round the middle's edge, eight points a side's length apart in quarters
        static const float K[5] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 5; i++) {
            for (int j = 0; j < 5; j++) {
                if (fabsf(K[i]) < 1.0f && fabsf(K[j]) < 1.0f) continue;
                float u = K[i] * halfU, y = K[j] * halfY;
                float p[3], n[3], t[3];
                if (curved) {
                    p[0] = radius * sinf(u); p[1] = y; p[2] = radius * (1.0f - cosf(u));
                    n[0] = -sinf(u); n[1] = 0.0f; n[2] = cosf(u);
                    t[0] = cosf(u); t[1] = 0.0f; t[2] = sinf(u);
                } else {
                    p[0] = u; p[1] = y; p[2] = 0.0f;
                    n[0] = 0.0f; n[1] = 0.0f; n[2] = 1.0f;
                    t[0] = 1.0f; t[1] = 0.0f; t[2] = 0.0f;
                }
                for (int e = 0; e < eyeCount; e++) {
                    float d[3] = { eyes[e][0] - p[0], eyes[e][1] - p[1], eyes[e][2] - p[2] };
                    float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    float dn = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
                    // Behind the surface or within about 78 degrees of grazing it: no hole
                    if (len <= 1e-4f || dn <= 0.2f * len) {
                        return 0;
                    }
                    float oh = proud * fabsf(d[0] * t[0] + d[1] * t[1] + d[2] * t[2]) / dn;
                    float ov = proud * fabsf(d[1]) / dn;
                    if (oh > mh) mh = oh;
                    if (ov > mv) mv = ov;
                }
            }
        }
        mh = mh * 1.25f + 0.01f;
        mv = mv * 1.25f + 0.01f;
    }
    float hu = halfU - (curved ? mh / (radius > proud + 1e-3f ? radius - proud : radius) : mh);
    float hv = halfY - mv;
    if (hu <= 0.0f || hv <= 0.0f) {
        return 0;
    }
    hu /= glowSpanU;
    hv /= glowH;
    *holeU = hu < 0.5f ? hu : 0.5f;
    *holeV = hv < 0.5f ? hv : 0.5f;
    return 1;
}
