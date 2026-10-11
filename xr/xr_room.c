// The 3d rooms: baked models, drawn per eye into the one projection layer
// this renderer has, with the picture hung on the far wall.
//
// Moonlight XR Native: ported from the Steam Frame fork of Moonlight XR's
// xr-renderer/xr_room.c (Gilleece, GPLv3). Kept as upstream wrote them:
// roomEffective, roomLevelsFromTable, roomScreenPercent, roomGlowOn,
// roomSizeDragging, roomScale, roomDim, applyRoomPlacement (the hold and
// restore of the user's own placement), roomAssetsReady, uploadRoomGeometry's
// bookkeeping, buildableRoomStyle, worldEyeSize, initRoom, locateRoomViews,
// prepareRoom, drawRoomEyes' uniforms, renderRoom, nativeSetRoomLevels
// (xrSetRoomLevels). The pure parts upstream kept here as statics (the room
// table, roomParams, roomFarPlane, the vertex build, the dither and the
// light) are rooms/room_logic.c's, under the same names. Native, each marked:
//   - the model and its atlases are read off the assets by a loader thread
//     (upstream: Java read them, nativeUploadRoomModel/Atlas took them on the
//     frame loop), which records the upload into a command buffer of its own
//     that the host submits ahead of the frame; "the assets have arrived" is
//     that room being resident (rooms/room_gpu's MlxrRoom: buffers, ASTC
//     atlases);
//   - a room replaced is freed once the GPU has finished the last frame that
//     drew it; a rescale waits the same way;
//   - the draw is mlxr_pass_room into the swapchain image, recorded into the
//     frame's command buffer; the image is released after the submission;
//   - the clear colour, gamma encoded as upstream wrote it, is decoded for the
//     _SRGB target (the pass's target encodes on write);
//   - the GL timer query is the host's timestamps (xr_world.c);
//   - the debug properties are environment variables (MLXR_ROOM,
//     MLXR_ROOM_SCALE, MLXR_ROOM_DIM; xr_prefs.c).
#include "xr_world.h"

void xrWorldQueueUpload(XrCtx* ctx, VkCommandPool pool, VkCommandBuffer cmd, MlxrRoom* room, MlxrModel* model);
void xrWorldRetireRoom(XrCtx* ctx, MlxrRoom* room, uint64_t lastUse);

// ---- native: the loader ------------------------------------------------------------------

typedef struct XrRoomLoader {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t wake;
    pthread_cond_t idle;
    int started;
    int quit;
    MlxrGpu gpu;
    char assetsDir[512];
    // What the frame loop asked for, 0 for nothing
    int askStyle;
    float askScale;
    int busy;
    // What it made: the room, and the pool its upload is recorded in
    MlxrRoom* done;
    VkCommandPool donePool;
    VkCommandBuffer doneCmd;
    double doneMs;
    // A style that would not load, so it is not asked for again
    int failedStyle;
    // Last use of the resident room, by the serial of the frame that drew it
    uint64_t lastUse;
} XrRoomLoader;

static void* loaderMain(void* arg) {
    XrRoomLoader* l = arg;
    pthread_mutex_lock(&l->lock);
    for (;;) {
        while (!l->quit && (l->askStyle <= 0 || l->done != NULL)) {
            pthread_cond_wait(&l->wake, &l->lock);
        }
        if (l->quit) {
            break;
        }
        int style = l->askStyle;
        float scale = l->askScale;
        l->askStyle = 0;
        l->busy = style;
        pthread_mutex_unlock(&l->lock);

        int64_t started = nowNs();
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        MlxrRoom* room = NULL;
        VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = l->gpu.queueFamily;
        int ok = vkCreateCommandPool(l->gpu.device, &pci, NULL, &pool) == VK_SUCCESS;
        if (ok) {
            VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            cai.commandPool = pool;
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cai.commandBufferCount = 1;
            ok = vkAllocateCommandBuffers(l->gpu.device, &cai, &cmd) == VK_SUCCESS;
        }
        if (ok) {
            VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(cmd, &bi);
            MlxrRoomLoadInfo li;
            memset(&li, 0, sizeof(li));
            li.assetsDir = l->assetsDir;
            li.style = style;
            li.scale = scale;
            ok = mlxr_room_record(&l->gpu, &li, cmd, &room) == 0;
            vkEndCommandBuffer(cmd);
        }
        pthread_mutex_lock(&l->lock);
        l->busy = 0;
        if (ok) {
            l->done = room;
            l->donePool = pool;
            l->doneCmd = cmd;
            l->doneMs = (nowNs() - started) / 1e6;
        }
        else {
            if (pool) vkDestroyCommandPool(l->gpu.device, pool, NULL);
            l->failedStyle = style;
        }
        pthread_cond_broadcast(&l->idle);
    }
    pthread_mutex_unlock(&l->lock);
    return NULL;
}

