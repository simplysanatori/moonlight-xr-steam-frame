// The end of the frame: every composition layer the session shows,
// assembled in draw order and handed to the compositor.
//
// Moonlight XR Native: ported from the Steam Frame fork of Moonlight XR's
// xr-renderer/xr_layers.c (Gilleece, GPLv3; the fork's transparent
// passthrough projection layer kept). The order, the poses, the sizes, the
// fades and the layer budget are upstream's. What changed:
//   - every piece of art is a sub-rect of the one UI atlas swapchain
//     (xr_assets.c) instead of a swapchain of its own;
//   - the settings panel's rings, thumbs, marks and readout strip are drawn
//     into the panel itself (lane U), so the panel is one quad plus the
//     clock strip; the picker's, the keyboard's, the hint's and the Ko-fi
//     sheet's rings stay upstream's outline quads;
//   - a curved picture is the host's projection layer (mlxr_pass_panel), there
//     being no cylinder layers on SteamVR;
//   - nativeEndFrame's video draw is the host's (xr_session.c); this builds
//     the layers it submits (xrBuildLayers);
//   - native: the launcher's list stands in the picture's place;
//   - the room and the controller models are projection layers the host drew
//     (xr_room.c, xr_model.c), upstream's arrangement; the glow is a quad of
//     its own swapchain, and on a curved picture it is drawn into the curved
//     picture's projection layer instead of a cylinder (xr_world.c);
//   - the splash is not drawn;
//   - native (lane eye-swapchains): every projection layer's two views name a
//     swapchain each, one per eye, rather than the two halves of one double
//     wide image: SteamVR downsamples any texture past 3458 px each frame.
#include "xr_native.h"
#include "xr_world.h"

// What a frame carries at its fullest on the Frame. The room (or passthrough's
// layer: never both), the glow, the picture, the stats, the move pill (not in a
// room), seven bar buttons (environment, cog, keyboard, exit, gamepad, ray,
// head aim; head aim only outside a room), the controller models, the ray, the
// dot and the toast: 16 of SteamVR's 16 outside a room with the screen head
// locked, 15 in one. A modal sheds the bar furniture the frame it opens, as
// upstream: the settings panel is one quad here (lane U draws its rings,
// thumbs and readouts into it) with the clock strip and the cog button, so the
// Room tab at its fullest is the room, the glow, the picture, the stats, the
// cog button, the panel, the clock, the models, ray, dot and toast: 11. The
// drop order below is upstream's (toast, models, hover ring, cog button,
// clock) for a frame that would still go over; tests/m5 counts the cases.
// The keyboard's move bar (native) is one more, but only with the keyboard up,
// which sheds the bar furniture the same way a modal does; tests/m4/test_budget.c.
#define FRAME_MAX_LAYERS XR_FRAME_MAX_LAYERS

typedef struct {
    XrCompositionLayerProjection passthrough;
    XrCompositionLayerProjectionView passthroughViews[ROOM_EYES];
    XrCompositionLayerProjection room;
    XrCompositionLayerProjectionView roomViews[ROOM_EYES];
    XrCompositionLayerQuad glow;
    XrCompositionLayerQuad video[2];
    XrCompositionLayerQuad launcher;
    XrCompositionLayerQuad overlay;
    XrCompositionLayerQuad handle;
    XrCompositionLayerQuad envButton;
    XrCompositionLayerQuad cogButton;
    XrCompositionLayerQuad kbButton;
    XrCompositionLayerQuad exitButton;
    XrCompositionLayerQuad stereoButton;
    XrCompositionLayerQuad rayButton;
    XrCompositionLayerQuad aimButton;
    XrCompositionLayerQuad padButton;
    XrCompositionLayerQuad hostButton;
    XrCompositionLayerQuad hereButton;
    XrCompositionLayerQuad exitPrompt;
    XrCompositionLayerQuad report;
    XrCompositionLayerQuad hint;
    XrCompositionLayerQuad kofi;
    XrCompositionLayerQuad host;
    XrCompositionLayerQuad firstRun;
    XrCompositionLayerQuad padCard;
    XrCompositionLayerQuad picker;
    XrCompositionLayerQuad outline[2];
    XrCompositionLayerQuad cogPanel;
    // The hover ring's slot (upstream: one per option row plus the hover)
    XrCompositionLayerQuad cogMark[COG_OPTION_COUNT + 1];
    XrCompositionLayerQuad cogClock;
    XrCompositionLayerQuad kbPanel;
    XrCompositionLayerQuad kbMark;
    XrCompositionLayerQuad kbBar;
    // The controller models, over the picture and the panels and under the
    // beam
    XrCompositionLayerProjection models;
    XrCompositionLayerProjectionView modelViews[ROOM_EYES];
    XrCompositionLayerQuad beam;
    // Native (lane radial-dwell): the dwell's radial menu, or a drag's mark, at the dwell's start
    // (only with the head or the eyes pointing, which never draw the beam)
    XrCompositionLayerQuad dwellMark;
    XrCompositionLayerQuad dot;
    XrCompositionLayerQuad toast;
    const XrCompositionLayerBaseHeader* order[FRAME_MAX_LAYERS];
    uint32_t count;
} FrameLayers;

// Upstream kept these on nativeEndFrame's stack for as long as xrEndFrame
// needed them; the host calls xrEndFrame after this returns, so they live here
static FrameLayers frameLayers;

// What a layer chains on as it fades. SteamVR has no colour scale bias, so
// every fade is a cut (fadeStep's smooth is off) and nothing is chained.
static const void* fadeNext(XrCtx* ctx, FrameLayers* layers, int slot, int sharp, float level) {
    (void)layers;
    (void)slot;
    (void)sharp;
    (void)level;
    (void)ctx;
    return NULL;
}

// What every layer builder reads about this frame, worked out once
typedef struct {
    XrSpace space;
    XrPosef screenPose;
    float screenWidth;
    float screenHeight;
    float aspect;
    float curve;
    int screenCurved;
    int stereo;
    int roomOn;
    int headLocked;
    int eyeSwap;
    int barArea;
    BarFrame bar;
} FrameView;

static void dropLayer(FrameLayers* layers, const void* layer) {
    for (uint32_t i = 0; i < layers->count; i++) {
        if (layers->order[i] == (const XrCompositionLayerBaseHeader*)layer) {
            memmove(&layers->order[i], &layers->order[i + 1],
                    (layers->count - i - 1) * sizeof(layers->order[0]));
            layers->count--;
            return;
        }
    }
}

static void pushLayer(XrCtx* ctx, FrameLayers* layers, const void* layer) {
    if (layers->count >= FRAME_MAX_LAYERS) {
        if (!ctx->layerDropWarned) {
            ctx->layerDropWarned = 1;
            LOGE("frame needs more than %d composition layers, dropping the rest",
                 FRAME_MAX_LAYERS);
        }
        return;
    }
    layers->order[layers->count++] = (const XrCompositionLayerBaseHeader*)layer;
}

// A quad in the given space showing a texW x texH rect of one swapchain
// image, from texX, texY
static void quadLayerRect(XrCompositionLayerQuad* quad, const void* next,
                          XrCompositionLayerFlags flags, XrSwapchain chain, int texX, int texY,
                          int texW, int texH, XrSpace space, XrPosef pose, float width,
                          float height) {
    memset(quad, 0, sizeof(*quad));
    quad->type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    quad->next = next;
    quad->layerFlags = flags;
    quad->eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    quad->subImage.swapchain = chain;
    quad->subImage.imageRect.offset.x = texX;
    quad->subImage.imageRect.offset.y = texY;
    quad->subImage.imageRect.extent.width = texW;
    quad->subImage.imageRect.extent.height = texH;
    quad->subImage.imageArrayIndex = 0;
    quad->space = space;
    quad->pose = pose;
    quad->size.width = width;
    quad->size.height = height;
}

