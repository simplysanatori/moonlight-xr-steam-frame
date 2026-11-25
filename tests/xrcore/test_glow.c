// The glow's shape: its cylinder round a curved picture, the hole under the
// picture, and the Backlight's reach, zones and ease
#include <stdlib.h>

#include "check.h"
#include "xr_glow.h"
#include "xr_math.h"
#include "xr_shared.h"

// A glow this many times the screen's width and height
#define SIZE 1.7f

// The glow round a curved picture. The viewer is at the origin looking down
// -z with the picture's centre straight ahead at distance d, which is where
// the sliders put it, and the radius the curvature slider gives: d at full
// curve, four times d at none.
#define PI_F 3.14159265f
#define DEG (180.0f / PI_F)

static float radiusFor(float d, float curve) {
    return d * (1.0f + 3.0f * (1.0f - curve));
}

// How far round to the right a point a radians round a cylinder of radius r
// is, seen from the viewer, when the cylinder's axis is axisZ along z
static float azimuthOnCylinder(float a, float r, float axisZ) {
    return atan2f(r * sinf(a), r * cosf(a) - axisZ) * DEG;
}

// How high a point h above the middle is, seen from the viewer, at a radians
// round the same cylinder
static float elevationOnCylinder(float a, float r, float axisZ, float h) {
    float x = r * sinf(a);
    float z = axisZ - r * cosf(a);
    return atan2f(h, sqrtf(x * x + z * z)) * DEG;
}

// Halfway out through the fade, as a fraction of the glow image from its
// middle: the screen ends at 0.5 / SIZE and the rim is at 0.5
#define HALF_FADE (0.5f / SIZE + 0.5f * (0.5f - 0.5f / SIZE))

static void testFlatGlowHidesBehindACloseCurve(void) {
    // Issue 9's case: a 3 m picture 1 m away at full curve. The flat glow sits
    // in a plane at the picture's centre, the cylinder's sides come round
    // past where the quad ends, so nothing of it shows at the sides.
    float d = 1.0f, w = 3.0f;
    float r = radiusFor(d, 1.0f);
    float edge = azimuthOnCylinder(0.5f * w / r, r, r - d);
    float flatRim = atan2f(0.5f * SIZE * w, d) * DEG;
    CHECK_NEAR(edge, 85.9, 0.1);
    CHECK(flatRim < edge);

    // The cylinder glow reaches well past it, halfway through its fade
    GlowCylinder g;
    CHECK(glowCylinderFor(w, w * 9.0f / 16.0f, r, SIZE, SIZE, &g));
    float half = HALF_FADE * g.centralAngle * GLOW_TEX / g.rectWidth;
    CHECK(azimuthOnCylinder(half, g.radius, r - d) > edge + 20.0f);

    // Halfway in, at a gentler curve, the flat glow's fade is still covered
    r = radiusFor(d, 0.5f);
    edge = azimuthOnCylinder(0.5f * w / r, r, r - d);
    float flatHalf = atan2f(HALF_FADE * SIZE * w, d) * DEG;
    CHECK(flatHalf < edge);
}

