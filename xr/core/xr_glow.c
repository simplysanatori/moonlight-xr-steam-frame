// The glow's shape round a flat or a curved picture (xr_glow.h). No GL and no
// context, so the host tests reach all of it.
#include <math.h>

#include "xr_glow.h"
#include "xr_shared.h"

int glowCylinderFor(float screenWidth, float screenHeight, float screenRadius, float scaleX, float scaleY,
                    GlowCylinder* out) {
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
    out->radius = screenRadius;
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

void backlightShapeFor(float picW, float picH, int spread, int inRoom, BacklightShape* out) {
    float aspect = picW > 0.0f && picH > 0.0f ? picH / picW : 9.0f / 16.0f;
    float longSide = aspect > 1.0f ? aspect : 1.0f;
    float shortSide = aspect > 1.0f ? 1.0f : aspect;
    int s = spread < GLOW_SPREAD_MIN ? GLOW_SPREAD_MIN : (spread > GLOW_SPREAD_MAX ? GLOW_SPREAD_MAX : spread);
    out->aspect = aspect;
    out->reach = BACKLIGHT_MAX_REACH * (float)s / 100.0f * longSide;
    if (inRoom) {
        // Its own scale, so Glow spread changes it all the way: no point of it goes further than that
        out->reach = BACKLIGHT_ROOM_REACH * shortSide * (0.4f + 0.6f * (float)s / 100.0f);
    }
    out->scaleX = (1.0f + 2.0f * out->reach) / (1.0f - BACKLIGHT_PAD);
    out->scaleY = (1.0f + 2.0f * out->reach / aspect) / (1.0f - BACKLIGHT_PAD);
    out->zonesAcross = glowZonesAcross(1.0f, aspect);
}

int backlightShapeSame(const BacklightShape* a, const BacklightShape* b) {
    return a->scaleX == b->scaleX && a->scaleY == b->scaleY && a->aspect == b->aspect && a->reach == b->reach
            && a->zonesAcross == b->zonesAcross;
}

int glowHoleFor(float picW, float picH, float radius, float glowSpanU, float glowH, float* holeU, float* holeV) {
    if (picW <= 0.0f || picH <= 0.0f || glowSpanU <= 0.0f || glowH <= 0.0f) {
        return 0;
    }
    // Half extents: an angle (curved) or metres (flat) across, metres up
    float hu = (radius > 1e-3f ? picW / radius : picW) * (0.5f - GLOW_HOLE_FILTER_FRAC);
    float hv = picH * (0.5f - GLOW_HOLE_FILTER_FRAC);
    hu /= glowSpanU;
    hv /= glowH;
    *holeU = hu < 0.5f ? hu : 0.5f;
    *holeV = hv < 0.5f ? hv : 0.5f;
    return 1;
}
