// The bundled controller model: one generic controller, baked like the rooms
// and drawn at each hand's grip, lit by a fixed light. Its own pass and its
// own projection layer, cleared to nothing round the models and put up over
// the picture and the panels, so a controller held up in front of the screen
// is never lost behind it. Only the beam, the dot and what hangs off the head
// go over it.
//
// Moonlight XR Native: ported from the Steam Frame fork of Moonlight XR's
// xr-renderer/xr_model.c (Gilleece, GPLv3). updateControllerModels is
// upstream's as written; initModelPass, locateModelViews and
// renderControllerModels keep upstream's order and rules. Native, each marked:
//   - the model is rooms/room_gpu's MlxrModel, read off the assets the first
//     frame the switch is on (xr_world.c; upstream: Java read it and
//     nativeUploadControllerModel took it). The Steam Frame fork ships no
//     Frame controller model in plain asset form, so by default this is the
//     generic controller (assets/models/controller.room), mirrored for the
//     left hand. When tools/frame-models has made the Frame's own pair from
//     the local SteamVR install (never shipped, packaging/install.sh makes
//     them) each hand draws its own, already in its own grip space and so not
//     mirrored: textured at full detail (frame_controller_{left,right}.model,
//     MLXR_PASS_MODEL_TEX: colour, occlusion, normal and roughness maps) or,
//     from an older converter, painted (frame_controller_{left,right}.room);
//   - the draw is mlxr_pass_model into the swapchain image, recorded into the
//     frame's command buffer, with the depth image the room's pass uses too;
//   - the GL timer query is the host's timestamps (xr_world.c).
#include "xr_world.h"

// Everything the pass draws with, the first frame a model shows, so a session
// that never shows one never makes any of it. The image is the room's size,
// a half per eye, made mid session the way the room's is. One failure is
// enough to stop asking.
static int initModelPass(XrCtx* ctx) {
    if (ctx->modelPassReady) {
        return 1;
    }
    if (ctx->modelPassFailed || ctx->session == XR_NULL_HANDLE || ctx->passes == NULL) {
        return 0;
    }
    ctx->modelPassFailed = 1;

    int eyeW;
    int eyeH;
    worldEyeSize(ctx, &eyeW, &eyeH);
    // Native (lane eye-swapchains): a swapchain per eye, as the room's
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        if (!createArtSwapchain(ctx, eyeW, eyeH, "create controller model swapchain", &ctx->modelSwapchain[eye],
                                &ctx->modelImages[eye], &ctx->modelImageViews[eye], &ctx->modelImageCount[eye])) {
            return 0;
        }
    }
    LOGEV("swapchains alive %d with the controller models' two (one per eye)", ctx->swapchainsAlive);
    // Native: the textured pair's own pipeline when that is what loaded
    mlxr_passes_warm(ctx->passes, ctx->model && ctx->model->textured ? MLXR_PASS_MODEL_TEX : MLXR_PASS_MODEL,
                     MLXR_VIDEO_YUV420P, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_D32_SFLOAT);

    ctx->modelEyeWidth = eyeW;
    ctx->modelEyeHeight = eyeH;
    ctx->modelPassReady = 1;
    ctx->modelPassFailed = 0;
    LOGEV("controller model pass ready at %dx%d per eye, a swapchain each", eyeW, eyeH);
    return 1;
}

// Where both eyes are this frame, for the models' own layer. A frame they
// cannot be placed on keeps the image it has.
static int locateModelViews(XrCtx* ctx) {
    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = ctx->predictedDisplayTime;
    locateInfo.space = ctx->localSpace;
    XrViewState state = { XR_TYPE_VIEW_STATE };
    XrView views[ROOM_EYES];
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        views[eye].type = XR_TYPE_VIEW;
        views[eye].next = NULL;
    }
    uint32_t count = 0;
    if (XR_FAILED(xrLocateViews(ctx->session, &locateInfo, &state, ROOM_EYES, &count, views))
            || count < ROOM_EYES) {
        return 0;
    }
    XrViewStateFlags needed = XR_VIEW_STATE_ORIENTATION_VALID_BIT
            | XR_VIEW_STATE_POSITION_VALID_BIT;
    if ((state.viewStateFlags & needed) != needed) {
        return 0;
    }
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        ctx->modelViews[eye] = views[eye];
    }
    return 1;
}