static void testCylinderFollowsTheScreen(void) {
    // Every distance the panel's track reaches, the widths a resize allows
    // and curves from barely to fully, 16:9
    const float distances[] = { 0.2f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f };
    const float widths[] = { 0.8f, 1.5f, 3.0f, 5.0f, 8.0f };
    const float curves[] = { 0.02f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f };
    int whole = 0, cut = 0, bad = 0, sides = 0, ends = 0;
    for (int i = 0; i < (int)(sizeof(distances) / sizeof(distances[0])); i++) {
        for (int j = 0; j < (int)(sizeof(widths) / sizeof(widths[0])); j++) {
            for (int k = 0; k < (int)(sizeof(curves) / sizeof(curves[0])); k++) {
                float d = distances[i], w = widths[j];
                float h = w * 9.0f / 16.0f;
                float r = radiusFor(d, curves[k]);
                float screenAngle = w / r;
                GlowCylinder g;
                if (!glowCylinderFor(w, h, r, SIZE, SIZE, &g)) {
                    bad++;
                    continue;
                }
                // Level with the picture, and as tall as the flat glow
                if (g.radius != r) bad++;
                float height = g.radius * g.centralAngle / g.aspectRatio;
                if (fabsf(height - SIZE * h) > 1e-4f * h) bad++;
                // The picture covers the middle 1 / SIZE of the image's
                // full width, whichever columns are shown
                float perColumn = g.centralAngle / g.rectWidth;
                if (fabsf(perColumn * GLOW_TEX / SIZE - screenAngle) > 1e-4f * screenAngle) {
                    bad++;
                }
                if (g.rectWidth % 2 != 0 || g.rectX * 2 + g.rectWidth != GLOW_TEX) bad++;
                if (g.centralAngle > GLOW_MAX_ANGLE || g.centralAngle >= 2.0f * PI_F) bad++;
                if (g.rectWidth == GLOW_TEX) {
                    whole++;
                    if (fabsf(g.centralAngle - SIZE * screenAngle) > 1e-5f) bad++;
                }
                else {
                    cut++;
                    // Only cut where the whole would close the circle
                    if (SIZE * screenAngle <= GLOW_MAX_ANGLE) bad++;
                }
                if (screenAngle >= GLOW_MAX_ANGLE) {
                    continue;
                }
                // Past the picture's side edge as the viewer sees it, halfway
                // through the fade or, where it is cut, at the cut
                float axisZ = r - d;
                float edge = azimuthOnCylinder(0.5f * screenAngle, r, axisZ);
                float fullAngle = g.centralAngle * GLOW_TEX / g.rectWidth;
                float out = fminf(HALF_FADE * fullAngle, 0.5f * g.centralAngle);
                if (azimuthOnCylinder(out, g.radius, axisZ) <= edge) {
                    sides++;
                }
                // And above the picture's top edge at that edge's column,
                // halfway through the vertical fade
                float top = elevationOnCylinder(0.5f * screenAngle, r, axisZ, 0.5f * h);
                float glowTop = elevationOnCylinder(0.5f * screenAngle, g.radius, axisZ,
                                                    HALF_FADE * SIZE * h);
                if (glowTop <= top) {
                    ends++;
                }
            }
        }
    }
    CHECK(bad == 0);
    CHECK(sides == 0);
    CHECK(ends == 0);
    // Both kinds turn up in that sweep
    CHECK(whole > 100);
    CHECK(cut > 10);
}

static void testCylinderEdgeCases(void) {
    GlowCylinder g;
    g.rectWidth = -7;
    CHECK(!glowCylinderFor(3.0f, 1.7f, 0.0f, SIZE, SIZE, &g));
    CHECK(!glowCylinderFor(0.0f, 1.7f, 3.0f, SIZE, SIZE, &g));
    CHECK(!glowCylinderFor(3.0f, 0.0f, 3.0f, SIZE, SIZE, &g));
    CHECK(!glowCylinderFor(3.0f, 1.7f, 3.0f, 0.0f, SIZE, &g));
    CHECK(!glowCylinderFor(3.0f, 1.7f, 3.0f, SIZE, 0.0f, &g));
    CHECK(g.rectWidth == -7);
    // A picture already wrapped all the way round leaves a sliver, never
    // nothing and never more than a cylinder can take
    CHECK(glowCylinderFor(8.0f, 4.5f, 0.2f, SIZE, SIZE, &g));
    CHECK(g.rectWidth >= 2);
    CHECK(g.centralAngle <= GLOW_MAX_ANGLE);
    // The nearest screen at full curve, 3 m wide, wraps 15 rad and is drawn
    // held under a full turn and smaller: the glow goes round that picture
    // and still stays short of a turn, its middle columns only
    float held = 0.2f * CYLINDER_MAX_ANGLE;
    CHECK(glowCylinderFor(held, held * 9.0f / 16.0f, 0.2f, SIZE, SIZE, &g));
    CHECK(g.centralAngle <= GLOW_MAX_ANGLE && g.centralAngle < 2.0f * PI_F);
    CHECK(g.rectWidth < GLOW_TEX && g.rectWidth >= 2);
    // So does the size it was asked for
    CHECK(glowCylinderFor(3.0f, 3.0f * 9.0f / 16.0f, 0.2f, SIZE, SIZE, &g));
    CHECK(g.centralAngle <= GLOW_MAX_ANGLE && g.centralAngle < 2.0f * PI_F);
    CHECK(GLOW_MAX_ANGLE < CYLINDER_MAX_ANGLE);
    // The default screen at full curve, 3 m off: 1 rad of picture, 1.7 of glow
    CHECK(glowCylinderFor(3.0f, 3.0f * 9.0f / 16.0f, 3.0f, SIZE, SIZE, &g));
    CHECK_NEAR(g.centralAngle, 1.7, 1e-5);
    CHECK_NEAR(g.radius, 3.0, 1e-6);
    CHECK(g.rectX == 0 && g.rectWidth == GLOW_TEX);
}

