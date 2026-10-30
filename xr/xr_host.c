// Moonlight XR Native: the Host sheet's input and layer side. See xr_host.h.
// GPLv3.
#include "xr_host.h"

#include "xr_keyboard.h"

void xrHostLoadMacros(XrCtx* ctx) {
    int got = xrMacrosLoadFile(&ctx->macros, NULL);
    if (got < 0) {
        LOGI("macros.ini could not be read");
    }
    else if (ctx->macros.errors > 0) {
        LOGI("macros.ini: %d lines left out, the first %s", ctx->macros.errors, ctx->macros.error);
    }
    LOGI("macros.ini: %d macros", ctx->macros.count);
}

XrPosef hostSheetPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float width = furnitureWidth(ctx) * HOST_WIDTH_FRAC;
    float height = width * (float)XR_HOST_TEX_H / (float)XR_HOST_TEX_W;
    *outWidth = width;
    *outHeight = height;

    Vec3 local = { 0.0f, 0.0f, 0.06f };
    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

void xrHostOpen(XrCtx* ctx) {
    xrHostLoadMacros(ctx);
    ctx->hostOpen = 1;
    ctx->hostReady = 1;
    ctx->hostConfirm = -1;
    ctx->hostHoverZone = XR_HOST_ZONE_NONE;
    ctx->hostPose = hostSheetPose(ctx, &ctx->hostW, &ctx->hostH);
    LOGEV("host sheet open");
}

void xrHostClose(XrCtx* ctx) {
    ctx->hostOpen = 0;
    ctx->hostConfirm = -1;
    ctx->hostHoverZone = XR_HOST_ZONE_NONE;
}

int xrHostZone(XrCtx* ctx, float u, float v) {
    return xrHostZoneAt(u, v, ctx->macros.count, ctx->hostConfirm >= 0);
}

// Starts the sequence of a cell
static void play(XrCtx* ctx, const XrHostSeq* seq, const char* label) {
    if (xrHostRunStart(&ctx->hostRun, seq)) {
        LOGEV("host sends %s", label);
    }
}

int xrHostPress(XrCtx* ctx, float u, float v, int* tick) {
    *tick = 0;
    int zone = xrHostZone(ctx, u, v);
    if (zone == XR_HOST_ZONE_NONE) {
        return zone;
    }
    *tick = 1;
    if (zone == XR_HOST_ZONE_CLOSE) {
        xrHostClose(ctx);
        return zone;
    }
    if (zone == XR_HOST_ZONE_CONFIRM_CANCEL) {
        ctx->hostConfirm = -1;
        return zone;
    }
    if (zone == XR_HOST_ZONE_PCVR) {
        // Asks first: this ends the stream and hands the headset to Steam Link
        ctx->hostConfirm = XR_HOST_CONFIRM_PCVR;
        return zone;
    }
    XrHostSeq seq;
    int confirm = 0;
    char label[XR_MACRO_NAME + 8];
    if (zone == XR_HOST_ZONE_CONFIRM_SEND) {
        int cell = ctx->hostConfirm;
        ctx->hostConfirm = -1;
        if (cell == XR_HOST_CONFIRM_PCVR) {
            ctx->pcvrRequest = 1;
            LOGEV("host: switch to PC VR confirmed");
            xrHostClose(ctx);
            return zone;
        }
        if (xrHostCellSeq(cell, &ctx->macros, &seq, &confirm, label, sizeof(label))
                != XR_HOST_CELL_NONE) {
            play(ctx, &seq, label);
        }
        xrHostClose(ctx);
        return zone;
    }
    int kind = xrHostCellSeq(zone, &ctx->macros, &seq, &confirm, label, sizeof(label));
    if (kind == XR_HOST_CELL_KEYS) {
        // Send keys: the keyboard up, one combo, sent whole
        xrHostClose(ctx);
        xrHostChordStart(&ctx->hostChord);
        if (!ctx->kbOpen) {
            ctx->kbOpen = 1;
            ctx->kbState = KB_STATE_LOWER;
            ctx->kbMods = 0;
            kbPlace(ctx);
        }
        LOGEV("host: send keys, waiting for a combo");
    }
    else if (kind != XR_HOST_CELL_NONE) {
        if (confirm) {
            ctx->hostConfirm = zone;
        }
        else {
            play(ctx, &seq, label);
            xrHostClose(ctx);
        }
    }
    return zone;
}

void xrHostFrame(XrCtx* ctx, float* out, int64_t now, int live) {
    if (!live || ctx->launcher != LAUNCHER_NONE) {
        // Nothing reaches the host: drop the sequence (what it holds is let
        // go of when the stream ends) and the chord
        xrHostRunInit(&ctx->hostRun);
        xrHostChordStop(&ctx->hostChord);
        return;
    }
    if (!ctx->kbOpen && ctx->hostChord.active) {
        xrHostChordStop(&ctx->hostChord);
    }
    if (!xrHostRunBusy(&ctx->hostRun) || out == NULL || out[IN_KEY] >= 0.0f) {
        return;
    }
    XrHostEmit e;
    if (!xrHostRunStep(&ctx->hostRun, (long long)now, &e)) {
        return;
    }
    static const int FLAGS[5] = { KBF_FLAG_CHORD, KBF_FLAG_DOWN, KBF_FLAG_UP, KBF_FLAG_HOLD,
                                  KBF_FLAG_LET };
    out[IN_KEY] = (float)(KB_CODE_VK + e.vk);
    out[IN_KEY_MODS] = (float)((e.mods & KBF_MOD_MASK) | FLAGS[e.kind]);
}