static XrRoomLoader* roomLoader(XrCtx* ctx) {
    XrRoomLoader* l = ctx->roomLoader;
    if (l != NULL) {
        return l;
    }
    l = calloc(1, sizeof(*l));
    pthread_mutex_init(&l->lock, NULL);
    pthread_cond_init(&l->wake, NULL);
    pthread_cond_init(&l->idle, NULL);
    l->gpu = xrWorldGpu(ctx);
    snprintf(l->assetsDir, sizeof(l->assetsDir), "%s", ctx->assetsDir);
    if (pthread_create(&l->thread, NULL, loaderMain, l) == 0) {
        l->started = 1;
    }
    else {
        LOGE("room loader thread could not start");
    }
    ctx->roomLoader = l;
    return l;
}

// Whether everything a baked room is made of has arrived and belongs to the
// room being asked about. One room is resident, so a style whose turn it is
// waits here while its own set is read. (Native: the resident MlxrRoom.)
static int roomAssetsReady(XrCtx* ctx, int style) {
    return ctx->room != NULL && ctx->room->style == style;
}

// Native: a room the loader has finished becomes the resident one (the one
// before goes once the GPU is done with it), and the room wanted is asked for
// when it is neither resident nor on its way
static void roomPollLoader(XrCtx* ctx) {
    XrRoomLoader* l = roomLoader(ctx);
    int wanted = roomEffective(ctx);
    pthread_mutex_lock(&l->lock);
    if (l->done != NULL && l->done->style != wanted && roomAssetsReady(ctx, wanted)) {
        // The picker came back to the resident room while another was read:
        // the one read goes (its upload was never submitted)
        MlxrGpu g = xrWorldGpu(ctx);
        LOGI("room %d read but no longer wanted, dropped", l->done->style);
        mlxr_room_unload(&g, l->done);
        vkDestroyCommandPool(g.device, l->donePool, NULL);
        l->done = NULL;
        l->donePool = VK_NULL_HANDLE;
        l->doneCmd = VK_NULL_HANDLE;
        pthread_cond_broadcast(&l->wake);
    }
    if (l->done != NULL) {
        MlxrRoom* r = l->done;
        if (ctx->room != NULL) {
            xrWorldRetireRoom(ctx, ctx->room, l->lastUse);
        }
        ctx->room = r;
        // Whatever the buffers held belonged to the room just retired
        ctx->roomBuiltStyle = 0;
        xrWorldQueueUpload(ctx, l->donePool, l->doneCmd, r, NULL);
        LOGEV("room model ready, style %d, %u vertices, %u triangles, %u parts, %u atlases %ux%u ASTC %ux%u, "
              "%d levels, %.1f MB on the GPU, read in %.1f ms off the frame loop",
              r->style, r->vertexCount, r->indexCount / 3, r->partCount, r->atlasCount,
              r->atlasCount ? r->atlas[0].width : 0, r->atlasCount ? r->atlas[0].height : 0,
              r->atlasCount ? r->atlas[0].blockWidth : 0, r->atlasCount ? r->atlas[0].blockHeight : 0,
              r->atlasCount ? (int)r->atlas[0].levels : 0, r->gpuBytes / (1024.0 * 1024.0), l->doneMs);
        l->done = NULL;
        l->donePool = VK_NULL_HANDLE;
        l->doneCmd = VK_NULL_HANDLE;
        l->lastUse = 0;
        pthread_cond_broadcast(&l->wake);
    }
    if (bakedRoomStyle(wanted) && !roomAssetsReady(ctx, wanted) && l->busy != wanted && l->askStyle != wanted
            && l->failedStyle != wanted && l->started) {
        l->askStyle = wanted;
        l->askScale = roomScaleFor(ctx->roomScaleOverride, wanted);
        LOGEV("room %d asked for, reading its model and atlas", wanted);
        pthread_cond_broadcast(&l->wake);
    }
    pthread_mutex_unlock(&l->lock);
}

