// Moonlight XR Native: stand ins for the parts of Moonlight XR's renderer that
// come in later milestones, answering the ported UI and input code the way
// upstream's own functions answer with the feature switched off.
//   rooms (xr_room.c): M5. roomEffective is always 0 (no room up), so the
//     furniture hangs off the picture and the Room tab never shows.
//   the glow (xr_ambilight.c): later. ambiEffective reports the switch and
//     level the Display tab holds, but nothing draws it (its rows are dead,
//     see xrRowLater).
//   the controller models (xr_model.c): later.
// GPLv3.
#include "xr_renderer.h"

int roomEffective(XrCtx* ctx) {
    (void)ctx;
    return 0;
}

int roomResizable(int style) {
    (void)style;
    return 0;
}

void roomLevelsFromTable(XrCtx* ctx) {
    for (int style = ROOM_STYLE_FIRST; style <= ROOM_STYLE_LAST; style++) {
        ctx->roomBrightness[style] = 100;
        ctx->roomGlow[style] = 0;
        ctx->roomLightLevel[style] = ROOM_LIGHT_DEFAULT;
        ctx->roomScreen[style] = ROOM_SCREEN_MAX;
    }
}

int roomScreenPercent(XrCtx* ctx, int style) {
    (void)ctx;
    (void)style;
    return ROOM_SCREEN_MAX;
}

int roomGlowOn(XrCtx* ctx, int style) {
    (void)style;
    return ctx->ambilightOn;
}

// No room ever takes the picture, so nothing is put aside or handed back
void applyRoomPlacement(XrCtx* ctx, int style, float aspect, int reseeded) {
    (void)ctx;
    (void)style;
    (void)aspect;
    (void)reseeded;
}

void ambiEffective(XrCtx* ctx, int* on, float* level) {
    *on = roomGlowOn(ctx, roomEffective(ctx));
    *level = ctx->ambiIntensity;
}

void updateControllerModels(XrCtx* ctx) {
    ctx->modelsShowing = 0;
}