// Native: a quad showing one slot of the UI atlas, premultiplied, alpha blended
static void atlasQuad(XrCtx* ctx, XrCompositionLayerQuad* quad, const void* next, int slot,
                      XrSpace space, XrPosef pose, float width, float height) {
    UiRectPx r = xrAtlasRect(slot);
    quadLayerRect(quad, next, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                  xrAtlasSwapchain(ctx), r.x, r.y, r.w, r.h, space, pose, width, height);
}

static XrPosef poseOffset(XrPosef base, Vec3 local) {
    Vec3 offset = quatRotate(base.orientation, local);
    XrPosef pose;
    pose.orientation = base.orientation;
    pose.position.x = base.position.x + offset.x;
    pose.position.y = base.position.y + offset.y;
    pose.position.z = base.position.z + offset.z;
    return pose;
}

// Compositor sharpening and supersampling. SteamVR offers neither layer
// setting, so this only says what is asked for; the sharpening is done in
// the warp pass instead (xr_session.c).
static void setLayerSettings(XrCtx* ctx, FrameLayers* layers) {
    (void)layers;
    int logged = ctx->sharpenMode * 3 + ctx->supersampleMode + 1;
    if (logged != ctx->layerFlagsLogged) {
        ctx->layerFlagsLogged = logged;
        LOGEV("layer flags: sharpen %d supersample %d%s", ctx->sharpenMode,
              ctx->supersampleMode, ctx->layerSettingsSupported ? ""
                      : " (no layer settings on this runtime: sharpening in the warp pass, supersampling by the "
                        "app's own pass when the picture is minified)");
    }
}

// A transparent projection layer for passthrough, filled once when it is
// made (the fork's: SteamVR on the Steam Frame leaves the background opaque
// without one)
#define PASSTHROUGH_TEX 16

int xrBackdropColor(XrCtx* ctx, int passthroughNow, float out[4]) {
    int mode = BACKDROP_NONE;
    if (passthroughNow) {
        mode = BACKDROP_PASSTHROUGH;
    }
    else if (!ctx->passthrough && roomEffective(ctx) <= 0) {
        mode = BACKDROP_VOID;
    }
    return backdropFor(mode, ctx->bgDim, ctx->voidColor, out);
}

// Native (lane wave3-visual): the same layer also holds the Picture tab's
// Background dim over the cameras (black at the dim's share of alpha) and the
// void's colour; `want` is that colour, and the swapchain's newest image is
// cleared to it when it changes
static void addPassthroughLayer(XrCtx* ctx, FrameLayers* layers, const float want[4]) {
    // Native: the picture's own transparent projection layer stands in for it
    // (one projection layer a frame, xrWorldPlanLayers)
    if (ctx->passthroughFailed || ctx->passthroughInPicture) {
        return;
    }
    memcpy(ctx->backdropWant, want, sizeof(ctx->backdropWant));
    int made = ctx->passthroughSwapchain != XR_NULL_HANDLE;
    if ((!made || !ctx->backdropDrawnValid || memcmp(ctx->backdropDrawn, want, sizeof(ctx->backdropDrawn)) != 0)
            && (ctx->ensurePassthrough == NULL || !ctx->ensurePassthrough(ctx, PASSTHROUGH_TEX))
            && !made) {
        // Making the layer failed; failing to change its colour keeps the old one
        ctx->passthroughFailed = 1;
        return;
    }

    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = ctx->predictedDisplayTime;
    locateInfo.space = ctx->localSpace;
    XrViewState state = { XR_TYPE_VIEW_STATE };
    XrView views[ROOM_EYES] = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    uint32_t count = 0;
    if (XR_FAILED(xrLocateViews(ctx->session, &locateInfo, &state, ROOM_EYES, &count, views))
            || count < ROOM_EYES) {
        return;
    }

    XrCompositionLayerProjection* layer = &layers->passthrough;
    memset(layer, 0, sizeof(*layer));
    memset(layers->passthroughViews, 0, sizeof(layers->passthroughViews));
    layer->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    layer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    layer->space = ctx->localSpace;
    layer->viewCount = ROOM_EYES;
    layer->views = layers->passthroughViews;
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        XrCompositionLayerProjectionView* projView = &layers->passthroughViews[eye];
        projView->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        projView->pose = views[eye].pose;
        projView->fov = views[eye].fov;
        projView->subImage.swapchain = ctx->passthroughSwapchain;
        projView->subImage.imageRect.extent.width = PASSTHROUGH_TEX;
        projView->subImage.imageRect.extent.height = PASSTHROUGH_TEX;
    }
    pushLayer(ctx, layers, layer);
}

// Native (lane eye-swapchains): one view of a projection layer, the whole
// image of that eye's own swapchain
void xrProjectionEye(XrCompositionLayerProjectionView* out, const XrView* view, XrSwapchain chain, int eyeW,
                     int eyeH) {
    memset(out, 0, sizeof(*out));
    out->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
    out->pose = view->pose;
    out->fov = view->fov;
    out->subImage.swapchain = chain;
    out->subImage.imageRect.offset.x = 0;
    out->subImage.imageRect.offset.y = 0;
    out->subImage.imageRect.extent.width = eyeW;
    out->subImage.imageRect.extent.height = eyeH;
    out->subImage.imageArrayIndex = 0;
}

int xrProjectionEyesOwn(const XrCompositionLayerProjection* layer) {
    if (layer == NULL || layer->viewCount != ROOM_EYES || layer->views == NULL) {
        return 0;
    }
    const XrCompositionLayerProjectionView* v = layer->views;
    for (int e = 0; e < ROOM_EYES; e++) {
        const XrRect2Di* r = &v[e].subImage.imageRect;
        if (v[e].subImage.swapchain == XR_NULL_HANDLE || r->offset.x + r->extent.width > STEAMVR_TEXTURE_MAX
                || r->offset.y + r->extent.height > STEAMVR_TEXTURE_MAX) {
            return 0;
        }
    }
    return v[0].subImage.swapchain != v[1].subImage.swapchain;
}

// The 3d room, drawn per eye into the one projection layer
static void addRoomLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    // The environment, when it is a room. Passthrough wants the real room
    // instead, so the two never go up together.
    // Native: not while it is drawn into the picture's layer (one projection
    // layer a frame, xrWorldPlanLayers)
    if (view->roomOn && ctx->roomRendered && ctx->roomViewsValid && !ctx->passthrough && !ctx->roomInPicture
            && ctx->roomSwapchain[0] != XR_NULL_HANDLE && ctx->roomSwapchain[1] != XR_NULL_HANDLE) {
        XrCompositionLayerProjection* room = &layers->room;
        memset(room, 0, sizeof(*room));
        room->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
        room->layerFlags = 0;
        // World locked, even when the screen is head locked, or the room would
        // swing about with the viewer
        room->space = ctx->localSpace;
        room->viewCount = ROOM_EYES;
        room->views = layers->roomViews;
        for (int eye = 0; eye < ROOM_EYES; eye++) {
            // The poses the image was actually drawn from, so the compositor
            // reprojects it rather than being told a pose it does not match.
            // Native (lane eye-swapchains): each eye its own swapchain.
            xrProjectionEye(&layers->roomViews[eye], &ctx->roomViews[eye], ctx->roomSwapchain[eye],
                            ctx->roomEyeWidth, ctx->roomEyeHeight);
        }
        pushLayer(ctx, layers, room);
    }
}