// Native: one control's current value, for the model to follow. Read-only: the
// pointer, pad and shortcuts in xr_input.c read the same actions their own way.
static float modelActionFloat(XrCtx* ctx, XrAction action, int h) {
    if (action == XR_NULL_HANDLE) {
        return 0.0f;
    }
    XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
    get.action = action;
    get.subactionPath = ctx->handPaths[h];
    XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
    if (XR_FAILED(xrGetActionStateFloat(ctx->session, &get, &state)) || !state.isActive) {
        return 0.0f;
    }
    return state.currentState;
}

static float modelActionBool(XrCtx* ctx, XrAction action, int h) {
    if (action == XR_NULL_HANDLE) {
        return 0.0f;
    }
    XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
    get.action = action;
    get.subactionPath = ctx->handPaths[h];
    XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
    if (XR_FAILED(xrGetActionStateBoolean(ctx->session, &get, &state)) || !state.isActive) {
        return 0.0f;
    }
    return state.currentState ? 1.0f : 0.0f;
}

// Native: the controls of one hand, by what the Frame's bindings (xr_input.c
// suggestFrameBindings) put on each action: right A/B on the right/middle
// click actions, X, Y, Menu on their pad actions; left d-pad down/right on
// the same two clicks, up/left on the pad ones, View on the menu action.
_Static_assert(sizeof(MlxrModelInput) <= sizeof(((XrCtx*)0)->modelInput[0]), "XrCtx.modelInput is too small");

static void readModelInput(XrCtx* ctx, int h, MlxrModelInput* in) {
    memset(in, 0, sizeof(*in));
    in->v[MLXR_IN_TRIGGER] = modelActionFloat(ctx, ctx->triggerAction, h);
    in->v[MLXR_IN_GRIP] = modelActionFloat(ctx, ctx->grabAction, h);
    XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
    get.action = ctx->scrollAction;
    get.subactionPath = ctx->handPaths[h];
    XrActionStateVector2f stick = { XR_TYPE_ACTION_STATE_VECTOR2F };
    if (ctx->scrollAction != XR_NULL_HANDLE && XR_SUCCEEDED(xrGetActionStateVector2f(ctx->session, &get, &stick))
            && stick.isActive) {
        in->v[MLXR_IN_STICK_X] = stick.currentState.x;
        in->v[MLXR_IN_STICK_Y] = stick.currentState.y;
    }
    in->v[MLXR_IN_STICK_CLICK] = modelActionBool(ctx, ctx->toggleAction, h);
    in->v[MLXR_IN_BUMPER] = modelActionBool(ctx, ctx->padBumperAction, h);
    if (h == HAND_RIGHT) {
        in->v[MLXR_IN_A] = modelActionBool(ctx, ctx->rightClickAction, h);
        in->v[MLXR_IN_B] = modelActionBool(ctx, ctx->middleClickAction, h);
        in->v[MLXR_IN_X] = modelActionBool(ctx, ctx->padXAction, h);
        in->v[MLXR_IN_Y] = modelActionBool(ctx, ctx->padYAction, h);
        in->v[MLXR_IN_MENU] = modelActionBool(ctx, ctx->padStartAction, h);
    }
    else {
        in->v[MLXR_IN_DPAD_DOWN] = modelActionBool(ctx, ctx->rightClickAction, h);
        in->v[MLXR_IN_DPAD_RIGHT] = modelActionBool(ctx, ctx->middleClickAction, h);
        in->v[MLXR_IN_DPAD_UP] = modelActionBool(ctx, ctx->padUpAction, h);
        in->v[MLXR_IN_DPAD_LEFT] = modelActionBool(ctx, ctx->padLeftAction, h);
        in->v[MLXR_IN_VIEW] = modelActionBool(ctx, ctx->menuAction, h);
    }
}

