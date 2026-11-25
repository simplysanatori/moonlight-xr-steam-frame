// Where everything around the picture sits and what the ray is over: the
// handles, the buttons under the bar, the panels, and the rows and cells
// on the settings panel.
//
// Moonlight XR Native: ported from Moonlight XR's xr-renderer/xr_ui.c
// (Gilleece, GPLv3), logic unchanged. Native changes, each marked "native":
// the JNI setters at the end are plain C functions fed from config.ini; the
// ray row has a third cell for the pointer dot; the tabs and their rows are one
// table (xr_layout.h), which the hit test and the drawing share.
#include "xr_renderer.h"
#include "xr_native.h"
#include "xr_keyboard.h"
#include "xr_depth.h"

// Whether the panels hang against the stand in screen rather than the
// picture, which they do whenever a room is up. The picker, the settings
// panel, the keyboard, the exit prompt and the sheets are all placed and sized
// off it. The bar's row is not: it hangs under the picture as drawn, see
// barFrame.
int furnitureOnStandIn(XrCtx* ctx) {
    return roomEffective(ctx) > 0;
}

// Where that screen is: the picture's own pose outside a room, and the stand
// in's inside one
XrPosef furniturePose(XrCtx* ctx) {
    return furnitureOnStandIn(ctx) ? standInPose() : ctx->screenPose;
}

float furnitureWidth(XrCtx* ctx) {
    return furnitureOnStandIn(ctx) ? STAND_IN_WIDTH_M : ctx->screenWidth;
}

// The same shape as the picture, so the furniture keeps the proportions it has
// outside a room at the same size
float furnitureHeight(XrCtx* ctx) {
    return furnitureWidth(ctx) * (float)ctx->videoHeight / (float)ctx->videoWidth;
}

// The frame the bar's row is laid out in. In a room, under the room's picture
// at the size the bar has on the stand in, so a corner drag, the Size row and
// the room's own screen all carry it. Outside one, the picture as drawn: on a
// cylinder held under a full turn that is smaller than the placement, and the
// row follows it there too.
BarFrame barFrame(XrCtx* ctx) {
    float aspect = (float)ctx->videoHeight / (float)ctx->videoWidth;
    if (roomEffective(ctx) > 0) {
        return roomBarFrame(ctx->screenPose, ctx->screenWidth * aspect, aspect);
    }
    BarFrame frame;
    memset(&frame, 0, sizeof(frame));
    frame.pose = ctx->screenPose;
    frame.curved = effectiveCurvature(ctx) > 0.01f && ctx->cylinderSupported;
    frame.radius = ctx->screenRadius;
    float fit = frame.curved ? cylinderFit(ctx->screenWidth, ctx->screenRadius) : 1.0f;
    frame.width = ctx->screenWidth * fit;
    frame.height = frame.width * aspect;
    return frame;
}

// Whether a button in the bar's row is there to be hit: the picker, cog,
// keyboard and exit buttons always, the switches once their art has arrived,
// and head aim's only while it can act, which is the only time it is drawn
static int barSlotShown(XrCtx* ctx, int slot) {
    switch (slot) {
        case BAR_SLOT_PAD:
            return ctx->padButtonReady;
        case BAR_SLOT_AIM:
            return ctx->aimButtonReady && headAimCanAct(ctx);
        case BAR_SLOT_RAY:
            return ctx->rayButtonReady;
        case BAR_SLOT_STEREO:
            return ctx->stereoButtonReady;
        // Native (lane bar-glare): from the keyboard's strip like the Host
        // button, and only while it can act, which is the only time it is drawn
        case BAR_SLOT_HERE:
            return ctx->kbButtonReady && xrBringHereCanAct(ctx);
        default:
            return 1;
    }
}

int barButtonHit(XrCtx* ctx, int slot, float u, float v, const BarFrame* frame) {
    return barSlotShown(ctx, slot) && barSlotHit(slot, u, v, frame->width, frame->height);
}

// How big the corner brackets are, in metres, and 0 where there are none. A
// room hangs its own picture, so only a room that lets it be resized has
// corners, and they are sized off the stand in rather than the picture, so
// they look the same wherever the room hangs it and however small it is.
float cornerSide(XrCtx* ctx) {
    int style = roomEffective(ctx);
    if (style > 0) {
        if (!roomResizable(style)) {
            return 0.0f;
        }
        XrVector3f p = ctx->screenPose.position;
        return roomCornerSide(sqrtf(p.x * p.x + p.y * p.y + p.z * p.z));
    }
    return CORNER_FRAC * ctx->screenWidth;
}

// The curve in force. The panel takes over from the preference the moment it
// is touched, and hands it back when the reset button clears it.
float effectiveCurvature(XrCtx* ctx) {
    // Native: the launcher's list is a flat quad (its art cannot be sampled
    // for the curved pass), so the picture's place is flat while it is up;
    // the stored curve is xrCurveSetting's
    if (ctx->launcher != LAUNCHER_NONE) {
        return 0.0f;
    }
    return xrCurveSetting(ctx);
}

// Native: the curve the panel or the preference asks for, launcher or not
float xrCurveSetting(XrCtx* ctx) {
    return ctx->panelCurve >= 0.0f ? ctx->panelCurve : ctx->prefCurvature;
}

// The room a room's own rows belong to, or 0 with none up. Only ever a style
// the per room values can be read at.
static int roomFaceStyle(XrCtx* ctx) {
    int style = roomEffective(ctx);
    return style >= ROOM_STYLE_FIRST && style <= ROOM_STYLE_LAST ? style : 0;
}

// What the session says of the panel's rows, for xr_layout.h's table
static CogState cogStateOf(XrCtx* ctx) {
    int style = roomEffective(ctx);
    CogState state = {
        .headLocked = ctx->headLockedPref != 0,
        .inRoom = style > 0,
        .roomResizable = style > 0 && roomResizable(style),
        .curveOk = ctx->cylinderSupported != 0,
        .stereoOk = ctx->stereoMode != DEPTH_MODE_OFF,
        .stereoSource = ctx->stereoSource,
        .gun = ctx->lightgun.active,
    };
    return state;
}

// Whether the Screen tab's Size row is the room's own: the share of the room's
// screen the picture takes
static int cogSizeIsRooms(XrCtx* ctx, int tab, int row) {
    return tab == COG_TAB_SCREEN && row == COG_SLIDER_SIZE && roomEffective(ctx) > 0;
}

// How far the screen's face is tipped up or down, in radians. Positive is
// looking up at it.
float screenPitch(XrCtx* ctx) {
    Vec3 back = { 0.0f, 0.0f, 1.0f };
    Vec3 fwd = quatRotate(ctx->screenPose.orientation, back);
    return atan2f(fwd.y, sqrtf(fwd.x * fwd.x + fwd.z * fwd.z));
}

// The three angles the screen is described by, built back into an orientation:
// yaw about world up, then pitch, then roll about the screen's own forward
// axis. Roll goes innermost on purpose. A turn about the forward axis leaves
// that axis where it is, so the pitch and yaw read back untouched however far
// the picture is rolled, which is what lets a drag hold one while recomputing
// the other. Pitch is negated for the same reason the tilt slider negates it:
// turning by +theta about local x takes the face downward.
XrQuaternionf screenOrient(float yaw, float pitch, float roll) {
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 fwd = { 0.0f, 0.0f, 1.0f };
    XrQuaternionf q = quatMul(axisAngleQuat(up, yaw), axisAngleQuat(right, -pitch));
    return quatNorm(quatMul(q, axisAngleQuat(fwd, roll)));
}

// How far the screen is twisted about the axis it faces along, in radians.
// Positive raises its right edge. Measured by undoing the yaw and pitch rather
// than by reading how high the right edge sits, since those two only agree
// while the screen is level, and this one comes back out of screenOrient
// exactly at any pitch.
float screenRoll(XrCtx* ctx) {
    XrQuaternionf q = ctx->screenPose.orientation;
    Vec3 back = { 0.0f, 0.0f, 1.0f };
    Vec3 fwd = quatRotate(q, back);
    float yaw = atan2f(fwd.x, fwd.z);
    XrQuaternionf level = screenOrient(yaw, screenPitch(ctx), 0.0f);
    XrQuaternionf twist = quatMul(quatConj(level), q);
    // A quaternion and its negation are the same rotation, and with the screen
    // turned to face behind the viewer the rebuilt yaw lands on the other one.
    // Without this the answer comes back a full turn out.
    if (twist.w < 0.0f) {
        twist.z = -twist.z;
        twist.w = -twist.w;
    }
    return 2.0f * atan2f(twist.z, twist.w);
}

// Native (owner's request): flat panels over a curved picture. The picture as
// drawn is a cylinder whose axis stands a radius in front of its middle, so it
// bulges toward the viewer away from the middle, and a panel laid out in its
// flat frame stands behind the surface there. A panel stays flat and turned as
// the picture is, but its middle goes where the arc really is (R sin of the
// angle, not the arc length) and it is pulled forward to the front of the
// curve under its whole width, and a centimetre more. Nothing is kept further
// round than 60 degrees, where the cylinder folds back beside the viewer.
// Flat pictures, rooms and the launcher's list (effectiveCurvature 0) are not
// curved, and nothing changes for them.
int furnitureCurved(XrCtx* ctx) {
    return xrPictureCurved(ctx) && ctx->screenRadius > 1e-3f;
}

// The flat frame's x of the point arc length s from the picture's middle
float curveArcX(float radius, float s) {
    float a = s / radius;
    if (a > STATS_CURVE_MAX_ANGLE) a = STATS_CURVE_MAX_ANGLE;
    if (a < -STATS_CURVE_MAX_ANGLE) a = -STATS_CURVE_MAX_ANGLE;
    return radius * sinf(a);
}

// How far the cylinder comes toward the viewer anywhere between x lo and hi of
// the flat frame: the most at whichever end is further from the middle
float curveSpanDepth(float radius, float lo, float hi) {
    float m = fabsf(lo) > fabsf(hi) ? fabsf(lo) : fabsf(hi);
    float s = m / radius;
    float a = s < sinf(STATS_CURVE_MAX_ANGLE) ? asinf(s) : STATS_CURVE_MAX_ANGLE;
    return radius * (1.0f - cosf(a));
}

// A panel width wide laid out at local in the picture's flat frame (x as arc
// length from the middle, z how far in front it was meant to stand), as it
// stands over the curved picture: its middle on the arc, and in front of the
// curve under it by a centimetre, or as far in front as it was if that is more
Vec3 curvePanelLocal(XrCtx* ctx, Vec3 local, float width) {
    if (!furnitureCurved(ctx)) {
        return local;
    }
    float r = ctx->screenRadius;
    local.x = curveArcX(r, local.x);
    float front = curveSpanDepth(r, local.x - width * 0.5f, local.x + width * 0.5f) + 0.01f;
    if (front > local.z) {
        local.z = front;
    }
    return local;
}