void xrRoomWaitLoader(XrCtx* ctx) {
    XrRoomLoader* l = roomLoader(ctx);
    roomPollLoader(ctx);
    pthread_mutex_lock(&l->lock);
    while (l->started && (l->busy || l->askStyle > 0)) {
        pthread_cond_wait(&l->idle, &l->lock);
    }
    pthread_mutex_unlock(&l->lock);
    roomPollLoader(ctx);
}

int xrRoomResident(XrCtx* ctx) {
    return ctx->room != NULL ? ctx->room->style : 0;
}

int xrRoomLoading(XrCtx* ctx) {
    XrRoomLoader* l = ctx->roomLoader;
    if (l == NULL) {
        return 0;
    }
    pthread_mutex_lock(&l->lock);
    int busy = l->busy || l->askStyle > 0 || l->done != NULL;
    pthread_mutex_unlock(&l->lock);
    return busy;
}

void xrRoomDestroy(XrCtx* ctx) {
    XrRoomLoader* l = ctx->roomLoader;
    MlxrGpu g = xrWorldGpu(ctx);
    if (l != NULL) {
        pthread_mutex_lock(&l->lock);
        l->quit = 1;
        pthread_cond_broadcast(&l->wake);
        pthread_mutex_unlock(&l->lock);
        if (l->started) {
            pthread_join(l->thread, NULL);
        }
        if (l->done != NULL) {
            mlxr_room_unload(&g, l->done);
            vkDestroyCommandPool(g.device, l->donePool, NULL);
        }
        pthread_mutex_destroy(&l->lock);
        pthread_cond_destroy(&l->wake);
        pthread_cond_destroy(&l->idle);
        free(l);
        ctx->roomLoader = NULL;
    }
    if (ctx->room != NULL) {
        xrWorldRetireRoom(ctx, ctx->room, 0);
        ctx->room = NULL;
    }
    ctx->roomBuiltStyle = 0;
}

// ---- upstream ----------------------------------------------------------------------------

// Which room is in force, the picker's unless the debug property has taken it
// over. 0 is no room at all, which is every other environment.
int roomEffective(XrCtx* ctx) {
    return roomEffectiveStyle(ctx->roomOverride, ctx->roomStyle);
}

// Every room's own values as its row starts them, which is where they stay
// until the preferences are handed down. The light level starts at the same
// place in every room.
void roomLevelsFromTable(XrCtx* ctx) {
    for (int style = ROOM_STYLE_FIRST; style <= ROOM_STYLE_LAST; style++) {
        const RoomModel* m = roomModel(style);
        ctx->roomBrightness[style] = (int)roundf(m->dim * 100.0f);
        ctx->roomGlow[style] = m->glow != 0;
        ctx->roomLightLevel[style] = ROOM_LIGHT_DEFAULT;
        ctx->roomScreen[style] = (int)roundf(m->screenFraction * 100.0f);
    }
}

// How much of its anchor a style's room hangs the picture on, in whole
// percent. A room that cannot be resized hangs the whole of it whatever was
// saved for it.
int roomScreenPercent(XrCtx* ctx, int style) {
    if (!bakedRoomStyle(style)) {
        return ROOM_SCREEN_MAX;
    }
    return roomScreenClamp(ctx->roomScreen[style], roomResizable(style));
}

