// Moonlight XR Native: the Host sheet's input and layer side. See xr_host.h.
// GPLv3.
#include "xr_host.h"

#include <stdio.h>

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
    ctx->hostPage = XR_HOST_PAGE_MAIN;
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
    return xrHostZoneAtPage(u, v, ctx->hostPage, ctx->macros.count, ctx->hostInfo.cmdCount,
                            ctx->hostConfirm >= 0);
}

// Starts the sequence of a cell
static void play(XrCtx* ctx, const XrHostSeq* seq, const char* label) {
    if (xrHostRunStart(&ctx->hostRun, seq)) {
        LOGEV("host sends %s", label);
    }
}

// ---- the PC page (lane wave4-apollo) --------------------------------------------------------------

// Asks the app for something at the end of this frame's input pass (xr_session.c dispatchInput)
static void act(XrCtx* ctx, int kind, int arg, const char* text) {
    ctx->hostActKind = kind;
    ctx->hostActArg = arg;
    snprintf(ctx->hostActText, sizeof(ctx->hostActText), "%s", text != NULL ? text : "");
}

// A press on the PC page's cell or its confirmation. Returns 1 if the zone was one of its own.
static int pcPress(XrCtx* ctx, int zone) {
    if (zone == XR_HOST_ZONE_CONFIRM_SEND) {
        int n = ctx->hostConfirm - XR_HOST_PC_CMD_FIRST;
        ctx->hostConfirm = -1;
        if (n >= 0 && n < ctx->hostInfo.cmdCount) {
            act(ctx, XR_HOSTACT_CMD, n, ctx->hostInfo.cmd[n]);
            LOGEV("host: server command %d", n);
        }
        xrHostClose(ctx);
        return 1;
    }
    switch (xrHostPcCellKind(zone, ctx->hostInfo.cmdCount)) {
        case XR_HOST_CELL_PASTE:
            act(ctx, XR_HOSTACT_PASTE, 0, NULL);
            LOGEV("host: paste from PC");
            xrHostClose(ctx);
            return 1;
        case XR_HOST_CELL_COPY:
            // The keyboard takes text for a field of its own; Enter sends it (xrHostComposeFeed)
            xrHostClose(ctx);
            xrHostChordStop(&ctx->hostChord);
            xrHostComposeStart(&ctx->hostCompose);
            if (!ctx->kbOpen) {
                ctx->kbOpen = 1;
                ctx->kbState = KB_STATE_LOWER;
                ctx->kbMods = 0;
                kbPlace(ctx);
            }
            LOGEV("host: send text to the PC clipboard, waiting for text");
            return 1;
        case XR_HOST_CELL_CMD:
            ctx->hostConfirm = zone;   // a command always asks first
            return 1;
        default:
            return 0;
    }
}

int xrHostComposeFeed(XrCtx* ctx, int code) {
    if (!ctx->hostCompose.active) {
        return 0;
    }
    if (xrHostComposeKey(&ctx->hostCompose, code) == XR_COMPOSE_SEND) {
        if (ctx->hostCompose.len > 0) {
            act(ctx, XR_HOSTACT_COPY, 0, ctx->hostCompose.text);
            LOGEV("host: %d bytes typed for the PC clipboard", ctx->hostCompose.len);
        }
        xrHostComposeStop(&ctx->hostCompose);
        ctx->kbOpen = 0;
    }
    return 1;
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
    if (zone == XR_HOST_ZONE_PAGE) {
        ctx->hostPage = ctx->hostPage == XR_HOST_PAGE_PC ? XR_HOST_PAGE_MAIN : XR_HOST_PAGE_PC;
        ctx->hostConfirm = -1;
        return zone;
    }
    if (zone == XR_HOST_ZONE_CONFIRM_CANCEL) {
        ctx->hostConfirm = -1;
        return zone;
    }
    if (ctx->hostPage == XR_HOST_PAGE_PC) {
        pcPress(ctx, zone);
        return zone;
    }
    XrHostSeq seq;
    int confirm = 0;
    char label[XR_MACRO_NAME + 8];
    if (zone == XR_HOST_ZONE_CONFIRM_SEND) {
        int cell = ctx->hostConfirm;
        ctx->hostConfirm = -1;
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
        xrHostComposeStop(&ctx->hostCompose);
        ctx->hostActKind = XR_HOSTACT_NONE;
        return;
    }
    if (!ctx->kbOpen && ctx->hostChord.active) {
        xrHostChordStop(&ctx->hostChord);
    }
    // Hide, or nothing reaching the host, ends "Send text to PC" with nothing sent
    if (ctx->hostCompose.active && !ctx->kbOpen) {
        xrHostComposeStop(&ctx->hostCompose);
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