// The frame a panel anchored to the picture's edges or the bar's buttons is
// laid out in: the picture as drawn, which on a cylinder wrapped past a full
// turn is cylinderFit smaller, as the bar's row is (barFrame)
static void furnitureFrame(XrCtx* ctx, float* width, float* height) {
    float fit = furnitureCurved(ctx) ? cylinderFit(ctx->screenWidth, ctx->screenRadius) : 1.0f;
    *width = furnitureWidth(ctx) * fit;
    *height = furnitureHeight(ctx) * fit;
}

// The picker floats just in front of the screen, centred on it
XrPosef pickerPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float width = furnitureWidth(ctx) * PICKER_WIDTH_FRAC;
    *outWidth = width;
    *outHeight = width * (float)PICKER_TEX_H / (float)PICKER_TEX_W;

    XrPosef pose = furniturePose(ctx);
    Vec3 local = { 0.0f, 0.0f, 0.06f };
    local = curvePanelLocal(ctx, local, width);   // native (owner's request)
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// The settings panel stands on top of the cog button that opens it, so it
// reads as belonging to that button and leaves the picture clear. The caller
// freezes what this returns for as long as the panel is open: the distance
// slider moves the screen, and a panel that followed it would drag the thumb
// out from under the ray halfway through a drag.
XrPosef cogPanelPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float frameWidth = furnitureWidth(ctx);
    float width = frameWidth * COG_WIDTH_FRAC;
    float height = width * (float)COG_TEX_H / (float)COG_TEX_W;
    *outWidth = width;
    *outHeight = height;

    // The button hangs below the screen, so the panel is placed off it rather
    // than off the screen. In a room that is where the button would hang on
    // the stand in, which the panel stays on while the bar follows the picture.
    // Native (owner's request): over the button where the bar's row has it,
    // which on a curved picture is the picture as drawn and the arc
    Vec3 button;
    float side, rowW, rowH;
    furnitureFrame(ctx, &rowW, &rowH);
    barSlotPlacement(BAR_SLOT_COG, rowW, rowH, &button, &side);

    Vec3 local;
    local.x = button.x;
    local.y = button.y + side * 0.5f + frameWidth * ENV_GAP_FRAC + height * 0.5f;
    local.z = 0.05f;
    local = curvePanelLocal(ctx, local, width);

    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// The keyboard hangs under the screen, centred on it, in the band the move bar
// lives in. Wider than the settings panel and squarer, so it wants the middle
// rather than a corner. Frozen while it is open, like the settings panel: the
// screen stays draggable behind it and the keys must not move under the ray.
XrPosef kbPanelPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float frameWidth = furnitureWidth(ctx);
    float width = frameWidth * KB_WIDTH_FRAC;
    // Native: the Full keyboard has more, smaller keys, so it hangs larger
    if (xrKeyboardLayout(ctx) == KB_LAYOUT_FULL) {
        width *= KB_FULL_SCALE;
    }
    float height = width * (float)KB_TEX_H / (float)KB_TEX_W;
    *outWidth = width;
    *outHeight = height;

    Vec3 local;
    local.x = 0.0f;
    // Top edge the same distance under the picture that the bar sits at.
    // Native (owner's request): under the picture as drawn, and in front of
    // its curve.
    float rowW, rowH;
    furnitureFrame(ctx, &rowW, &rowH);
    local.y = -(rowH * 0.5f + frameWidth * BAR_GAP_FRAC + height * 0.5f);
    local.z = 0.05f;
    local = curvePanelLocal(ctx, local, width);

    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// Native (owner's request): the keyboard where it is now. Under the screen
// as ever until its move bar has moved it, and then where it was left, at the
// size the layout showing gives it.
void kbPlace(XrCtx* ctx) {
    XrPosef pose = kbPanelPose(ctx, &ctx->kbW, &ctx->kbH);
    ctx->kbPose = ctx->kbMoved ? ctx->kbMovedPose : pose;
    // Native (owner's request): a moved keyboard at the size its stick resize
    // left it
    if (ctx->kbMoved && ctx->kbScale > 0.0f) {
        ctx->kbW *= ctx->kbScale;
        ctx->kbH *= ctx->kbScale;
    }
}

// Native (owner's request): the stats window. Shown when the setting is on and
// its lines have been drawn.
int statsShown(XrCtx* ctx) {
    return ctx->overlayVisible && ctx->overlayHasContent && ctx->overlayW > 0 && ctx->overlayH > 0;
}

// Native (owner's request): where the stats window is now, and its size. A
// pixel of its slot is the size it has always had (the slot's 384 px were 30%
// of the picture), so the type reads as before. Under the picture's top left
// corner it stays pinned to, in the picture's own frame, until a grip has
// taken it; then it stays where it was left, with its top edge fixed as the
// number of lines changes.
// Native (owner's request): its size, and a moved one's at the multiple its
// stick resize left it
void statsSize(XrCtx* ctx) {
    float metresPerPx = ctx->screenWidth * 0.30f / 384.0f;
    if (ctx->statsMoved && ctx->statsScale > 0.0f) {
        metresPerPx *= ctx->statsScale;
    }
    ctx->statsW = (float)ctx->overlayW * metresPerPx;
    ctx->statsH = (float)ctx->overlayH * metresPerPx;
}

void statsPlace(XrCtx* ctx) {
    if (!statsShown(ctx)) {
        return;
    }
    float screenH = ctx->screenWidth * (float)ctx->videoHeight / (float)ctx->videoWidth;
    statsSize(ctx);
    if (ctx->statsMoved) {
        Vec3 shift = { 0.0f, -(ctx->statsH - ctx->statsMovedH) * 0.5f, 0.0f };
        Vec3 at = quatRotate(ctx->statsMovedPose.orientation, shift);
        ctx->statsPose = ctx->statsMovedPose;
        ctx->statsPose.position.x += at.x;
        ctx->statsPose.position.y += at.y;
        ctx->statsPose.position.z += at.z;
        return;
    }
    float margin = ctx->screenWidth * 0.02f;
    Vec3 local = { -ctx->screenWidth * 0.5f + ctx->statsW * 0.5f + margin,
                   screenH * 0.5f - ctx->statsH * 0.5f - margin, 0.01f };
    // Native (owner's request): on a curved picture still flat and turned as
    // the picture is, but under the corner the cylinder really has, and pulled
    // forward to where the surface bulges at its outer edge, so none of the
    // picture stands in front of it. The edge is the corner's angle less the
    // margin as arc; past 60 degrees the corner folds round beside the viewer,
    // so the window keeps to 60. A window wide enough to reach further from
    // the middle on the other side (a picture wrapped well past a half turn)
    // is pulled forward by that edge instead, as far as 60 degrees needs.
    // (curveArcX, curveSpanDepth: the same sums as every panel over the curve)
    if (furnitureCurved(ctx)) {
        float r = ctx->screenRadius;
        float edge = cylinderAngle(ctx->screenWidth, r) * 0.5f - margin / r;
        if (edge < 0.0f) edge = 0.0f;
        float outer = curveArcX(r, -edge * r);
        float fit = cylinderFit(ctx->screenWidth, r);
        local.x = outer + ctx->statsW * 0.5f;
        local.y = screenH * fit * 0.5f - ctx->statsH * 0.5f - margin;
        local.z = curveSpanDepth(r, outer, outer + ctx->statsW) + 0.01f;
    }
    Vec3 at = quatRotate(ctx->screenPose.orientation, local);
    ctx->statsPose = ctx->screenPose;
    ctx->statsPose.position.x += at.x;
    ctx->statsPose.position.y += at.y;
    ctx->statsPose.position.z += at.z;
}

// Native (owner's request): the keyboard's move bar. The frame it is laid
// out in is the keyboard panel's width as a share of the screen's frame
// (KB_WIDTH_FRAC, KB_FULL_SCALE for the Full layout) turned back, so the pill
// is as big as the picture's whatever the layout, and hangs the picture's
// BAR_DROP_FRAC of that frame under the panel's bottom edge.
void kbBarGeometry(XrCtx* ctx, float* outWidth, float* outHeight, float* outDrop) {
    float share = KB_WIDTH_FRAC;
    if (xrKeyboardLayout(ctx) == KB_LAYOUT_FULL) {
        share *= KB_FULL_SCALE;
    }
    float frame = ctx->kbW / share;
    *outWidth = frame * BAR_WIDTH_FRAC;
    *outHeight = frame * BAR_HEIGHT_FRAC;
    *outDrop = ctx->kbH * 0.5f + frame * BAR_DROP_FRAC;
}

// Which key a point on the panel is inside, or -1. The rectangles are the
// whole of what this side knows about the layout, so a row of them is all
// there is to search. A blank on the sheet showing is no key at all.
int kbKeyAt(XrCtx* ctx, float u, float v) {
    int found = -1;
    for (int i = 0; i < ctx->kbKeyCount; i++) {
        if (ctx->kbCodes[ctx->kbState][i] == 0) {
            continue;
        }
        const float* r = &ctx->kbKeyRects[i * 4];
        if (u >= r[0] && u <= r[2] && v >= r[1] && v <= r[3]) {
            found = i;
        }
    }
    // Native: the Compact keyboard's layout chip sits past the keys
    if (found < 0 && ctx->kbCodes[ctx->kbState][KB_KEY_LAYOUT] != 0) {
        const float* r = &ctx->kbKeyRects[KB_KEY_LAYOUT * 4];
        if (u >= r[0] && u <= r[2] && v >= r[1] && v <= r[3]) {
            found = KB_KEY_LAYOUT;
        }
    }
    return found;
}

// Whether head aim can act at all: the screen locked to the head, which a
// room overrides. Its bar button only shows then, and its rows on the panel
// are greyed otherwise.
int headAimCanAct(XrCtx* ctx) {
    return ctx->headLockedPref && roomEffective(ctx) <= 0;
}

// Head aim on or off for the rest of the session, from the bar or the Controls
// tab. Nothing is stored: the setting is what the next session starts from.
void setHeadAimOn(XrCtx* ctx, int on, const char* from) {
    on = on ? 1 : 0;
    if (headAimSwitchOn(ctx->headAimSetting, ctx->headAimFlipped) == on) {
        return;
    }
    ctx->headAimFlipped = headAimFlipFor(ctx->headAimSetting, on);
    LOGEV("head aim %s from %s", on ? "on" : "off", from);
}

// Gamepad mode on or off for the rest of the session, from the shortcut on the
// controllers, the bar or the Controls tab, and the toast says which. Nothing is
// stored: the next session starts as the pointer again. Going on, the input
// pass puts the panels away, since nothing on the controllers can reach one
// now. Going off, a trigger still held is taken as already down and kept from
// the host, so the pointer's first frame does not click wherever the ray
// happens to be.
void setPadMode(XrCtx* ctx, int on, const char* from) {
    on = on ? 1 : 0;
    if (ctx->padMode == on) {
        return;
    }
    // Native: gamepad mode is M3. Until the pad reaches the host, the switch
    // would only take the pointer away, so it stays the pointer and says why.
    if (on && !ctx->padButtonReady) {
        LOGEV("controllers: gamepad mode asked for from %s, which comes in a later update", from);
        xrPanelsNotice(ctx, "Gamepad mode is not in this version");
        return;
    }
    ctx->padMode = on;
    if (on) {
        ctx->padPutAway = 1;
    }
    else {
        for (int h = 0; h < HAND_COUNT; h++) {
            if (ctx->profileKind[h] == PROFILE_CONTROLLER && ctx->padRead[h].trigger >= PRESS_ON) {
                ctx->triggerDown[h] = 1;
                ctx->triggerSwallowed[h] = 1;
            }
        }
    }
    LOGEV("controllers: %s from %s", on ? "gamepad mode" : "pointer mode", from);
    noticePush(&ctx->notices, on ? TOAST_GAMEPAD_MODE : TOAST_POINTER_MODE, 0);
}