// Whether the glow goes up around the picture. In a room that is the room's
// own switch, and anywhere else the one app wide switch.
int roomGlowOn(XrCtx* ctx, int style) {
    return bakedRoomStyle(style) ? ctx->roomGlow[style] : ctx->ambilightOn;
}

// Whether the picture's size is under a hand or a thumb right now, which is
// when its every step would otherwise be a line in the log
static int roomSizeDragging(XrCtx* ctx) {
    return ctx->grabMode == GRAB_RESIZE
            || (ctx->cogDragSlider == COG_ROOM_ROW_SIZE && ctx->cogDragFace == COG_FACE_ROOM);
}

// How large a style is drawn, which is the size it was built at unless a
// property set inside the range says otherwise
static float roomScale(XrCtx* ctx, int style) {
    return roomScaleFor(ctx->roomScaleOverride, style);
}

// How far down the room is turned as it draws, which is the room's own
// brightness row, and the property wins over the row
static float roomDim(XrCtx* ctx) {
    return roomDimFor(ctx->roomDimOverride, ctx->roomBuiltStyle, ctx->roomBrightness, ctx->roomDim);
}

// In a 3d room the picture hangs on the far wall, so the placement the sliders
// and the grab produce is put aside on the way in and handed back on the way
// out. Nothing is written to preferences either way: what the user set up in a
// normal environment is still there when they come back to one.
void applyRoomPlacement(XrCtx* ctx, int style, float aspect, int reseeded) {
    int roomOn = style > 0;
    if (roomOn && !ctx->roomHoldingScreen) {
        ctx->savedScreenPose = ctx->screenPose;
        ctx->savedScreenWidth = ctx->screenWidth;
        ctx->savedScreenRadius = ctx->screenRadius;
        ctx->roomHoldingScreen = 1;
        // Anything held would spend the rest of the drag fighting the wall
        ctx->grabMode = GRAB_NONE;
    }
    else if (roomOn && reseeded) {
        // The panel's reset landed while the room had the screen. What it
        // seeded is the placement that should be waiting when the room ends.
        ctx->savedScreenPose = ctx->screenPose;
        ctx->savedScreenWidth = ctx->screenWidth;
        ctx->savedScreenRadius = ctx->screenRadius;
    }
    else if (!roomOn && ctx->roomHoldingScreen) {
        ctx->screenPose = ctx->savedScreenPose;
        ctx->screenWidth = ctx->savedScreenWidth;
        ctx->screenRadius = ctx->savedScreenRadius;
        ctx->roomHoldingScreen = 0;
        // A room's corner still held as the room went would carry on as a free
        // resize of the placement just handed back
        ctx->grabMode = GRAB_NONE;
        if (ctx->recentredInRoom) {
            ctx->recentredInRoom = 0;
            ctx->poseDirty = 1;
        }
    }
    if (!roomOn) {
        ctx->roomPlacedStyle = 0;
        return;
    }

    // The same scale the geometry was built at, so the picture and the walls
    // around it never disagree
    RoomParams p = roomParams(style, roomScale(ctx, style));
    // The room says how big its picture is, not the size slider: the anchor is
    // a known size and the picture is fitted inside it with its own shape
    // kept, so a taller film loses width rather than running up the wall, and
    // hung at the share of that the room's size row says
    float width = p.screenWidth;
    if (aspect > 0.0f && p.screenHeight > 0.0f && width * aspect > p.screenHeight) {
        width = p.screenHeight / aspect;
    }
    width *= roomScreenPercent(ctx, style) / 100.0f;
    // The clamps only catch a room whose own anchor does not fit its wall, and
    // an open room has no wall to fit: its anchor is hung as it is written
    if (!p.open) {
        float maxWidth = 2.0f * p.halfWidth - 0.4f;
        float maxHeight = (p.ceilingY - p.floorY) - 0.3f;
        if (width > maxWidth) {
            width = maxWidth;
        }
        if (width * aspect > maxHeight) {
            width = maxHeight / aspect;
        }
    }
    float height = width * aspect;
    // And hung where the whole of it is on the wall rather than through the
    // floor or the ceiling. The floor here is the one under the picture, not
    // the tier the viewer is on. An open room is left alone: its picture is
    // meant to stand above the room.
    float mount = p.screenMountY;
    if (!p.open) {
        float lowest = p.screenFloorY + height * 0.5f + 0.1f;
        float highest = p.ceilingY - height * 0.5f - 0.1f;
        if (mount < lowest) {
            mount = lowest;
        }
        if (mount > highest) {
            mount = highest;
        }
    }

    // Square to the wall and facing the viewer, the same identity orientation
    // the placement starts out with
    memset(&ctx->screenPose, 0, sizeof(ctx->screenPose));
    ctx->screenPose.orientation.w = 1.0f;
    ctx->screenPose.position.x = p.screenAt.x;
    ctx->screenPose.position.y = mount;
    ctx->screenPose.position.z = p.screenZ + p.screenProud;
    ctx->screenWidth = width;
    // Once per room and picture shape rather than every frame, and once for a
    // resize, when it lets go, rather than for every step of it
    if (style != ctx->roomPlacedStyle
            || (fabsf(width - ctx->roomPlacedWidth) > 0.001f && !roomSizeDragging(ctx))) {
        ctx->roomPlacedStyle = style;
        ctx->roomPlacedWidth = width;
        LOGEV("room %d hangs the picture %.2f by %.2f m, centre y %.2f z %.2f",
              style, width, height, mount, ctx->screenPose.position.z);
    }
}