// The ambilight glow behind the picture. Native (lane wave3-visual): or over
// it, when the Picture tab's Glow edge is on, since the glow image is then
// the fade of the picture's border into it; `over` says which of the two
// calls this is, and each does nothing in the other's case. The mode the
// image was drawn with decides, so a switch lands together with the redraw.
static void addGlowLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers, int over) {
    // The glow, over the environment and under the picture. Deliberately not
    // sharpened: being soft is the whole of the effect. Native: a curved
    // picture's glow is drawn into the curved picture's own projection layer
    // (there are no cylinder layers), so only the flat glow is a layer here.
    // Native: inside the picture's projection layer when it has one (curved, or flat in 3D)
    (void)view;
    if ((ctx->glowDrawnFeather > 0) != (over != 0)) {
        return;
    }
    if (!xrWorldGlowShown(ctx) || xrPictureProjected(ctx) || ctx->glowSwapchain == XR_NULL_HANDLE
            || !ctx->glowSwapchainFilled) {
        return;
    }
    // Local +z is toward the viewer, so this sits just proud of the picture.
    // The layer order is what keeps it under the picture, not the depth.
    // Over the picture it sits in the picture's plane, so its border fade
    // lands on the picture's edge from every angle.
    Vec3 proudLocal = { 0.0f, 0.0f, over ? 0.0f : GLOW_PROUD_M };
    quadLayerRect(&layers->glow, NULL, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT, ctx->glowSwapchain, 0,
                  0, GLOW_TEX, GLOW_TEX, view->space, poseOffset(view->screenPose, proudLocal),
                  view->screenWidth * GLOW_SCALE, view->screenHeight * GLOW_SCALE);
    pushLayer(ctx, layers, &layers->glow);
}

// The controller models' own projection layer, alpha blended over everything
// before it: a controller is nearer than the picture and the panels, so it is
// drawn over them wherever the two cross. World locked, with the poses its
// image was drawn from.
static void addModelLayer(XrCtx* ctx, FrameLayers* layers) {
    // Native: not while they are drawn into the picture's or the room's layer
    if (!ctx->modelsShowing || !ctx->modelRendered || ctx->passthrough || ctx->modelSwapchain[0] == XR_NULL_HANDLE
            || ctx->modelSwapchain[1] == XR_NULL_HANDLE || ctx->modelsInPicture || ctx->modelsInRoom) {
        return;
    }
    XrCompositionLayerProjection* models = &layers->models;
    memset(models, 0, sizeof(*models));
    models->type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    models->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    models->space = ctx->localSpace;
    models->viewCount = ROOM_EYES;
    models->views = layers->modelViews;
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        // Native (lane eye-swapchains): each eye its own swapchain
        xrProjectionEye(&layers->modelViews[eye], &ctx->modelViews[eye], ctx->modelSwapchain[eye],
                        ctx->modelEyeWidth, ctx->modelEyeHeight);
    }
    pushLayer(ctx, layers, models);
}

// The picture, one layer per eye. With the 3D off the left half alone,
// flat, to both eyes. On a curve the host's
// projection layer stands in for upstream's cylinder. Native (lane
// eye-swapchains): on SteamVR the picture in 3D is always the host's
// projection layer (an image per eye there, no seam), so the double wide quads
// below, with their eye swap and seam inset, are upstream's path for a runtime
// that shows one-eye quads; the Frame never takes it.
static void addVideoLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers,
                           const XrFrameIn* in) {
    (void)view;
    // Native: also flat in 3D (SteamVR shows no quad seen by one eye only)
    if (xrPictureProjected(ctx)) {
        if (in->curved != NULL) {
            pushLayer(ctx, layers, in->curved);
        }
        return;
    }
    // M5: the supersampled picture (flat, mono), the same quad from a smaller
    // image the app filtered
    if (in->picture != XR_NULL_HANDLE && ctx->drawnEyes != 2) {
        XrCompositionLayerQuad* quad = &layers->video[0];
        quadLayerRect(quad, NULL, 0, in->picture, 0, 0, in->pictureW, in->pictureH, view->space, view->screenPose,
                      view->screenWidth, view->screenHeight);
        pushLayer(ctx, layers, quad);
        return;
    }
    int viewCount = view->stereo && ctx->drawnEyes == 2 ? 2 : 1;
    for (int eye = 0; eye < viewCount; eye++) {
        XrSwapchainSubImage subImage;
        subImage.swapchain = ctx->swapchain;
        int half = viewCount == 2 ? sbsEyeHalf(eye, view->eyeSwap) : eye;
        int x = view->stereo ? half * ctx->videoWidth : 0;
        int w = ctx->videoWidth;
        if (view->stereo && ctx->seamInset && w > 2 * SEAM_INSET_TEXELS) {
            x += SEAM_INSET_TEXELS;
            w -= 2 * SEAM_INSET_TEXELS;
        }
        subImage.imageRect.offset.x = x;
        subImage.imageRect.offset.y = 0;
        subImage.imageRect.extent.width = w;
        subImage.imageRect.extent.height = ctx->videoHeight;
        subImage.imageArrayIndex = 0;

        XrEyeVisibility visibility = viewCount == 1 ? XR_EYE_VISIBILITY_BOTH :
                (eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT);
        XrCompositionLayerQuad* quad = &layers->video[eye];
        quadLayerRect(quad, NULL, 0, ctx->swapchain, 0, 0, ctx->videoWidth, ctx->videoHeight,
                      view->space, view->screenPose, view->screenWidth, view->screenHeight);
        quad->eyeVisibility = visibility;
        quad->subImage = subImage;
        pushLayer(ctx, layers, quad);
    }
}

// Native: the launcher's list in the picture's place, as large as fits it
static void addLauncherLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    if (ctx->launcher == LAUNCHER_NONE) {
        return;
    }
    float pictureAspect = view->screenWidth / view->screenHeight;
    float listAspect = ctx->launcherAspect > 0.0f ? ctx->launcherAspect : 1.6f;
    float w = view->screenWidth, h = view->screenHeight;
    if (listAspect < pictureAspect) {
        w = h * listAspect;
    }
    else {
        h = w / listAspect;
    }
    atlasQuad(ctx, &layers->launcher, NULL,
              ctx->launcher == LAUNCHER_PCS ? UI_PANEL_PC_LIST : UI_PANEL_APP_LIST, view->space,
              view->screenPose, w, h);
    pushLayer(ctx, layers, &layers->launcher);
}

// The stats window: in the corner of the picture until a grip has moved it
// (statsPlace). Native: it shows just the part of its slot the lines used.
static void addOverlayLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    (void)view;
    if (statsShown(ctx)) {
        statsPlace(ctx);
        UiRectPx r = xrAtlasRect(ATLAS_OVERLAY);
        quadLayerRect(&layers->overlay, NULL, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                      xrAtlasSwapchain(ctx), r.x, r.y, ctx->overlayW, ctx->overlayH, view->space,
                      ctx->statsPose, ctx->statsW, ctx->statsH);
        pushLayer(ctx, layers, &layers->overlay);
    }
}

