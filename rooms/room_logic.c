// Moonlight XR Native: see room_logic.h. Ported from Moonlight XR's
// xr_room.c (xr-renderer), logic unchanged. GPLv3.
#include "room_logic.h"

#include <math.h>
#include <string.h>

#include "xr_layout.h"

// A dither of about one 255th, from the seed and the vertex number. Without it
// the gradients the light lays over a wall are shallow enough over enough
// pixels to band.
float roomDither(unsigned seed, unsigned index) {
    unsigned h = seed + index * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return ((float)(h & 0xffffu) / 65535.0f - 0.5f) * (2.0f / 255.0f);
}

// How much of the picture's light reaches a point on the room. Baked from a
// single point where the screen sits by default: a distance and a facing term
// is as much of a light that size as a dark wall ever shows.
float roomSpillWeight(const RoomParams* p, Vec3 pos, Vec3 normal) {
    Vec3 toScreen = vecSub(p->screenAt, pos);
    float dist = sqrtf(vecDot(toScreen, toScreen));
    if (dist < 1e-4f) {
        return 1.0f;
    }
    Vec3 dir = {toScreen.x / dist, toScreen.y / dist, toScreen.z / dist};
    float facing = vecDot(normal, dir);
    if (facing <= 0.0f) {
        return 0.0f;
    }

    float ratio = dist / p->spillRadius;
    float weight = facing / (1.0f + ratio * ratio);

    // Nothing behind the viewer catches any of it, faded in over the back half
    // of the room so the falloff has no edge in it
    if (pos.z > 0.0f && p->backZ > 0.0f) {
        float behind = 1.0f - pos.z / p->backZ;
        weight *= behind > 0.0f ? behind : 0.0f;
    }
    return weight;
}

// Writes one vertex in the layout the room's buffer is in
void roomWriteVertex(const RoomParams* p, float* verts, int index, Vec3 pos, const float* rgb, float spill, float u,
                     float v) {
    float dither = roomDither(p->seed, (unsigned)index);
    float* out = verts + (size_t)index * ROOM_VERTEX_FLOATS;
    out[0] = pos.x;
    out[1] = pos.y;
    out[2] = pos.z;
    out[3] = rgb[0] + dither;
    out[4] = rgb[1] + dither;
    out[5] = rgb[2] + dither;
    out[6] = spill;
    out[7] = u;
    out[8] = v;
    out[9] = 0.0f;
}

int roomEffectiveStyle(int roomOverride, int roomStyle) {
    return roomOverride >= 0 ? roomOverride : roomStyle;
}

// Every room that ships, indexed by its style less ROOM_STYLE_FIRST. The
// values the Room tab starts from are in xr_shared.h.
static const RoomModel ROOM_MODELS[] = {
    // Home Theater: a small room with the picture flat on the front wall, four
    // fifths of the anchor, 2.88 m across, which sits better from the seat than
    // a picture filling the whole of it. The light is down to half about the
    // depth of the seating, and over a painted atlas less gain than this never
    // reads as light at all. A little over a third of the atlas as it was
    // baked, picked by eye in a headset.
    {
        .eye = {0.0f, 1.15f, 1.12f}, .eyeRaise = 0.35f, .eyeForward = 0.10f,
        .screen = {0.0f, 1.55f, -3.132f}, .screenWidth = 3.6f, .screenHeight = 2.025f,
        .screenFraction = ROOM_THEATER_SCREEN / 100.0f,
        .floorY = -0.14f, .ceilingY = 2.84f, .halfWidth = 2.8f, .backZ = 4.41f,
        .spillRadius = 3.0f, .spillGain = 0.55f, .dim = ROOM_THEATER_BRIGHTNESS / 100.0f,
        .seed = 0xc2b2ae35u, .scale = 1.0f, .resizable = ROOM_THEATER_RESIZABLE, .open = 0,
        .glow = ROOM_THEATER_GLOW,
    },
    // Grand Cinema: a raked auditorium, watched from partway up the rake, with
    // the picture filling the whole of its screen. Built around that picture,
    // so it cannot be resized, and dark enough that the glow spoils it.
    {
        .eye = {0.0f, 4.8f, 6.0f}, .eyeRaise = 0.40f, .eyeForward = 0.0f,
        .screen = {0.0f, 7.4f, -15.42f}, .screenWidth = 22.0f, .screenHeight = 12.375f,
        .screenFraction = ROOM_GRAND_CINEMA_SCREEN / 100.0f,
        .floorY = -0.3f, .ceilingY = 16.3f, .halfWidth = 14.3f, .backZ = 16.3f,
        .spillRadius = 8.0f, .spillGain = 0.55f, .dim = ROOM_GRAND_CINEMA_BRIGHTNESS / 100.0f,
        .seed = 0x85ebca6bu, .scale = 1.0f, .resizable = ROOM_GRAND_CINEMA_RESIZABLE,
        .open = 0, .glow = ROOM_GRAND_CINEMA_GLOW,
    },
    // Synthwave: no room at all, a ground and a sky that run to the horizon,
    // so the walls are put where nothing can reach them and the picture hangs
    // in the open. 14 m across and raised to y 4.5, which keeps its bottom edge
    // half a metre off the ground; the model's own anchor says 10 by 5.625 at
    // y 3.1. The light falls off slowly and little of it comes back.
    {
        .eye = {0.0f, 1.2f, 0.0f}, .eyeRaise = 0.35f, .eyeForward = 0.10f,
        .screen = {0.0f, 4.5f, -14.0f}, .screenWidth = 14.0f, .screenHeight = 7.875f,
        .screenFraction = ROOM_SYNTHWAVE_SCREEN / 100.0f,
        .floorY = 0.0f, .ceilingY = 1800.0f, .halfWidth = 1800.0f, .backZ = 1800.0f,
        .spillRadius = 6.0f, .spillGain = 0.4f, .dim = ROOM_SYNTHWAVE_BRIGHTNESS / 100.0f,
        .seed = 0x2545f491u, .scale = 1.0f, .resizable = ROOM_SYNTHWAVE_RESIZABLE,
        .open = 1, .glow = ROOM_SYNTHWAVE_GLOW,
    },
};

