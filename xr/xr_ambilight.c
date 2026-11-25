// The ambilight: the frame boiled down to a tiny colour texture, the
// letterbox detection that keeps black bars out of it, and the glow quad
// drawn from it behind the screen.
//
// Moonlight XR Native: ported from the Steam Frame fork of Moonlight XR's
// xr-renderer/xr_ambilight.c (Gilleece, GPLv3). ambiEffective, ambiBarEdge,
// detectAmbiBars, the order and the rules of runFrameColorSample,
// runAmbiBarDetect, finishAmbiBarDetect, glowStale and redrawGlow are
// upstream's; the glow itself is the Backlight (issue #7), after FramePlayerX's.
// Native, each marked:
//   - the GL program is mlxr_pass_ambi recording into the frame's command
//     buffer, the textures images of the app's own;
//   - the frame comes in as an MlxrVideo (the decoder's NV12 import or the
//     software planes) rather than the external OES texture and its matrix;
//   - the glow is drawn into an image of the app's own and copied into its
//     swapchain when it goes up flat (xr_world.c), since the curved picture's
//     pass samples it again every display frame;
//   - the detector's pixel buffer is a host visible buffer the copy lands in,
//     read once the frame that copied it has finished (the host waits for it
//     at the start of the next);
//   - rows are top first (the decoder's order), so the edge upstream calls
//     bottom, the readback's row 0, is the picture's top: the log line says so.
#include "xr_native.h"
#include "xr_world.h"