// The move bar or the resize corner, whichever the ray is over
static void addHandleLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    int isBar = view->barArea && !view->roomOn;
    int isCorner = !view->barArea && ctx->hoverKind == HOVER_CORNER && cornerSide(ctx) > 0.0f;
    if (ctx->handleArtReady && (isBar || isCorner)) {
        Vec3 local;
        float sizeW, sizeH;
        float roll = 0.0f;

        if (isBar) {
            sizeW = view->bar.width * BAR_WIDTH_FRAC;
            sizeH = view->bar.width * BAR_HEIGHT_FRAC;
            local.x = 0.0f;
            local.y = -(view->bar.height * 0.5f + view->bar.width * BAR_DROP_FRAC);
        }
        else {
            sizeW = sizeH = cornerSide(ctx);
            int right = ctx->hoverCorner == 1 || ctx->hoverCorner == 3;
            int bottom = ctx->hoverCorner >= 2;
            // Half a bracket outside the corner in both axes, so its inner
            // tip touches the corner and none of it covers the picture
            local.x = (right ? 0.5f : -0.5f) * (view->screenWidth + sizeW);
            local.y = (bottom ? -0.5f : 0.5f) * (view->screenHeight + sizeH);
            // The art is a top left bracket, so the other three are the
            // same picture rolled about the screen normal
            if (ctx->hoverCorner == 1) roll = -1.5707963f;
            else if (ctx->hoverCorner == 2) roll = 1.5707963f;
            else if (ctx->hoverCorner == 3) roll = 3.1415927f;
        }
        local.z = 0.005f;
        float yaw = 0.0f;
        curveLocal(&local, ctx->screenRadius, view->screenCurved, &yaw);

        XrQuaternionf rollQ = { 0.0f, 0.0f, sinf(roll * 0.5f), cosf(roll * 0.5f) };
        Vec3 yawAxis = { 0.0f, 1.0f, 0.0f };
        XrQuaternionf turnQ = quatMul(axisAngleQuat(yawAxis, yaw), rollQ);

        atlasQuad(ctx, &layers->handle, NULL, isBar ? ATLAS_HANDLE : ATLAS_CORNER, view->space,
                  poseOffset(view->screenPose, local), sizeW, sizeH);
        layers->handle.pose.orientation = quatNorm(quatMul(view->screenPose.orientation, turnQ));
        pushLayer(ctx, layers, &layers->handle);
    }
}

// Where the bar's row hangs, said when it moves by a centimetre or more
static void logBarPlacement(XrCtx* ctx, const BarFrame* frame) {
    if (ctx->grabMode != GRAB_NONE || ctx->cogDragSlider >= 0) {
        return;
    }
    Vec3 edgeLocal = { 0.0f, -frame->height * 0.5f, 0.0f };
    Vec3 barLocal = { 0.0f, -(frame->height * 0.5f + frame->width * BAR_DROP_FRAC), 0.0f };
    XrPosef edge = poseOffset(frame->pose, edgeLocal);
    XrPosef bar = poseOffset(frame->pose, barLocal);
    Vec3 e = { edge.position.x, edge.position.y, edge.position.z };
    Vec3 b = { bar.position.x, bar.position.y, bar.position.z };
    Vec3 moved = vecSub(e, ctx->barSaidEdge);
    if (fabsf(frame->width - ctx->barSaidWidth) < 0.01f && vecDot(moved, moved) < 1e-4f) {
        return;
    }
    ctx->barSaidWidth = frame->width;
    ctx->barSaidEdge = e;
    LOGI("bar row: picture's bottom edge at %.2f %.2f %.2f, %.2f m off; bar at %.2f %.2f %.2f, "
         "%.2f m off, %.2f m wide, buttons %.3f m, %s",
         e.x, e.y, e.z, sqrtf(vecDot(e, e)), b.x, b.y, b.z, sqrtf(vecDot(b, b)),
         frame->width * BAR_WIDTH_FRAC, frame->width * COG_BUTTON_FRAC,
         roomEffective(ctx) > 0 ? "a room's picture at the stand in's size"
                                : frame->curved ? "on the curved picture" : "on the picture");
}

// One of the buttons beside the move bar, in its place in the bar's row,
// showing its face's cell of the buttons' strip (a slot of the atlas)
static void addBarButton(XrCtx* ctx, const FrameView* view, FrameLayers* layers,
                         XrCompositionLayerQuad* quad, int cell, int slot, int hot,
                         const void* next) {
    const BarFrame* frame = &view->bar;
    Vec3 local;
    float side;
    barSlotPlacement(slot, frame->width, frame->height, &local, &side);
    float yaw = 0.0f;
    curveLocal(&local, frame->radius, frame->curved, &yaw);
    // Grows a little when the ray is on it, which is the only feedback
    // a quad layer can give without a second texture
    float scale = hot ? 1.18f : 1.0f;
    int cellX, cellY;
    buttonCellOrigin(cell, &cellX, &cellY);
    UiRectPx strip = xrAtlasRect(UI_PANEL_BUTTON_ATLAS);
    quadLayerRect(quad, next, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                  xrAtlasSwapchain(ctx), strip.x + cellX, strip.y + cellY, BUTTON_TEX, BUTTON_TEX,
                  view->space, poseOffset(frame->pose, local), side * scale, side * scale);
    if (yaw != 0.0f) {
        Vec3 up = { 0.0f, 1.0f, 0.0f };
        quad->pose.orientation = quatNorm(quatMul(frame->pose.orientation,
                                                  axisAngleQuat(up, yaw)));
    }
    pushLayer(ctx, layers, quad);
}

static int panelButton(XrCtx* ctx, const FrameView* view, FrameLayers* layers, int panel,
                       int open, const void** next) {
    float level = ctx->panelFades[panel].level;
    *next = NULL;
    if (view->barArea || open) {
        return 1;
    }
    if (level > 0.0f) {
        *next = fadeNext(ctx, layers, panel, 0, level);
        return 1;
    }
    return 0;
}

// The environment, settings, keyboard and exit buttons beside the move bar,
// and the switches further out
static void addBarButtonLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    const void* next = NULL;
    if (ctx->envButtonReady
            && panelButton(ctx, view, layers, FADE_PICKER, ctx->pickerOpen, &next)) {
        addBarButton(ctx, view, layers, &layers->envButton, BTN_CELL_ENV,
                     BAR_SLOT_ENV, ctx->envButtonHot, next);
    }
    int kofi = ctx->kofiOpen || ctx->panelFades[FADE_KOFI].level > 0.0f;
    if (ctx->cogButtonReady
            && panelButton(ctx, view, layers, kofi ? FADE_KOFI : FADE_COG,
                           ctx->cogOpen || ctx->kofiOpen, &next)) {
        addBarButton(ctx, view, layers, &layers->cogButton, BTN_CELL_COG,
                     BAR_SLOT_COG, ctx->cogButtonHot || ctx->cogOpen || ctx->kofiOpen, next);
    }
    if (ctx->kbButtonReady && view->barArea) {
        addBarButton(ctx, view, layers, &layers->kbButton, BTN_CELL_KB,
                     BAR_SLOT_KB, ctx->kbButtonHot, NULL);
    }
    if (ctx->exitButtonReady
            && panelButton(ctx, view, layers, FADE_EXIT, ctx->exitConfirmOpen, &next)) {
        addBarButton(ctx, view, layers, &layers->exitButton, BTN_CELL_EXIT,
                     BAR_SLOT_EXIT, ctx->exitButtonHot || ctx->exitConfirmOpen, next);
    }
    if (ctx->padButtonReady && view->barArea) {
        addBarButton(ctx, view, layers, &layers->padButton,
                     BTN_CELL_PAD + (ctx->padMode ? 1 : 0),
                     BAR_SLOT_PAD, ctx->padButtonHot, NULL);
    }
    if (ctx->aimButtonReady && view->barArea && headAimCanAct(ctx)) {
        addBarButton(ctx, view, layers, &layers->aimButton,
                     BTN_CELL_AIM + (headAimSwitchOn(ctx->headAimSetting,
                                                     ctx->headAimFlipped) ? 1 : 0),
                     BAR_SLOT_AIM, ctx->aimButtonHot, NULL);
    }
    if (ctx->rayButtonReady && view->barArea) {
        addBarButton(ctx, view, layers, &layers->rayButton,
                     BTN_CELL_RAY + (raySwitchOn(ctx->raySetting, ctx->rayFlipped) ? 1 : 0),
                     BAR_SLOT_RAY, ctx->rayButtonHot, NULL);
    }
    if (ctx->stereoButtonReady && view->barArea) {
        addBarButton(ctx, view, layers, &layers->stereoButton,
                     barStereoCell(ctx->stereoSource, ctx->stereoLive),
                     BAR_SLOT_STEREO, ctx->stereoButtonHot, NULL);
    }
    // Native (lane wave2-bind-wire): the Host sheet's, from the same strip
    // the keyboard's face comes from
    if (ctx->kbButtonReady
            && panelButton(ctx, view, layers, FADE_HOST, ctx->hostOpen, &next)) {
        addBarButton(ctx, view, layers, &layers->hostButton, BTN_CELL_HOST,
                     BAR_SLOT_HOST, ctx->hostButtonHot || ctx->hostOpen, next);
    }
    // Native (lane bar-glare): Bring here, in head aim's place, only while it
    // can act. Lit by the ray being on it; it has no state to show.
    if (ctx->kbButtonReady && view->barArea && xrBringHereCanAct(ctx)) {
        addBarButton(ctx, view, layers, &layers->hereButton, BTN_CELL_HERE, BAR_SLOT_HERE,
                     ctx->hoverKind == HOVER_HEREBUTTON, NULL);
    }
}