// Native (lane gpu-144): the hole under the picture is the picture's middle, short of its edge by its filter's reach
static void testGlowHoleStaysUnderThePicture(void) {
    const float W = 2.6f, H = 2.6f * 1440.0f / 3440.0f, RAD = 3.0f;
    BacklightShape s;
    GlowCylinder g;
    backlightShapeFor(3440.0f, 1440.0f, GLOW_SPREAD_MAX, 0, &s);
    CHECK(glowCylinderFor(W, H, RAD, s.scaleX, s.scaleY, &g));
    float glowH = g.radius * g.centralAngle / g.aspectRatio, hu = 0.0f, hv = 0.0f;
    CHECK(glowHoleFor(W, H, RAD, g.centralAngle, glowH, &hu, &hv));
    CHECK_NEAR(hu * g.centralAngle, W / RAD * (0.5f - GLOW_HOLE_FILTER_FRAC), 1e-5);
    CHECK_NEAR(hv * glowH, H * (0.5f - GLOW_HOLE_FILTER_FRAC), 1e-5);
    // Flat, in the glow's own uv
    CHECK(glowHoleFor(2.0f, 1.0f, 0.0f, 2.0f * SIZE, 1.0f * SIZE, &hu, &hv));
    CHECK_NEAR(hu, (0.5f - GLOW_HOLE_FILTER_FRAC) / SIZE, 1e-5);
    CHECK_NEAR(hv, (0.5f - GLOW_HOLE_FILTER_FRAC) / SIZE, 1e-5);
    // Never more than the glow drawn (a curve cut to a full turn), and none from nothing
    CHECK(glowHoleFor(2.0f, 1.0f, 0.0f, 1.0f, 0.5f, &hu, &hv) && hu == 0.5f && hv == 0.5f);
    hu = -7.0f;
    CHECK(!glowHoleFor(0.0f, 1.0f, 0.0f, 1.0f, 1.0f, &hu, &hv) && !glowHoleFor(1.0f, 1.0f, 0.0f, 0.0f, 1.0f, &hu, &hv));
    CHECK(hu == -7.0f);
}