// The ray on or off for the rest of the session, from the bar or the Display
// tab. Nothing is stored: the setting is what the next session starts from.
void setRayOn(XrCtx* ctx, int on, const char* from) {
    on = on ? 1 : 0;
    if (raySwitchOn(ctx->raySetting, ctx->rayFlipped) == on) {
        return;
    }
    ctx->rayFlipped = rayFlipFor(ctx->raySetting, on);
    LOGEV("ray %s from %s%s", on ? "on" : "off", from,
          on ? "" : ", the dot stays and presses land as before");
}

// Whether one of the panels is up: the settings panel, the picker, the
// keyboard, the exit prompt, the report sheet, the hand lock hint or the Ko-fi
// sheet. The ray comes back for them while it is switched off.
int panelUp(XrCtx* ctx) {
    return ctx->cogOpen || ctx->pickerOpen || ctx->kbOpen || ctx->exitConfirmOpen
            || ctx->reportOpen || ctx->hintOpen || ctx->kofiOpen || ctx->hostOpen;
}

// The 3D on or off for the rest of the session, from the bar or the 3D tab.
// Takes effect on the next draw, which is asked for now so a picture standing
// still shows it too. Coming back on with a model running, the warp waits
// flat for a map of what is on screen now rather than picking up with the
// one left from whenever it went off.
void setStereoLive(XrCtx* ctx, int on, const char* from) {
    on = on ? 1 : 0;
    if (ctx->stereoMode == DEPTH_MODE_OFF || ctx->stereoLive == on) {
        return;
    }
    ctx->stereoLive = on;
    ctx->warpRedraw = 1;
    ctx->stereoWaiting = on && ctx->stereoMode == DEPTH_MODE_MODEL;
    ctx->stereoWaitIndex = atomic_load_explicit(&ctx->depthStagedIndex, memory_order_acquire);
    ctx->stereoWaitNs = nowNs();
    // The shift test measures again in the new state
    ctx->barTestFramesLogged = 0;
    LOGEV("3d %s from %s", on ? "on" : "off", from);
}

// The prompt stands on the button that opened it, the way the settings panel
// stands on the cog. Frozen for as long as it is up for the same reason: the
// screen can still be dragged behind it, and the two buttons must not move out
// from under the ray on the way to a press.
XrPosef exitPromptPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float frameWidth = furnitureWidth(ctx);
    float width = frameWidth * EXIT_WIDTH_FRAC;
    float height = width * (float)EXIT_TEX_H / (float)EXIT_TEX_W;
    *outWidth = width;
    *outHeight = height;

    // Native (owner's request): over its button on a curved picture too, as
    // the settings panel is
    Vec3 button;
    float side, rowW, rowH;
    furnitureFrame(ctx, &rowW, &rowH);
    barSlotPlacement(BAR_SLOT_EXIT, rowW, rowH, &button, &side);

    Vec3 local;
    local.x = button.x;
    local.y = button.y + side * 0.5f + frameWidth * ENV_GAP_FRAC + height * 0.5f;
    local.z = 0.05f;
    local = curvePanelLocal(ctx, local, width);

    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// The report sheet stands over the middle of the picture's bottom edge, just
// clear of it, so the keyboard hanging under the picture is right below it
// and the two read as one form. Frozen while it is up, like the prompt.
XrPosef reportSheetPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float frameWidth = furnitureWidth(ctx);
    float width = frameWidth * REPORT_WIDTH_FRAC;
    float height = width * (float)REPORT_TEX_H / (float)REPORT_TEX_W;
    *outWidth = width;
    *outHeight = height;

    // Native (owner's request): over the bottom edge of the picture as drawn,
    // and in front of its curve
    float rowW, rowH;
    furnitureFrame(ctx, &rowW, &rowH);
    Vec3 local;
    local.x = 0.0f;
    local.y = -rowH * 0.5f + height * 0.5f + frameWidth * 0.01f;
    local.z = 0.06f;
    local = curvePanelLocal(ctx, local, width);

    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// The hand lock hint stands over the middle of the picture, a little in front
// of it, where it is seen whatever the hand is pointing at. Frozen while it is
// up, like the prompt.
XrPosef handHintPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float width = furnitureWidth(ctx) * HINT_WIDTH_FRAC;
    float height = width * (float)HINT_TEX_H / (float)HINT_TEX_W;
    *outWidth = width;
    *outHeight = height;

    Vec3 local = { 0.0f, 0.0f, 0.06f };
    local = curvePanelLocal(ctx, local, width);   // native (owner's request)
    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// The Ko-fi sheet stands where the hint does, over the middle of the picture
// and a little in front of it. Frozen while it is up, like the prompt.
XrPosef kofiSheetPose(XrCtx* ctx, float* outWidth, float* outHeight) {
    float width = furnitureWidth(ctx) * KOFI_WIDTH_FRAC;
    float height = width * (float)KOFI_TEX_H / (float)KOFI_TEX_W;
    *outWidth = width;
    *outHeight = height;

    Vec3 local = { 0.0f, 0.0f, 0.06f };
    local = curvePanelLocal(ctx, local, width);   // native (owner's request)
    XrPosef pose = furniturePose(ctx);
    Vec3 offset = quatRotate(pose.orientation, local);
    pose.position.x += offset.x;
    pose.position.y += offset.y;
    pose.position.z += offset.z;
    return pose;
}

// Which of the prompt's two buttons a point is on, in the sheet's own
// coordinates. Everything else on it is a question and a background.
int exitPromptZone(float u, float v) {
    return exitPromptZoneFor(u, v, 0);
}

// The same for the prompt a stream gets, whose three buttons (Quit app,
// Disconnect, Cancel) share the row when withDisconnect is set
int exitPromptZoneFor(float u, float v, int withDisconnect) {
    if (v < EXIT_BTN_T || v > EXIT_BTN_B) {
        return EXIT_ZONE_NONE;
    }
    if (withDisconnect) {
        if (u >= EXIT3_QUIT_L && u <= EXIT3_QUIT_R) {
            return EXIT_ZONE_EXIT;
        }
        if (u >= EXIT3_DISC_L && u <= EXIT3_DISC_R) {
            return EXIT_ZONE_DISCONNECT;
        }
        if (u >= EXIT3_CANCEL_L && u <= EXIT3_CANCEL_R) {
            return EXIT_ZONE_CANCEL;
        }
        return EXIT_ZONE_NONE;
    }
    if (u >= EXIT_EXIT_L && u <= EXIT_EXIT_R) {
        return EXIT_ZONE_EXIT;
    }
    if (u >= EXIT_CANCEL_L && u <= EXIT_CANCEL_R) {
        return EXIT_ZONE_CANCEL;
    }
    return EXIT_ZONE_NONE;
}

// Whether the exit prompt offers Disconnect: while a stream is up, not over
// the launcher's lists, where leaving is quitting the app
int exitPromptHasDisconnect(const XrCtx* ctx) {
    return ctx->launcher == LAUNCHER_NONE;
}

// Whether a row can do anything here. A dead row is drawn greyed, carries no
// thumb and takes no press, so the art, the layers and the hit test all ask
// the same table (xr_layout.h cogRowWhy).
int cogRowLive(XrCtx* ctx, int tab, int row) {
    // Native (lane a11y-tab): the dwell click needs the head or the eyes pointing
    if (tab == COG_TAB_ACCESS) {
        return cogAccessRowLive(ctx, row);
    }
    CogState state = cogStateOf(ctx);
    return cogRowWhy(&state, tab, row) == COG_LIVE;
}

int cogResetActs(XrCtx* ctx, int tab) {
    CogState state = cogStateOf(ctx);
    return cogResetLive(&state, tab);
}