// A row a style, so the table and the list of styles cannot drift apart
_Static_assert(sizeof(ROOM_MODELS) / sizeof(ROOM_MODELS[0]) == ROOM_STYLE_LAST - ROOM_STYLE_FIRST + 1,
               "a room style with no row");

int roomStyleForCell(int cell) {
    if (cell == ENV_CELL_HOME_THEATER) {
        return ROOM_STYLE_THEATER;
    }
    if (cell == ENV_CELL_GRAND_CINEMA) {
        return ROOM_STYLE_GRAND_CINEMA;
    }
    if (cell == ENV_CELL_SYNTHWAVE) {
        return ROOM_STYLE_SYNTHWAVE;
    }
    return 0;
}

int roomCellForStyle(int style) {
    for (int cell = 0; cell < ENV_CELL_COUNT; cell++) {
        if (style > 0 && roomStyleForCell(cell) == style) {
            return cell;
        }
    }
    return -1;
}

int bakedRoomStyle(int style) {
    return style >= ROOM_STYLE_FIRST && style <= ROOM_STYLE_LAST;
}

// The row a style reads. Anything unknown falls back to the first room rather
// than reading past the table.
const RoomModel* roomModel(int style) {
    if (!bakedRoomStyle(style)) {
        return &ROOM_MODELS[0];
    }
    return &ROOM_MODELS[style - ROOM_STYLE_FIRST];
}

// Which room a style asks for, at the scale that style is drawn. The anchor is
// the model's seated eye point moved by the two offsets, and every number off
// the model goes through the same (model - anchor) * scale the geometry does,
// so moving the anchor moves the whole room around the viewer.
RoomParams roomParams(int style, float scale) {
    const RoomModel* m = roomModel(style);

    RoomParams p;
    memset(&p, 0, sizeof(p));
    Vec3 anchor = {m->eye.x, m->eye.y + m->eyeRaise, m->eye.z - m->eyeForward};
    p.anchor = anchor;
    p.halfWidth = m->halfWidth * scale;
    p.floorY = (m->floorY - anchor.y) * scale;
    // One floor throughout in every room that ships, so the picture stands on
    // the same one the viewer does
    p.screenFloorY = p.floorY;
    p.ceilingY = (m->ceilingY - anchor.y) * scale;
    p.screenZ = (m->screen.z - anchor.z) * scale;
    p.backZ = (m->backZ - anchor.z) * scale;
    p.screenMountY = (m->screen.y - anchor.y) * scale;
    // The picture sits flat against the anchor, so nothing stands it off
    p.screenProud = 0.0f;
    p.screenWidth = m->screenWidth * scale;
    p.screenHeight = m->screenHeight * scale;
    p.open = m->open;
    Vec3 screenAt = {(m->screen.x - anchor.x) * scale, p.screenMountY, p.screenZ};
    p.screenAt = screenAt;
    p.spillRadius = m->spillRadius * scale;
    p.spillGain = m->spillGain;
    p.texMix = 1.0f;
    p.dim = m->dim;
    p.seed = m->seed;
    return p;
}