// Builds a style's room for the buffers. Natively the vertices were built
// with the loader (rooms/room_gpu, the same buildModelRoomVertices); what is
// left is upstream's bookkeeping, and a rebuild at another scale.
static int uploadRoomGeometry(XrCtx* ctx, int style) {
    // No room, which is what a room whose assets have not arrived asks for.
    // The layer clears black until they do.
    if (style <= 0) {
        ctx->roomFarZ = ROOM_FAR_MIN_M;
        ctx->roomClear[0] = 0.0f;
        ctx->roomClear[1] = 0.0f;
        ctx->roomClear[2] = 0.0f;
        return 1;
    }
    if (!bakedRoomStyle(style) || !roomAssetsReady(ctx, style)) {
        return 0;
    }
    MlxrRoom* r = ctx->room;
    float scale = roomScale(ctx, style);
    if (fabsf(scale - r->scale) > 1e-4f) {
        // Native: the vertex buffer is rewritten, so only once the GPU has
        // finished every frame that drew from it
        XrRoomLoader* l = roomLoader(ctx);
        if (l->lastUse > ctx->gpuDone || ctx->frameCmd == VK_NULL_HANDLE) {
            vkDeviceWaitIdle(ctx->vk->device);
        }
        MlxrGpu g = xrWorldGpu(ctx);
        if (mlxr_room_rescale(&g, r, scale, ctx->frameCmd) != 0) {
            LOGE("room geometry rebuild at scale %.2f failed", scale);
            return 0;
        }
        xrWorldQueueUpload(ctx, VK_NULL_HANDLE, VK_NULL_HANDLE, r, NULL);
    }
    ctx->roomFarZ = r->farZ;
    ctx->roomSpillGain = r->spillGain;
    ctx->roomTexMix = r->texMix;
    ctx->roomDim = r->dim;
    // A textured room has no wall shade to take this from, and its shell is
    // closed, so all this covers is the frame before the first draw
    ctx->roomClear[0] = ROOM_CLEAR_R;
    ctx->roomClear[1] = ROOM_CLEAR_G;
    ctx->roomClear[2] = ROOM_CLEAR_B;
    LOGEV("room ready, style %d, scale %.2f, %u vertices, %u indices, %u parts, far %.0f m, "
          "open %d, resizable %d, screen %d percent of its anchor, glow %s, brightness %d, "
          "light level %d", style, scale, r->vertexCount, r->indexCount, r->partCount,
          (double)ctx->roomFarZ, r->params.open, roomResizable(style),
          roomScreenPercent(ctx, style), ctx->roomGlow[style] ? "on" : "off",
          ctx->roomBrightness[style], ctx->roomLightLevel[style]);
    return 1;
}

