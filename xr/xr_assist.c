// Native (lane a11y-tab): the Accessibility tab on the XR side. Which cells its rows show, a press
// on one (applied at once and handed back to be stored, as every cog cell is), the eye tracking
// line's state, and the once-a-session log line that says whether eye gaze reached the app and,
// when it did not, what the runtime said. Nothing here changes a setting except a press on the
// tab. The rules themselves are xr/core/xr_access.c's. GPLv3.
#include "xr_renderer.h"
#include "xr_native.h"
#include "xr_pad_bridge.h"

// How long the eyes are watched for, focused, before the log says none came
#define GAZE_WATCH_NS 10000000000LL

int xrAccessEyes(const XrCtx* ctx) {
    if (ctx == NULL) {
        return ACCESS_EYES_UNAVAILABLE;
    }
    int working = ctx->eyeGaze && ctx->gazeBound && ctx->gazeSupportFlag > 0;
    return accessEyesState(ctx->gazeOffered, ctx->gazeAsked, working, ctx->gazeSeen);
}

int cogAccessCells(XrCtx* ctx, int row) {
    if (row == COG_ACCESS_ROW_POINTER) {
        return accessPointerCells(xrAccessEyes(ctx), ctx->pointerSource);
    }
    return row == COG_ACCESS_ROW_DWELL ? COG_ACCESS_DWELL_CELLS : COG_ACCESS_SWITCH_CELLS;
}

int cogAccessRowLive(XrCtx* ctx, int row) {
    // the dwell click only acts with the head or the eyes pointing
    return row != COG_ACCESS_ROW_DWELL || ctx->pointerSource != POINTER_SOURCE_CONTROLLER;
}

void cogApplyAccessCell(XrCtx* ctx, int row, int cell, float* out) {
    if (cell < 0 || cell >= cogAccessCells(ctx, row) || !cogAccessRowLive(ctx, row)) {
        return;
    }
    int setting = -1;
    int value = cell;
    switch (row) {
    case COG_ACCESS_ROW_POINTER:
        // the cells are POINTER_SOURCE_ in order; the dwell time stays as it is. The one in force
        // pressed again (a dwell resting on it) leaves the pointer and its dwell as they are, so the
        // dwell's "look away before clicking again" holds.
        if (cell != ctx->pointerSource) {
            xrSetPointerSource(ctx, cell, ctx->headDwellMs);
        }
        setting = SETTING_POINTER_SOURCE;
        LOGEV("pointer %s from the Accessibility tab",
              cell == POINTER_SOURCE_HEAD ? "head" : cell == POINTER_SOURCE_GAZE ? "eyes" : "controller");
        break;
    case COG_ACCESS_ROW_DWELL:
        value = accessDwellMs(cell);
        // only the time: the dwell that clicked this cell stays spent until the dot leaves it
        ctx->headDwellMs = dwellMsClamp(value);
        setting = SETTING_HEAD_DWELL;
        LOGEV("dwell click %d ms from the Accessibility tab", value);
        break;
    case COG_ACCESS_ROW_HEAD_LOOK:
        xrSetHeadLook(ctx, cell != 0, ctx->headLookPxDeg);
        setting = SETTING_HEAD_LOOK;
        LOGEV("head-look %s from the Accessibility tab", cell != 0 ? "on" : "off");
        break;
    case COG_ACCESS_ROW_BACK_GUIDE:
        xrPadBridgeSetBackAsGuide(cell != 0);
        setting = SETTING_BACK_AS_GUIDE;
        LOGEV("Back as Guide %s from the Accessibility tab", cell != 0 ? "on" : "off");
        break;
    case COG_ACCESS_ROW_MOUSE:
        value = cell != 0 ? MOUSE_MODE_RELATIVE : MOUSE_MODE_ABSOLUTE;
        xrSetMouseMode(ctx, value);
        setting = SETTING_MOUSE_MODE;
        LOGEV("mouse mode %s from the Accessibility tab", cell != 0 ? "relative" : "absolute");
        break;
    default:
        return;
    }
    out[IN_SETTING] = (float)setting;
    out[IN_SETTING_VALUE] = (float)value;
}

void xrGazeWatch(XrCtx* ctx, int64_t nowNs, int focused, int actionActive) {
    if (ctx->gazeValid == 1 && !ctx->gazeSeen) {
        ctx->gazeSeen = 1;
        LOGEV("eye gaze: the first tracked gaze pose arrived (location flags 0x%x, action active %d, "
              "supportsEyeGazeInteraction %d)", ctx->gazeFlags, actionActive, ctx->gazeSupportFlag);
        ctx->gazeReported = 1;
        return;
    }
    if (ctx->gazeReported || !focused) {
        return;
    }
    if (!ctx->eyeGaze) {
        // the session's start said why already (xr_session.c), once
        ctx->gazeReported = 1;
        return;
    }
    if (ctx->gazeWatchStartNs == 0) {
        ctx->gazeWatchStartNs = nowNs;
        return;
    }
    if (nowNs - ctx->gazeWatchStartNs >= GAZE_WATCH_NS) {
        ctx->gazeReported = 1;
        LOGW("eye gaze: no tracked gaze pose in %d s focused: the gaze action is %s, the pose's location "
             "flags 0x%x, supportsEyeGazeInteraction %d. SteamVR is not giving eye gaze to this app: check "
             "SteamVR Settings > General > Eye Tracking on the headset (and that it is worn), then restart",
             (int)(GAZE_WATCH_NS / 1000000000LL), actionActive ? "active" : "not active", ctx->gazeFlags,
             ctx->gazeSupportFlag);
    }
}
