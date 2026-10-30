// Moonlight XR Native: the art and where it goes up. Upstream's xr_assets.c
// made a swapchain per panel sheet, button and handle (the fork trimmed them
// for SteamVR's 16 a session) and uploaded Java's bitmaps into them. Here every
// panel, every button face, the handles and the pointer are slots of ONE atlas
// swapchain (lane U's 2048x2960 layout plus the code drawn art in its free
// corners), shown by quad layers through sub-rects, and only the slots that
// changed are copied up, into each swapchain image as that image comes round.
//
// Ported from xr_assets.c (Gilleece, GPLv3): buildHandleArt and
// uploadPointerArt's drawing (now xrDrawCodeArt, rows top down: a Vulkan image
// has no GL flip to undo), roomStyleForCell, roomCellForStyle and
// nativeSetEnvironment (xrSetEnvironment). The upload paths are new.
#include "xr_native.h"
#include "xr_vk.h"

// ---- slots --------------------------------------------------------------------

UiRectPx xrAtlasRect(int slot) {
    // In the free corner right of the picker (x 1280..2048, y 2240..2536) and
    // under the clock strip (x 1632..2048, y 1840..2240), and right of the keyboard
    // (the splash, x 1408..2048, y 3072..3432); tests/m4 checks none
    // of them overlap each other or lane U's panels
    static const UiRectPx R[ATLAS_SLOTS - UI_PANEL_COUNT] = {
        [ATLAS_BEAM - UI_PANEL_COUNT] = { 1280, 2240, PTR_TEX_W, PTR_BEAM_H },
        [ATLAS_DOT - UI_PANEL_COUNT] = { 1344, 2240, PTR_TEX_W, PTR_DOT_H },
        [ATLAS_OUTLINE - UI_PANEL_COUNT] = { 1408, 2240, OUTLINE_TEX, OUTLINE_TEX },
        [ATLAS_CORNER - UI_PANEL_COUNT] = { 1536, 2240, CORNER_TEX_W, CORNER_TEX_H },
        [ATLAS_HANDLE - UI_PANEL_COUNT] = { 1600, 2240, BAR_TEX_W, BAR_TEX_H },
        [ATLAS_OVERLAY - UI_PANEL_COUNT] = { 1632, 1840, ATLAS_OVERLAY_W, ATLAS_OVERLAY_H },
        [ATLAS_BEAM_FAINT - UI_PANEL_COUNT] = { 1280, 2240 + PTR_BEAM_H, PTR_TEX_W, ATLAS_BEAM_FAINT_H },
        [ATLAS_SPLASH - UI_PANEL_COUNT] = { 1408, 3072, ATLAS_SPLASH_W, ATLAS_SPLASH_H },
    };
    if (slot >= 0 && slot < UI_PANEL_COUNT) {
        return ui_atlas_rect((UiPanelId)slot);
    }
    if (slot >= UI_PANEL_COUNT && slot < ATLAS_SLOTS) {
        return R[slot - UI_PANEL_COUNT];
    }
    UiRectPx none = { 0, 0, 0, 0 };
    return none;
}

#define ATLAS_IMAGES 8

typedef struct XrUiAtlas {
    uint8_t* px;              // the CPU copy, UI_ATLAS_W x UI_ATLAS_H RGBA8
    int cpuOnly;              // headless: px is plain memory, no swapchain
    // Native: the staging buffer the copies read, mapped. Its memory is
    // host-visible uncached (write-combined) on Turnip, where the panels'
    // read-modify-write blending ran about 40 times slower than in cached
    // memory (cog 180-200 ms instead of 4-5 ms, the "panel drawings ~200 ms"
    // of the headset logs). So the panels draw into px (plain memory) and a
    // slot is copied in here, in one sequential write, when it is touched.
    uint8_t* stage;
    XrSwapchain swapchain;
    uint32_t imageCount;
    XrSwapchainImageVulkan2KHR images[ATLAS_IMAGES];
    int imageInit[ATLAS_IMAGES];
    uint32_t version[ATLAS_SLOTS];
    uint32_t imageVersion[ATLAS_IMAGES][ATLAS_SLOTS];
    uint32_t acquiredIndex;
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    uint64_t copies;
} XrUiAtlas;