// Which style can actually be built at this moment: 0 until its model and
// atlas have been read, which is a moment of void rather than a wrong room
static int buildableRoomStyle(XrCtx* ctx, int style) {
    return roomAssetsReady(ctx, style) ? style : 0;
}

// How big each eye of the room's pass is drawn: what the runtime recommends per
// eye, capped by the chosen tier (rooms/room_logic's roomEyeSize, the same
// sums). The controller models draw at the same size.
void worldEyeSize(XrCtx* ctx, int* outW, int* outH) {
    roomEyeSize(ctx->envResTier, ctx->recommendedEyeWidth, ctx->recommendedEyeHeight, ctx->maxEyeWidth,
                ctx->maxEyeHeight, outW, outH);
    // Native: inside the width SteamVR shows without downsampling it again
    roomEyeFitScene(ctx->envResTier, outW, outH);
}

// Brings up everything the room draws with, the first frame that asks for it.
// A swapchain made mid session, the way the panels' art arrives.
static int initRoom(XrCtx* ctx) {
    if (ctx->roomReady) {
        return 1;
    }
    if (ctx->roomFailed || (ctx->session == XR_NULL_HANDLE && !ctx->offscreen) || ctx->passes == NULL) {
        return 0;
    }
    ctx->roomFailed = 1;

    int tier = ctx->envResTier;
    int eyeW;
    int eyeH;
    worldEyeSize(ctx, &eyeW, &eyeH);

    // Side by side, the same arrangement the video swapchain uses in stereo
    if (!ctx->offscreen) {
        if (!createArtSwapchain(ctx, eyeW * ROOM_EYES, eyeH, "create room swapchain", &ctx->roomSwapchain,
                                &ctx->roomImages, &ctx->roomImageViews, &ctx->roomImageCount)) {
            return 0;
        }
        LOGEV("swapchains alive %d with the room's", ctx->swapchainsAlive);
    }
    // Native: the pipeline built now rather than on the first draw
    mlxr_passes_warm(ctx->passes, MLXR_PASS_ROOM, MLXR_VIDEO_YUV420P, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_D32_SFLOAT);

    // Whichever room is being asked for, so the first frame is already the one
    // the picker is on rather than a rebuild later
    roomPollLoader(ctx);
    int wanted = roomEffective(ctx);
    int style = buildableRoomStyle(ctx, wanted);
    if (!uploadRoomGeometry(ctx, style)) {
        return 0;
    }
    ctx->roomBuiltStyle = style;
    ctx->roomWantedStyle = wanted;
    ctx->roomAssetsSeen = roomAssetsReady(ctx, wanted);
    ctx->roomWantedScale = roomScale(ctx, style);

    ctx->roomEyeWidth = eyeW;
    ctx->roomEyeHeight = eyeH;
    ctx->roomReady = 1;
    ctx->roomFailed = 0;
    const char* tierName = tier == ENV_RES_LOW ? "low"
                         : tier == ENV_RES_HIGH ? "high"
                         : tier == ENV_RES_ULTRA ? "ultra"
                         : "standard";
    LOGEV("room ready at %dx%d per eye, env res %s%s", eyeW, eyeH, tierName,
          eyeW * ROOM_EYES > ROOM_SCENE_MAX_WIDTH ? " (wider than SteamVR's 3458, which it downsamples each frame)"
                                                  : "");
    return 1;
}

// Where both eyes are this frame. Only the room needs this, so it is only
// asked for while a room is on.
static int locateRoomViews(XrCtx* ctx) {
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
    // Both bits, since a room drawn from an orientation with no position in it
    // would sit still while the head moves through the walls
    XrViewStateFlags needed = XR_VIEW_STATE_ORIENTATION_VALID_BIT
            | XR_VIEW_STATE_POSITION_VALID_BIT;
    if ((state.viewStateFlags & needed) != needed) {
        return 0;
    }

    for (int eye = 0; eye < ROOM_EYES; eye++) {
        ctx->roomViews[eye] = views[eye];
    }
    ctx->roomViewsValid = 1;
    return 1;
}