// Native (issue #7): Backlight's shape, its zones and its ease
static void testBacklightShape(void) {
    const float pad = 1.0f - BACKLIGHT_PAD;
    BacklightShape s, t;
    // 16:9 at the default: FramePlayerX's 20 zones across, half of its most spread
    backlightShapeFor(1920.0f, 1080.0f, GLOW_SPREAD_DEFAULT, 0, &s);
    CHECK(s.zonesAcross == 20);
    CHECK_NEAR(s.reach, 0.275, 1e-6);
    CHECK_NEAR(s.scaleX, 1.55 / pad, 1e-5);
    CHECK_NEAR(s.scaleY, (1.0 + 2.0 * 0.275 * 16.0 / 9.0) / pad, 1e-5);
    // The rows' ends are held, and the light is the screen's, not the stream's, size
    backlightShapeFor(3840.0f, 2160.0f, 400, 0, &t);
    CHECK_NEAR(t.reach, BACKLIGHT_MAX_REACH, 1e-6);
    CHECK_NEAR(t.scaleX, (1.0 + 2.0 * 0.55) / pad, 1e-5);
    backlightShapeFor(16.0f, 9.0f, -5, 0, &t);
    CHECK_NEAR(t.reach, 0.055, 1e-6);
    // A tall picture: of its longer side
    backlightShapeFor(1080.0f, 1920.0f, 100, 0, &t);
    CHECK_NEAR(t.reach, 0.55 * 16.0 / 9.0, 1e-5);
    CHECK(t.zonesAcross == 11);
    // In a room not far past the picture
    backlightShapeFor(1920.0f, 1080.0f, 100, 1, &t);
    CHECK_NEAR(t.reach, 0.2 * 9.0 / 16.0, 1e-5);
    CHECK_NEAR(t.scaleY, 1.4 / pad, 1e-5);
    // and Glow spread changes it there too, step by step
    float last = 0.0f;
    for (int s = GLOW_SPREAD_MIN; s <= GLOW_SPREAD_MAX; s += 5) {
        BacklightShape r;
        backlightShapeFor(1920.0f, 1080.0f, s, 1, &r);
        CHECK(r.reach > last + 1e-3f && r.reach <= 0.2f * 9.0f / 16.0f + 1e-5f);
        last = r.reach;
    }
    backlightShapeFor(3840.0f, 1080.0f, 100, 1, &t);
    CHECK_NEAR(t.reach, 0.2 * 1080.0 / 3840.0, 1e-5);
    CHECK(t.scaleX <= t.scaleY);
    // Its outermost texels' middles are past the reach, with something to spare
    backlightShapeFor(1920.0f, 1080.0f, GLOW_SPREAD_MIN, 0, &t);
    CHECK(0.5f * t.scaleX * (1.0f - 1.0f / GLOW_TEX) > 0.5f + t.reach);
    CHECK(0.5f * t.scaleY * t.aspect * (1.0f - 1.0f / GLOW_TEX) > 0.5f * t.aspect + t.reach);
    // Same and not
    backlightShapeFor(1920.0f, 1080.0f, GLOW_SPREAD_DEFAULT, 0, &t);
    CHECK(backlightShapeSame(&s, &t));
    backlightShapeFor(1920.0f, 1080.0f, GLOW_SPREAD_DEFAULT, 1, &t);
    CHECK(!backlightShapeSame(&s, &t));
    backlightShapeFor(1920.0f, 1080.0f, GLOW_SPREAD_DEFAULT + 5, 0, &t);
    CHECK(!backlightShapeSame(&s, &t));
    // The zones: more across a wide picture, never none along a side
    CHECK(glowZonesAcross(3440.0f, 1440.0f) == 22 || glowZonesAcross(3440.0f, 1440.0f) == 23);
    CHECK(glowZonesAcross(5120.0f, 1440.0f) == 25);
    CHECK(glowZonesAcross(100.0f, 1.0f) == 28 && glowZonesAcross(1.0f, 100.0f) == 4 && glowZonesAcross(0.0f, 9.0f) == 20);
    // The ease is the same over a time however it is cut
    CHECK(backlightEase(0.0f) == 0.0f && backlightEase(-1.0f) == 0.0f);
    float left = 1.0f;
    for (int i = 0; i < 15; i++) left *= 1.0f - backlightEase(0.01f);
    CHECK_NEAR(1.0f - left, backlightEase(0.15f), 1e-5);
    CHECK_NEAR(backlightEase(0.15f), 0.6321, 1e-4);
    // Its cylinder: level with the picture, the width and the height each by its own scale
    GlowCylinder g;
    CHECK(glowCylinderFor(3.0f, 3.0f * 9.0f / 16.0f, 3.0f, s.scaleX, s.scaleY, &g));
    CHECK_NEAR(g.radius, 3.0, 1e-6);
    CHECK_NEAR(g.centralAngle, 1.55 / pad, 1e-5);
    CHECK_NEAR(g.radius * g.centralAngle / g.aspectRatio, s.scaleY * 3.0f * 9.0f / 16.0f, 1e-4);
}

int main(void) {
    testBacklightShape();
    testFlatGlowHidesBehindACloseCurve();
    testCylinderFollowsTheScreen();
    testCylinderEdgeCases();
    testGlowHoleStaysUnderThePicture();
    return checksDone("xr_glow");
}
