// Moonlight XR Native: the GL free, XrCtx free parts of upstream's xr_room.c
// (the room table, where the picture hangs, how the light is baked into the
// vertices, the far plane, the eye size) as pure functions. GPLv3.
//
// Names are upstream's wherever upstream's function took no XrCtx. Where
// upstream's took one (roomEffective, roomScreenPercent, roomGlowOn,
// roomScale, roomDim, worldEyeSize, roomLevelsFromTable) the pure core is
// here under a suffixed name (roomEffectiveStyle, roomScreenPercentOf, ...)
// and the M5 xr_room.c keeps a one line XrCtx wrapper over it. See
// docs/dev/reports/rooms.md for the list of what still needs the XrCtx.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "xr_math.h"
#include "xr_shared.h"

#ifdef __cplusplus
extern "C" {
#endif

// Upstream xr_renderer.h
#define ROOM_STYLE_THEATER 1
#define ROOM_STYLE_GRAND_CINEMA 2
#define ROOM_STYLE_SYNTHWAVE 3
#define ROOM_STYLE_FIRST ROOM_STYLE_THEATER
#define ROOM_STYLE_LAST ROOM_STYLE_SYNTHWAVE
#define ROOM_EYES 2
#define ROOM_MAX_EYE_FULL 2560     // high
#define ROOM_MAX_EYE_STANDARD 1760 // standard
#define ROOM_MAX_EYE 1280          // low, on half the recommendation
#define ROOM_ULTRA_EYE 2800        // ultra, taken rather than capped
// Floats a vertex in the room's buffer: position, colour, spill, uv, spare
#define ROOM_VERTEX_FLOATS 10
// Floats a vertex in the .room file (= ROOM_MESH_VERTEX_FLOATS)
#define ROOM_MODEL_FLOATS 11
#define ROOM_NEAR_M 0.05f
#define ROOM_FAR_MIN_M 60.0f
#define ROOM_FAR_MARGIN 1.5f
#define ROOM_SCALE_MIN 0.25f
#define ROOM_SCALE_MAX 4.0f
#define ROOM_DIM_MIN 0.10f
#define ROOM_DIM_MAX 2.0f
#define ROOM_ANISOTROPY_MAX 4.0f
// uploadRoomGeometry's clear colour, the frame before anything is drawn
#define ROOM_CLEAR_R 0.010f
#define ROOM_CLEAR_G 0.010f
#define ROOM_CLEAR_B 0.012f

// Everything a room's shape and lighting is made of (upstream RoomParams)
typedef struct {
    float halfWidth;
    float floorY;
    // The floor under the picture (floorY in every room that ships)
    float screenFloorY;
    float ceilingY;
    // The wall the picture hangs on, and the one behind the viewer
    float screenZ;
    float backZ;
    // Where the picture hangs, which is what the light is baked from
    Vec3 screenAt;
    // The point in the model's own space that lands on the viewer's origin.
    // The geometry is built as (model - anchor) * scale.
    Vec3 anchor;
    float screenMountY;
    float screenProud;
    // The largest the picture is hung
    float screenWidth;
    float screenHeight;
    // Whether the picture may hang past the walls, the floor and the ceiling
    int open;
    float spillRadius;
    float spillGain;
    float texMix;
    float dim;
    unsigned seed;
} RoomParams;

// One row of the room table, as measured off the model (upstream RoomModel)
typedef struct {
    Vec3 eye;
    float eyeRaise;
    float eyeForward;
    Vec3 screen;
    float screenWidth;
    float screenHeight;
    float screenFraction;
    float floorY;
    float ceilingY;
    float halfWidth;
    float backZ;
    float spillRadius;
    float spillGain;
    float dim;
    unsigned seed;
    float scale;
    int resizable;
    int open;
    int glow;
} RoomModel;

// What the Room tab's rows hold per style (upstream XrCtx roomBrightness,
// roomGlow, roomLightLevel, roomScreen), indexed by style
typedef struct {
    int brightness[ROOM_STYLE_LAST + 1];
    int glow[ROOM_STYLE_LAST + 1];
    int lightLevel[ROOM_STYLE_LAST + 1];
    int screen[ROOM_STYLE_LAST + 1];
} RoomLevels;

// Where the picture is hung in the room's space (applyRoomPlacement's result:
// ctx->screenPose position, ctx->screenWidth)
typedef struct {
    float x, y, z;
    float width, height;
} RoomPlacement;

// The cell numbering <-> style numbering (xr_assets.c)
int roomStyleForCell(int cell);
int roomCellForStyle(int style);

// Whether a style is one of the rooms that ship as a model
int bakedRoomStyle(int style);
// The row a style reads; anything unknown falls back to the first room
const RoomModel* roomModel(int style);

// Which room is in force: the debug override unless it is negative, else the
// picker's style (upstream roomEffective without the ctx)
int roomEffectiveStyle(int roomOverride, int roomStyle);

// Which room a style asks for at a scale (upstream roomParams)
RoomParams roomParams(int style, float scale);

// Whether the picture may be resized in a style's room (upstream)
int roomResizable(int style);

// Every room's own values as its row starts them (roomLevelsFromTable's body)
void roomLevelsDefault(RoomLevels* out);

// roomScreenPercent / roomGlowOn without the ctx: the stored percent / the
// stored per room glow, and the app wide glow for a style that is no room
int roomScreenPercentOf(int stored, int style);
int roomGlowOf(int style, int storedRoomGlow, int ambilightOn);

// nativeSetRoomLevels' clamping, in place
void roomLevelsSet(RoomLevels* l, int style, int brightness, int glow, int light, int screen);

// How large a style is drawn (upstream roomScale): the property's override if
// above 0, else the row's, clamped
float roomScaleFor(float scaleOverride, int style);

// How far the room's pass has to see (upstream roomFarPlane)
float roomFarPlane(const RoomParams* p, float reach);

// The dim as it draws (upstream roomDim): the override clamped, else the
// built style's brightness row, else the fallback
float roomDimFor(float dimOverride, int builtStyle, const int* brightnessByStyle, float fallbackDim);

// u_spillGain for the frame: 0 when unlit, else the room's gain times its
// light level (percent) for a baked style (drawRoomEyes)
float roomSpillGainFor(int lit, int builtStyle, float roomSpillGain, const int* lightLevelByStyle);

// The dither and the light weight (upstream, static there)
float roomDither(unsigned seed, unsigned index);
float roomSpillWeight(const RoomParams* p, Vec3 pos, Vec3 normal);

// Writes one vertex in the layout the room's buffer is in (upstream)
void roomWriteVertex(const RoomParams* p, float* verts, int index, Vec3 pos, const float* rgb, float spill, float u,
                     float v);

// Turns a model's vertices (ROOM_MODEL_FLOATS each) into the room's buffer
// layout (ROOM_VERTEX_FLOATS each). Returns how far the room runs from the
// viewer. (upstream buildModelRoomVertices without the ctx)
float roomBuildVertices(const RoomParams* p, float scale, const float* src, int count, float* verts);

// The pure core of applyRoomPlacement: where the picture hangs and how wide,
// for an aspect (height / width of the picture, 0 for unknown), the room's
// size row (percent as roomScreenPercentOf returns it) and the room scale
RoomPlacement roomPlacement(int style, float scale, float aspect, int screenPercent);

// The size of each eye of the room's image (upstream worldEyeSize without the
// ctx): envResTier is an ENV_RES_ value; recommended and max sizes may be 0
void roomEyeSize(int envResTier, int recommendedW, int recommendedH, int maxW, int maxH, int* outW, int* outH);

// Native: the widest side-by-side projection image SteamVR 2.18 on the Steam
// Frame hands to its compositor as it is. Its client library downsamples any
// scene texture past 1.6 x the panel's 2160 + 2 = 3458 pixels, every frame, in
// a pass of its own inside our process (CDownsampleRenderer, "Setting max
// texture dimensions to 3458x3458 before requiring downsampling" in
// xrclient_moonlight-xr.txt). The room at standard took 2 x 1760 = 3520.
#define ROOM_SCENE_MAX_WIDTH 3456

// Native: the standard tier's eye size brought inside ROOM_SCENE_MAX_WIDTH for
// both eyes side by side, keeping its aspect. The other tiers are the owner's
// explicit choice of more pixels and are left as they are.
void roomEyeFitScene(int envResTier, int* eyeW, int* eyeH);

// The view projection for one eye, the way drawRoomEyes builds it
// (projectionFromFov * viewFromPose, near ROOM_NEAR_M, far farZ)
void roomEyeViewProj(float* out16, XrFovf fov, XrPosef pose, float farZ);

#ifdef __cplusplus
}
#endif