// Everything the room has to have built or rebuilt before it can be drawn.
void prepareRoom(XrCtx* ctx) {
    if (!initRoom(ctx)) {
        return;
    }
    // Native: the loader's news first, the way upstream's assets arrived from
    // Java between frames
    roomPollLoader(ctx);
    // The picker can move between rooms with the session running, a baked one
    // can be picked before its assets have arrived, and the scale property can
    // move under either. Nothing about any of them changes frame to frame, so
    // the work only happens when the style asked for, the readiness of those
    // assets or the scale has moved: a build that fails leaves whichever room
    // is already in the buffers and is not tried again.
    int wanted = roomEffective(ctx);
    int assets = roomAssetsReady(ctx, wanted);
    int style = buildableRoomStyle(ctx, wanted);
    float scale = roomScale(ctx, style);
    if (wanted == ctx->roomWantedStyle && assets == ctx->roomAssetsSeen
            && scale == ctx->roomWantedScale && (style == 0 || ctx->roomBuiltStyle == style)) {
        return;
    }
    // Decided before the ask is recorded, since the scale is part of both
    int rebuild = style != ctx->roomBuiltStyle || scale != ctx->roomWantedScale;
    ctx->roomWantedStyle = wanted;
    ctx->roomAssetsSeen = assets;
    ctx->roomWantedScale = scale;

    if (rebuild && uploadRoomGeometry(ctx, style)) {
        ctx->roomBuiltStyle = style;
    }
}

// The room itself, both eyes into their halves of the image (upstream
// drawRoomEyes and the clear in renderRoom; native: mlxr_pass_room)
void xrRoomRecord(XrCtx* ctx, VkCommandBuffer cmd, VkImageView colour, VkFormat format, VkImageView depth,
                  int eyeW, int eyeH, const XrView* views) {
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = colour;
    t.format = format;
    t.rect.extent = (VkExtent2D){ (uint32_t)(eyeW * ROOM_EYES), (uint32_t)eyeH };
    t.clear = 1;
    // Native: upstream's clear colour is gamma encoded and went into the image
    // as it was; an _SRGB target encodes the clear, so it is decoded first
    for (int c = 0; c < 3; c++) {
        float v = ctx->roomClear[c];
        t.clearColor[c] = v <= 0.04045f ? v / 12.92f : powf((v + 0.055f) / 1.055f, 2.4f);
    }
    t.clearColor[3] = 1.0f;

    MlxrRoomParams rp;
    memset(&rp, 0, sizeof(rp));
    int built = ctx->roomBuiltStyle;
    if (built > 0 && roomAssetsReady(ctx, built)) {
        mlxr_room_fill(ctx->room, &rp);
    }
    // Nothing has been sampled off the video yet on the first frames, so the
    // room is just its baked self until there is, and the same for the option
    // turned off: the baked colours and the atlas stay, only the light the
    // picture throws goes. How much of the room's gain goes on is the room's
    // own light level.
    int lit = ctx->ambiSeeded && ctx->roomLightOn;
    rp.ambi = ctx->ambiTexture.view;
    rp.eyes = ROOM_EYES;
    for (int eye = 0; eye < ROOM_EYES; eye++) {
        // Near enough to walk into a wall without it clipping, far enough to
        // hold whatever this room reaches to
        roomEyeViewProj(rp.viewProj[eye], views[eye].fov, views[eye].pose, ctx->roomFarZ);
        rp.eyeRect[eye].offset.x = eye * eyeW;
        rp.eyeRect[eye].extent = (VkExtent2D){ (uint32_t)eyeW, (uint32_t)eyeH };
    }
    rp.spillGain = roomSpillGainFor(lit, built, ctx->roomSpillGain, ctx->roomLightLevel);
    rp.dim = roomDim(ctx);
    rp.depth = depth;
    rp.depthFormat = VK_FORMAT_D32_SFLOAT;
    // Nothing until the room's model and atlas have both landed: the clear is
    // the whole pass until they do (a pass with no parts)
    mlxr_pass_room(ctx->passes, cmd, &t, &rp);
}