// Where each hand's grip is this frame, and whether its model shows, read once
// a frame at the time the frame is shown, the time the eyes are drawn from.
// The input pass syncs the actions; where it returned before doing so, they
// are synced here.
void updateControllerModels(XrCtx* ctx) {
    int shown[HAND_COUNT] = { 0, 0 };
    int live = ctx->modelOn && ctx->modelReady && !ctx->modelPassFailed && ctx->inputReady
            && ctx->gripAction != XR_NULL_HANDLE && !ctx->passthrough && ctx->shouldRender
            && ctx->sessionState == XR_SESSION_STATE_FOCUSED
            && ctx->splash.phase == SPLASH_GONE;
    if (live && !ctx->actionsSynced) {
        XrActiveActionSet active;
        active.actionSet = ctx->actionSet;
        active.subactionPath = XR_NULL_PATH;
        XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
        sync.countActiveActionSets = 1;
        sync.activeActionSets = &active;
        live = XR_SUCCEEDED(xrSyncActions(ctx->session, &sync));
    }
    ctx->actionsSynced = 0;

    for (int h = 0; live && h < HAND_COUNT; h++) {
        if (ctx->gripSpaces[h] == XR_NULL_HANDLE) {
            continue;
        }
        XrActionStateGetInfo get = { XR_TYPE_ACTION_STATE_GET_INFO };
        get.action = ctx->gripAction;
        get.subactionPath = ctx->handPaths[h];
        XrActionStatePose state = { XR_TYPE_ACTION_STATE_POSE };
        int active = XR_SUCCEEDED(xrGetActionStatePose(ctx->session, &get, &state))
                && state.isActive;
        // In the space the models' layer is drawn in, whatever the picture is
        // locked to: a controller is in the room, not on the head
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        int placed = active && XR_SUCCEEDED(xrLocateSpace(ctx->gripSpaces[h], ctx->localSpace,
                                                          ctx->predictedDisplayTime, &loc));
        // Native (lane ray-glitch): the ray's rule (controllerPoseLive, which
        // controllerModelShown goes by) and the ray's hold, so a short loss of
        // tracking keeps the model where it last was for as long as it keeps
        // the ray, and the two go together after that
        int tracked = placed && controllerModelShown(ctx->modelOn, ctx->passthrough,
                                                     ctx->profileKind[h], active,
                                                     (unsigned)loc.locationFlags);
        int hold = poseHoldStep(&ctx->gripHold[h], tracked, nowNs(), POSE_HOLD_NS);
        if (tracked) {
            poseHoldKeep(&ctx->gripHold[h], loc.pose);
        }
        shown[h] = ctx->modelOn && !ctx->passthrough
                && ctx->profileKind[h] == PROFILE_CONTROLLER && hold != POSE_GONE;
        if (shown[h]) {
            ctx->modelGrip[h] = ctx->gripHold[h].pose;
            // Native: the controls the Frame's moving parts follow (the generic
            // model has none: nothing reads them)
            if (ctx->model && ctx->model->textured) {
                readModelInput(ctx, h, (MlxrModelInput*)ctx->modelInput[h]);
            }
        }
    }

    for (int h = 0; h < HAND_COUNT; h++) {
        ctx->modelShown[h] = shown[h];
    }
    ctx->modelsShowing = shown[HAND_LEFT] || shown[HAND_RIGHT];
    // An image left from before the models went is not shown when they come
    // back: the pass draws a fresh one first
    if (!ctx->modelsShowing) {
        ctx->modelRendered = 0;
    }
    int said = shown[HAND_LEFT] | (shown[HAND_RIGHT] << 1);
    if (said != ctx->modelsSaid) {
        ctx->modelsSaid = said;
        LOGI("controller models: left %s, right %s", shown[HAND_LEFT] ? "drawn" : "not drawn",
             shown[HAND_RIGHT] ? "drawn" : "not drawn");
    }
}

