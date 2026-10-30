// Native (lane a11y-tab, a11y-2): the Accessibility tab on the XR side. Which cells its rows show,
// a press on one (applied at once and handed back to be stored, as every cog cell is), the Dot size
// track, the row the footer describes, the eye tracking line's state, and the once-a-session log
// line that says whether eye gaze reached the app and, when it did not, what the runtime said.
// Nothing here changes a setting except a press or a drag on the tab. The rules themselves are
// xr/core/xr_access.c's. GPLv3.
#include "xr_renderer.h"
#include "xr_native.h"
#include <math.h>

// How long the eyes are watched for, focused, before the log says none came
#define GAZE_WATCH_NS 10000000000LL

int xrAccessEyes(const XrCtx* ctx) {
    if (ctx == NULL) {
        return ACCESS_EYES_UNAVAILABLE;
    }
    return accessEyesState(ctx->gazeOffered, ctx->eyeGaze && ctx->gazeBound, ctx->gazeSeen);
}

int cogAccessCells(XrCtx* ctx, int row) {
    (void)ctx;
    return row == COG_ACCESS_ROW_POINTER ? COG_ACCESS_POINTER_CELLS
         : row == COG_ACCESS_ROW_DWELL ? COG_ACCESS_DWELL_CELLS : 0;
}

int cogAccessCellLive(XrCtx* ctx, int row, int cell) {
    // native (lane a11y-2): Eyes is always there, dimmed only where the runtime has no eye gaze
    if (row == COG_ACCESS_ROW_POINTER && cell == POINTER_SOURCE_GAZE) {
        return accessEyesCellLive(xrAccessEyes(ctx), ctx->pointerSource);
    }
    return cell >= 0 && cell < cogAccessCells(ctx, row);
}

int cogAccessRowLive(XrCtx* ctx, int row) {
    // the dwell click and the dot only act with the head or the eyes pointing
    return (row != COG_ACCESS_ROW_DWELL && row != COG_ACCESS_ROW_DOT) || ctx->pointerSource != POINTER_SOURCE_CONTROLLER;
}

void cogApplyAccessCell(XrCtx* ctx, int row, int cell, float* out) {
    if (!cogAccessCellLive(ctx, row, cell) || !cogAccessRowLive(ctx, row)) {
        return;
    }
    int setting = -1;
    int value = cell;
    switch (row) {
    case COG_ACCESS_ROW_POINTER:
        // the cells are POINTER_SOURCE_ in order; the dwell time stays as it is. The one in force
        // again changes nothing, so a dwell resting on it does not start over and click it again.
        if (cell != ctx->pointerSource) {
            xrSetPointerSource(ctx, cell, ctx->headDwellMs);
        }
        setting = SETTING_POINTER_SOURCE;
        LOGEV("pointer %s from the Accessibility tab",
              cell == POINTER_SOURCE_HEAD ? "head" : cell == POINTER_SOURCE_GAZE ? "eyes" : "controller");
        break;
    case COG_ACCESS_ROW_DWELL:
        value = accessDwellMs(cell);
        // native (lane a11y-2): the time alone, read by the next dwell step; the dwell under way keeps
        // its place, so resting on the cell clicks it once (xrSetPointerSource would start it over)
        ctx->headDwellMs = dwellMsClamp(value);
        setting = SETTING_HEAD_DWELL;
        LOGEV("dwell click %d ms from the Accessibility tab", value);
        break;
    default:
        return;
    }
    out[IN_SETTING] = (float)setting;
    out[IN_SETTING_VALUE] = (float)value;
}

// Native (lane a11y-2): the Dot size track, in the 25 % steps it is stored in, so the thumb and
// the readout show exactly what is written when the drag ends
float cogAccessSliderValue(XrCtx* ctx, int row) {
    if (row != COG_ACCESS_ROW_DOT) {
        return 0.0f;
    }
    return lanePlace(accessDotClamp(ctx->assistDotSize), ACCESS_DOT_MIN, ACCESS_DOT_MAX);
}

void cogApplyAccessSlider(XrCtx* ctx, int row, float t) {
    if (row != COG_ACCESS_ROW_DOT || !cogAccessRowLive(ctx, row)) {
        return;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    ctx->assistDotSize = ACCESS_DOT_MIN + (int)lroundf(t * (float)ACCESS_DOT_STEPS) * ACCESS_DOT_STEP;
}

void cogAccessDragEnded(XrCtx* ctx, int row, float* out) {
    if (row != COG_ACCESS_ROW_DOT) {
        return;
    }
    out[IN_SETTING] = (float)SETTING_ASSIST_DOT_SIZE;
    out[IN_SETTING_VALUE] = (float)ctx->assistDotSize;
    LOGEV("dot size %d%% from the Accessibility tab", ctx->assistDotSize);
}

int xrAccessHoverRow(const XrCtx* ctx) {
    if (ctx == NULL || !ctx->cogOpen || ctx->cogTab != COG_TAB_ACCESS || ctx->cogPointV < 0.0f) {
        return -1;
    }
    return accessRowAt(ctx->cogPointV);
}

float xrAssistDotScale(const XrCtx* ctx) {
    return ctx != NULL ? accessDotScale(ctx->assistDotSize, ctx->pointerSource) : 1.0f;
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
