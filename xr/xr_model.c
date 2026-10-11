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
    if (!createArtSwapchain(ctx, eyeW * ROOM_EYES, eyeH, "create controller model swapchain",
                            &ctx->modelSwapchain, &ctx->modelImages, &ctx->modelImageViews,
                            &ctx->modelImageCount)) {
        return 0;
    }
    LOGEV("swapchains alive %d with the controller models'", ctx->swapchainsAlive);
    // Native: the textured pair's own pipeline when that is what loaded
    mlxr_passes_warm(ctx->passes, ctx->model && ctx->model->textured ? MLXR_PASS_MODEL_TEX : MLXR_PASS_MODEL,
                     MLXR_VIDEO_YUV420P, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_D32_SFLOAT);

    ctx->modelEyeWidth = eyeW;
    ctx->modelEyeHeight = eyeH;
    ctx->modelPassReady = 1;
    ctx->modelPassFailed = 0;
    LOGEV("controller model pass ready at %dx%d per eye", eyeW, eyeH);
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
        if (!active || XR_FAILED(xrLocateSpace(ctx->gripSpaces[h], ctx->localSpace,
                                               ctx->predictedDisplayTime, &loc))) {
            continue;
        }
        shown[h] = controllerModelShown(ctx->modelOn, ctx->passthrough, ctx->profileKind[h],
                                        active, (unsigned)loc.locationFlags);
        if (shown[h]) {
            ctx->modelGrip[h] = loc.pose;
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
// tool both draw through it)
void xrModelRecord(XrCtx* ctx, VkCommandBuffer cmd, VkImageView colour, VkFormat format, VkImageView depth,
                   int eyeW, int eyeH, const XrView* views) {
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = colour;
    t.format = format;
    t.rect.extent = (VkExtent2D){ (uint32_t)(eyeW * ROOM_EYES), (uint32_t)eyeH };
    t.clear = 1;
    MlxrModelParams mp;
    memset(&mp, 0, sizeof(mp));
    mlxr_model_fill(ctx->model, &mp);
    mp.eyes = ROOM_EYES;
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        // Near enough for a controller held up to the face, and nothing it
        // draws is ever far off
        roomEyeViewProj(mp.viewProj[eye], views[eye].fov, views[eye].pose, ROOM_FAR_MIN_M);
        mp.eyeRect[eye].offset.x = eye * eyeW;
        mp.eyeRect[eye].extent = (VkExtent2D){ (uint32_t)eyeW, (uint32_t)eyeH };
    }
    for (int h = 0; h < HAND_COUNT; h++) {
        mp.shown[h] = ctx->modelShown[h];
        if (ctx->modelShown[h]) {
            // Native: only the generic one is mirrored; the Frame's left is its own mesh
            controllerModelMatrix(ctx->modelGrip[h], h == HAND_LEFT && !ctx->model->perHand, mp.model[h]);
        }
    }
    mp.depth = depth;
    mp.depthFormat = VK_FORMAT_D32_SFLOAT;
    if (mp.textured) {
        frameModelKnobs(&mp.gain, &mp.debugView);
    }
    mlxr_pass_model(ctx->passes, cmd, &t, &mp);
}

// The models' own pass, every display frame one shows, picture or not: a
// controller moves on its own, and the compositor only moves an image with
// the head. Queued after the warp, so the picture never waits on it.
void renderControllerModels(XrCtx* ctx) {
    if (!ctx->modelsShowing || ctx->model == NULL || ctx->frameCmd == VK_NULL_HANDLE || !initModelPass(ctx)
            || !locateModelViews(ctx)) {
        return;
    }
    uint32_t index = 0;
    if (!xrArtAcquire(ctx->modelSwapchain, "acquire controller model image", &index)) {
        return;
    }
    VkCommandBuffer cmd = ctx->frameCmd;
    xrGpuTimeBegin(ctx, GPU_MODELS);
    mlxr_barrier(cmd, ctx->modelImages[index].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkImageView depth = xrWorldDepth(ctx, ctx->modelEyeWidth * ROOM_EYES, ctx->modelEyeHeight);
    if (depth != VK_NULL_HANDLE) {
        xrModelRecord(ctx, cmd, ctx->modelImageViews[index], VK_FORMAT_R8G8B8A8_SRGB, depth, ctx->modelEyeWidth,
                      ctx->modelEyeHeight, ctx->modelViews);
    }
    xrGpuTimeEnd(ctx, GPU_MODELS);
    ctx->modelAcquired = 1;
    ctx->modelRendered = 1;
}