// Native: the textured Frame models' two knobs, read once from the
// environment. MLXR_FRAME_MODEL_GAIN (default 1) multiplies the base colour in
// linear light: Valve's plastic is near black and is shown as such, with its
// shape in its highlights. MLXR_FRAME_MODEL_VIEW: 0 lit (default), 1 the
// normals, 2 lit without the normal map, 3 base colour times occlusion.
static void frameModelKnobs(float* gain, int* view) {
    static int read;
    static float g = 1.0f;
    static int v;
    if (!read) {
        read = 1;
        const char* s = getenv("MLXR_FRAME_MODEL_GAIN");
        if (s && *s) {
            float f = strtof(s, NULL);
            g = f > 0.0f && f <= 100.0f ? f : 1.0f;
        }
        s = getenv("MLXR_FRAME_MODEL_VIEW");
        if (s && *s) {
            v = atoi(s);
            v = v >= 0 && v <= 3 ? v : 0;
        }
        if (g != 1.0f || v != 0) {
            LOGI("Frame controller models: gain %.2f, view %d (environment)", g, v);
        }
    }
    *gain = g;
    *view = v;
}

// Both eyes, each shown model at its grip, over a clear to nothing (native:
// the pass itself, into any target; renderControllerModels and the snapshot
// tool both draw through it). Native (lane eye-swapchains): an image per eye
// is a pass each, the depth cleared again between them; the halves of one
// image are one pass.
static void modelRecord(XrCtx* ctx, VkCommandBuffer cmd, const XrEyeTargets* target, const XrView* views,
                        int clear) {
    MlxrModelParams mp;
    memset(&mp, 0, sizeof(mp));
    mlxr_model_fill(ctx->model, &mp);
    float viewProj[ROOM_EYES][16];
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        // Near enough for a controller held up to the face, and nothing it
        // draws is ever far off
        roomEyeViewProj(viewProj[eye], views[eye].fov, views[eye].pose, ROOM_FAR_MIN_M);
    }
    for (int h = 0; h < HAND_COUNT; h++) {
        mp.shown[h] = ctx->modelShown[h];
        if (ctx->modelShown[h]) {
            // Native: only the generic one is mirrored; the Frame's left is its own mesh
            controllerModelMatrix(ctx->modelGrip[h], h == HAND_LEFT && !ctx->model->perHand, mp.model[h]);
        }
    }
    // Native: the textured Frame models' moving parts (a hand that is not shown
    // draws nothing). Pass reads them while recording, so these live to its end.
    MlxrModelPart parts[HAND_COUNT][MLXR_MODEL_COMPONENTS_MAX];
    if (mp.textured && ctx->model) {
        for (int h = 0; h < HAND_COUNT; h++) {
            if (ctx->modelShown[h]) {
                mp.handPartCount[h] = mlxr_model_pose(ctx->model, h, (const MlxrModelInput*)ctx->modelInput[h], parts[h],
                                                      MLXR_MODEL_COMPONENTS_MAX);
                mp.handParts[h] = parts[h];
            }
        }
    }
    mp.depth = target->depthView;
    mp.depthFormat = VK_FORMAT_D32_SFLOAT;
    mp.over = !clear;
    if (mp.textured) {
        frameModelKnobs(&mp.gain, &mp.debugView);
    }
    int shared = xrEyesShared(target);
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        MlxrTarget t = xrEyeTarget(target, eye);
        t.clear = clear;
        if (shared) {
            t.rect.extent.width = (uint32_t)(target->eyeW * ROOM_EYES);
            mp.eyes = ROOM_EYES;
            for (int e = 0; e < ROOM_EYES; e++) {
                memcpy(mp.viewProj[e], viewProj[e], sizeof(viewProj[e]));
                mp.eyeRect[e].offset.x = target->x[e];
                mp.eyeRect[e].offset.y = 0;
                mp.eyeRect[e].extent = (VkExtent2D){ (uint32_t)target->eyeW, (uint32_t)target->eyeH };
            }
        }
        else {
            if (eye > 0 && target->depthImage != VK_NULL_HANDLE) {
                mlxr_barrier(cmd, target->depthImage, VK_IMAGE_ASPECT_DEPTH_BIT,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            }
            mp.eyes = 1;
            memcpy(mp.viewProj[0], viewProj[eye], sizeof(viewProj[eye]));
            mp.eyeRect[0] = t.rect;
        }
        mlxr_pass_model(ctx->passes, cmd, &t, &mp);
        if (shared) {
            break;
        }
    }
}