int roomResizable(int style) {
    return bakedRoomStyle(style) && roomModel(style)->resizable;
}

void roomLevelsDefault(RoomLevels* out) {
    memset(out, 0, sizeof(*out));
    for (int style = ROOM_STYLE_FIRST; style <= ROOM_STYLE_LAST; style++) {
        const RoomModel* m = roomModel(style);
        out->brightness[style] = (int)roundf(m->dim * 100.0f);
        out->glow[style] = m->glow != 0;
        out->lightLevel[style] = ROOM_LIGHT_DEFAULT;
        out->screen[style] = (int)roundf(m->screenFraction * 100.0f);
    }
}

// A room that cannot be resized hangs the whole of it whatever was saved
int roomScreenPercentOf(int stored, int style) {
    if (!bakedRoomStyle(style)) {
        return ROOM_SCREEN_MAX;
    }
    return roomScreenClamp(stored, roomResizable(style));
}

int roomGlowOf(int style, int storedRoomGlow, int ambilightOn) {
    return bakedRoomStyle(style) ? storedRoomGlow : ambilightOn;
}

void roomLevelsSet(RoomLevels* l, int style, int brightness, int glow, int light, int screen) {
    if (!bakedRoomStyle(style)) {
        return;
    }
    l->brightness[style] = brightness < ROOM_BRIGHTNESS_MIN ? ROOM_BRIGHTNESS_MIN
                         : (brightness > ROOM_BRIGHTNESS_MAX ? ROOM_BRIGHTNESS_MAX : brightness);
    l->glow[style] = glow ? 1 : 0;
    l->lightLevel[style] =
        light < ROOM_LIGHT_MIN ? ROOM_LIGHT_MIN : (light > ROOM_LIGHT_MAX ? ROOM_LIGHT_MAX : light);
    l->screen[style] = roomScreenClamp(screen, roomResizable(style));
}

float roomScaleFor(float scaleOverride, int style) {
    if (!bakedRoomStyle(style)) {
        return 1.0f;
    }
    float scale = scaleOverride > 0.0f ? scaleOverride : roomModel(style)->scale;
    if (scale < ROOM_SCALE_MIN) {
        scale = ROOM_SCALE_MIN;
    }
    if (scale > ROOM_SCALE_MAX) {
        scale = ROOM_SCALE_MAX;
    }
    return scale;
}

// How far the room's own pass has to see. Every room is drawn on its own, so
// the far plane is that room's rather than one number that has to suit all of
// them: the geometry's own reach with room to spare, the walls the table gives
// under that in case a model comes up short, and what every room was drawn with
// before as the floor, so a small room keeps the precision it had and a sky that
// runs to the horizon is inside the frustum.
float roomFarPlane(const RoomParams* p, float reach) {
    float walls = p->halfWidth;
    if (fabsf(p->backZ) > walls) {
        walls = fabsf(p->backZ);
    }
    if (p->ceilingY > walls) {
        walls = p->ceilingY;
    }
    if (reach > walls) {
        walls = reach;
    }
    float far = walls * ROOM_FAR_MARGIN;
    return far < ROOM_FAR_MIN_M ? ROOM_FAR_MIN_M : far;
}

float roomDimFor(float dimOverride, int builtStyle, const int* brightnessByStyle, float fallbackDim) {
    if (dimOverride <= 0.0f) {
        return bakedRoomStyle(builtStyle) ? brightnessByStyle[builtStyle] / 100.0f : fallbackDim;
    }
    float dim = dimOverride;
    if (dim < ROOM_DIM_MIN) {
        dim = ROOM_DIM_MIN;
    }
    if (dim > ROOM_DIM_MAX) {
        dim = ROOM_DIM_MAX;
    }
    return dim;
}

float roomSpillGainFor(int lit, int builtStyle, float roomSpillGain, const int* lightLevelByStyle) {
    float level = bakedRoomStyle(builtStyle) ? lightLevelByStyle[builtStyle] / 100.0f : 1.0f;
    return lit ? roomSpillGain * level : 0.0f;
}