// Where a slider's thumb sits along its track, 0 at the left end and 1 at the
// right. Read back from the thing the slider controls rather than stored, so
// dragging the screen about cannot leave the panel disagreeing with it.
float cogSliderValue(XrCtx* ctx, int face, int slider) {
    XrVector3f p = ctx->screenPose.position;
    float t = 0.0f;

    if (cogSizeIsRooms(ctx, face, slider)) {
        // A quarter of the room's screen at the left end, all of it at the
        // right
        int style = roomFaceStyle(ctx);
        t = style == 0 ? 0.0f : lanePlace(roomScreenPercent(ctx, style), ROOM_SCREEN_MIN, ROOM_SCREEN_MAX);
    }
    else if (face == COG_TAB_ROOM) {
        // The room showing's own values, so the thumbs move when the picker
        // moves to another room
        int style = roomFaceStyle(ctx);
        if (slider == COG_ROOM_ROW_BG_DIM) {
            t = lanePlace(ctx->bgDim, BG_DIM_MIN, BG_DIM_MAX);
        }
        else if (slider == COG_ROOM_ROW_GLOW_LEVEL) {
            t = ctx->ambiIntensity;
        }
        else if (slider == COG_ROOM_ROW_GLOW_SPREAD) {   // native (issue #7)
            t = lanePlace(ctx->glowSpread, GLOW_SPREAD_MIN, GLOW_SPREAD_MAX);
        }
        else if (style != 0 && slider == COG_ROOM_ROW_BRIGHTNESS) {
            t = lanePlace(ctx->roomBrightness[style], ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX);
        }
        else if (style != 0 && slider == COG_ROOM_ROW_LIGHT_LEVEL) {
            t = lanePlace(ctx->roomLightLevel[style], ROOM_LIGHT_MIN, ROOM_LIGHT_MAX);
        }
    }
    else if (face == COG_TAB_CONTROLS) {
        if (slider == COG_CONTROLS_ROW_SCROLL) {
            t = scrollTrackPlace(ctx->scrollSpeed);
        }
        else if (slider == COG_CONTROLS_ROW_AIM_SENSITIVITY) {
            t = lanePlace(ctx->headAimSensitivity, HEAD_AIM_SENSITIVITY_MIN, HEAD_AIM_SENSITIVITY_MAX);
        }
        else if (slider == COG_CONTROLS_ROW_AIM_DEADZONE) {
            t = lanePlace(ctx->headAimDeadZone, HEAD_AIM_DEADZONE_MIN, HEAD_AIM_DEADZONE_MAX);
        }
    }
    else if (face == COG_TAB_3D) {
        if (slider == COG_ROW3D_SEPARATION) {
            t = ctx->separationCurrent / COG_SEP_MAX;
        }
        else if (slider == COG_ROW3D_CONVERGENCE) {
            t = ctx->convergence;
        }
        else if (slider == COG_ROW3D_EDGE) {
            t = lanePlace((int)lroundf(ctx->depthSharp * 100.0f), EDGE_SHARPNESS_MIN, EDGE_SHARPNESS_MAX);
        }
        else if (slider == COG_ROW3D_DETAIL) {
            t = lanePlace((int)lroundf(ctx->depthLocal * 100.0f), DEPTH_SCALE_MIN, DEPTH_SCALE_MAX);
        }
        else if (slider == COG_ROW3D_SHAPE) {
            t = lanePlace((int)lroundf(ctx->depthGlobal * 100.0f), DEPTH_SCALE_MIN, DEPTH_SCALE_MAX);
        }
        else if (slider == COG_ROW3D_MOTION) {
            t = lanePlace((int)lroundf(ctx->depthMotion * 100.0f), MOTION_RESET_MIN, MOTION_RESET_MAX);
        }
        else if (slider == COG_ROW3D_BALANCE) {
            t = lanePlace((int)lroundf(ctx->eyeBalance * 100.0f), EYE_BALANCE_MIN, EYE_BALANCE_MAX);
        }
    }
    else if (face == COG_TAB_ACCESS) {
        t = cogAccessSliderValue(ctx, slider);   // native (lane a11y-2): Dot size
    }
    else if (face == COG_TAB_DISPLAY) {
        if (slider >= 0 && slider < PICTURE_VALUES) {
            t = lanePlace(ctx->pictureUnits[slider], pictureMin(slider), pictureMax(slider));
        }
    }
    else if (slider == COG_SLIDER_DISTANCE) {
        float d = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
        t = (d - COG_DIST_MIN) / (COG_DIST_MAX - COG_DIST_MIN);
    }
    else if (slider == COG_SLIDER_HEIGHT) {
        t = (p.y - COG_HEIGHT_MIN) / (COG_HEIGHT_MAX - COG_HEIGHT_MIN);
    }
    else if (slider == COG_SLIDER_TILT) {
        // Level sits at the middle of the track
        t = (screenPitch(ctx) + COG_TILT_MAX) / (2.0f * COG_TILT_MAX);
    }
    else if (slider == COG_SLIDER_ROTATE) {
        // Reversed against the others: the right hand end turns the picture
        // clockwise, which lowers the right edge that screenRoll counts as
        // positive. Level sits at the middle either way.
        t = (COG_ROLL_MAX - screenRoll(ctx)) / (2.0f * COG_ROLL_MAX);
    }
    else if (slider == COG_SLIDER_CURVE) {
        t = effectiveCurvature(ctx);
    }
    else if (slider == COG_SLIDER_SIZE) {
        // Native (lane aspect-ratio): the size, a 16:9 picture's width at this height
        t = (ctx->screenWidth / xrScreenWidthScale(ctx) - SCREEN_MIN_WIDTH) / (SCREEN_MAX_WIDTH - SCREEN_MIN_WIDTH);
    }

    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// A point on a track as the units of a row that moves in whole steps: the
// nearest of steps+1 places from min, step apart
static int stepUnits(float t, int min, int step, int steps) {
    return min + (int)lroundf(t * (float)steps) * step;
}

static void screenSlider(XrCtx* ctx, int slider, float pu);

// Applies a point on the track to whatever the row controls. The run the
// thumb travels stops short of the track's ends, where the step buttons are.
void cogApplySlider(XrCtx* ctx, int face, int slider, float pu) {
    float t = cogRunPlace(pu);

    if (face == COG_TAB_SCREEN) {
        if (!cogSizeIsRooms(ctx, face, slider)) {
            screenSlider(ctx, slider, pu);
            return;
        }
        // In whole percent, about the picture's centre, which the room keeps
        // where it was
        int style = roomFaceStyle(ctx);
        if (style != 0 && roomResizable(style)) {
            ctx->roomScreen[style] = laneUnits(t, ROOM_SCREEN_MIN, ROOM_SCREEN_MAX);
        }
        return;
    }

    if (face == COG_TAB_ROOM) {
        // Whole units, the ones each preference is stored in, so the thumb
        // shows exactly what gets written when the drag ends. All are read
        // afresh every frame, so the surroundings change under the thumb.
        int style = roomFaceStyle(ctx);
        if (slider == COG_ROOM_ROW_BG_DIM) {
            // Five percent steps, the ones the step buttons move by
            ctx->bgDim = bgDimClamp(laneUnits(t, BG_DIM_MIN, BG_DIM_MAX));
        }
        else if (slider == COG_ROOM_ROW_GLOW_LEVEL) {
            // Five percent steps
            int units = (int)roundf(t * 20.0f) * 5;
            ctx->ambiIntensity = units / 100.0f;
        }
        else if (slider == COG_ROOM_ROW_GLOW_SPREAD) {   // native (issue #7): five percent steps
            ctx->glowSpread = GLOW_SPREAD_MIN
                    + GLOW_SPREAD_STEP * laneUnits(t, 0, (GLOW_SPREAD_MAX - GLOW_SPREAD_MIN) / GLOW_SPREAD_STEP);
        }
        else if (style != 0 && slider == COG_ROOM_ROW_BRIGHTNESS) {
            ctx->roomBrightness[style] = laneUnits(t, ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX);
        }
        else if (style != 0 && slider == COG_ROOM_ROW_LIGHT_LEVEL) {
            ctx->roomLightLevel[style] = laneUnits(t, ROOM_LIGHT_MIN, ROOM_LIGHT_MAX);
        }
        return;
    }

    if (face == COG_TAB_CONTROLS) {
        // Head aim's two in the whole units they are stored in. All are read
        // every frame, so the next turn already goes at what the thumb shows.
        if (slider == COG_CONTROLS_ROW_SCROLL) {
            xrSetScrollSpeed(ctx, scrollTrackPercent(t));
        }
        else if (slider == COG_CONTROLS_ROW_AIM_SENSITIVITY) {
            ctx->headAimSensitivity = laneUnits(t, HEAD_AIM_SENSITIVITY_MIN,
                                                HEAD_AIM_SENSITIVITY_MAX);
        }
        else if (slider == COG_CONTROLS_ROW_AIM_DEADZONE) {
            ctx->headAimDeadZone = laneUnits(t, HEAD_AIM_DEADZONE_MIN, HEAD_AIM_DEADZONE_MAX);
        }
        return;
    }

    // Native (lane a11y-2): the Accessibility tab's Dot size, in its 25 % steps (xr_assist.c)
    if (face == COG_TAB_ACCESS) {
        cogApplyAccessSlider(ctx, slider, t);
        return;
    }

    if (face == COG_TAB_DISPLAY) {
        // Whole units, so the thumb and the readout show exactly what gets
        // written when the drag ends. The grade is read every frame, so the
        // picture changes under the thumb.
        if (slider >= 0 && slider < PICTURE_VALUES) {
            pictureSet(ctx, slider, laneUnits(t, pictureMin(slider), pictureMax(slider)));
        }
        return;
    }

    if (face == COG_TAB_3D) {
        // Both tracks move something only the 3D shows, so taking hold of
        // either brings it back if it was switched off
        setStereoLive(ctx, 1, "the 3D tab's tracks");
        if (slider == COG_ROW3D_SEPARATION) {
            // Snapped to the units the preference is stored in, so what the
            // thumb shows is exactly what gets written when the drag ends
            ctx->panelSeparation = separationOf(laneUnits(t, 0, COG_SEP_STEPS));
            ctx->separationCurrent = ctx->panelSeparation;
        }
        else if (slider == COG_ROW3D_CONVERGENCE) {
            // Whole percent, same reason
            int units = (int)roundf(t * 100.0f);
            ctx->convergence = units / 100.0f;
        }
        else if (slider == COG_ROW3D_EDGE) {
            // The depth model's five are read every frame, so the next depth
            // map is already made with what the thumb shows; each lands on
            // one of the steps its preference is stored in
            depthEdgeParams(stepUnits(t, EDGE_SHARPNESS_MIN, COG_EDGE_STEP, COG_EDGE_STEPS),
                            &ctx->upsampleSigmaR, &ctx->depthSharp);
        }
        else if (slider == COG_ROW3D_DETAIL) {
            ctx->depthLocal = (float)stepUnits(t, DEPTH_SCALE_MIN, COG_DEPTH_STEP, COG_DEPTH_STEPS) / 100.0f;
        }
        else if (slider == COG_ROW3D_SHAPE) {
            ctx->depthGlobal = (float)stepUnits(t, DEPTH_SCALE_MIN, COG_DEPTH_STEP, COG_DEPTH_STEPS) / 100.0f;
        }
        else if (slider == COG_ROW3D_MOTION) {
            ctx->depthMotion = (float)stepUnits(t, MOTION_RESET_MIN, COG_MOTION_STEP, COG_MOTION_STEPS) / 100.0f;
        }
        else if (slider == COG_ROW3D_BALANCE) {
            ctx->eyeBalance = (float)stepUnits(t, EYE_BALANCE_MIN, COG_BALANCE_STEP, COG_BALANCE_STEPS) / 100.0f;
        }
    }
}

// The placement's six, whatever is up: a point on the track to the screen's
// own pose and width (the presets come here too)
static void screenSlider(XrCtx* ctx, int slider, float pu) {
    float t = cogRunPlace(pu);

    if (slider == COG_SLIDER_DISTANCE) {
        XrVector3f p = ctx->screenPose.position;
        float d = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
        float wanted = COG_DIST_MIN + t * (COG_DIST_MAX - COG_DIST_MIN);
        if (d > 0.01f) {
            // Straight out along the line it already sits on, so only how far
            // away it is changes
            float scale = wanted / d;
            ctx->screenPose.position.x = p.x * scale;
            ctx->screenPose.position.y = p.y * scale;
            ctx->screenPose.position.z = p.z * scale;
            // Keeping the arc the same shape as it moves, same reason as the
            // resize path
            ctx->screenRadius *= scale;
        }
        else {
            // Sitting on top of the viewer, so there is no line to follow and
            // straight ahead is the only sensible answer
            ctx->screenPose.position.x = 0.0f;
            ctx->screenPose.position.y = 0.0f;
            ctx->screenPose.position.z = -wanted;
        }
    }
    else if (slider == COG_SLIDER_HEIGHT) {
        // Straight up and down, so raising the screen does not also bring it
        // nearer the way an arc about the viewer would
        ctx->screenPose.position.y = COG_HEIGHT_MIN + t * (COG_HEIGHT_MAX - COG_HEIGHT_MIN);
    }
    else if (slider == COG_SLIDER_TILT) {
        float target = -COG_TILT_MAX + t * (2.0f * COG_TILT_MAX);
        // Level is worth being able to land on exactly, same as the roll row
        if (fabsf(target) < COG_TILT_SNAP) {
            target = 0.0f;
        }
        // Rebuilt from the three angles rather than turned about the local x
        // axis, which stops being horizontal once the picture has been rolled
        // and would swing it round instead of tipping it. Identical result on
        // a level screen, and it lands on the target in one step.
        Vec3 back = { 0.0f, 0.0f, 1.0f };
        Vec3 fwd = quatRotate(ctx->screenPose.orientation, back);
        float yaw = atan2f(fwd.x, fwd.z);
        float roll = screenRoll(ctx);
        // Position untouched, so it tilts about its own centre rather than
        // swinging around the viewer
        ctx->screenPose.orientation = screenOrient(yaw, target, roll);
    }
    else if (slider == COG_SLIDER_ROTATE) {
        // Dragging right turns the picture clockwise as the viewer sees it,
        // the way a rotate right button does. The forward axis points out at
        // the viewer, so a right handed turn about it reads anticlockwise, and
        // the track runs from +max down to -max to match.
        float target = COG_ROLL_MAX - t * (2.0f * COG_ROLL_MAX);
        // Level is the whole point of the row and the track is far too coarse
        // to land on it by hand, so the middle of it clips to exactly zero
        if (fabsf(target) < COG_ROLL_SNAP) {
            target = 0.0f;
        }
        float delta = target - screenRoll(ctx);
        Vec3 fwd = { 0.0f, 0.0f, 1.0f };
        Vec3 axis = quatRotate(ctx->screenPose.orientation, fwd);
        XrQuaternionf turn = axisAngleQuat(axis, delta);
        // Turning about the axis it already faces along leaves the facing
        // alone, so this only rolls: the tilt and the yaw come back unchanged
        ctx->screenPose.orientation = quatNorm(quatMul(turn, ctx->screenPose.orientation));
    }
    else if (slider == COG_SLIDER_CURVE) {
        ctx->panelCurve = t;
        // The radius updatePlacement would have picked for this curve, so a
        // reseed later agrees with what is on screen now
        XrVector3f p = ctx->screenPose.position;
        float d = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
        ctx->screenRadius = d * (1.0f + 3.0f * (1.0f - t));
    }
    else if (slider == COG_SLIDER_SIZE) {
        // Native (lane aspect-ratio): the track is the size; the picture keeps that height in its own shape, at
        // most 8 m wide (the presets come here too)
        float wanted = xrScreenWidthHeld(ctx, (SCREEN_MIN_WIDTH + t * (SCREEN_MAX_WIDTH - SCREEN_MIN_WIDTH))
                                                      * xrScreenWidthScale(ctx));
        if (ctx->screenWidth > 0.01f) {
            // Keeping the arc the same shape rather than flattening as it
            // grows, same rule the corner resize follows
            ctx->screenRadius *= wanted / ctx->screenWidth;
        }
        // Centre and facing untouched, so it grows about the middle rather
        // than away from a corner
        ctx->screenWidth = wanted;
    }
}

// Which cell of a row is the one in force. Head lock is the one value this side
// does not keep: it arrives with the frame, and reading it back means the ring
// still tells the truth if something outside the panel changes it.
int cogOptionValue(XrCtx* ctx, int option, int headLocked) {
    if (option == COG_OPTION_SHARPEN) {
        return ctx->sharpenMode;
    }
    if (option == COG_OPTION_SUPERSAMPLE) {
        return ctx->supersampleMode;
    }
    if (option == COG_OPTION_HEAD_LOCK) {
        // Off, On or Follow, which is not locked and has the screen come after
        // the head
        return headLocked ? 1 : (ctx->followOn ? 2 : 0);
    }
    if (option == COG_OPTION_HEAD_AIM) {
        // The switch as it stands, whether or not it can act just now
        return headAimSwitchOn(ctx->headAimSetting, ctx->headAimFlipped);
    }
    if (option == COG_OPTION_GAMEPAD) {
        return ctx->padMode ? 1 : 0;
    }
    if (option == COG_OPTION_POINTER_SLEEP) {
        return ctx->pointerSleepOn ? 1 : 0;
    }
    if (option == COG_OPTION_RAY) {
        // The switch as it stands, with no regard for the panel that is up
        // and bringing the beam back while it is. Native: three cells, the
        // ray (with its dot), the dot alone, neither.
        return settings_pointer_cell(raySwitchOn(ctx->raySetting, ctx->rayFlipped), ctx->dotOn);
    }
    if (option == COG_OPTION_CONTROLLERS) {
        // Off, Frame, Gun; a custom model (the file's, the launcher's) has no cell
        return controllerModelKind(ctx->modelOn, ctx->modelKind);
    }
    if (option == COG_OPTION_CLICK_SOUND) {
        return ctx->clickSoundOn ? 1 : 0;
    }
    if (option == COG_OPTION_AMBILIGHT) {
        // The switch in force, which in a room is the room's own. Native (1.3.4): off in passthrough
        // until its sheet's I Understand
        return roomGlowOn(ctx, roomEffective(ctx)) && !xrPassthroughHidesGlow(ctx) ? 1 : 0;
    }
    if (option == COG_OPTION_ROOM_LIGHT) {
        return ctx->roomLightOn ? 1 : 0;
    }
    if (option == COG_OPTION_NOTICES) {
        return ctx->notices.level;
    }
    if (option == COG_OPTION_CLICK_STEADY) {
        return ctx->clickSteady;
    }
    if (option == COG_OPTION_FOVEATED) {
        // Native (1.3.4): the setting, for the next stream (auto is On); this session's own is
        // whatever its start said
        // (the cells read On, Off)
        int on = ctx->foveated == FOVEATED_AUTO ? FOVEATED_AUTO_IS_ON : ctx->foveated == FOVEATED_ON;
        return on ? 0 : 1;
    }
    if (option == COG_OPTION_STATS) {
        // Off, then the window's styles
        return ctx->overlayVisible ? 1 + ctx->statsStyle : 0;
    }
    if (option == COG_OPTION_MOUSE_MODE) {
        return ctx->mouseMode == MOUSE_MODE_RELATIVE ? 1 : 0;
    }
    return -1;
}

// Takes effect here and now, and hands back the setting id so the frame can
// tell Java to store it too. Both of these are read fresh every frame by the
// code that acts on them, so there is nothing to restart.
int cogApplyOption(XrCtx* ctx, int option, int cell) {
    if (option == COG_OPTION_SHARPEN) {
        ctx->sharpenMode = cell;
        return SETTING_SHARPEN;
    }
    if (option == COG_OPTION_SUPERSAMPLE) {
        ctx->supersampleMode = cell;
        return SETTING_SUPERSAMPLE;
    }
    if (option == COG_OPTION_STATS) {
        // Native (1.3.4): Off keeps the style for the next time; a style turns
        // the window on
        ctx->overlayVisible = cell != 0;
        if (cell > 0) {
            ctx->statsStyle = STATS_STYLE_FULL + cell - 1;
        }
        LOGEV("stats %s from the panel", cell == 0 ? "off" : cell == 1 ? "full" : cell == 2 ? "compact" : "minimal");
        return SETTING_STATS;
    }
    if (option == COG_OPTION_MOUSE_MODE) {
        // Native (1.3.4): from the next frame's motion
        xrSetMouseMode(ctx, cell != 0 ? MOUSE_MODE_RELATIVE : MOUSE_MODE_ABSOLUTE);
        LOGEV("mouse mode %s from the panel", cell != 0 ? "relative" : "absolute");
        return SETTING_MOUSE_MODE;
    }
    if (option == COG_OPTION_HEAD_LOCK) {
        // Nothing to set here: Java writes the preference it already hands down
        // every frame, and the space is picked from that value on the next one.
        ctx->followOn = cell == 2;
        if (!ctx->followOn) {
            followReset(&ctx->follow);
        }
        LOGEV("head lock %s from the panel", cell == 2 ? "follow" : cell != 0 ? "on" : "off");
        return SETTING_HEAD_LOCK;
    }
    if (option == COG_OPTION_HEAD_AIM) {
        // The bar button's switch, for this session only, so nothing goes to
        // Java to store
        setHeadAimOn(ctx, cell != 0, "the Controls tab");
        return -1;
    }
    if (option == COG_OPTION_GAMEPAD) {
        // The bar button's switch too. Gamepad puts the panel away with the
        // others, since the controllers cannot reach it any more.
        setPadMode(ctx, cell != 0, "the Controls tab");
        return -1;
    }
    if (option == COG_OPTION_POINTER_SLEEP) {
        // Set here as well as handed to Java, which hands it back down with
        // the next frame, so the ring moves on the press
        ctx->pointerSleepOn = cell != 0;
        LOGEV("pointer sleep %s from the panel", cell != 0 ? "on" : "off");
        return SETTING_POINTER_SLEEP;
    }
    if (option == COG_OPTION_RAY) {
        // Native: the row is saved (the bar button's ray switch is not): the
        // cell's ray becomes the setting each session starts from, and the
        // session's own flip from the bar is dropped so the row and the beam
        // agree. The third cell is the dot, which is stored as well.
        int ray = 0, dot = 1;
        settings_pointer_from_cell(cell, &ray, &dot);
        setRayOn(ctx, ray, "the Controls tab");
        ctx->raySetting = ray ? 1 : 0;
        ctx->rayFlipped = 0;
        if (dot != ctx->dotOn) {
            ctx->dotOn = dot;
            LOGEV("pointer dot %s from the panel", dot ? "on" : "off");
        }
        return SETTING_POINTER_DOT;
    }
    if (option == COG_OPTION_CONTROLLERS) {
        // Drawn from the next frame (loaded then if this kind is not the one in memory), and stored
        // like the rows around it
        ctx->modelKind = cell > CONTROLLER_MODEL_OFF && cell < CONTROLLER_MODEL_COUNT ? cell : CONTROLLER_MODEL_OFF;
        ctx->modelOn = ctx->modelKind != CONTROLLER_MODEL_OFF;
        LOGEV("controller model %s from the panel", controllerModelKindName(ctx->modelKind));
        return SETTING_CONTROLLER_MODEL;
    }
    if (option == COG_OPTION_CLICK_SOUND) {
        // Java plays it, and takes the switch from the setting this hands it
        ctx->clickSoundOn = cell != 0;
        LOGEV("click sound %s from the panel", cell != 0 ? "on" : "off");
        return SETTING_CLICK_SOUND;
    }
    if (option == COG_OPTION_AMBILIGHT) {
        // In a room the row is the room's own switch, anywhere else the one for
        // the void and passthrough
        int style = roomFaceStyle(ctx);
        if (style != 0) {
            ctx->roomGlow[style] = cell != 0;
            LOGEV("room %d glow %s from the panel", style, cell != 0 ? "on" : "off");
            return SETTING_ROOM_GLOW;
        }
        ctx->ambilightOn = cell != 0;
        LOGEV("ambilight %s from the panel", ctx->ambilightOn ? "on" : "off");
        return SETTING_AMBILIGHT;
    }
    if (option == COG_OPTION_ROOM_LIGHT) {
        ctx->roomLightOn = cell != 0;
        LOGEV("room light %s from the panel", ctx->roomLightOn ? "on" : "off");
        return SETTING_ROOM_LIGHT;
    }
    if (option == COG_OPTION_NOTICES) {
        // Native: takes effect for the next notice, and takes down one that
        // the new level would not have said
        noticeSetLevel(&ctx->notices, cell);
        LOGEV("notices %s from the panel", cell == NOTICES_OFF ? "off"
              : cell == NOTICES_IMPORTANT ? "important only" : "all");
        return SETTING_NOTICES;
    }
    if (option == COG_OPTION_CLICK_STEADY) {
        // Native (lane wave2-bind-wire): from the next click
        xrSetClickSteadiness(ctx, cell);
        return SETTING_CLICK_STEADY;
    }
    if (option == COG_OPTION_FOVEATED) {
        // Native (1.3.4): stored as a choice (on or off, never auto again), taken from the next stream:
        // Valve's layer is set up with the OpenXR instance
        ctx->foveated = cell == 0 ? FOVEATED_ON : FOVEATED_OFF;   // the cells read On, Off
        LOGEV("foveated rendering %s from the panel, from the next stream", cell == 0 ? "on" : "off");
        // Native (1.3.4, owner's request): said on the toast mid-stream, with how to get it when it differs
        // from what this stream runs. Not over the headset's own list: the stream it starts takes it.
        if (ctx->launcher == LAUNCHER_NONE) {
            int on = cell == 0;
            noticePush(&ctx->notices, on ? TOAST_FOVEATED_ON : TOAST_FOVEATED_OFF,
                       ctx->foveatedStartKnown && on != ctx->foveatedStartOn);
        }
        return SETTING_FOVEATED;
    }
    return -1;
}

// Which cell of a row of cells is in force, or -1 where none is. A separation
// dragged to somewhere between the presets is none of them, so their row
// carries no ring.
int cogCellInForce(XrCtx* ctx, int face, int row) {
    int option = cogRowOption(face, row);
    if (option >= 0) {
        return cogOptionValue(ctx, option, ctx->headLockedPref);
    }
    if (face == COG_TAB_SCREEN && row == COG_SCREEN_ROW_PRESET) {
        return xrScreenPresetNow(ctx);
    }
    if (face == COG_TAB_SCREEN && row == COG_SCREEN_ROW_PER_APP) {
        return ctx->perAppOn ? 1 : 0;
    }
    if (face == COG_TAB_SCREEN && row == COG_SCREEN_ROW_LOCK) {
        return ctx->angularLockOn ? 1 : 0;
    }
    if (face == COG_TAB_3D && row == COG_ROW3D_PRESET) {
        return cogPresetAt(separationUnits(ctx->separationCurrent), ctx->presetUnits);
    }
    if (face == COG_TAB_3D && row == COG_ROW3D_SWITCH) {
        return ctx->stereoLive ? 1 : 0;
    }
    if (face == COG_TAB_3D && row == COG_ROW3D_SOURCE) {
        return ctx->stereoSource;
    }
    if (face == COG_TAB_3D && row == COG_ROW3D_MODEL) {
        int source = xrDepthModelSource(ctx);
        return source == DEPTH_SOURCE_LIGHT ? COG_MODEL_LIGHT : source == DEPTH_SOURCE_FULL ? COG_MODEL_FULL : -1;
    }
    return -1;
}

int xrPassthroughHidesModels(XrCtx* ctx) {
    return ctx->passthrough && !ctx->ptControllersOk;
}

int xrPassthroughHidesGlow(XrCtx* ctx) {
    return ctx->passthrough && !ctx->ptGlowOk;
}

void ptSheetOpen(XrCtx* ctx, int kind) {
    ctx->cogOpen = 0;
    ctx->hintOpen = 1;
    ctx->hintKind = kind;
    ctx->hintHoverZone = HINT_ZONE_NONE;
    ctx->hintPose = handHintPose(ctx, &ctx->hintW, &ctx->hintH);
    ctx->pointerAwake = 1;
    ctx->stillFor = 0.0f;
    LOGEV("passthrough: %s asks first, the sheet is up", kind == HINT_KIND_PT_GLOW ? "the glow" : "Show controllers");
}

void ptSheetAnswer(XrCtx* ctx, int yes, float* out) {
    int glow = ctx->hintKind == HINT_KIND_PT_GLOW;
    ctx->hintOpen = 0;
    ctx->hintHoverZone = HINT_ZONE_NONE;
    if (yes && glow) {
        // I Understand turns the glow on as the press asked, and it stays allowed in passthrough
        ctx->ptGlowOk = 1;
        ctx->ambilightOn = 1;
        out[IN_SETTING] = (float)SETTING_PT_GLOW_OK;
        out[IN_SETTING_VALUE] = 1.0f;
    }
    else if (yes) {
        // The model Show controllers names is drawn in passthrough from the next frame (Off draws none)
        ctx->ptControllersOk = 1;
        out[IN_SETTING] = (float)SETTING_PT_CONTROLLERS_OK;
        out[IN_SETTING_VALUE] = 1.0f;
    }
    LOGEV("passthrough: %s %s", glow ? "the glow" : "Show controllers",
          yes ? "understood, drawn in passthrough too" : "cancelled, it stays off in passthrough");
    ctx->cogOpen = 1;
    ctx->cogPose = cogPanelPose(ctx, &ctx->cogW, &ctx->cogH);
}

void depthFullSheetOpen(XrCtx* ctx) {
    ctx->cogOpen = 0;
    ctx->hintOpen = 1;
    ctx->hintKind = HINT_KIND_DEPTH_FULL;
    ctx->hintHoverZone = HINT_ZONE_NONE;
    ctx->hintPose = handHintPose(ctx, &ctx->hintW, &ctx->hintH);
    // The answer wants a ray to aim with
    ctx->pointerAwake = 1;
    ctx->stillFor = 0.0f;
    LOGEV("3d: ZipDepth Full asks first, the sheet is up");
}

void depthFullSheetAnswer(XrCtx* ctx, int yes, float* out) {
    // (hintKind stays, so the sheet fades out with its own words)
    ctx->hintOpen = 0;
    ctx->hintHoverZone = HINT_ZONE_NONE;
    if (yes && xrDepthChangeModel(ctx, DEPTH_SOURCE_FULL)) {
        out[IN_SETTING] = (float)SETTING_DEPTH_SOURCE;
        out[IN_SETTING_VALUE] = (float)DEPTH_SOURCE_FULL;
    }
    LOGEV("3d: ZipDepth Full %s", yes ? "confirmed" : "cancelled, the model stays");
    // Back to the tab the question came from
    ctx->cogOpen = 1;
    ctx->cogPose = cogPanelPose(ctx, &ctx->cogW, &ctx->cogH);
}

// The switch is the bar button's, for this session only, so nothing goes to
// Java to store. A preset writes its separation the way letting go of the
// track there would, so it goes to the preference by the same road, and it
// brings the 3D back if it was off, since a strength is something to see.
static void cogApply3dCell(XrCtx* ctx, int row, int cell, float* out) {
    static const char* const PRESET_NAMES[COG_PRESET_CELLS] = {
        "Comfort", "Balanced", "Strong"
    };
    if (row == COG_ROW3D_SWITCH) {
        setStereoLive(ctx, cell != 0, "the 3D tab");
        return;
    }
    // Native (1.3.3, issue #6): the depth model. Light at once; Full, which
    // works the CPU much harder, after the sheet's question
    if (row == COG_ROW3D_MODEL) {
        if (cell == cogCellInForce(ctx, COG_TAB_3D, row) || cell < 0 || cell >= COG_MODEL_CELLS) {
            return;
        }
        if (cell == COG_MODEL_FULL) {
            depthFullSheetOpen(ctx);
            return;
        }
        if (xrDepthChangeModel(ctx, DEPTH_SOURCE_LIGHT)) {
            out[IN_SETTING] = (float)SETTING_DEPTH_SOURCE;
            out[IN_SETTING_VALUE] = (float)DEPTH_SOURCE_LIGHT;
        }
        return;
    }
    // Native (PLAN 4.3): where the 3D comes from, at once, and stored
    if (row == COG_ROW3D_SOURCE) {
        if (cell >= 0 && cell < COG_SOURCE_CELLS) {
            xrDepthSetSource(ctx, cell);
            out[IN_SETTING] = (float)SETTING_STEREO_SOURCE;
            out[IN_SETTING_VALUE] = (float)cell;
        }
        return;
    }
    if (row != COG_ROW3D_PRESET || cell < 0 || cell >= COG_PRESET_CELLS) {
        return;
    }
    int units = ctx->presetUnits[cell];
    ctx->panelSeparation = separationOf(units);
    ctx->separationCurrent = ctx->panelSeparation;
    out[IN_SETTING] = (float)SETTING_SEPARATION;
    out[IN_SETTING_VALUE] = (float)units;
    LOGEV("3d preset %s from the panel, separation %d", PRESET_NAMES[cell], units);
    setStereoLive(ctx, 1, "a preset");
}

int xrScreenPresetNow(XrCtx* ctx) {
    XrVector3f p = ctx->screenPose.position;
    float d = sqrtf(p.x * p.x + p.y * p.y + p.z * p.z);
    return layoutPresetAt((int)lroundf(d * 100.0f), (int)lroundf(ctx->screenWidth / xrScreenWidthScale(ctx) * 100.0f));
}

// A preset puts the distance and the width where it says, by the same sums the
// two tracks use, keeping the direction, height, tilt and curve, and is saved
// with the placement. The Screen tab's cells and the bar's Bring here both
// come here.
void xrScreenApplyPreset(XrCtx* ctx, int preset) {
    static const char* const PRESET_NAMES[LAYOUT_PRESETS] = { "Desk", "Couch", "Cinema" };
    if (preset < 0 || preset >= LAYOUT_PRESETS) {
        return;
    }
    LayoutPreset p = layoutPresetOf(preset);
    float d = (float)p.distanceCm / 100.0f;
    float w = (float)p.widthCm / 100.0f;
    screenSlider(ctx, COG_SLIDER_DISTANCE,
                 cogRunU((d - COG_DIST_MIN) / (COG_DIST_MAX - COG_DIST_MIN)));
    screenSlider(ctx, COG_SLIDER_SIZE,
                 cogRunU((w - SCREEN_MIN_WIDTH) / (SCREEN_MAX_WIDTH - SCREEN_MIN_WIDTH)));
    ctx->poseDirty = 1;
    LOGEV("screen preset %s: %.2f m away, %.2f m wide", PRESET_NAMES[preset], d, w);
}

// The Screen tab's cells. A preset is xrScreenApplyPreset. Bring here goes to
// xrBringScreenHere. The switches go to Java to store like the option rows.
static void cogApplyScreenCell(XrCtx* ctx, int row, int cell, float* out) {
    if (row == COG_SCREEN_ROW_PRESET) {
        if (cell == COG_SCREEN_CELL_BRING) {
            LOGEV("bring here from the panel: %s", xrBringScreenHere(ctx) ? "moved" : "nothing to move");
            return;
        }
        LOGEV("screen preset cell %d from the panel", cell);
        xrScreenApplyPreset(ctx, cell);
        return;
    }
    if (row == COG_SCREEN_ROW_PER_APP) {
        if (cell == COG_SCREEN_CELL_FORGET) {
            out[IN_SETTING] = (float)SETTING_FORGET_LAYOUT;
            out[IN_SETTING_VALUE] = 0.0f;
            LOGEV("forget the remembered layout from the panel");
            return;
        }
        ctx->perAppOn = cell != 0;
        out[IN_SETTING] = (float)SETTING_PER_APP;
        out[IN_SETTING_VALUE] = (float)cell;
        LOGEV("remember per app %s from the panel", cell != 0 ? "on" : "off");
        return;
    }
    if (row == COG_SCREEN_ROW_LOCK) {
        ctx->angularLockOn = cell != 0;
        out[IN_SETTING] = (float)SETTING_ANGULAR_LOCK;
        out[IN_SETTING_VALUE] = (float)cell;
        LOGEV("angular-size lock %s from the panel", cell != 0 ? "on" : "off");
    }
}

// A press on a cell, whichever tab it is on, applied here and now and handed
// to Java to store in the same frame
void cogApplyCell(XrCtx* ctx, int face, int row, int cell, float* out) {
    int option = cogRowOption(face, row);
    if (option < 0) {
        if (face == COG_TAB_SCREEN) {
            cogApplyScreenCell(ctx, row, cell, out);
        }
        else if (face == COG_TAB_3D) {
            cogApply3dCell(ctx, row, cell, out);
        }
        else if (face == COG_TAB_ACCESS) {
            cogApplyAccessCell(ctx, row, cell, out);   // native (lane a11y-tab), xr_assist.c
        }
        return;
    }
    // Native (1.3.4): in passthrough, Show controllers (greyed, "off in passthrough") and the glow's On ask
    // first, on the hint sheet; nothing changes until I Understand
    if (option == COG_OPTION_CONTROLLERS && xrPassthroughHidesModels(ctx)) {
        ptSheetOpen(ctx, HINT_KIND_PT_CONTROLLERS);
        return;
    }
    if (option == COG_OPTION_AMBILIGHT && cell != 0 && roomFaceStyle(ctx) == 0 && xrPassthroughHidesGlow(ctx)) {
        ptSheetOpen(ctx, HINT_KIND_PT_GLOW);
        return;
    }
    int id = cogApplyOption(ctx, option, cell);
    if (id >= 0) {
        out[IN_SETTING] = (float)id;
        out[IN_SETTING_VALUE] = (float)cell;
    }
}

// The picture rows' settings and names, in the PICTURE_ order
static const int PICTURE_SETTINGS[PICTURE_VALUES] = {
    SETTING_PICTURE_BRIGHTNESS, SETTING_PICTURE_CONTRAST, SETTING_PICTURE_GAMMA,
    SETTING_PICTURE_SATURATION
};
static const char* const PICTURE_NAMES[PICTURE_VALUES] = {
    "brightness", "contrast", "gamma", "saturation"
};

// Letting go of a slider, either on purpose or because focus went away mid
// drag. Persisting where it ended up rather than every frame on the way there
// is the same policy a grab uses, so this is where the writing happens.
void cogDragEnded(XrCtx* ctx, float* out) {
    int slider = ctx->cogDragSlider;
    int face = ctx->cogDragFace;
    ctx->cogDragSlider = -1;
    ctx->cogDragHand = -1;
    ctx->cogDragFace = -1;
    ctx->cogDragByGaze = 0;

    if (slider < 0) {
        return;
    }
    if (face == COG_TAB_SCREEN && ctx->cogDragRoom > 0) {
        // Size in a room, written under it. A room gone from under the drag
        // leaves nothing to write it to.
        int style = roomFaceStyle(ctx);
        if (style == ctx->cogDragRoom && slider == COG_SLIDER_SIZE && roomResizable(style)) {
            out[IN_SETTING] = (float)SETTING_ROOM_SCREEN;
            out[IN_SETTING_VALUE] = (float)roomScreenPercent(ctx, style);
            LOGEV("room %d screen percent %d from the panel", style, roomScreenPercent(ctx, style));
        }
    }
    else if (face == COG_TAB_SCREEN) {
        // The placement is saved from the pose the frame hands back
        ctx->poseDirty = 1;
    }
    else if (face == COG_TAB_ROOM) {
        // A room's own in its units, written under the room showing. A room
        // gone from under the drag leaves nothing to write it to.
        int style = roomFaceStyle(ctx);
        int setting = -1;
        int value = 0;
        if (slider == COG_ROOM_ROW_BG_DIM) {
            setting = SETTING_BG_DIM;
            value = ctx->bgDim;
            LOGEV("background dim %d%% from the panel", value);
        }
        else if (slider == COG_ROOM_ROW_GLOW_LEVEL) {
            // Whole percent, the preference's units
            setting = SETTING_AMBI_LEVEL;
            value = (int)roundf(ctx->ambiIntensity * 100.0f);
        }
        else if (slider == COG_ROOM_ROW_GLOW_SPREAD) {   // native (issue #7)
            setting = SETTING_GLOW_SPREAD;
            value = ctx->glowSpread;
            LOGEV("glow spread %d%% from the panel", value);
        }
        else if (style != 0 && slider == COG_ROOM_ROW_BRIGHTNESS) {
            setting = SETTING_ROOM_BRIGHTNESS;
            value = ctx->roomBrightness[style];
            LOGEV("room %d brightness %d from the panel", style, value);
        }
        else if (style != 0 && slider == COG_ROOM_ROW_LIGHT_LEVEL) {
            setting = SETTING_ROOM_LIGHT_LEVEL;
            value = ctx->roomLightLevel[style];
            LOGEV("room %d light level %d from the panel", style, value);
        }
        if (setting >= 0) {
            out[IN_SETTING] = (float)setting;
            out[IN_SETTING_VALUE] = (float)value;
        }
    }
    else if (face == COG_TAB_CONTROLS) {
        if (slider == COG_CONTROLS_ROW_SCROLL) {
            out[IN_SETTING] = (float)SETTING_SCROLL_SPEED;
            out[IN_SETTING_VALUE] = (float)ctx->scrollSpeed;
            LOGEV("scroll speed %d%% from the panel", ctx->scrollSpeed);
        }
        else if (slider == COG_CONTROLS_ROW_AIM_SENSITIVITY) {
            out[IN_SETTING] = (float)SETTING_HEAD_AIM_SENSITIVITY;
            out[IN_SETTING_VALUE] = (float)ctx->headAimSensitivity;
            LOGEV("head aim %d px a degree from the panel", ctx->headAimSensitivity);
        }
        else if (slider == COG_CONTROLS_ROW_AIM_DEADZONE) {
            out[IN_SETTING] = (float)SETTING_HEAD_AIM_DEADZONE;
            out[IN_SETTING_VALUE] = (float)ctx->headAimDeadZone;
            LOGEV("head aim dead zone %d deg/s from the panel", ctx->headAimDeadZone);
        }
    }
    else if (face == COG_TAB_3D) {
        if (slider == COG_ROW3D_SEPARATION) {
            out[IN_SETTING] = (float)SETTING_SEPARATION;
            // Tenths of a percent of frame width, the preference's units
            out[IN_SETTING_VALUE] = (float)separationUnits(ctx->separationCurrent);
        }
        else if (slider == COG_ROW3D_CONVERGENCE) {
            out[IN_SETTING] = (float)SETTING_CONVERGENCE;
            out[IN_SETTING_VALUE] = roundf(ctx->convergence * 100.0f);
        }
        else if (slider == COG_ROW3D_EDGE) {
            out[IN_SETTING] = (float)SETTING_EDGE;
            out[IN_SETTING_VALUE] = roundf(ctx->depthSharp * 100.0f);
        }
        else if (slider == COG_ROW3D_DETAIL) {
            out[IN_SETTING] = (float)SETTING_DEPTH_DETAIL;
            out[IN_SETTING_VALUE] = roundf(ctx->depthLocal * 100.0f);
        }
        else if (slider == COG_ROW3D_SHAPE) {
            out[IN_SETTING] = (float)SETTING_DEPTH_SHAPE;
            out[IN_SETTING_VALUE] = roundf(ctx->depthGlobal * 100.0f);
        }
        else if (slider == COG_ROW3D_MOTION) {
            out[IN_SETTING] = (float)SETTING_MOTION_RESET;
            out[IN_SETTING_VALUE] = roundf(ctx->depthMotion * 100.0f);
        }
        else if (slider == COG_ROW3D_BALANCE) {
            out[IN_SETTING] = (float)SETTING_EYE_BALANCE;
            out[IN_SETTING_VALUE] = roundf(ctx->eyeBalance * 100.0f);
        }
    }
    else if (face == COG_TAB_ACCESS) {
        cogAccessDragEnded(ctx, slider, out);   // native (lane a11y-2): Dot size
    }
    else if (face == COG_TAB_DISPLAY && slider < PICTURE_VALUES) {
        out[IN_SETTING] = (float)PICTURE_SETTINGS[slider];
        out[IN_SETTING_VALUE] = (float)ctx->pictureUnits[slider];
        LOGEV("picture %s %d from the panel, grade %s", PICTURE_NAMES[slider],
              ctx->pictureUnits[slider], ctx->gradeOn ? "on" : "off");
    }
}

// A press on one of a track's step buttons: one step that way, applied at once
// the way a drag is and written at once the way letting go of one is. Held
// down it does not repeat.
void cogStepTrack(XrCtx* ctx, int face, int row, int dir, float* out) {
    // Size in a room moves in whole percent of the room's screen
    int steps = cogSizeIsRooms(ctx, face, row) ? ROOM_SCREEN_MAX - ROOM_SCREEN_MIN : cogTrackSteps(face, row);
    if (steps <= 0) {
        return;
    }
    float before = cogSliderValue(ctx, face, row);
    if (face == COG_TAB_CONTROLS && row == COG_CONTROLS_ROW_SCROLL) {
        // Its places are uneven, and a speed the file set past an end stays
        // there for a step further out
        xrSetScrollSpeed(ctx, scrollTrackStep(ctx->scrollSpeed, dir));
    }
    else {
        int step = cogStepIndex(before, steps, dir);
        cogApplySlider(ctx, face, row, cogRunU((float)step / (float)steps));
    }
    float after = cogSliderValue(ctx, face, row);
    LOGEV("track step %s: face %d row %d, step %.2f to %.2f of %d", dir > 0 ? "up" : "down",
          face, row, before * steps, after * steps, steps);
    ctx->cogDragSlider = row;
    ctx->cogDragFace = face;
    ctx->cogDragRoom = roomEffective(ctx);
    ctx->cogDragHand = -1;
    cogDragEnded(ctx, out);
}

// Which cell of a row the ray is on, or -1 off the ends
int cogCellAt(float pu, int cells) {
    if (pu < COG_TRACK_L || pu > COG_TRACK_R) {
        return -1;
    }
    int cell = (int)((pu - COG_TRACK_L) / (COG_TRACK_R - COG_TRACK_L) * cells);
    if (cell < 0) cell = 0;
    if (cell >= cells) cell = cells - 1;
    return cell;
}

// What is said beside the tracks of the tab showing, in IN_READOUT order:
// which tab it is for, or -1 first while none is up, then its values and -1
// past them or for a row that is greyed. The Room tab's are Background dim,
// Glow level, Glow spread, then the room's brightness and light level as the
// place along their lanes; the Display tab's the picture's four in their own
// units; the Screen tab's the room's size in percent of its screen; the
// Controls tab's the scroll speed, then head aim's pixels a degree and dead
// zone.
void cogReadouts(XrCtx* ctx, int* values) {
    for (int i = 0; i < READOUT_VALUES; i++) {
        values[i] = -1;
    }
    // Still up while the panel fades out, so the strip goes with it
    int showing = ctx->cogOpen || ctx->panelFades[FADE_COG].level > 0.0f;
    if (!showing) {
        return;
    }
    int tab = ctx->cogTab;
    int style = roomFaceStyle(ctx);
    if (tab == COG_TAB_DISPLAY) {
        values[0] = READOUT_DISPLAY;
        for (int row = 0; row < PICTURE_VALUES; row++) {
            values[1 + row] = ctx->pictureUnits[row];
        }
    }
    else if (tab == COG_TAB_SCREEN && style != 0) {
        values[0] = READOUT_SCREEN;
        values[1] = roomResizable(style) ? roomScreenPercent(ctx, style) : -1;
    }
    else if (tab == COG_TAB_CONTROLS) {
        values[0] = READOUT_CONTROLS;
        values[1] = ctx->scrollSpeed;
        if (cogRowLive(ctx, tab, COG_CONTROLS_ROW_AIM_SENSITIVITY)) {
            values[2] = ctx->headAimSensitivity;
            values[3] = ctx->headAimDeadZone;
        }
    }
    else if (tab == COG_TAB_ROOM) {
        values[0] = READOUT_ROOM;
        values[1] = ctx->bgDim;
        values[2] = (int)roundf(ctx->ambiIntensity * 100.0f);
        values[3] = ctx->glowSpread;
        if (style != 0) {
            values[4] = lanePercent(ctx->roomBrightness[style], ROOM_BRIGHTNESS_MIN, ROOM_BRIGHTNESS_MAX);
            values[5] = lanePercent(ctx->roomLightLevel[style], ROOM_LIGHT_MIN, ROOM_LIGHT_MAX);
        }
    }
}

// Held on the depth track, which is all the panel can show or write
static int onSeparationTrack(int units) {
    return units < 0 ? 0 : (units > COG_SEP_STEPS ? COG_SEP_STEPS : units);
}

// The running model's own pair and the separations its presets write, all in
// the preferences' units and the presets in cell order, handed down once
// before the first frame. The 3D tab's reset goes back to the pair.
void xrSetDepthDefaults(XrCtx* ctx, int separation, int convergence, const int* presets) {
    if (ctx == NULL) {
        return;
    }
    int units = onSeparationTrack(separation);
    int percent = convergence < 0 ? 0 : (convergence > 100 ? 100 : convergence);
    ctx->defaultSeparation = separationOf(units);
    ctx->defaultConvergence = percent / 100.0f;
    if (presets != NULL) {
        for (int i = 0; i < COG_PRESET_CELLS; i++) {
            ctx->presetUnits[i] = onSeparationTrack(presets[i]);
        }
    }
    LOGEV("3d defaults: separation %d, convergence %d, presets %d %d %d", units, percent,
          ctx->presetUnits[COG_PRESET_COMFORT], ctx->presetUnits[COG_PRESET_BALANCED],
          ctx->presetUnits[COG_PRESET_STRONG]);
}

// Whether a press ticks, which only the Controls tab's ring reads on this
// side. Handed down before the first frame.
void xrSetClickSound(XrCtx* ctx, int on) {
    if (ctx != NULL) {
        ctx->clickSoundOn = on;
    }
}

// Whether the ray shows, the setting each session starts from, which the bar
// button and the Controls tab's row then turn over for the session. Handed
// down before the first frame.
void xrSetShowRay(XrCtx* ctx, int on) {
    if (ctx == NULL) {
        return;
    }
    ctx->raySetting = on ? 1 : 0;
    ctx->rayFlipped = 0;
    LOGEV("ray %s at the start of the session", on ? "shown" : "hidden");
}

// Head aim's setting, which each session starts from, and its pixels a degree
// and dead zone in degrees a second, each held to its lane. Handed down before
// the first frame.
void xrSetHeadAim(XrCtx* ctx, int on, int sensitivity, int deadZone) {
    if (ctx == NULL) {
        return;
    }
    ctx->headAimSetting = on ? 1 : 0;
    ctx->headAimFlipped = 0;
    ctx->headAimSensitivity = headAimSensitivityClamp(sensitivity);
    ctx->headAimDeadZone = headAimDeadZoneClamp(deadZone);
    LOGEV("head aim %s at the start of the session, %d px a degree, dead zone %d deg/s",
          on ? "on" : "off", ctx->headAimSensitivity, ctx->headAimDeadZone);
}

// The shortcut that switches gamepad mode, said the way the toast says it
const char* padShortcutName(int shortcut) {
    switch (shortcut) {
        case PAD_SHORTCUT_STICKS: return "both thumbsticks";
        case PAD_SHORTCUT_TRIGGERS_GRIPS: return "both triggers and both grips";
        default: return "the left View button and grip";
    }
}

// Gamepad mode's shortcut and the sticks' dead zone in the whole percent the
// settings keep for a real pad. Handed down before the first frame. Every
// session starts as the pointer: the mode is only ever entered from inside
// one, where the toast says how to get back.
void xrSetGamepad(XrCtx* ctx, int shortcut, int deadzonePercent) {
    if (ctx == NULL) {
        return;
    }
    ctx->padMode = 0;
    ctx->padShortcut = shortcut == PAD_SHORTCUT_STICKS || shortcut == PAD_SHORTCUT_TRIGGERS_GRIPS
            ? shortcut : PAD_SHORTCUT_MENU_GRIP;
    ctx->padDeadzone = padDeadzoneFromPercent(deadzonePercent);
    LOGEV("controllers start as the pointer, gamepad mode shortcut %s, stick dead zone %.0f%%",
          padShortcutName(ctx->padShortcut), ctx->padDeadzone * 100.0f);
}

// What the grips are on the pad, a PAD_GRIP_ mode (the pad_grip setting)
// Native (lane wave2-bind-wire): the press fix and the click hold as the
// level has them, with any MLXR_ variable that is set still deciding its part
void xrSetClickSteadiness(XrCtx* ctx, int level) {
    if (ctx == NULL) {
        return;
    }
    ctx->clickSteady = level < CLICK_STEADY_OFF ? CLICK_STEADY_OFF
                     : level > CLICK_STEADY_STEADY ? CLICK_STEADY_STEADY : level;
    clickSteadyConfig(ctx->clickSteady, 1, &ctx->pressCfg, &ctx->clickHoldPx);
    pressFixReset(&ctx->pressFix);
    clickHoldReset(&ctx->clickHold);
    LOGEV("click steadiness %s: press stabilise %d, double click %.0f px %lld ms, click hold %.0f px",
          ctx->clickSteady == CLICK_STEADY_OFF ? "off"
          : ctx->clickSteady == CLICK_STEADY_STEADY ? "steady" : "normal",
          ctx->pressCfg.stabilise, ctx->pressCfg.doublePx,
          (long long)(ctx->pressCfg.doubleNs / 1000000LL), ctx->clickHoldPx);
}

void xrSetScrollSpeed(XrCtx* ctx, int percent) {
    if (ctx == NULL) {
        return;
    }
    ctx->scrollSpeed = scrollSpeedClamp(percent);
}

void xrSetPadGrip(XrCtx* ctx, int mode) {
    if (ctx == NULL) {
        return;
    }
    ctx->padGripMode = padGripModeClamp(mode);
    LOGEV("gamepad mode grips: %s", ctx->padGripMode == PAD_GRIP_NONE ? "nothing"
          : ctx->padGripMode == PAD_GRIP_STICK_CLICKS ? "the stick clicks" : "the bumpers");
}

// One row of the picture grade to a value in its own whole units, held to its
// lane, with the grade worked out again. Off once every row is back at its
// default, which is all the shaders look at before skipping it.
void pictureSet(XrCtx* ctx, int row, int units) {
    if (row < 0 || row >= PICTURE_VALUES) {
        return;
    }
    ctx->pictureUnits[row] = pictureClamp(row, units);
    ctx->grade = pictureGradeFor(ctx->pictureUnits);
    ctx->gradeOn = !pictureNeutral(ctx->pictureUnits);
}

// The Display tab's reset: all four back to the picture as streamed, which
// turns the grade off
void pictureReset(XrCtx* ctx) {
    for (int row = 0; row < PICTURE_VALUES; row++) {
        pictureSet(ctx, row, pictureDefault(row));
    }
    LOGEV("picture reset from the panel, grade %s", ctx->gradeOn ? "on" : "off");
}

// The Room tab's Reset: what is around the picture, wherever it is
void surroundingsReset(XrCtx* ctx) {
    ctx->bgDim = BG_DIM_DEFAULT;
    ctx->ambiIntensity = (float)GLOW_LEVEL_DEFAULT / 100.0f;
    ctx->glowSpread = GLOW_SPREAD_DEFAULT;   // native (issue #7)
    LOGEV("surroundings reset from the panel: background dim %d%%, glow level %d%%, glow spread %d%%",
          ctx->bgDim, GLOW_LEVEL_DEFAULT, ctx->glowSpread);
}

// The Screen tab's Reset in a room: the room's own size, under the room
// showing. The placement waiting behind the room is left alone.
int roomSizeReset(XrCtx* ctx, float* out) {
    int style = roomFaceStyle(ctx);
    if (style == 0 || !roomResizable(style)) {
        return 0;
    }
    ctx->roomScreen[style] = (int)roundf(roomModel(style)->screenFraction * 100.0f);
    out[IN_SETTING] = (float)SETTING_ROOM_SCREEN;
    out[IN_SETTING_VALUE] = (float)ctx->roomScreen[style];
    LOGEV("room %d screen percent reset to %d from the panel", style, ctx->roomScreen[style]);
    return 1;
}

// The four picture values from the preferences, in PICTURE_ order, handed down
// before the first frame
void xrSetPicture(XrCtx* ctx, const int* units) {
    if (ctx == NULL || units == NULL) {
        return;
    }
    for (int row = 0; row < PICTURE_VALUES; row++) {
        pictureSet(ctx, row, units[row]);
    }
    LOGEV("picture: brightness %d, contrast %d, gamma %d, saturation %d, grade %s",
          ctx->pictureUnits[PICTURE_BRIGHTNESS], ctx->pictureUnits[PICTURE_CONTRAST],
          ctx->pictureUnits[PICTURE_GAMMA], ctx->pictureUnits[PICTURE_SATURATION],
          ctx->gradeOn ? "on" : "off");
}