uint8_t* xrAtlasPixels(XrCtx* ctx) {
    return ctx->atlas != NULL ? ctx->atlas->px : NULL;
}

void xrAtlasTouch(XrCtx* ctx, int slot) {
    XrUiAtlas* a = ctx->atlas;
    if (a == NULL || slot < 0 || slot >= ATLAS_SLOTS) {
        return;
    }
    // The slot's rows into the staging buffer (the frame loop calls this
    // after the last frame's fence, so no copy is reading it)
    UiRectPx r = xrAtlasRect(slot);
    if (a->stage != NULL && r.w > 0 && r.h > 0) {
        for (int y = r.y; y < r.y + r.h; y++) {
            size_t at = ((size_t)y * UI_ATLAS_W + (size_t)r.x) * 4;
            memcpy(a->stage + at, a->px + at, (size_t)r.w * 4);
        }
    }
    a->version[slot]++;
}

XrSwapchain xrAtlasSwapchain(XrCtx* ctx) {
    return ctx->atlas != NULL ? ctx->atlas->swapchain : XR_NULL_HANDLE;
}

static XrUiAtlas* atlasNew(void) {
    XrUiAtlas* a = calloc(1, sizeof(XrUiAtlas));
    if (a == NULL) {
        return NULL;
    }
    // Every slot starts one version ahead of every image, so the first time
    // an image comes round the whole of it goes up
    for (int s = 0; s < ATLAS_SLOTS; s++) {
        a->version[s] = 1;
    }
    return a;
}

int xrAtlasCreateCpu(XrCtx* ctx) {
    XrUiAtlas* a = atlasNew();
    if (a == NULL) {
        return 0;
    }
    a->cpuOnly = 1;
    a->px = calloc((size_t)UI_ATLAS_W * UI_ATLAS_H, 4);
    if (a->px == NULL) {
        free(a);
        return 0;
    }
    xrDrawCodeArt(a->px);
    ctx->atlas = a;
    ctx->pointerArtReady = 1;
    ctx->handleArtReady = 1;
    ctx->outlineReady = 1;
    return 1;
}