// The images the ambilight draws into, and the detector's readback. Set up
// whatever the depth settings are, since the glow needs no depth of any kind.
int initAmbilight(XrCtx* ctx) {
    if (ctx->ambiReady) {
        return 1;
    }
    if (ctx->vk == NULL || ctx->passes == NULL || ctx->frameCmd == VK_NULL_HANDLE) {
        return 0;
    }
    ctx->ambiFailed = 1;
    const VkImageUsageFlags rw = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!xrVkImageMake(ctx, &ctx->ambiTexture, AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX, MLXR_PASS_FORMAT,
                       rw | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            // The letterbox detector's own target: drawn once in a while and
            // read back, never sampled
            || !xrVkImageMake(ctx, &ctx->ambiDetectTexture, AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX, MLXR_PASS_FORMAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT)
            // Native: the glow itself, gamma encoded bytes in a _UNORM image
            || !xrVkImageMake(ctx, &ctx->glowTexture, GLOW_TEX, GLOW_TEX, MLXR_PASS_FORMAT,
                              rw | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            // Native (issue #7): the Backlight's colours, tiny and linear
            || !xrVkImageMake(ctx, &ctx->glowZonesTexture, MLXR_GLOW_ZONES, 1, MLXR_GLOW_LIGHT_FORMAT, rw,
                              VK_IMAGE_ASPECT_COLOR_BIT)
            || !xrVkImageMake(ctx, &ctx->glowSpreadTexture, MLXR_GLOW_SPREAD_W, MLXR_GLOW_SPREAD_H,
                              MLXR_GLOW_LIGHT_FORMAT, rw, VK_IMAGE_ASPECT_COLOR_BIT)
            // and its glow for the panel pass (read back by the tests)
            || !xrVkImageMake(ctx, &ctx->glowLightTexture, GLOW_TEX, GLOW_TEX, MLXR_GLOW_LIGHT_FORMAT,
                              rw | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT)) {
        LOGE("ambilight images could not be made");
        return 0;
    }
    // Where the detector's readback lands, to be looked at a frame later
    // rather than waited for
    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = AMBI_SAMPLE_TEX * AMBI_SAMPLE_TEX * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (vkCreateBuffer(ctx->vk->device, &bi, NULL, &ctx->ambiDetectBuffer) != VK_SUCCESS) {
        return 0;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(ctx->vk->device, ctx->ambiDetectBuffer, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(ctx->vk, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX
            || vkAllocateMemory(ctx->vk->device, &ai, NULL, &ctx->ambiDetectMemory) != VK_SUCCESS) {
        return 0;
    }
    vkBindBufferMemory(ctx->vk->device, ctx->ambiDetectBuffer, ctx->ambiDetectMemory, 0);
    vkMapMemory(ctx->vk->device, ctx->ambiDetectMemory, 0, VK_WHOLE_SIZE, 0, &ctx->ambiDetectMap);

    // Native: the sample starts black and readable, since the room's pass
    // binds it whether or not anything has been sampled yet
    VkClearColorValue black = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    xrVkImageTo(ctx, &ctx->ambiTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdClearColorImage(ctx->frameCmd, ctx->ambiTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                         &range);
    xrVkImageTo(ctx, &ctx->ambiTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ctx->ambiReady = 1;
    ctx->ambiFailed = 0;
    LOGI("ambilight ready, a %dx%d colour sample, %d glow zones, a %dx%d glow", AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX,
         MLXR_GLOW_ZONES, GLOW_TEX, GLOW_TEX);
    return 1;
}

// What the glow is doing this frame. The panel owns it, with the debug
// property over the top of it the way the separation override works. In a
// room the switch is the room's own, and the app wide one everywhere else.
void ambiEffective(XrCtx* ctx, int* on, float* level) {
    // Native (1.3.4): none in passthrough until its sheet's I Understand
    int enabled = roomGlowOn(ctx, roomEffective(ctx)) && !xrPassthroughHidesGlow(ctx);
    float value = ctx->ambiIntensity;
    if (ctx->ambiOverride >= 0) {
        enabled = ctx->ambiOverride > 0;
        value = ctx->ambiOverride / 100.0f;
    }
    *on = enabled;
    *level = value;
}

// The whole frame, which is what the sample pass used before there was a crop
// and what the detector always draws
static const float AMBI_CROP_FULL[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

// One edge of the hysteresis. The crop grows only once a larger bar has held
// for AMBI_BAR_APPLY ticks, and then only to the smallest of them, so a dark
// scene that reads as bar for a moment moves nothing. It shrinks after
// AMBI_BAR_RELEASE ticks, to the largest of those, which is the same idea the
// other way: the least the evidence supports.
static void ambiBarEdge(XrCtx* ctx, int edge, int measured) {
    if (measured > ctx->ambiBarApplied[edge]) {
        ctx->ambiBarShrinkTicks[edge] = 0;
        if (ctx->ambiBarGrowTicks[edge] == 0 || measured < ctx->ambiBarGrowMin[edge]) {
            ctx->ambiBarGrowMin[edge] = measured;
        }
        ctx->ambiBarGrowTicks[edge]++;
        if (ctx->ambiBarGrowTicks[edge] >= AMBI_BAR_APPLY) {
            ctx->ambiBarApplied[edge] = ctx->ambiBarGrowMin[edge];
            ctx->ambiBarGrowTicks[edge] = 0;
        }
        return;
    }
    if (measured < ctx->ambiBarApplied[edge]) {
        ctx->ambiBarGrowTicks[edge] = 0;
        if (ctx->ambiBarShrinkTicks[edge] == 0 || measured > ctx->ambiBarShrinkMax[edge]) {
            ctx->ambiBarShrinkMax[edge] = measured;
        }
        ctx->ambiBarShrinkTicks[edge]++;
        if (ctx->ambiBarShrinkTicks[edge] >= AMBI_BAR_RELEASE) {
            ctx->ambiBarApplied[edge] = ctx->ambiBarShrinkMax[edge];
            ctx->ambiBarShrinkTicks[edge] = 0;
        }
        return;
    }
    ctx->ambiBarGrowTicks[edge] = 0;
    ctx->ambiBarShrinkTicks[edge] = 0;
}

// Counts black bars in a readback of the sample texture and folds the result
// into the applied crop. Rows and columns are named by the readback's own
// order: row 0 is the v = 0 end, which natively is the picture's top. Nothing
// here or downstream cares, because the crop is applied in the same space it
// was measured in. Native: not static, so the tests reach it.
void detectAmbiBars(XrCtx* ctx, const unsigned char* rgba);
void detectAmbiBars(XrCtx* ctx, const unsigned char* rgba) {
    const int n = AMBI_SAMPLE_TEX;
    float rowSum[AMBI_SAMPLE_TEX];
    float colSum[AMBI_SAMPLE_TEX];
    for (int i = 0; i < n; i++) {
        rowSum[i] = 0.0f;
        colSum[i] = 0.0f;
    }
    for (int y = 0; y < n; y++) {
        for (int x = 0; x < n; x++) {
            const unsigned char* p = rgba + ((size_t)y * n + x) * 4;
            // The brightest channel rather than a weighted luma: a saturated
            // blue at the edge of the picture is not a bar, and the weights
            // would nearly call it one
            int m = p[0] > p[1] ? p[0] : p[1];
            if (p[2] > m) {
                m = p[2];
            }
            float l = m * (1.0f / 255.0f);
            rowSum[y] += l;
            colSum[x] += l;
        }
    }

    int rowBar[AMBI_SAMPLE_TEX];
    int colBar[AMBI_SAMPLE_TEX];
    int rowsBar = 0;
    int colsBar = 0;
    for (int i = 0; i < n; i++) {
        rowBar[i] = rowSum[i] * (1.0f / n) < AMBI_BAR_LUMA;
        colBar[i] = colSum[i] * (1.0f / n) < AMBI_BAR_LUMA;
        rowsBar += rowBar[i];
        colsBar += colBar[i];
    }

    // A frame that reads as bar end to end is a fade or a stall, not a
    // letterbox. Each axis gets the rule on its own, which also covers the
    // frame being black.
    int measured[AMBI_EDGES];
    memset(measured, 0, sizeof(measured));
    if (rowsBar < n) {
        while (measured[AMBI_EDGE_BOTTOM] < n && rowBar[measured[AMBI_EDGE_BOTTOM]]) {
            measured[AMBI_EDGE_BOTTOM]++;
        }
        while (measured[AMBI_EDGE_TOP] < n && rowBar[n - 1 - measured[AMBI_EDGE_TOP]]) {
            measured[AMBI_EDGE_TOP]++;
        }
    }
    if (colsBar < n) {
        while (measured[AMBI_EDGE_LEFT] < n && colBar[measured[AMBI_EDGE_LEFT]]) {
            measured[AMBI_EDGE_LEFT]++;
        }
        while (measured[AMBI_EDGE_RIGHT] < n && colBar[n - 1 - measured[AMBI_EDGE_RIGHT]]) {
            measured[AMBI_EDGE_RIGHT]++;
        }
    }

    int changed = 0;
    for (int e = 0; e < AMBI_EDGES; e++) {
        if (measured[e] > AMBI_BAR_MAX) {
            measured[e] = AMBI_BAR_MAX;
        }
        int before = ctx->ambiBarApplied[e];
        ambiBarEdge(ctx, e, measured[e]);
        if (ctx->ambiBarApplied[e] != before) {
            changed = 1;
        }
    }
    if (!changed) {
        return;
    }

    int left = ctx->ambiBarApplied[AMBI_EDGE_LEFT];
    int right = ctx->ambiBarApplied[AMBI_EDGE_RIGHT];
    int bottom = ctx->ambiBarApplied[AMBI_EDGE_BOTTOM];
    int top = ctx->ambiBarApplied[AMBI_EDGE_TOP];
    ctx->ambiCrop[0] = left / (float)n;
    ctx->ambiCrop[1] = bottom / (float)n;
    ctx->ambiCrop[2] = (n - left - right) / (float)n;
    ctx->ambiCrop[3] = (n - bottom - top) / (float)n;
    // Native: row 0 (upstream's "bottom") is the picture's top here
    LOGI("ambilight bars: top %d bottom %d left %d right %d", bottom, top, left, right);
}

int ambiBarDetectOn(const XrCtx* ctx) {
    return ambiBarDetectApplies(ctx->ambiBarDetect, ctx->stereoMode);
}

// Draws the frame uncropped into the detector's target and asks for it back
// into the readback buffer, where finishAmbiBarDetect reads it a frame later
// rather than draining the GPU queue for it here
void runAmbiBarDetect(XrCtx* ctx, const MlxrVideo* video) {
    const int n = AMBI_SAMPLE_TEX;
    if (ctx->ambiDetectBuffer == VK_NULL_HANDLE) {
        return;
    }
    VkCommandBuffer cmd = ctx->frameCmd;
    ctx->ambiDetectTexture.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    xrVkImageTo(ctx, &ctx->ambiDetectTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = ctx->ambiDetectTexture.view;
    t.format = MLXR_PASS_FORMAT;
    t.rect.extent = (VkExtent2D){ (uint32_t)n, (uint32_t)n };
    // The current frame whole and as it arrived. The crop would hide the bars
    // being looked for, the smoothing would drag old ones in for ten frames
    // after a cut, and the picture grade would lift a black bar into a grey
    // one that no longer reads as a bar, so all three are off for this draw.
    MlxrAmbiParams a;
    memset(&a, 0, sizeof(a));
    a.video = *video;
    memcpy(a.crop, AMBI_CROP_FULL, sizeof(a.crop));
    a.gradeOn = 0;
    a.blend = 1.0f;
    mlxr_pass_ambi(ctx->passes, cmd, &t, &a);
    xrVkImageTo(ctx, &ctx->ambiDetectTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy rg;
    memset(&rg, 0, sizeof(rg));
    rg.imageSubresource = (VkImageSubresourceLayers){ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    rg.imageExtent = (VkExtent3D){ (uint32_t)n, (uint32_t)n, 1 };
    vkCmdCopyImageToBuffer(cmd, ctx->ambiDetectTexture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           ctx->ambiDetectBuffer, 1, &rg);
    VkBufferMemoryBarrier hb = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hb.srcQueueFamilyIndex = hb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hb.buffer = ctx->ambiDetectBuffer;
    hb.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &hb, 0,
                         NULL);
    ctx->ambiDetectPending = 1;
    ctx->ambiDetectSerial = ctx->gpuSerial; // native (lane frames-in-flight)
}

// Counts the bars in the readback asked for last time. By now the draw behind
// it has long finished, so the map costs a copy and nothing more.
void finishAmbiBarDetect(XrCtx* ctx) {
    if (!ctx->ambiDetectPending) {
        return;
    }
    // Native (lane frames-in-flight): with two frames in flight that frame may still be running;
    // read it on a later one (the next request waits for this one)
    if (ctx->ambiDetectSerial > ctx->gpuDone) {
        return;
    }
    ctx->ambiDetectPending = 0;
    if (ctx->ambiDetectMap != NULL) {
        detectAmbiBars(ctx, (const unsigned char*)ctx->ambiDetectMap);
    }
}

// The part of the decoded frame the glow and the room's light are sampled
// from, x0 y0 w h in frame uv: what the screen shows (the zoom's part of the
// picture, which with a host-rendered pair is the left or top eye's half),
// narrowed to the content inside the letterbox bars where the detector runs
static void glowSampleRect(const XrCtx* ctx, float out[4]) {
    float zoom[4], shown[4];
    xrZoomRect(ctx, zoom);
    videoShownRect(ctx->stereoMode == DEPTH_MODE_SBS ? ctx->stereoSource : STEREO_SOURCE_DEPTH, zoom, shown);
    videoGlowRect(shown, ambiBarDetectOn(ctx) ? ctx->ambiCrop : AMBI_CROP_FULL, out);
}

// Boils the frame down to the tiny colour texture. Kept self contained and
// free of anything glow shaped, since it is the frame's colours rather than
// the glow that anything else would want.
void runFrameColorSample(XrCtx* ctx, const MlxrVideo* video) {
    xrVkImageTo(ctx, &ctx->ambiTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = ctx->ambiTexture.view;
    t.format = MLXR_PASS_FORMAT;
    t.rect.extent = (VkExtent2D){ AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX };
    MlxrAmbiParams a;
    memset(&a, 0, sizeof(a));
    a.video = *video;
    // Whatever the detector last settled on, so the glow and the room's light
    // come from the picture rather than from the bars around it. With the
    // detection off the crop is not applied, but the state it found is kept.
    // Native (PLAN 4b.6): and only the part of the picture the screen shows (the
    // zoom, inside the eye's half with a host-rendered pair)
    glowSampleRect(ctx, a.crop);
    // Graded the way the screen is, so the glow and the room's light come from
    // the picture as it is seen
    a.gradeOn = ctx->gradeOn;
    a.grade[0] = ctx->grade.offset;
    a.grade[1] = ctx->grade.contrast;
    a.grade[2] = ctx->grade.exponent;
    a.grade[3] = ctx->grade.saturation;
    // Mixed into what is already there rather than replacing it. A cut to a
    // different scene would otherwise strobe the whole glow in one frame,
    // where this takes about ten to get there. The first frame has nothing to
    // mix with, so it lands whole.
    // (a zoom that moved lands whole too: the old colours are of another part of the picture)
    int moved = ctx->glowZoom[0] != ctx->zoomPct || ctx->glowZoom[1] != ctx->zoomX || ctx->glowZoom[2] != ctx->zoomY;
    a.blend = ctx->ambiSeeded && !moved ? ctx->ambiSmooth : 1.0f;
    ctx->glowZoom[0] = ctx->zoomPct;
    ctx->glowZoom[1] = ctx->zoomX;
    ctx->glowZoom[2] = ctx->zoomY;
    mlxr_pass_ambi(ctx->passes, ctx->frameCmd, &t, &a);
    xrVkImageTo(ctx, &ctx->ambiTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx->ambiSeeded = 1;
}

// ---- native (issue #7): the glow, a Backlight ----------------------------------------------

// Under this much of a new picture left to take (linear light, so a dark picture after a bright one needs it
// small: under a step of eight bits), the colours stand
#define BACKLIGHT_SETTLED 0.0005f
// No new picture for this long is a picture standing still, whose colours are finished from the held frame
#define BACKLIGHT_STILL_NS 50000000LL

static int64_t realClock(void) {
    return nowNs();
}

int64_t (*xrBacklightClock)(void) = realClock;

static int glowOn(XrCtx* ctx) {
    int on;
    float level;
    ambiEffective(ctx, &on, &level);
    return on;
}

static void backlightShapeNow(XrCtx* ctx, BacklightShape* s) {
    backlightShapeFor((float)ctx->videoWidth, (float)ctx->videoHeight, ctx->glowSpread,
                      roomEffective(ctx) >= ROOM_STYLE_FIRST, s);
}

// How the glow image goes up: 0 a quad layer, 1 through the panel pass
static int backlightProjected(XrCtx* ctx) {
    return xrPictureProjected(ctx) != 0;
}

VkImageView xrGlowPanelSource(const XrCtx* ctx) {
    return ctx->glowDrawnProjected ? ctx->glowLightTexture.view : VK_NULL_HANDLE;
}

int xrGlowForQuad(const XrCtx* ctx) {
    return !ctx->glowDrawnProjected;
}

int xrBacklightSettling(XrCtx* ctx) {
    if (!glowOn(ctx) || !ctx->glowZonesSeeded || ctx->glowUnsettled < BACKLIGHT_SETTLED) {
        return 0;
    }
    int64_t now = xrBacklightClock();
    return now - ctx->glowPictureNs > BACKLIGHT_STILL_NS && now - ctx->glowEasedNs >= BACKLIGHT_EVERY_NS;
}

// The zone colours, moved toward the picture's by the time since they last were
static void runBacklightZones(XrCtx* ctx, const MlxrVideo* video) {
    BacklightShape s;
    backlightShapeNow(ctx, &s);
    int64_t now = xrBacklightClock();
    int moved = ctx->glowZonesZoom[0] != ctx->zoomPct || ctx->glowZonesZoom[1] != ctx->zoomX
            || ctx->glowZonesZoom[2] != ctx->zoomY || ctx->glowDrawnShape.zonesAcross != s.zonesAcross;
    int whole = !ctx->glowZonesSeeded || moved;
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = ctx->glowZonesTexture.view;
    t.format = MLXR_GLOW_LIGHT_FORMAT;
    t.rect.extent = (VkExtent2D){ MLXR_GLOW_ZONES, 1 };
    MlxrAmbiParams a;
    memset(&a, 0, sizeof(a));
    a.video = *video;
    glowSampleRect(ctx, a.crop);
    a.gradeOn = ctx->gradeOn;
    a.grade[0] = ctx->grade.offset;
    a.grade[1] = ctx->grade.contrast;
    a.grade[2] = ctx->grade.exponent;
    a.grade[3] = ctx->grade.saturation;
    a.blend = whole ? 1.0f : backlightEase((float)(now - ctx->glowEasedNs) * 1e-9f);
    xrVkImageTo(ctx, &ctx->glowZonesTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_pass_glow_zones(ctx->passes, ctx->frameCmd, &t, &a, s.zonesAcross);
    xrVkImageTo(ctx, &ctx->glowZonesTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx->glowUnsettled = whole ? 0.0f : ctx->glowUnsettled * (1.0f - a.blend);
    ctx->glowEasedNs = now;
    ctx->glowZonesSeeded = 1;
    ctx->glowZonesZoom[0] = ctx->glowZoom[0] = ctx->zoomPct;
    ctx->glowZonesZoom[1] = ctx->glowZoom[1] = ctx->zoomX;
    ctx->glowZonesZoom[2] = ctx->glowZoom[2] = ctx->zoomY;

    xrVkImageTo(ctx, &ctx->glowSpreadTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    t.view = ctx->glowSpreadTexture.view;
    t.rect.extent = (VkExtent2D){ MLXR_GLOW_SPREAD_W, MLXR_GLOW_SPREAD_H };
    mlxr_pass_glow_spread(ctx->passes, ctx->frameCmd, &t, ctx->glowZonesTexture.view);
    xrVkImageTo(ctx, &ctx->glowSpreadTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// The glow image, from the spread colours
static void runBacklightRender(XrCtx* ctx) {
    BacklightShape s;
    backlightShapeNow(ctx, &s);
    int on;
    float level;
    ambiEffective(ctx, &on, &level);
    int projected = backlightProjected(ctx);
    XrVkImage* image = projected ? &ctx->glowLightTexture : &ctx->glowTexture;
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = image->view;
    t.format = projected ? MLXR_GLOW_LIGHT_FORMAT : MLXR_PASS_FORMAT;
    t.rect.extent = (VkExtent2D){ GLOW_TEX, GLOW_TEX };
    MlxrBacklightParams b;
    memset(&b, 0, sizeof(b));
    b.spread = ctx->glowSpreadTexture.view;
    b.scaleX = s.scaleX;
    b.scaleY = s.scaleY;
    b.aspect = s.aspect;
    b.zonesAcross = s.zonesAcross;
    b.reach = s.reach;
    b.level = level;
    b.projected = projected;
    xrVkImageTo(ctx, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_pass_backlight(ctx->passes, ctx->frameCmd, &t, &b);
    xrVkImageTo(ctx, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx->glowRendered = 1;
    ctx->glowDrawnOn = 1;
    ctx->glowDrawnLevel = level;
    ctx->glowDrawnShape = s;
    ctx->glowDrawnProjected = b.projected;
    ctx->glowCopied = 0;
}

void xrBacklightVideoFrame(XrCtx* ctx, const MlxrVideo* video) {
    int64_t now = xrBacklightClock();
    ctx->glowPictureNs = now;
    ctx->glowUnsettled = 1.0f;
    // Not at every picture of a fast stream: the colours ease by time, so fewer steps look the same
    if (ctx->glowZonesSeeded && now - ctx->glowEasedNs < BACKLIGHT_EVERY_NS && !glowStale(ctx)) {
        return;
    }
    runBacklightZones(ctx, video);
    runBacklightRender(ctx);
}

int glowStale(XrCtx* ctx) {
    int on;
    float level;
    ambiEffective(ctx, &on, &level);
    // Native (PLAN 4b.6): or the zoom moved, which changes what the colours are of
    int zoomed = ctx->glowZoom[0] != ctx->zoomPct || ctx->glowZoom[1] != ctx->zoomX || ctx->glowZoom[2] != ctx->zoomY;
    if (!on) {
        return 0;
    }
    // Native (issue #7): its shape (Glow spread, the picture's shape, a room), where it goes up, or its ease
    BacklightShape s;
    backlightShapeNow(ctx, &s);
    return !ctx->glowDrawnOn || level != ctx->glowDrawnLevel || !backlightShapeSame(&s, &ctx->glowDrawnShape)
            || ctx->glowDrawnProjected != backlightProjected(ctx) || zoomed || xrBacklightSettling(ctx);
}

// A desktop standing still sends no frames, which left the level slider and
// the switch looking dead. Only the glow is drawn, from the frame still
// latched: its zones move on with the time, or land whole where they are from
// another picture.
void redrawGlow(XrCtx* ctx, const MlxrVideo* video) {
    runBacklightZones(ctx, video);
    runBacklightRender(ctx);
}