void xrModelRecord(XrCtx* ctx, VkCommandBuffer cmd, const XrEyeTargets* target, const XrView* views) {
    modelRecord(ctx, cmd, target, views, 1);
}

// Native: the models over an image another layer already drew this frame (the
// picture's or the room's: one projection layer a frame, xrWorldPlanLayers),
// from that layer's views and at its size. The depth is cleared for them, so
// they go over what is there, as their own layer used to.
void xrModelRecordOver(XrCtx* ctx, VkCommandBuffer cmd, const XrEyeTargets* target, const XrView* views) {
    if (!ctx->modelsShowing || ctx->model == NULL || cmd == VK_NULL_HANDLE) {
        return;
    }
    mlxr_passes_warm(ctx->passes, ctx->model->textured ? MLXR_PASS_MODEL_TEX : MLXR_PASS_MODEL, MLXR_VIDEO_YUV420P,
                     target->format, VK_FORMAT_D32_SFLOAT);
    XrEyeTargets t = *target;
    if (!xrEyesWorldDepth(ctx, &t)) {
        return;
    }
    // After the draws already in the images
    xrEyesBarrier(cmd, &t);
    xrGpuTimeBegin(ctx, GPU_MODELS);
    modelRecord(ctx, cmd, &t, views, 0);
    xrGpuTimeEnd(ctx, GPU_MODELS);
    ctx->modelRendered = 1;
}

// The models' own pass, every display frame one shows, picture or not: a
// controller moves on its own, and the compositor only moves an image with
// the head. Queued after the warp, so the picture never waits on it.
void renderControllerModels(XrCtx* ctx) {
    if (!ctx->modelsShowing || ctx->model == NULL || ctx->frameCmd == VK_NULL_HANDLE || !initModelPass(ctx)
            || !locateModelViews(ctx)) {
        return;
    }
    // Native (lane eye-swapchains): an image of each eye's swapchain
    uint32_t index[ROOM_EYES] = { 0, 0 };
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        if (!xrArtAcquire(ctx->modelSwapchain[eye], "acquire controller model image", &index[eye])) {
            if (eye > 0) {
                XrSwapchainImageReleaseInfo rl = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
                xrReleaseSwapchainImage(ctx->modelSwapchain[0], &rl);
            }
            return;
        }
    }
    VkCommandBuffer cmd = ctx->frameCmd;
    VkImage images[ROOM_EYES];
    VkImageView views[ROOM_EYES];
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        images[eye] = ctx->modelImages[eye][index[eye]].image;
        views[eye] = ctx->modelImageViews[eye][index[eye]];
    }
    XrEyeTargets tg = xrEyesPerImage(images, views, VK_FORMAT_R8G8B8A8_SRGB, ctx->modelEyeWidth,
                                     ctx->modelEyeHeight);
    xrGpuTimeBegin(ctx, GPU_MODELS);
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        mlxr_barrier(cmd, images[eye], VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    if (xrEyesWorldDepth(ctx, &tg)) {
        xrModelRecord(ctx, cmd, &tg, ctx->modelViews);
    }
    xrGpuTimeEnd(ctx, GPU_MODELS);
    ctx->modelAcquired = (1 << ROOM_EYES) - 1;
    ctx->modelRendered = 1;
}