int xrAtlasCreate(XrCtx* ctx) {
    XrVk* vk = ctx->vk;
    XrUiAtlas* a = atlasNew();
    if (a == NULL || vk == NULL) {
        free(a);
        return 0;
    }
    XrSwapchainCreateInfo ci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    ci.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT
            | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    // Upstream's art went into GL_SRGB8_ALPHA8 swapchains as bytes, so the
    // compositor decoded it to linear before blending; the same here
    ci.format = VK_FORMAT_R8G8B8A8_SRGB;
    ci.sampleCount = 1;
    ci.width = UI_ATLAS_W;
    ci.height = UI_ATLAS_H;
    ci.faceCount = ci.arraySize = ci.mipCount = 1;
    if (!checkXr(xrCreateSwapchain(ctx->session, &ci, &a->swapchain), "create UI atlas swapchain")) {
        free(a);
        return 0;
    }
    ctx->swapchainsAlive++;
    xrEnumerateSwapchainImages(a->swapchain, 0, &a->imageCount, NULL);
    if (a->imageCount > ATLAS_IMAGES) {
        a->imageCount = ATLAS_IMAGES;
    }
    for (uint32_t i = 0; i < a->imageCount; i++) {
        a->images[i] = (XrSwapchainImageVulkan2KHR){ XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR };
    }
    xrEnumerateSwapchainImages(a->swapchain, a->imageCount, &a->imageCount,
                               (XrSwapchainImageBaseHeader*)a->images);

    VkBufferCreateInfo bi = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bi.size = (VkDeviceSize)UI_ATLAS_W * UI_ATLAS_H * 4;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(vk->device, &bi, NULL, &a->staging) != VK_SUCCESS) {
        xrDestroySwapchain(a->swapchain);
        free(a);
        return 0;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(vk->device, a->staging, &req);
    VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = xr_vk_memory_type(vk, req.memoryTypeBits,
                                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                                   | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* mapped = NULL;
    if (vkAllocateMemory(vk->device, &ai, NULL, &a->stagingMem) != VK_SUCCESS
            || vkBindBufferMemory(vk->device, a->staging, a->stagingMem, 0) != VK_SUCCESS
            || vkMapMemory(vk->device, a->stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS) {
        if (a->stagingMem) vkFreeMemory(vk->device, a->stagingMem, NULL);
        vkDestroyBuffer(vk->device, a->staging, NULL);
        xrDestroySwapchain(a->swapchain);
        free(a);
        return 0;
    }
    a->px = calloc((size_t)UI_ATLAS_W * UI_ATLAS_H, 4);
    if (a->px == NULL) {
        vkUnmapMemory(vk->device, a->stagingMem);
        vkFreeMemory(vk->device, a->stagingMem, NULL);
        vkDestroyBuffer(vk->device, a->staging, NULL);
        xrDestroySwapchain(a->swapchain);
        ctx->swapchainsAlive--;
        free(a);
        return 0;
    }
    a->stage = mapped;
    xrDrawCodeArt(a->px);
    memcpy(a->stage, a->px, (size_t)UI_ATLAS_W * UI_ATLAS_H * 4);
    ctx->atlas = a;
    ctx->pointerArtReady = 1;
    ctx->handleArtReady = 1;
    ctx->outlineReady = 1;
    LOGEV("UI atlas swapchain %dx%d R8G8B8A8_SRGB, %u images, %d slots", UI_ATLAS_W, UI_ATLAS_H,
          a->imageCount, ATLAS_SLOTS);
    return 1;
}

void xrAtlasDestroy(XrCtx* ctx) {
    XrUiAtlas* a = ctx->atlas;
    if (a == NULL) {
        return;
    }
    free(a->px);
    if (!a->cpuOnly) {
        XrVk* vk = ctx->vk;
        vkUnmapMemory(vk->device, a->stagingMem);
        vkDestroyBuffer(vk->device, a->staging, NULL);
        vkFreeMemory(vk->device, a->stagingMem, NULL);
        xrDestroySwapchain(a->swapchain);
        ctx->swapchainsAlive--;
    }
    free(a);
    ctx->atlas = NULL;
}

// Whether any image is missing a slot's latest drawing
static int atlasStale(const XrUiAtlas* a) {
    for (uint32_t i = 0; i < a->imageCount; i++) {
        for (int s = 0; s < ATLAS_SLOTS; s++) {
            if (a->imageVersion[i][s] != a->version[s]) {
                return 1;
            }
        }
    }
    return 0;
}

int xrAtlasRecord(XrCtx* ctx, VkCommandBuffer cmd, int* acquired) {
    XrUiAtlas* a = ctx->atlas;
    *acquired = 0;
    if (a == NULL || a->cpuOnly || !atlasStale(a)) {
        return 0;
    }
    XrSwapchainImageAcquireInfo acq = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (!checkXr(xrAcquireSwapchainImage(a->swapchain, &acq, &a->acquiredIndex), "acquire UI atlas image")) {
        return -1;
    }
    XrSwapchainImageWaitInfo wait = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wait.timeout = XR_INFINITE_DURATION;
    if (!checkXr(xrWaitSwapchainImage(a->swapchain, &wait), "wait UI atlas image")) {
        XrSwapchainImageReleaseInfo rel = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
        xrReleaseSwapchainImage(a->swapchain, &rel);
        return -1;
    }
    *acquired = 1;
    uint32_t i = a->acquiredIndex;
    VkImage image = a->images[i].image;
    VkBufferImageCopy regions[ATLAS_SLOTS];
    uint32_t n = 0;
    for (int s = 0; s < ATLAS_SLOTS; s++) {
        if (a->imageVersion[i][s] == a->version[s]) {
            continue;
        }
        UiRectPx r = xrAtlasRect(s);
        a->imageVersion[i][s] = a->version[s];
        if (r.w <= 0 || r.h <= 0) {
            continue;
        }
        VkBufferImageCopy* c = &regions[n++];
        memset(c, 0, sizeof(*c));
        c->bufferOffset = ((VkDeviceSize)r.y * UI_ATLAS_W + (VkDeviceSize)r.x) * 4;
        c->bufferRowLength = UI_ATLAS_W;
        c->imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        c->imageSubresource.layerCount = 1;
        c->imageOffset.x = r.x;
        c->imageOffset.y = r.y;
        c->imageExtent.width = (uint32_t)r.w;
        c->imageExtent.height = (uint32_t)r.h;
        c->imageExtent.depth = 1;
    }
    // A fresh image has nothing worth keeping; one that has been shown keeps
    // every slot that did not change
    VkImageLayout from = a->imageInit[i] ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    xr_vk_image_barrier(cmd, image, from, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (n > 0) {
        vkCmdCopyBufferToImage(cmd, a->staging, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n, regions);
    }
    xr_vk_image_barrier(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    a->imageInit[i] = 1;
    a->copies += n;
    return 1;
}

void xrAtlasRelease(XrCtx* ctx) {
    XrUiAtlas* a = ctx->atlas;
    if (a == NULL || a->cpuOnly) {
        return;
    }
    XrSwapchainImageReleaseInfo rel = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    checkXr(xrReleaseSwapchainImage(a->swapchain, &rel), "release UI atlas image");
}

// ---- the art drawn in code --------------------------------------------------------------

// Soft edged coverage for a distance from a shape, in pixels
static float edgeAlpha(float distance, float halfStroke) {
    float a = (halfStroke - distance) / 1.5f + 0.5f;
    if (a < 0.0f) return 0.0f;
    if (a > 1.0f) return 1.0f;
    return a;
}

static uint8_t* slotPx(uint8_t* atlas, int slot, int x, int y) {
    UiRectPx r = xrAtlasRect(slot);
    return atlas + (((size_t)(r.y + y) * UI_ATLAS_W) + (size_t)(r.x + x)) * 4;
}

static void putGrey(uint8_t* p, float lit, float alpha) {
    p[0] = p[1] = p[2] = (uint8_t)(lit + 0.5f);
    p[3] = (uint8_t)(alpha + 0.5f);
}

void xrDrawCodeArt(uint8_t* atlas) {
    // A rounded bar, symmetric (buildHandleArt)
    float barR = BAR_TEX_H * 0.5f;
    for (int y = 0; y < BAR_TEX_H; y++) {
        for (int x = 0; x < BAR_TEX_W; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float cx = px;
            if (cx < barR) cx = barR;
            if (cx > BAR_TEX_W - barR) cx = BAR_TEX_W - barR;
            float dx = px - cx, dy = py - barR;
            float d = sqrtf(dx * dx + dy * dy);
            float a = (float)(unsigned char)(edgeAlpha(d, barR - 1.0f) * 235.0f);
            putGrey(slotPx(atlas, ATLAS_HANDLE, x, y), a, a);
        }
    }

    // A rounded bracket whose outer corner sits at the middle of the tile, with
    // the two runs going right and down from it, so centring the quad on a
    // corner of the screen wraps that corner. Top down: no GL flip to undo.
    const float mid = CORNER_TEX_W * 0.5f;
    const float arcR = 10.0f;
    const float stroke = 3.0f;
    for (int y = 0; y < CORNER_TEX_H; y++) {
        for (int x = 0; x < CORNER_TEX_W; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float d;
            if (px < mid + arcR && py < mid + arcR) {
                float ax = px - (mid + arcR), ay = py - (mid + arcR);
                d = fabsf(sqrtf(ax * ax + ay * ay) - arcR);
            }
            else if (px >= mid + arcR) {
                d = fabsf(py - mid);
            }
            else {
                d = fabsf(px - mid);
            }
            float a = (float)(unsigned char)(edgeAlpha(d, stroke) * 235.0f);
            putGrey(slotPx(atlas, ATLAS_CORNER, x, y), a, a);
        }
    }

    // Rounded rectangle border, used to mark the hovered and the selected
    // cell in the picker, the key under the ray and the sheets' buttons
    const float radius = 16.0f;
    const float border = 2.5f;
    const float half = OUTLINE_TEX * 0.5f;
    for (int y = 0; y < OUTLINE_TEX; y++) {
        for (int x = 0; x < OUTLINE_TEX; x++) {
            float qx = fabsf(x + 0.5f - half) - (half - radius);
            float qy = fabsf(y + 0.5f - half) - (half - radius);
            float mx = qx > 0.0f ? qx : 0.0f;
            float my = qy > 0.0f ? qy : 0.0f;
            float outside = sqrtf(mx * mx + my * my);
            float inside = (qx > qy ? qx : qy);
            if (inside > 0.0f) {
                inside = 0.0f;
            }
            float dist = fabsf(outside + inside);
            float a = (float)(unsigned char)(edgeAlpha(dist, border) * 255.0f);
            putGrey(slotPx(atlas, ATLAS_OUTLINE, x, y), a, a);
        }
    }

    // The beam: fades at both ends, a soft line down the middle
    // (uploadPointerArt)
    const float pmid = PTR_TEX_W * 0.5f;
    for (int y = 0; y < PTR_BEAM_H; y++) {
        float along = (y + 0.5f) / PTR_BEAM_H;
        float edge = along < 0.5f ? along : 1.0f - along;
        float lengthFade = edge < 0.12f ? edge / 0.12f : 1.0f;
        for (int x = 0; x < PTR_TEX_W; x++) {
            float r = fabsf((x + 0.5f) - pmid) / pmid;
            float t = r * 3.2f;
            float a = expf(-t * t) * lengthFade;
            float lit = (float)(unsigned char)(a * 255.0f + 0.5f);
            putGrey(slotPx(atlas, ATLAS_BEAM, x, y), lit, lit);
        }
    }

    // The resting beam: the same profile at a third of the strength (the
    // atlas is premultiplied, so grey and alpha scale together), shown over the
    // bar with the ray off. It has no colour scale to fade with, so it is its
    // own art.
    for (int y = 0; y < ATLAS_BEAM_FAINT_H; y++) {
        float along = (y + 0.5f) / ATLAS_BEAM_FAINT_H;
        float edge = along < 0.5f ? along : 1.0f - along;
        float lengthFade = edge < 0.12f ? edge / 0.12f : 1.0f;
        for (int x = 0; x < PTR_TEX_W; x++) {
            float r = fabsf((x + 0.5f) - pmid) / pmid;
            float t = r * 3.2f;
            float a = expf(-t * t) * lengthFade * BEAM_FAINT_LEVEL;
            float lit = (float)(unsigned char)(a * 255.0f + 0.5f);
            putGrey(slotPx(atlas, ATLAS_BEAM_FAINT, x, y), lit, lit);
        }
    }

    // The cursor: solid core with a soft edge, and a darker rim so it stays
    // visible against a bright picture
    for (int y = 0; y < PTR_DOT_H; y++) {
        for (int x = 0; x < PTR_TEX_W; x++) {
            float dx = ((x + 0.5f) - pmid) / pmid;
            float dy = ((y + 0.5f) - PTR_DOT_H * 0.5f) / (PTR_DOT_H * 0.5f);
            float r = sqrtf(dx * dx + dy * dy);
            float a = r < 0.45f ? 1.0f : (r < 0.75f ? (0.75f - r) / 0.30f : 0.0f);
            float shade = r < 0.35f ? 1.0f : 0.25f;
            putGrey(slotPx(atlas, ATLAS_DOT, x, y), (float)(unsigned char)(a * 255.0f * shade + 0.5f),
                    a * 255.0f);
        }
    }
}

// ---- the environment ------------------------------------------------------------------

// roomStyleForCell and roomCellForStyle are rooms/room_logic.c's (native:
// the same functions, moved with the rest of the room's pure parts)

// Which cell the picker is showing as chosen (nativeSetEnvironment)
void xrSetEnvironment(XrCtx* ctx, int choice) {
    if (ctx == NULL) {
        return;
    }
    ctx->pickerChoice = choice;
    ctx->roomStyle = roomStyleForCell(choice);
    if (choice != ctx->loggedChoice) {
        ctx->loggedChoice = choice;
        LOGEV("environment %d, room %d", choice, roomEffective(ctx));
    }
}
