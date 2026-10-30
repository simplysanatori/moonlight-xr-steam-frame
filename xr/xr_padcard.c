// The first-run controls card (PLAN 3.9) and the pad card (PLAN 2b.6) on the frame loop's
// side: when each is up, and where each stands. The arithmetic is xr/core/xr_cards.c's, the
// pictures are ui/panel_firstrun.c and ui/panel_padcard.c, the layers are xr_layers.c's.
//
// The first-run card comes up once, on the first stream of an install, in front of the middle
// of the picture, and goes by itself after FIRSTRUN_SHOW_NS or when any sheet opens. It has no
// buttons (the input pass is not touched): config.ini's first_run_shown is written the frame
// it comes up, so it never returns; set it to false to see it again.
//
// The pad card is a schematic of the Bluetooth or USB gamepad being played, held in the lap
// where a pad would sit: PADCARD_DOWN_M under the eyes and PADCARD_FWD_M ahead along the head's
// heading (not its pitch, so it stays in the lap when you look up). It is up while
// gamepadsPhysicalActive() and, in the default auto mode, for PADCARD_AUTOHIDE_NS after the last
// input. GPLv3.
#include "xr_native.h"

#include "../input/gamepads.h"

void xrCardsApplySettings(XrCtx* ctx, const Settings* s) {
    firstRunInit(&ctx->firstRun, s->first_run_shown);
    ctx->padCardMode = s->pad_card;
    ctx->firstRunUp = 0;
    ctx->padCardUp = 0;
}

int xrCardsFrame(XrCtx* ctx, int64_t now) {
    // the picture is up and the session is the one in front of the eyes
    int ready = ctx->streaming && ctx->launcher == LAUNCHER_NONE && ctx->videoShown
            && ctx->sessionState == XR_SESSION_STATE_FOCUSED;
    ctx->firstRunUp = firstRunStep(&ctx->firstRun, ready, panelUp(ctx), now);
    int mark = firstRunTakeMark(&ctx->firstRun);

    PadLive live;
    gamepadsLiveState(&live);
    ctx->padLive = live;
    int up = padCardVisible(ctx->padCardMode, live.present, live.ageNs);
    if (up != ctx->padCardUp) {
        LOGEV("pad card %s", up ? "up" : "away");
    }
    ctx->padCardUp = up;
    if (mark) {
        LOGEV("first-run card up");
    }
    return mark;
}

XrPosef firstRunPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float width = furnitureWidth(ctx) * FIRSTRUN_WIDTH_FRAC;
    *outWidth = width;
    *outHeight = width * (float)FIRSTRUN_TEX_H / (float)FIRSTRUN_TEX_W;
    Vec3 local = { 0.0f, 0.0f, FIRSTRUN_LIFT_M };
    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// The head's heading about the vertical in the local space, 0 facing -z and positive turning
// left, from where the gaze points; looking steeply down or up it comes from the head's up
// instead, which then points along (down) or against (up) the heading
static float headingOf(XrQuaternionf head) {
    Vec3 fwd = { 0.0f, 0.0f, -1.0f }, up = { 0.0f, 1.0f, 0.0f };
    Vec3 f = quatRotate(head, fwd);
    Vec3 u = quatRotate(head, up);
    float hx = f.x, hz = f.z;
    if (hx * hx + hz * hz < 0.25f) {
        float s = f.y < 0.0f ? 1.0f : -1.0f;
        hx = u.x * s;
        hz = u.z * s;
    }
    if (hx * hx + hz * hz < 1e-6f) {
        return 0.0f;
    }
    return atan2f(-hx, -hz);
}

XrPosef padCardPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    *outWidth = PADCARD_WIDTH_M;
    *outHeight = PADCARD_WIDTH_M * (float)PADCARD_TEX_H / (float)PADCARD_TEX_W;
    float yaw = 0.0f;
    Vec3 base = { 0.0f, 0.0f, 0.0f };
    if (ctx->headPoseValid) {
        yaw = headingOf(ctx->headPose.orientation);
        base.x = ctx->headPose.position.x;
        base.y = ctx->headPose.position.y;
        base.z = ctx->headPose.position.z;
    }
    float off[3], tilt;
    padCardLap(yaw, off, &tilt);
    Vec3 yAxis = { 0.0f, 1.0f, 0.0f }, xAxis = { 1.0f, 0.0f, 0.0f };
    XrPosef pose;
    pose.orientation = quatNorm(quatMul(axisAngleQuat(yAxis, yaw), axisAngleQuat(xAxis, tilt)));
    pose.position.x = base.x + off[0];
    pose.position.y = base.y + off[1];
    pose.position.z = base.z + off[2];
    return pose;
}
