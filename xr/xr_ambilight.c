// The ambilight: the frame boiled down to a tiny colour texture, the
// letterbox detection that keeps black bars out of it, and the glow quad
// drawn from it behind the screen.
//
// Moonlight XR Native: ported from the Steam Frame fork of Moonlight XR's
// xr-renderer/xr_ambilight.c (Gilleece, GPLv3). ambiEffective, ambiBarEdge,
// detectAmbiBars, the order and the rules of runFrameColorSample,
// runAmbiBarDetect, finishAmbiBarDetect, runGlowEdge, runGlowRender,
// glowStale and redrawGlow are upstream's. Native, each marked:
//   - the GL programs are mlxr_pass_ambi, _glow_edge and _glow recording into
//     the frame's command buffer, the textures images of the app's own;
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
    // The glow reads past both ends of the sample on purpose, and the clamp is
    // what carries the frame's edge colours out to the rim of the quad (the
    // passes' sampler clamps)
    if (!xrVkImageMake(ctx, &ctx->ambiTexture, AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX, MLXR_PASS_FORMAT,
                       rw | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT)
            // The letterbox detector's own target: drawn once in a while and
            // read back, never sampled
            || !xrVkImageMake(ctx, &ctx->ambiDetectTexture, AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX, MLXR_PASS_FORMAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT)
            // The lifted and rolled off copy the glow is drawn from
            || !xrVkImageMake(ctx, &ctx->glowEdgeTexture, AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX, MLXR_PASS_FORMAT, rw,
                              VK_IMAGE_ASPECT_COLOR_BIT)
            // Native: the glow itself, gamma encoded bytes in a _UNORM image
            || !xrVkImageMake(ctx, &ctx->glowTexture, GLOW_TEX, GLOW_TEX, MLXR_PASS_FORMAT,
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
    LOGI("ambilight ready, sampling %dx%d into a %dx%d glow, luma %.2f from %.2f to %.2f, "
         "edge reach %.1f texels, inward %d",
         AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX, GLOW_TEX, GLOW_TEX, GLOW_LUMA_TARGET,
         GLOW_LUMA_FLOOR, GLOW_LUMA_FULL, glowReachTexels(), glowInwardSteps());
    return 1;
}

// What the glow is doing this frame. The panel owns it, with the debug
// property over the top of it the way the separation override works. In a
// room the switch is the room's own, and the app wide one everywhere else.
void ambiEffective(XrCtx* ctx, int* on, float* level) {
    int enabled = roomGlowOn(ctx, roomEffective(ctx));
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
    return ctx->ambiBarDetect && ctx->stereoMode != DEPTH_MODE_SBS;
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
}

// Counts the bars in the readback asked for last time. By now the draw behind
// it has long finished, so the map costs a copy and nothing more.
void finishAmbiBarDetect(XrCtx* ctx) {
    if (!ctx->ambiDetectPending) {
        return;
    }
    ctx->ambiDetectPending = 0;
    if (ctx->ambiDetectMap != NULL) {
        detectAmbiBars(ctx, (const unsigned char*)ctx->ambiDetectMap);
    }
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
    memcpy(a.crop, ambiBarDetectOn(ctx) ? ctx->ambiCrop : AMBI_CROP_FULL, sizeof(a.crop));
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
    a.blend = ctx->ambiSeeded ? ctx->ambiSmooth : 1.0f;
    mlxr_pass_ambi(ctx->passes, ctx->frameCmd, &t, &a);
    xrVkImageTo(ctx, &ctx->ambiTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx->ambiSeeded = 1;
}

// Lifts the sample to a steady luma and rolls its dark edges off, into the
// glow's copy of it
static void runGlowEdge(XrCtx* ctx) {
    xrVkImageTo(ctx, &ctx->glowEdgeTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = ctx->glowEdgeTexture.view;
    t.format = MLXR_PASS_FORMAT;
    t.rect.extent = (VkExtent2D){ AMBI_SAMPLE_TEX, AMBI_SAMPLE_TEX };
    MlxrGlowEdgeParams g;
    memset(&g, 0, sizeof(g));
    g.sample = ctx->ambiTexture.view;
    g.luma[0] = GLOW_LUMA_TARGET;
    g.luma[1] = GLOW_LUMA_FLOOR;
    g.luma[2] = GLOW_LUMA_FULL;
    g.lit[0] = GLOW_LIT_LO;
    g.lit[1] = GLOW_LIT_HI;
    g.reach = glowReachTexels();
    g.steps = glowInwardSteps();
    mlxr_pass_glow_edge(ctx->passes, ctx->frameCmd, &t, &g);
    xrVkImageTo(ctx, &ctx->glowEdgeTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

// Spreads those colours over the glow image, which the compositor then places
// behind the screen (native: the app's own image; xrWorldFlatGlow copies it
// into the swapchain, the curved picture's pass draws it on the cylinder)
void runGlowRender(XrCtx* ctx) {
    if (ctx->glowTexture.image == VK_NULL_HANDLE) {
        return;
    }
    // Read once, so the copy and the draw agree on which way this glow goes
    int norm = ctx->glowNorm;
    if (norm) {
        runGlowEdge(ctx);
    }
    xrVkImageTo(ctx, &ctx->glowTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    int on;
    float level;
    ambiEffective(ctx, &on, &level);
    MlxrTarget t;
    memset(&t, 0, sizeof(t));
    t.view = ctx->glowTexture.view;
    t.format = MLXR_PASS_FORMAT;
    t.rect.extent = (VkExtent2D){ GLOW_TEX, GLOW_TEX };
    MlxrGlowParams g;
    memset(&g, 0, sizeof(g));
    g.sample = norm ? ctx->glowEdgeTexture.view : ctx->ambiTexture.view;
    g.intensity = level;
    g.blur = norm ? 1 : 0;
    mlxr_pass_glow(ctx->passes, ctx->frameCmd, &t, &g);
    xrVkImageTo(ctx, &ctx->glowTexture, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    ctx->glowRendered = 1;
    ctx->glowDrawnOn = 1;
    ctx->glowDrawnLevel = level;
    ctx->glowCopied = 0;
}

int glowStale(XrCtx* ctx) {
    int on;
    float level;
    ambiEffective(ctx, &on, &level);
    return on && (!ctx->glowDrawnOn || level != ctx->glowDrawnLevel);
}

// A desktop standing still sends no frames, which left the level slider and
// the switch looking dead. Only the glow is drawn, from the frame still
// latched; the sample is taken again only where the glow was off, since
// then it may be from another picture.
void redrawGlow(XrCtx* ctx, const MlxrVideo* video) {
    if (!ctx->glowDrawnOn) {
        runFrameColorSample(ctx, video);
    }
    runGlowRender(ctx);
}