// The prompt the exit button opens, on the pose frozen when it opened; the
// hovered button is drawn into it (lane U), as upstream's three sheets were
static void addExitPromptLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_EXIT].level;
    if (level > 0.0f) {
        atlasQuad(ctx, &layers->exitPrompt, fadeNext(ctx, layers, FADE_EXIT, 1, level),
                  UI_PANEL_EXIT, view->space, ctx->exitPose, ctx->exitW, ctx->exitH);
        pushLayer(ctx, layers, &layers->exitPrompt);
    }
}

static void addReportLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_REPORT].level;
    if (level > 0.0f && ctx->reportReady) {
        atlasQuad(ctx, &layers->report, fadeNext(ctx, layers, FADE_REPORT, 1, level),
                  UI_PANEL_REPORT, view->space, ctx->reportPose, ctx->reportW, ctx->reportH);
        pushLayer(ctx, layers, &layers->report);
    }
}

// An outline ring over part of a sheet (upstream's hover marks)
static void addRing(XrCtx* ctx, FrameLayers* layers, XrCompositionLayerQuad* mark, XrSpace space,
                    XrPosef sheetPose, float sheetW, float sheetH, float l, float t, float r,
                    float b, float scaleW, float scaleH) {
    Vec3 local;
    local.x = ((l + r) * 0.5f - 0.5f) * sheetW;
    local.y = (0.5f - (t + b) * 0.5f) * sheetH;
    local.z = 0.004f;
    atlasQuad(ctx, mark, NULL, ATLAS_OUTLINE, space, poseOffset(sheetPose, local),
              (r - l) * sheetW * scaleW, (b - t) * sheetH * scaleH);
    pushLayer(ctx, layers, mark);
}

static void addHandHintLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_HINT].level;
    if (level <= 0.0f || !ctx->hintReady) {
        return;
    }
    atlasQuad(ctx, &layers->hint, fadeNext(ctx, layers, FADE_HINT, 1, level), UI_PANEL_HINT,
              view->space, ctx->hintPose, ctx->hintW, ctx->hintH);
    pushLayer(ctx, layers, &layers->hint);

    int zone = ctx->hintHoverZone;
    if (!ctx->hintOpen || zone == HINT_ZONE_NONE || !ctx->outlineReady) {
        return;
    }
    float l = zone == HINT_ZONE_OK ? HINT_OK_L : HINT_NEVER_L;
    float r = zone == HINT_ZONE_OK ? HINT_OK_R : HINT_NEVER_R;
    addRing(ctx, layers, &layers->cogMark[COG_OPTION_COUNT], view->space, ctx->hintPose, ctx->hintW,
            ctx->hintH, l, HINT_BTN_T, r, HINT_BTN_B, 1.04f, 1.12f);
}

static void addKofiLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_KOFI].level;
    if (level <= 0.0f || !ctx->kofiReady) {
        return;
    }
    atlasQuad(ctx, &layers->kofi, fadeNext(ctx, layers, FADE_KOFI, 1, level), UI_PANEL_KOFI,
              view->space, ctx->kofiPose, ctx->kofiW, ctx->kofiH);
    pushLayer(ctx, layers, &layers->kofi);
    int zone = ctx->kofiHoverZone;
    if (!ctx->kofiOpen || (zone != KOFI_ZONE_CLOSE && zone != KOFI_ZONE_OPEN) || !ctx->outlineReady) {
        return;
    }
    int open = zone == KOFI_ZONE_OPEN;
    addRing(ctx, layers, &layers->cogMark[COG_OPTION_COUNT], view->space, ctx->kofiPose, ctx->kofiW,
            ctx->kofiH, open ? KOFI_OPEN_L : KOFI_CLOSE_L, KOFI_BTN_T, open ? KOFI_OPEN_R : KOFI_CLOSE_R,
            KOFI_BTN_B, 1.04f, 1.12f);
}

// Native: the Host sheet, over the middle of the picture; the hovered cell is
// drawn into it
static void addHostLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_HOST].level;
    if (level <= 0.0f || !ctx->hostReady) {
        return;
    }
    atlasQuad(ctx, &layers->host, fadeNext(ctx, layers, FADE_HOST, 1, level), UI_PANEL_HOST,
              view->space, ctx->hostPose, ctx->hostW, ctx->hostH);
    pushLayer(ctx, layers, &layers->host);
}

// Native (lane wave3-cards): layers that may only take the room the rest leave. The models, the
// beam, the dot and the toast come after them and keep their place, so the frame that was full
// before is as full now
#define CARD_LAYER_RESERVE 4

static int cardLayerRoom(XrCtx* ctx, const FrameLayers* layers) {
    int cap = ctx->maxLayerCount < FRAME_MAX_LAYERS ? ctx->maxLayerCount : FRAME_MAX_LAYERS;
    return (int)layers->count + 1 + CARD_LAYER_RESERVE <= cap;
}

// The first-run controls card (PLAN 3.9), over the middle of the picture
static void addFirstRunLayer(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    if (!ctx->firstRunUp || !ctx->firstRunReady || !cardLayerRoom(ctx, layers)) {
        return;
    }
    float w, h;
    XrPosef pose = firstRunPose(ctx, &w, &h);
    atlasQuad(ctx, &layers->firstRun, NULL, UI_PANEL_FIRSTRUN, view->space, pose, w, h);
    pushLayer(ctx, layers, &layers->firstRun);
}

// The pad card (PLAN 2b.6), in the lap: in the local space under the head's heading, or in the
// view space when the head is not located
static void addPadCardLayer(XrCtx* ctx, FrameLayers* layers) {
    if (!ctx->padCardUp || !ctx->padCardReady || !cardLayerRoom(ctx, layers)) {
        return;
    }
    float w, h;
    XrPosef pose = padCardPose(ctx, &w, &h);
    atlasQuad(ctx, &layers->padCard, NULL, UI_PANEL_PADCARD,
              ctx->headPoseValid ? ctx->localSpace : ctx->viewSpace, pose, w, h);
    pushLayer(ctx, layers, &layers->padCard);
}