// Turns the loaded model into the layout the room's buffer is in. Nothing is
// generated here beyond the light: the shape, the texture coordinates and the
// colour all come off the model. A textured part carries no colour of its own
// and its atlas is mixed over the top of it as the part draws; a part painted
// from its vertex colours carries them here. The model arrives in its own
// space, so this is where the anchor and the scale go on. The normals are left
// alone, since a uniform scale does not turn them. Hands back how far the room
// runs from the viewer, which is what its far plane is worked out from.
float roomBuildVertices(const RoomParams* p, float scale, const float* src0, int count, float* verts) {
    float furthest = 0.0f;
    for (int i = 0; i < count; i++) {
        const float* src = src0 + (size_t)i * ROOM_MODEL_FLOATS;
        Vec3 pos = {(src[0] - p->anchor.x) * scale, (src[1] - p->anchor.y) * scale, (src[2] - p->anchor.z) * scale};
        float away = pos.x * pos.x + pos.y * pos.y + pos.z * pos.z;
        if (away > furthest) {
            furthest = away;
        }
        Vec3 normal = {src[3], src[4], src[5]};
        roomWriteVertex(p, verts, i, pos, src + 8, roomSpillWeight(p, pos, normal), src[6], src[7]);
    }
    return sqrtf(furthest);
}

// In a 3d room the picture hangs on the far wall (applyRoomPlacement after
// the ctx bookkeeping): fitted inside the room's anchor with its own shape
// kept, at the share of that the room's size row says, clamped to the walls
// unless the room is open, and hung where the whole of it is on the wall.
RoomPlacement roomPlacement(int style, float scale, float aspect, int screenPercent) {
    RoomParams p = roomParams(style, scale);
    float width = p.screenWidth;
    if (aspect > 0.0f && p.screenHeight > 0.0f && width * aspect > p.screenHeight) {
        width = p.screenHeight / aspect;
    }
    width *= screenPercent / 100.0f;
    // The clamps only catch a room whose own anchor does not fit its wall, and
    // an open room has no wall to fit: its anchor is hung as it is written
    if (!p.open) {
        float maxWidth = 2.0f * p.halfWidth - 0.4f;
        float maxHeight = (p.ceilingY - p.floorY) - 0.3f;
        if (width > maxWidth) {
            width = maxWidth;
        }
        if (width * aspect > maxHeight) {
            width = maxHeight / aspect;
        }
    }
    float height = width * aspect;
    // And hung where the whole of it is on the wall rather than through the
    // floor or the ceiling. The floor here is the one under the picture, not
    // the tier the viewer is on. An open room is left alone.
    float mount = p.screenMountY;
    if (!p.open) {
        float lowest = p.screenFloorY + height * 0.5f + 0.1f;
        float highest = p.ceilingY - height * 0.5f - 0.1f;
        if (mount < lowest) {
            mount = lowest;
        }
        if (mount > highest) {
            mount = highest;
        }
    }
    RoomPlacement out = {p.screenAt.x, mount, p.screenZ + p.screenProud, width, height};
    return out;
}

void roomEyeSize(int tier, int recW, int recH, int maxW, int maxH, int* outW, int* outH) {
    int eyeW;
    int eyeH;
    if (tier == ENV_RES_ULTRA) {
        eyeW = ROOM_ULTRA_EYE;
        eyeH = ROOM_ULTRA_EYE;
        if (maxW > 0 && eyeW > maxW) {
            eyeW = maxW;
        }
        if (maxH > 0 && eyeH > maxH) {
            eyeH = maxH;
        }
    }
    else {
        int maxEye = tier == ENV_RES_LOW ? ROOM_MAX_EYE : tier == ENV_RES_HIGH ? ROOM_MAX_EYE_FULL : ROOM_MAX_EYE_STANDARD;
        eyeW = recW > 0 ? recW : 1024;
        eyeH = recH > 0 ? recH : 1024;
        if (tier == ENV_RES_LOW) {
            eyeW /= 2;
            eyeH /= 2;
        }
        if (eyeW > maxEye) {
            eyeW = maxEye;
        }
        if (eyeH > maxEye) {
            eyeH = maxEye;
        }
    }
    *outW = eyeW;
    *outH = eyeH;
}

void roomEyeViewProj(float* out16, XrFovf fov, XrPosef pose, float farZ) {
    float proj[16];
    float view[16];
    projectionFromFov(proj, fov, ROOM_NEAR_M, farZ);
    viewFromPose(view, pose);
    matMul(out16, proj, view);
}
