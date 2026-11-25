// The glow's shape round a flat or a curved picture: how far it reaches, the
// zones its colours come from, and the part of it the picture covers. No GL and
// no context, so the host tests reach all of it.

#ifndef XR_GLOW_H
#define XR_GLOW_H

// The frame is boiled down to this many texels a side, and the glow image
// drawn from it is this big
#define AMBI_SAMPLE_TEX 32
#define GLOW_TEX 256

// The widest the glow's cylinder goes, in radians, short of the full turn a
// cylinder layer may not reach
#define GLOW_MAX_ANGLE 6.0f

// The glow around a curved picture. A flat quad behind a cylinder only shows
// above and below it, since the cylinder's sides come round toward the viewer
// and cover the quad's. So the glow is a cylinder too, about the picture's own
// axis and level with it, the glow's size times its angle and its height, which
// keeps the picture on the middle of the glow image the way the flat quad does.
typedef struct {
    float radius;
    float centralAngle;
    // Arc length over height, the way the cylinder layer takes it
    float aspectRatio;
    // The columns of the glow image it shows. All of them, unless the
    // picture wraps so far round that the glow would close the circle: then
    // the middle ones, so the picture still lands where it should and the
    // fade is cut short behind the viewer.
    int rectX;
    int rectWidth;
} GlowCylinder;

// The glow's size in screen widths and heights, scaleX and scaleY. 0, and nothing
// written, when there is no cylinder to be had
int glowCylinderFor(float screenWidth, float screenHeight, float screenRadius, float scaleX, float scaleY,
                    GlowCylinder* out);

// Native (lane gpu-144): the hole the glow drawn into the picture's layer can leave out, since
// the picture covers it: half extents about the glow surface's middle in its own uv (0..1 across
// the drawn surface), *holeU and *holeV, GLOW_HOLE_FILTER_FRAC inside the picture's edge for its
// filter's reach. The picture: picW x picH metres, radius 0 flat, else curved about an axis at
// (0, 0, radius) in its own space, the glow level with it. glowSpanU: the drawn glow's central angle
// (curved) or width in metres (flat); glowH its height. 0, and nothing written, when there is none.
#define GLOW_HOLE_FILTER_FRAC 0.005f
int glowHoleFor(float picW, float picH, float radius, float glowSpanU, float glowH, float* holeU, float* holeV);

// Native (issue #7): the glow is a Backlight, after FramePlayerX's glow
// (https://github.com/remotesojourner/frameplayerx, crates/fp-render/src/glow.rs, Copyright 2026 remotesojourner,
// Apache-2.0, a modified version of FramePlayer by yellkell): its 64 zones, its ease and its most spread. Changed: how
// many zones go across follows the picture's shape (20 for 16:9 as there), and the rest of the shape is this client's.
#define GLOW_ZONES 64
// The colours move about two thirds of the way to the new ones in this long
#define BACKLIGHT_EASE_SECS 0.15f
// The colours are taken no more often than this
#define BACKLIGHT_EVERY_NS 13000000LL
// At full Glow spread the light fades out over this share of the screen's longer side
#define BACKLIGHT_MAX_REACH 0.55f
// In a room, where it goes up over the room's own walls, ceiling and furniture: it reaches at most this share of the
// shorter side, about as far as the room leaves round the picture (its wall ends above and the
// Home Theater's cabinet begins below a sixth of the picture's height away), so it is a halo and no light in the air
#define BACKLIGHT_ROOM_REACH 0.2f

// The glow image's share of its own width and height past the light's reach: its outermost texels are dark, so a
// short reach has no hard rim where the image ends
#define BACKLIGHT_PAD (3.0f / GLOW_TEX)

typedef struct {
    float scaleX, scaleY;  // the glow's size in screen widths and heights
    float aspect;          // the screen's height over its width
    float reach;           // in screen widths
    int zonesAcross;
} BacklightShape;
// The shape for a picture picW x picH (any units), Glow spread in percent (GLOW_SPREAD_), and whether a room is up
void backlightShapeFor(float picW, float picH, int spread, int inRoom, BacklightShape* out);
int backlightShapeSame(const BacklightShape* a, const BacklightShape* b);
// Zones along the top (and as many along the bottom) of a picture picW x picH, 4 to 28
int glowZonesAcross(float picW, float picH);
// The share of the new colours a draw dt seconds after the last one takes
float backlightEase(float dt);

#endif