// The environment grid and the rings on its hovered and chosen cells
static void addPickerLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_PICKER].level;
    if (level > 0.0f && ctx->pickerReady) {
        float pickW, pickH;
        XrPosef pickPose = pickerPose(ctx, &pickW, &pickH);
        atlasQuad(ctx, &layers->picker, fadeNext(ctx, layers, FADE_PICKER, 1, level),
                  UI_PANEL_PICKER, view->space, pickPose, pickW, pickH);
        pushLayer(ctx, layers, &layers->picker);

        if (ctx->outlineReady) {
            float cellW = pickW / (float)PICKER_COLS;
            float cellH = pickH * (float)PICKER_CELL_PX / (float)PICKER_TEX_H;
            int marks[2] = { ctx->pickerHover, ctx->pickerChoice };
            float scales[2] = { 1.0f, 0.84f };
            for (int m = 0; m < 2; m++) {
                int cell = marks[m];
                if (cell < 0 || cell >= ctx->pickerCells) {
                    continue;
                }
                int col = cell % PICKER_COLS;
                int row = cell / PICKER_COLS;
                float centreV = (row * PICKER_BAND_PX + PICKER_HEADER_PX
                                 + PICKER_CELL_PX * 0.5f) / (float)PICKER_TEX_H;
                Vec3 local;
                local.x = ((col + 0.5f) / PICKER_COLS - 0.5f) * pickW;
                local.y = (0.5f - centreV) * pickH;
                local.z = 0.004f;
                atlasQuad(ctx, &layers->outline[m], fadeNext(ctx, layers, FADE_PICKER, 0, level),
                          ATLAS_OUTLINE, view->space, poseOffset(pickPose, local),
                          cellW * scales[m], cellH * scales[m]);
                pushLayer(ctx, layers, &layers->outline[m]);
            }
        }
    }
}

// The settings panel at the pose it was opened with, and the clock strip
// over its top edge. Its rings, thumbs and readouts are in the panel's art.
static void addCogLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_COG].level;
    if (level > 0.0f) {
        atlasQuad(ctx, &layers->cogPanel, fadeNext(ctx, layers, FADE_COG, 1, level),
                  UI_PANEL_COG, view->space, ctx->cogPose, ctx->cogW, ctx->cogH);
        pushLayer(ctx, layers, &layers->cogPanel);
        if (ctx->cogClockReady) {
            float stripW = (float)COG_CLOCK_TEX_W / (float)COG_TEX_W * ctx->cogW;
            float stripH = (float)COG_CLOCK_TEX_H / (float)COG_TEX_H * ctx->cogH;
            Vec3 local = { 0.0f, ctx->cogH * 0.5f + stripH * 0.75f, 0.0f };
            atlasQuad(ctx, &layers->cogClock, fadeNext(ctx, layers, FADE_COG, 1, level),
                      UI_PANEL_CLOCK, view->space, poseOffset(ctx->cogPose, local), stripW, stripH);
            pushLayer(ctx, layers, &layers->cogClock);
        }
    }
}

// The keyboard and the ring on the key under the ray
static void addKeyboardLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    float level = ctx->panelFades[FADE_KB].level;
    if (level > 0.0f) {
        atlasQuad(ctx, &layers->kbPanel, fadeNext(ctx, layers, FADE_KB, 1, level),
                  UI_PANEL_KEYBOARD, view->space, ctx->kbPose, ctx->kbW, ctx->kbH);
        pushLayer(ctx, layers, &layers->kbPanel);

        // Native (owner's request): the keyboard's move bar, the picture's
        // pill under the panel. Always there while the keyboard is, since the
        // keyboard has no bar area to hover into as the picture has; it swells
        // a little when the ray is on it or it is held, which is the lighting
        // a quad layer can give.
        if (ctx->handleArtReady) {
            float pillW, pillH, drop;
            kbBarGeometry(ctx, &pillW, &pillH, &drop);
            float grow = ctx->hoverKind == HOVER_KBBAR ? 1.25f : 1.0f;
            Vec3 local = { 0.0f, -drop, 0.005f };
            atlasQuad(ctx, &layers->kbBar, NULL, ATLAS_HANDLE, view->space,
                      poseOffset(ctx->kbPose, local), pillW * grow, pillH * grow);
            pushLayer(ctx, layers, &layers->kbBar);
        }

        // The Compact keyboard's "Full" chip has its rect in the last slot
        if (ctx->outlineReady && ctx->kbHoverKey >= 0
                && (ctx->kbHoverKey < ctx->kbKeyCount || ctx->kbHoverKey == KB_KEY_LAYOUT)) {
            const float* r = &ctx->kbKeyRects[ctx->kbHoverKey * 4];
            // Swells while the trigger is held, which is the only press
            // feedback a quad layer can give
            float grow = ctx->kbKeyDown ? 1.12f : 1.0f;
            addRing(ctx, layers, &layers->kbMark, view->space, ctx->kbPose, ctx->kbW, ctx->kbH,
                    r[0], r[1], r[2], r[3], grow, grow);
        }
    }
}

