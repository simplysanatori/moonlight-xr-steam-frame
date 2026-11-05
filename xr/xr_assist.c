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
         : row == COG_ACCESS_ROW_DWELL ? COG_ACCESS_DWELL_CELLS
         : row == COG_ACCESS_ROW_STYLE ? COG_ACCESS_STYLE_CELLS
         : row == COG_ACCESS_ROW_EYECAL ? COG_ACCESS_EYECAL_CELLS : 0;
}

int cogAccessCellLive(XrCtx* ctx, int row, int cell) {
    // native (lane a11y-2): Eyes is always there, dimmed only where the runtime has no eye gaze
    if (row == COG_ACCESS_ROW_POINTER && cell == POINTER_SOURCE_GAZE) {
        return accessEyesCellLive(xrAccessEyes(ctx), ctx->pointerSource);
    }
    // native (lane eye-calibration): Start while the eyes reach the app, Reset with a calibration in force
    if (row == COG_ACCESS_ROW_EYECAL) {
        return accessEyeCalCellLive(xrAccessEyes(ctx), cell, ctx->eyeCal.model != EYECAL_MODEL_NONE);
    }
    return cell >= 0 && cell < cogAccessCells(ctx, row);
}

int cogAccessRowLive(XrCtx* ctx, int row) {
    // every row but the pointer's acts only with the head or the eyes pointing
    if (row == COG_ACCESS_ROW_POINTER) {
        return 1;
    }
    // native (lane eye-calibration): the calibration's, whatever points, while the eyes reach the app
    if (row == COG_ACCESS_ROW_EYECAL) {
        return accessEyeCalLive(xrAccessEyes(ctx));
    }
    return ctx->pointerSource != POINTER_SOURCE_CONTROLLER;
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
    case COG_ACCESS_ROW_STYLE:
        // native (lane radial-dwell): read by the next dwell; one under way keeps its style
        value = cell == DWELL_STYLE_RADIAL ? DWELL_STYLE_RADIAL : DWELL_STYLE_SIMPLE;
        ctx->assistDwellStyle = value;
        setting = SETTING_ASSIST_DWELL_STYLE;
        LOGEV("dwell click style %s from the Accessibility tab", value == DWELL_STYLE_RADIAL ? "radial menu" : "simple");
        break;
    case COG_ACCESS_ROW_EYECAL:
        // native (lane eye-calibration): Start opens the run (nothing stored until it ends well); Reset
        // takes the calibration out of force and empties the key
        if (cell == COG_ACCESS_EYECAL_START) {
            xrEyeCalStart(ctx);
            return;
        }
        memset(&ctx->eyeCal, 0, sizeof(ctx->eyeCal));
        ctx->eyeCalResult = -1;
        ctx->eyeCalUnsaved = 0;
        setting = SETTING_EYE_CAL;
        value = 0;
        LOGEV("eye calibration reset from the Accessibility tab");
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

// ---- lane a11y-final: the right hand's way back to the controllers, the Eyes pointer's steadiness ----

void xrPointerBackReset(XrCtx* ctx) {
    pointerBackReset(&ctx->pointerBack);
}

void xrPointerBackStep(XrCtx* ctx, uint64_t* down, int64_t now, float* out) {
    const uint64_t menuBit = XR_BIND_BIT(XR_BIND_SRC_RIGHT_MENU);
    const uint64_t gripBit = XR_BIND_BIT(XR_BIND_SRC_RIGHT_GRIP);
    int menuOut = 0;
    if (pointerBackStep(&ctx->pointerBack, (*down & menuBit) != 0, (*down & gripBit) != 0, now, &menuOut)) {
        if (ctx->pointerSource == POINTER_SOURCE_CONTROLLER) {
            LOGI("right Menu and grip held %d ms: the controllers point already",
                 (int)(POINTER_BACK_HOLD_NS / 1000000LL));
        }
        else {
            LOGEV("pointer controller from the right Menu and grip held %d ms (was %s)",
                  (int)(POINTER_BACK_HOLD_NS / 1000000LL),
                  ctx->pointerSource == POINTER_SOURCE_GAZE ? "eyes" : "head");
            xrSetPointerSource(ctx, POINTER_SOURCE_CONTROLLER, ctx->headDwellMs);
            ctx->pointerBackUnsaved = 1;
            xrPanelsNotice(ctx, ui_str(UIS_TOAST_POINTER_BACK));
        }
    }
    *down = menuOut ? (*down | menuBit) : (*down & ~menuBit);
    // stored as a press on the tab's Controller is, on the first frame the slot is free; a pointer
    // chosen again meanwhile was stored by its own press
    if (ctx->pointerBackUnsaved && out != NULL && out[IN_SETTING] < 0.0f) {
        ctx->pointerBackUnsaved = 0;
        if (ctx->pointerSource == POINTER_SOURCE_CONTROLLER) {
            out[IN_SETTING] = (float)SETTING_POINTER_SOURCE;
            out[IN_SETTING_VALUE] = (float)POINTER_SOURCE_CONTROLLER;
        }
    }
}

XrQuaternionf xrEyesSteady(XrCtx* ctx, XrQuaternionf q, float dt) {
    Vec3 fwd = { 0.0f, 0.0f, -1.0f };
    Vec3 d = quatRotate(q, fwd);
    float raw[3] = { d.x, d.y, d.z }, to[3];
    gazeSteadyStep(&ctx->gazeSteady, raw, dt, to);
    // the turn from the gaze to the steadied direction, put on top of the gaze's own
    Vec3 want = { to[0], to[1], to[2] };
    Vec3 axis = vecCross(d, want);
    float s = sqrtf(vecDot(axis, axis)), c = vecDot(d, want);
    if (s < 1e-7f) {
        return q;
    }
    return quatNorm(quatMul(axisAngleQuat(vecNorm(axis), atan2f(s, c)), q));
}

// ---- lane eye-calibration: the eye calibration's run, its result, and the gaze it corrects ----

void xrEyeCalFromSettings(XrCtx* ctx, const char* text) {
    EyeCal cal;
    if (!eyeCalParse(text, &cal) && text != NULL && text[0] != 0) {
        LOGW("eye calibration: [vr] eye_calibration \"%s\" is not one this version reads (or is out of "
             "bounds), so the gaze goes uncorrected", text);
    }
    ctx->eyeCal = cal;
}

int xrEyeCalUp(const XrCtx* ctx) {
    return ctx != NULL && eyeCalRunUp(&ctx->eyeCalRun);
}

int xrEyeCalStep(const XrCtx* ctx) {
    return xrEyeCalUp(ctx) ? ctx->eyeCalRun.step + 1 : 0;
}

void xrEyeCalStart(XrCtx* ctx) {
    eyeCalRunStart(&ctx->eyeCalRun);
    ctx->eyeCalBegun = 0;
    ctx->eyeCalResult = -1;
    LOGEV("eye calibration started from the Accessibility tab (pointer %s, %s)",
          ctx->pointerSource == POINTER_SOURCE_GAZE ? "eyes"
          : ctx->pointerSource == POINTER_SOURCE_HEAD ? "head" : "controller",
          ctx->eyeCal.model != EYECAL_MODEL_NONE ? "a calibration in force" : "none in force");
}

void xrEyeCalCancel(XrCtx* ctx, const char* why) {
    if (!xrEyeCalUp(ctx)) {
        return;
    }
    LOGEV("eye calibration cancelled (%s) at dot %d of %d; the calibration in force is kept", why,
          ctx->eyeCalRun.step + 1, EYECAL_DOTS);
    eyeCalRunStop(&ctx->eyeCalRun);
    ctx->eyeCalBegun = 0;
}

void xrEyeCalSave(XrCtx* ctx, float* out) {
    if (ctx->eyeCalUnsaved && out != NULL && out[IN_SETTING] < 0.0f) {
        out[IN_SETTING] = (float)SETTING_EYE_CAL;
        out[IN_SETTING_VALUE] = ctx->eyeCalUnsaved == 1 ? 1.0f : 0.0f;
        ctx->eyeCalUnsaved = 0;
    }
}

void xrEyeCalFrame(XrCtx* ctx, int64_t now, int gazeOk, float gazeYaw, float gazePitch, float dotYaw,
                   float dotPitch, float* out) {
    EyeCalRun* r = &ctx->eyeCalRun;
    int was = r->step;
    int what = eyeCalRunStep(r, now, gazeOk, gazeYaw, gazePitch, dotYaw, dotPitch);
    if (what == EYECAL_STEP_NEXT) {
        LOGI("eye calibration: dot %d of %d %s", was + 1, EYECAL_DOTS,
             r->step == was ? "not steady, once more" : r->have[eyeCalOrder(was)] ? "steady" : "skipped");
    }
    if (what != EYECAL_STEP_FINISHED) {
        return;
    }
    EyeCal fit;
    int res = eyeCalFit(r->points, r->have, &fit);
    static const char* const WHY[EYECAL_RESULTS] = { "kept", "too few steady dots", "no clear improvement",
                                                     "the correction is too large" };
    LOGEV("eye calibration: %d of %d dots steady (%d tried again, %d skipped), %d used (%d dropped), %s: "
          "average %.2f deg -> %.2f, largest %.2f -> %.2f: %s", r->good, EYECAL_DOTS, r->retries, r->skipped,
          fit.used, fit.dropped, fit.model == EYECAL_MODEL_QUAD ? "second order" : "affine", fit.meanBefore,
          fit.meanAfter, fit.maxBefore, fit.maxAfter, WHY[res]);
    ctx->eyeCalResult = res;
    if (res == EYECAL_OK) {
        // in force exactly as it is stored
        char text[UI_STR_MAX];
        EyeCal kept;
        if (eyeCalFormat(&fit, text, sizeof text) && eyeCalParse(text, &kept)) {
            kept.used = fit.used;
            kept.dropped = fit.dropped;
            ctx->eyeCal = kept;
            ctx->eyeCalUnsaved = 1;
        }
    }
    xrEyeCalSave(ctx, out);
}

int xrEyeCalDotPoint(const XrCtx* ctx, Vec3 headPos, int64_t now, Vec3* point, float* size) {
    int dot = xrEyeCalUp(ctx) ? eyeCalRunDot(&ctx->eyeCalRun) : -1;
    if (dot < 0 || ctx->videoWidth <= 0) {
        return 0;
    }
    float u, v;
    eyeCalTarget(dot, &u, &v);
    float w = ctx->screenWidth, h = w * (float)ctx->videoHeight / (float)ctx->videoWidth;
    // on the picture's plane, a little in front of it
    Vec3 local = { (u - 0.5f) * w, (0.5f - v) * h, 0.02f };
    Vec3 off = quatRotate(ctx->screenPose.orientation, local);
    point->x = ctx->screenPose.position.x + off.x;
    point->y = ctx->screenPose.position.y + off.y;
    point->z = ctx->screenPose.position.z + off.z;
    Vec3 away = vecSub(*point, headPos);
    float dist = sqrtf(vecDot(away, away));
    *size = 2.0f * dist * tanf(0.5f * eyeCalRunDotDeg(&ctx->eyeCalRun, now) * 0.0174532925f);
    return 1;
}

void xrEyeCalHeadAngles(XrQuaternionf headQ, Vec3 dir, float* yaw, float* pitch) {
    Vec3 d = quatRotate(quatConj(headQ), dir);
    float v[3] = { d.x, d.y, d.z };
    eyeCalAngles(v, yaw, pitch);
}

XrQuaternionf xrEyeCalCorrect(const XrCtx* ctx, XrQuaternionf headQ, XrQuaternionf gazeQ) {
    if (ctx->eyeCal.model == EYECAL_MODEL_NONE) {
        return gazeQ;
    }
    Vec3 fwd = { 0.0f, 0.0f, -1.0f };
    Vec3 local = quatRotate(gazeQ, fwd);
    Vec3 d = quatRotate(quatConj(headQ), local);
    float yaw, pitch, cy, cp, v[3] = { d.x, d.y, d.z }, to[3];
    eyeCalAngles(v, &yaw, &pitch);
    eyeCalApply(&ctx->eyeCal, yaw, pitch, &cy, &cp);
    eyeCalDir(cy, cp, to);
    // the turn from the measured direction to the corrected one, in the head's frame, put on the gaze
    Vec3 want = { to[0], to[1], to[2] };
    Vec3 axis = vecCross(d, want);
    float sn = sqrtf(vecDot(axis, axis)), cs = vecDot(d, want);
    if (sn < 1e-7f) {
        return gazeQ;
    }
    XrQuaternionf turn = axisAngleQuat(vecNorm(axis), atan2f(sn, cs));
    return quatNorm(quatMul(quatMul(quatMul(headQ, turn), quatConj(headQ)), gazeQ));
}
