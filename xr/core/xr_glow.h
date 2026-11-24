// The ambilight glow's colour arithmetic: the steady luma its colours are
// lifted to, how lit a colour counts as, and the roll off along the picture's
// edge where it goes dark. No GL and no context, so the host tests reach all
// of it. The glow edge shader does the same sums on the GPU and takes every
// constant here as a uniform, so the two cannot drift apart on a number.

#ifndef XR_GLOW_H
#define XR_GLOW_H

// The frame is boiled down to this many texels a side, and the glow image
// drawn from it is this big
#define AMBI_SAMPLE_TEX 32
#define GLOW_TEX 256
// How much larger than the screen the glow is on each axis, and how far proud
// of the picture it sits, toward the viewer, in metres. The compositor stacks
// layers in the order they are handed over, so the picture still covers it.
#define GLOW_SCALE 1.7f
// Native (lane borderless): the Wide Glow edge's glow outside a room, wide
// enough to carry the picture's colours across most of the view (Lobs1994's figure)
#define GLOW_SCALE_WIDE 6.0f
#define GLOW_PROUD_M 0.05f

// The glow's colour comes from the picture and its brightness from the glow
// level: each sample texel is scaled to this luma, so a bright frame and a dark
// one glow equally in their own hue
#define GLOW_LUMA_TARGET 0.5f
// The knee under it. Below the floor nothing is lifted, which keeps a black
// frame dark; from the top of the knee up everything lands on the target. A
// knee rather than a step, since the sample is already smoothed over time and
// a texel crossing a step would jump to full in one frame.
#define GLOW_LUMA_FLOOR 0.03f
#define GLOW_LUMA_FULL 0.08f
// How lit a colour counts as, off its brightest channel for the reason the bar
// detection uses it, ramping between these. Anything the luma normalisation
// lifted is fully lit.
#define GLOW_LIT_LO 0.08f
#define GLOW_LIT_HI 0.35f
// Where the picture's edge goes dark the glow rolls off instead of cutting
// out. How far along an edge a lit colour carries into a dark run, as a
// fraction of that edge's length. The weights fall as 1 - t squared, so three
// quarters of the light is left halfway out.
#define GLOW_ROLLOFF_REACH (1.0f / 3.0f)
// How deep into the picture an edge texel looks when its own colour is dark,
// as a fraction of the picture's width or height, so a thin dark border does
// not take the colour with it. Read in whole sample texels centred inside it:
// 0.09 is the two texels behind the edge one, which end at 9.4 percent.
#define GLOW_INWARD_DEPTH 0.09f

// The ring of edge texels, anticlockwise from the bottom left corner, a side
// of AMBI_SAMPLE_TEX - 1 texels each
#define GLOW_RING (4 * (AMBI_SAMPLE_TEX - 1))

// The widest the glow's cylinder goes, in radians, short of the full turn a
// cylinder layer may not reach
#define GLOW_MAX_ANGLE 6.0f

// The glow around a curved picture. A flat quad behind a cylinder only shows
// above and below it, since the cylinder's sides come round toward the viewer
// and cover the quad's. So the glow is a cylinder too, about the picture's own
// axis, GLOW_SCALE times its angle and its height, which keeps the picture on
// the middle of the glow image the way the flat quad does.
typedef struct {
    // A little inside the picture's, so it sits proud of it as the quad does
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

// 0, and nothing written, when there is no cylinder to be had
int glowCylinderFor(float screenWidth, float screenHeight, float screenRadius,
                    GlowCylinder* out);
// Native (lane borderless): the same at a glow size of the caller's choosing
// (GLOW_SCALE or GLOW_SCALE_WIDE); glowCylinderFor is this at GLOW_SCALE
int glowCylinderForScale(float screenWidth, float screenHeight, float screenRadius, float scale,
                         GlowCylinder* out);

// Native (issue #7): its width and height apart, and how far inside the picture's radius it goes
int glowCylinderForScales(float screenWidth, float screenHeight, float screenRadius, float scaleX, float scaleY,
                          float proud, GlowCylinder* out);

// Native (lane gpu-144): the hole the glow drawn into the picture's layer can leave out, since
// the picture's opaque middle covers it: half extents about the glow surface's middle in its own
// uv (0..1 across the drawn surface), *holeU and *holeV. The middle is inside the picture's border
// fade (fadeFrac of its width and height a side) and GLOW_HOLE_FILTER_FRAC more for its filter's
// reach, shrunk by how far beside the picture point behind it each eye sees the glow sitting
// proud metres in front (1.25 times the worst over the edge of the middle, plus 1 cm).
// The picture: picW x picH metres, radius 0 flat, else curved about an axis at (0, 0, radius) in
// its own space (middle at the origin, +z toward the viewer), the glow about the same axis.
// glowSpanU: the drawn glow's central angle (curved) or width in metres (flat); glowH its height.
// eyes: the eyes in the picture's space. 0, and nothing written, when there is no hole to have
// (an eye at a grazing angle, or the margins eat it).
#define GLOW_HOLE_FILTER_FRAC 0.005f
int glowHoleFor(float picW, float picH, float radius, float fadeFrac, float glowSpanU, float glowH, float proud,
                const float eyes[][3], int eyeCount, float* holeU, float* holeV);

// Rec. 709 luma
float glowLuma(const float rgb[3]);

// The colour scaled toward GLOW_LUMA_TARGET through the knee, each channel
// held to 0..1
void glowNormalise(const float in[3], float out[3]);

// 0 for dark, 1 for lit, smooth between
float glowLit(const float rgb[3]);

// GLOW_ROLLOFF_REACH in sample texels, and how many texels in an edge texel
// looks past itself
float glowReachTexels(void);
int glowInwardSteps(void);

// Native (issue #7): the Glow edge's Backlight, after FramePlayerX's glow
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
// In a room, where it goes up over the room's own walls, ceiling and furniture: it and the bezel reach at most this
// share of the shorter side, about as far as the room leaves round the picture (its wall ends above and the
// Home Theater's cabinet begins below a sixth of the picture's height away), so it is a halo and no light in the air
#define BACKLIGHT_ROOM_REACH 0.2f

// The glow image's share of its own width and height past the light's reach: its outermost texels are dark, so a
// short reach has no hard rim where the image ends
#define BACKLIGHT_PAD (3.0f / GLOW_TEX)

typedef struct {
    float scaleX, scaleY;  // the glow's size in screen widths and heights
    float aspect;          // the screen's height over its width
    float reach, bezel;    // in screen widths
    int zonesAcross;
} BacklightShape;
// The shape for a picture picW x picH (any units), Glow spread and Bezel in their percent (GLOW_SPREAD_,
// GLOW_BEZEL_), and whether a room is up
void backlightShapeFor(float picW, float picH, int spread, int bezel, int inRoom, BacklightShape* out);
int backlightShapeSame(const BacklightShape* a, const BacklightShape* b);
// Zones along the top (and as many along the bottom) of a picture picW x picH, 4 to 28
int glowZonesAcross(float picW, float picH);
// The share of the new colours a draw dt seconds after the last one takes
float backlightEase(float dt);

// The weight a colour k texels along the ring carries, 1 at the texel itself
// and 0 at reach texels away or further
float glowRolloffWeight(int k, float reach);

// Ring position i to texel and back. The index is -1 for a texel off the ring.
void glowRingTexel(int i, int* x, int* y);
int glowRingIndex(int x, int y);

#endif