// Native: frames the room was drawn on and frames it was asked for but kept an
// older image (eyes not located, image not acquired), for the 5 s stats line
static int g_roomDrawn, g_roomStale;

void xrRoomTakeCounts(int* drawn, int* stale) {
    *drawn = g_roomDrawn;
    *stale = g_roomStale;
    g_roomDrawn = g_roomStale = 0;
}

// Draws the room into its own image, one half per eye. The layer that shows it
// is submitted in xrBuildLayers, with the very poses drawn from here.
void renderRoom(XrCtx* ctx) {
    if (!ctx->roomReady || ctx->frameCmd == VK_NULL_HANDLE || ctx->ambiTexture.view == VK_NULL_HANDLE) {
        return;
    }
    // A frame the eyes could not be located for keeps the image it already
    // has. The layer still goes up, with the poses that image was drawn from.
    if (!locateRoomViews(ctx)) {
        g_roomStale++;
        return;
    }
    uint32_t index = 0;
    if (!xrArtAcquire(ctx->roomSwapchain, "acquire room image", &index)) {
        g_roomStale++;
        return;
    }
    g_roomDrawn++;
    VkCommandBuffer cmd = ctx->frameCmd;
    xrGpuTimeBegin(ctx, GPU_ROOM);
    mlxr_barrier(cmd, ctx->roomImages[index].image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkImageView depth = xrWorldDepth(ctx, ctx->roomEyeWidth * ROOM_EYES, ctx->roomEyeHeight);
    if (depth != VK_NULL_HANDLE) {
        xrRoomRecord(ctx, cmd, ctx->roomImageViews[index], VK_FORMAT_R8G8B8A8_SRGB, depth, ctx->roomEyeWidth,
                     ctx->roomEyeHeight, ctx->roomViews);
    }
    xrGpuTimeEnd(ctx, GPU_ROOM);
    ctx->roomAcquired = 1;
    if (!ctx->roomRendered) {
        LOGEV("room drawn for the first time (style %d built)", ctx->roomBuiltStyle);
    }
    ctx->roomRendered = 1;
    if (ctx->roomLoader != NULL) {
        ctx->roomLoader->lastUse = ctx->gpuSerial;
    }
}

// One room's own values for the Room tab's rows, as its preferences keep them,
// by the picker cell that shows it. Anything out of its lane is brought back
// inside it. (upstream nativeSetRoomLevels)
void xrSetRoomLevels(XrCtx* ctx, int cell, int brightness, int glow, int light, int screen) {
    if (ctx == NULL) {
        return;
    }
    int style = roomStyleForCell(cell);
    if (!bakedRoomStyle(style)) {
        LOGW("room levels for cell %d, which is not a baked room, ignoring it", cell);
        return;
    }
    ctx->roomBrightness[style] = brightness < ROOM_BRIGHTNESS_MIN ? ROOM_BRIGHTNESS_MIN
            : (brightness > ROOM_BRIGHTNESS_MAX ? ROOM_BRIGHTNESS_MAX : brightness);
    ctx->roomGlow[style] = glow ? 1 : 0;
    ctx->roomLightLevel[style] = light < ROOM_LIGHT_MIN ? ROOM_LIGHT_MIN
            : (light > ROOM_LIGHT_MAX ? ROOM_LIGHT_MAX : light);
    ctx->roomScreen[style] = roomScreenClamp(screen, roomResizable(style));
    LOGEV("room %d levels: brightness %d, glow %s, light level %d, screen %d percent",
          style, ctx->roomBrightness[style], ctx->roomGlow[style] ? "on" : "off",
          ctx->roomLightLevel[style], ctx->roomScreen[style]);
}