// The laser and the cursor at the end of it
static void addPointerLayers(XrCtx* ctx, const FrameView* view, FrameLayers* layers) {
    int beamShown = rayDrawn(ctx->raySetting, ctx->rayFlipped, panelUp(ctx));
    if (beamShown != ctx->rayDrawnSaid) {
        if (ctx->rayDrawnSaid >= 0 || !beamShown) {
            LOGI("ray %s", beamShown ? (raySwitchOn(ctx->raySetting, ctx->rayFlipped)
                                        ? "drawn" : "drawn while a panel is up")
                                     : "hidden, the dot stays");
        }
        ctx->rayDrawnSaid = beamShown;
    }
    // Native: with the ray and the dot off, a faint beam while the controller
    // is in the bar's hover band, so the bar can be found (xr_controller.c)
    int beamKind = rayBeamKind(ctx->raySetting, ctx->rayFlipped, panelUp(ctx), ctx->dotOn, view->barArea);
    int beamSlot = beamKind == RAY_BEAM_RESTING ? ATLAS_BEAM_FAINT : ATLAS_BEAM;
    Vec3 head = { ctx->headPos.x, ctx->headPos.y, ctx->headPos.z };

    // Native (lane radial-dwell): the dwell's circle round the dot, and at the dwell's start its
    // radial menu (or a held drag's mark), under the dot. The menu is drawn whether or not the dot is
    // on anything: the dot may leave the picture to pick.
    XrDwellArt art;
    int dwellArt = ctx->outlineReady && xrDwellMenuArt(ctx, &art);
    if (dwellArt && art.mark.w > 0) {
        Vec3 at = { art.markAt.x, art.markAt.y, art.markAt.z };
        Vec3 toHead = vecSub(head, at);
        float away = sqrtf(vecDot(toHead, toHead));
        if (away > 0.05f) {
            Vec3 z = vecNorm(toHead);
            Vec3 worldUp = { 0.0f, 1.0f, 0.0f };
            Vec3 x = vecNorm(vecCross(worldUp, z));
            Vec3 y = vecCross(z, x);
            XrPosef pose = { quatFromBasis(x, y, z), { at.x + z.x * 0.006f, at.y + z.y * 0.006f, at.z + z.z * 0.006f } };
            float side = art.menuHalfDeg > 0.0f ? 2.0f * away * tanf(art.menuHalfDeg * 0.0174532925f)
                                                : pointerDotSize(away) * xrAssistDotScale(ctx) * art.markSide;
            quadLayerRect(&layers->dwellMark, NULL, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                          xrAtlasSwapchain(ctx), art.mark.x, art.mark.y, art.mark.w, art.mark.h, view->space, pose,
                          side, side);
            pushLayer(ctx, layers, &layers->dwellMark);
        }
    }

    if (ctx->beamVisible && !ctx->beamGaze && ctx->pointerArtReady) {
        Vec3 start = { ctx->beamStart.x, ctx->beamStart.y, ctx->beamStart.z };
        Vec3 end = { ctx->beamEnd.x, ctx->beamEnd.y, ctx->beamEnd.z };
        Vec3 along = vecSub(end, start);
        float length = sqrtf(along.x * along.x + along.y * along.y + along.z * along.z);

        Vec3 mid = { (start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f,
                     (start.z + end.z) * 0.5f };
        Vec3 beamY = vecNorm(along);
        // Native (lane ray-glitch, H4): turned to the head, and leaning to
        // lie along the head's right as the head comes in line with the beam,
        // with no switch to the world's up that flickered the quad edge on
        Vec3 headRight = { ctx->headRight.x, ctx->headRight.y, ctx->headRight.z };
        Vec3 beamX = beamBasis(start, end, head, headRight);

        if (length > 0.10f && beamKind != RAY_BEAM_NONE) {
            Vec3 beamZ = vecCross(beamX, beamY);
            XrPosef beamPose = { quatFromBasis(beamX, beamY, beamZ),
                                 { mid.x, mid.y, mid.z } };
            atlasQuad(ctx, &layers->beam, NULL, beamSlot, view->space, beamPose, ctx->beamWidth,
                      length);
            pushLayer(ctx, layers, &layers->beam);
        }

        // Native: the dot is the owner's to hide (the host draws its own
        // cursor into the picture); a panel up keeps it, being ours
        // Native (lane wave4-headinput, PLAN 6.5): a charging dwell click shows whatever the
        // Ray row says. Native (lane radial-dwell): as a circle in the dot's colour that sweeps round
        // and closes in on the dot (xr_dwellmenu.c), the dot drawn into each of its pictures
        if (!ctx->beamFree && (ctx->dotOn || panelUp(ctx) || ctx->launcher != LAUNCHER_NONE
                               || (dwellArt && art.dot.w > 0))) {
            Vec3 toHead2 = vecSub(head, end);
            float away = sqrtf(vecDot(toHead2, toHead2));
            // native (lane a11y-2): the head and eye pointer's dot, and its ring, at the Dot size
            float dotSize = pointerDotSize(away) * xrAssistDotScale(ctx);
            if (fabsf(dotSize - ctx->dotSizeSaid) > 0.2f * ctx->dotSizeSaid) {
                LOGI("cursor dot %.1f cm across at %.2f m", dotSize * 100.0f, away);
                ctx->dotSizeSaid = dotSize;
            }
            Vec3 dotZ = vecNorm(toHead2);
            Vec3 worldUp = { 0.0f, 1.0f, 0.0f };
            Vec3 dotX = vecNorm(vecCross(worldUp, dotZ));
            Vec3 dotY = vecCross(dotZ, dotX);
            XrPosef dotPose = { quatFromBasis(dotX, dotY, dotZ),
                                { end.x + dotZ.x * 0.012f, end.y + dotZ.y * 0.012f,
                                  end.z + dotZ.z * 0.012f } };
            if (dwellArt && art.dot.w > 0) {
                quadLayerRect(&layers->dot, NULL, XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
                              xrAtlasSwapchain(ctx), art.dot.x, art.dot.y, art.dot.w, art.dot.h, view->space,
                              dotPose, dotSize * art.dotSide, dotSize * art.dotSide);
            }
            else {
                atlasQuad(ctx, &layers->dot, NULL, ATLAS_DOT, view->space, dotPose, dotSize, dotSize);
            }
            pushLayer(ctx, layers, &layers->dot);
        }
    }
}

// The toast, hung off the eyes ahead and a little below where they look
static void addToastLayer(XrCtx* ctx, FrameLayers* layers, int64_t now) {
    int shown = ctx->toastArtReady && noticeShowing(&ctx->notices, now)
            && ctx->toastDrawnKind == ctx->notices.current.kind
            && ctx->toastDrawnArg == ctx->notices.current.arg;
    fadeStep(&ctx->toastFade, shown, now, ctx->fadeNs, ctx->colorScaleSupported);
    float level = ctx->toastFade.level;
    if (level <= 0.0f || !ctx->toastArtReady) {
        return;
    }
    XrPosef pose;
    memset(&pose, 0, sizeof(pose));
    pose.orientation.w = 1.0f;
    pose.position.y = -TOAST_DROP_M;
    pose.position.z = -TOAST_DISTANCE_M;
    atlasQuad(ctx, &layers->toast, fadeNext(ctx, layers, FADE_SLOT_TOAST, 1, level), UI_PANEL_TOAST,
              ctx->viewSpace, pose, TOAST_W_M, TOAST_W_M * TOAST_TEX_H / TOAST_TEX_W);
    pushLayer(ctx, layers, &layers->toast);
}

// Steps each panel's fade toward whether it is showing
static void stepPanelFades(XrCtx* ctx, int64_t now) {
    static const char* const NAMES[FADE_PANELS] = {
        "settings panel", "picker", "keyboard", "exit prompt", "report sheet", "hand lock hint",
        "Ko-fi sheet", "Host sheet"
    };
    int modal = ctx->pickerOpen || ctx->cogOpen || ctx->exitConfirmOpen || ctx->reportOpen
            || ctx->hintOpen || ctx->kofiOpen || ctx->hostOpen;
    int shown[FADE_PANELS];
    shown[FADE_COG] = ctx->cogOpen;
    shown[FADE_PICKER] = ctx->pickerOpen;
    shown[FADE_KB] = ctx->kbOpen && (!modal || ctx->reportOpen);
    shown[FADE_EXIT] = ctx->exitConfirmOpen;
    shown[FADE_REPORT] = ctx->reportOpen;
    shown[FADE_HINT] = ctx->hintOpen;
    shown[FADE_KOFI] = ctx->kofiOpen;
    shown[FADE_HOST] = ctx->hostOpen;
    ctx->panelFadingOut = 0;
    for (int p = 0; p < FADE_PANELS; p++) {
        Fade* fade = &ctx->panelFades[p];
        if (modal && !shown[p]) {
            fade->level = 0.0f;
            fade->running = 0;
        }
        int landed = fadeStep(fade, shown[p], now, ctx->fadeNs, ctx->colorScaleSupported);
        if (landed != FADE_NONE) {
            LOGI("%s faded %s over %ld ms, %d frames", NAMES[p],
                 landed == FADE_IN_DONE ? "in" : "out", (long)((now - fade->fromNs) / 1000000L),
                 fade->frames);
        }
        if (!shown[p] && fade->level > 0.0f) {
            ctx->panelFadingOut = 1;
        }
    }
}

void xrPictureView(XrCtx* ctx, XrPosef* pose, float* width, float* height, XrSpace* space) {
    *pose = ctx->screenPose;
    *width = ctx->screenWidth;
    *height = ctx->screenWidth * (float)ctx->videoHeight / (float)ctx->videoWidth;
    *space = (ctx->headLockedPref && roomEffective(ctx) <= 0) ? ctx->viewSpace : ctx->localSpace;
}

// nativeEndFrame without the video draw and without xrEndFrame
void xrBuildLayers(XrCtx* ctx, const XrFrameIn* in, XrFrameOut* out) {
    ctx->passthrough = in->passthrough;
    ctx->prefCurvature = in->curvature;
    // Native: updateControllerModels runs in the host before the models'
    // pass draws (upstream called it here, before its own draws)

    FrameView view;
    view.aspect = (float)ctx->videoHeight / (float)ctx->videoWidth;
    view.stereo = ctx->stereoMode != DEPTH_MODE_OFF;
    int roomStyle = roomEffective(ctx);
    view.roomOn = roomStyle > 0;
    view.headLocked = in->headLocked;
    view.eyeSwap = in->eyeSwap;
    // A room hangs the picture on a wall, and a wall does not follow the head
    // about however the preference is set
    view.space = (in->headLocked && !view.roomOn) ? ctx->viewSpace : ctx->localSpace;

    view.curve = effectiveCurvature(ctx);
    int reseeded = updatePlacement(ctx, in->distance, in->quadWidth, view.curve);
    applyRoomPlacement(ctx, roomStyle, view.aspect, reseeded);
    if (view.roomOn) {
        view.curve = 0.0f;
    }
    view.screenPose = ctx->screenPose;
    view.screenWidth = ctx->screenWidth;
    view.screenHeight = view.screenWidth * view.aspect;
    view.screenCurved = view.curve > 0.01f && ctx->cylinderSupported;
    view.bar = barFrame(ctx);
    logBarPlacement(ctx, &view.bar);
    int64_t frameNs = nowNs();
    stepPanelFades(ctx, frameNs);
    view.barArea = !ctx->pickerOpen && !ctx->cogOpen && !ctx->kbOpen
            && !ctx->exitConfirmOpen && !ctx->reportOpen && !ctx->hintOpen && !ctx->kofiOpen
            && !ctx->hostOpen && !ctx->panelFadingOut
            && (ctx->hoverKind == HOVER_BAR || ctx->hoverKind == HOVER_ENVBUTTON
                || ctx->hoverKind == HOVER_COGBUTTON
                || ctx->hoverKind == HOVER_KBBUTTON
                || ctx->hoverKind == HOVER_EXITBUTTON
                || ctx->hoverKind == HOVER_STEREOBUTTON
                || ctx->hoverKind == HOVER_RAYBUTTON
                || ctx->hoverKind == HOVER_AIMBUTTON
                || ctx->hoverKind == HOVER_PADBUTTON
                || ctx->hoverKind == HOVER_HOSTBUTTON
                || ctx->hoverKind == HOVER_HEREBUTTON);

    // Some runtimes only bring the cameras up on a change of blend mode seen
    // after the session is focused, so the first focused frame goes opaque
    int wantPassthrough = ctx->passthrough && ctx->alphaBlendSupported;
    int blendNow = wantPassthrough && ctx->focusedFrames > 0;
    out->blend = blendNow ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    if (blendNow && !ctx->passthroughBlendSaid) {
        ctx->passthroughBlendSaid = 1;
        LOGEV("passthrough blend enabled after %d focused frames", ctx->focusedFrames);
    }
    if (ctx->sessionState == XR_SESSION_STATE_FOCUSED) {
        ctx->focusedFrames++;
    }

    FrameLayers* layers = &frameLayers;
    layers->count = 0;
    setLayerSettings(ctx, layers);
    float backdrop[4];
    if (xrBackdropColor(ctx, blendNow, backdrop)) {
        addPassthroughLayer(ctx, layers, backdrop);
    }
    addRoomLayer(ctx, &view, layers);
    addGlowLayer(ctx, &view, layers, 0);
    if (ctx->everRendered && ctx->shouldRender) {
        if (ctx->launcher != LAUNCHER_NONE) {
            addLauncherLayer(ctx, &view, layers);
        }
        else if (ctx->videoShown) {
            addVideoLayers(ctx, &view, layers, in);
            // Feathered, the glow is the layer over the picture's border
            addGlowLayer(ctx, &view, layers, 1);
        }
        addOverlayLayer(ctx, &view, layers);
        addHandleLayer(ctx, &view, layers);
        addBarButtonLayers(ctx, &view, layers);
        addExitPromptLayer(ctx, &view, layers);
        addPickerLayers(ctx, &view, layers);
        addCogLayers(ctx, &view, layers);
        addReportLayer(ctx, &view, layers);
        addHandHintLayers(ctx, &view, layers);
        addKofiLayers(ctx, &view, layers);
        addHostLayers(ctx, &view, layers);
        addKeyboardLayers(ctx, &view, layers);
        addFirstRunLayer(ctx, &view, layers);
        addPadCardLayer(ctx, layers);
        addModelLayer(ctx, layers);
        addPointerLayers(ctx, &view, layers);
    }
    addToastLayer(ctx, layers, frameNs);

    // Over the limit: upstream's order of what goes first (the toast, the
    // controller models, the hover ring, the cog button, the clock strip)
    if (layers->count > (uint32_t)ctx->maxLayerCount) {
        dropLayer(layers, &layers->toast);
    }
    if (layers->count > (uint32_t)ctx->maxLayerCount) {
        dropLayer(layers, &layers->models);
    }
    if (layers->count > (uint32_t)ctx->maxLayerCount) {
        dropLayer(layers, &layers->cogMark[COG_OPTION_COUNT]);
    }
    if (layers->count > (uint32_t)ctx->maxLayerCount) {
        dropLayer(layers, &layers->cogButton);
    }
    if (layers->count > (uint32_t)ctx->maxLayerCount) {
        dropLayer(layers, &layers->cogClock);
    }
    if (layers->count >= (uint32_t)ctx->maxLayerCount && !ctx->layerLimitWarned) {
        ctx->layerLimitWarned = 1;
        LOGW("submitted %u composition layers against a limit of %d",
             layers->count, ctx->maxLayerCount);
    }
    out->count = layers->count;
    memcpy(out->order, layers->order, sizeof(out->order[0]) * layers->count);
}

// Native: the curved picture as mlxr_pass_panel draws it (panel.vert): the
// surface's size and bend, and one eye's mvp, GL conventions (xr_math), with
// the model the picture's own pose. The surface is upstream's cylinder layer
// (axis a radius behind the picture, cylinderFit's scale when it would wrap
// too far), so the ray's hits (screenProject, curved) land on what is drawn.
int xrPictureCurved(XrCtx* ctx) {
    return ctx->cylinderSupported && effectiveCurvature(ctx) > 0.01f && roomEffective(ctx) <= 0;
}

int xrPictureProjected(XrCtx* ctx) {
    if (!ctx->cylinderSupported || ctx->launcher != LAUNCHER_NONE) {
        return 0;
    }
    return ctx->stereoMode != DEPTH_MODE_OFF || xrPictureCurved(ctx);
}

void xrCurveShape(XrCtx* ctx, float* width, float* height, float* radius) {
    float aspect = (float)ctx->videoHeight / (float)ctx->videoWidth;
    if (!xrPictureCurved(ctx)) {
        // Native: flat in 3D, the quad's size on a flat surface
        *width = ctx->screenWidth;
        *height = ctx->screenWidth * aspect;
        *radius = 0.0f;
        return;
    }
    float fit = cylinderFit(ctx->screenWidth, ctx->screenRadius);
    *width = ctx->screenWidth * fit;
    *height = ctx->screenWidth * aspect * fit;
    *radius = ctx->screenRadius;
}

void xrCurveMvp(XrFovf fov, XrPosef eye, XrPosef surface, float* mvp) {
    float proj[16], view[16], model[16], vp[16];
    projectionFromFov(proj, fov, 0.05f, 100.0f);
    viewFromPose(view, eye);
    Vec3 ex = { 1, 0, 0 }, ey = { 0, 1, 0 }, ez = { 0, 0, 1 };
    Vec3 x = quatRotate(surface.orientation, ex);
    Vec3 y = quatRotate(surface.orientation, ey);
    Vec3 z = quatRotate(surface.orientation, ez);
    model[0] = x.x;  model[1] = x.y;  model[2] = x.z;  model[3] = 0.0f;
    model[4] = y.x;  model[5] = y.y;  model[6] = y.z;  model[7] = 0.0f;
    model[8] = z.x;  model[9] = z.y;  model[10] = z.z; model[11] = 0.0f;
    model[12] = surface.position.x;
    model[13] = surface.position.y;
    model[14] = surface.position.z;
    model[15] = 1.0f;
    matMul(vp, proj, view);
    matMul(mvp, vp, model);
}
