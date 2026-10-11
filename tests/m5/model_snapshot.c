// Moonlight XR Native, tests/m5: mlxr-model-snapshot. The controller models
// drawn through xrModelRecord on the Frame's GPU: the generic one beside the
// Steam Frame's own (tools/frame-models), painted (.room, the older converter)
// and textured (.model), whichever FRAMEMODELSDIR holds, at the same grip
// poses, from the front, the side, above and three-quarters behind, one hand
// at a time, tiled into one PNG; and the models pass timed for each. The grip
// is identity-oriented (trigger forward, -z; top face up, +y), so a model that
// sits in the hand as the generic one does shows the same way up and the same
// way round in every row.
//
//   mlxr-model-snapshot OUTDIR FRAMEMODELSDIR
//
// FRAMEMODELSDIR holds frame_controller_{left,right}.room and/or .model (the
// converters' output). MLXR_FRAME_MODEL_VIEW and MLXR_FRAME_MODEL_GAIN act on
// the textured rows as in the client; the sheet's name carries the view.
// GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "m5_gpu.h"
#include "xr_native.h"

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

#define CW 480
#define CH 360
#define VIEWS 4
#define KINDS 3

static M5Gpu g;

typedef struct {
    M5Image colour, depth;
    XrView views[2];
    int w, h; // one eye
} Cell;

static void body(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    Cell* c = user;
    mlxr_barrier(cmd, c->colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, c->depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    // Both eyes side by side in one image, one pass
    XrEyeTargets t = xrEyesSideBySide(c->colour.img.image, c->colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, c->w, c->h);
    t.depthImage = c->depth.img.image;
    t.depthView = c->depth.img.view;
    xrModelRecord(ctx, cmd, &t, c->views);
}

static XrCtx* newCtx(const char* assets) {
    XrCtx* c = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(c, 1920, 1080, 60);
    Settings st;
    settings_defaults(&st);
    xrApplySettings(c, &st);
    m5_ctx_gpu(&g, c);
    snprintf(c->assetsDir, sizeof(c->assetsDir), "%s", assets);
    c->sessionState = XR_SESSION_STATE_FOCUSED;
    c->shouldRender = 1;
    c->modelOn = 1;
    m5_frame(&g, c, NULL, NULL); // the model loads
    c->modelsShowing = 1;
    return c;
}

// The camera for a view, looking at the grip's position (0, 0, -0.4) from the
// front (the user's own side, +z), the side (+x), above (+y) and from behind,
// above and to the right (three-quarters, where the grip's indent shows)
static void camera(int view, XrView* v) {
    *v = m5_view(34.0f, CW, CH, 0.0f);
    Vec3 up = { 0, 1, 0 }, right = { 1, 0, 0 };
    if (view == 1) {
        v->pose.orientation = axisAngleQuat(up, 1.5707963f);
        v->pose.position = (XrVector3f){ 0.40f, 0.0f, -0.4f };
    }
    else if (view == 2) {
        v->pose.orientation = axisAngleQuat(right, -1.5707963f);
        v->pose.position = (XrVector3f){ 0.0f, 0.40f, -0.4f };
    }
    else if (view == 3) {
        // yaw 135 degrees (facing back toward -x+z... i.e. looking from -z+x
        // toward the grip), then pitched down 30 degrees
        XrQuaternionf yaw = axisAngleQuat(up, 2.3561945f);
        XrQuaternionf pitch = axisAngleQuat(right, -0.5235988f);
        v->pose.orientation = quatMul(yaw, pitch);
        const float d = 0.40f;
        v->pose.position = (XrVector3f){ d * 0.866f * 0.7071f, d * 0.5f, -0.4f - d * 0.866f * 0.7071f };
    }
    else {
        v->pose.position = (XrVector3f){ 0.0f, 0.0f, 0.0f };
    }
}

static void render(XrCtx* ctx, Cell* cell, int hand, int view, uint8_t* rgb /* CW x CH x 3 */) {
    camera(view, &cell->views[0]);
    cell->views[1] = cell->views[0];
    for (int e = 0; e < 2; e++) ctx->modelViews[e] = cell->views[e];
    ctx->modelShown[hand] = 1;
    ctx->modelShown[1 - hand] = 0;
    ctx->modelGrip[hand] = (XrPosef){ { 0, 0, 0, 1 }, { 0.0f, 0.0f, -0.4f } };
    m5_frame(&g, ctx, body, cell);
    uint8_t* px = malloc((size_t)2 * CW * CH * 4);
    m5_readback(&g, cell->colour.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2 * CW, CH, px);
    // Over a mid grey: the Frame's controllers are black plastic
    const float bg[3] = { 96, 104, 118 };
    for (int y = 0; y < CH; y++) {
        for (int x = 0; x < CW; x++) {
            const uint8_t* p = px + ((size_t)y * 2 * CW + x) * 4;
            float a = p[3] / 255.0f;
            for (int k = 0; k < 3; k++) {
                float o = p[k] + bg[k] * (1.0f - a);
                rgb[((size_t)y * CW + x) * 3 + k] = (uint8_t)(o > 255.0f ? 255.0f : o);
            }
        }
    }
    free(px);
}

// The models pass at the Frame's recommended eye size (1728 x 1728 a eye), both hands
// Native: pressed 0 at rest (a hand is one draw), 1 the trigger, grip and stick moved (a few draws), 2 every
// control down (the most draws)
static double timeModels(XrCtx* ctx, int pressed) {
    for (int h = 0; h < 2; h++) {
        memset(ctx->modelInput[h], 0, sizeof(ctx->modelInput[h]));
        if (pressed == 1) {
            ctx->modelInput[h][MLXR_IN_TRIGGER] = 1;
            ctx->modelInput[h][MLXR_IN_STICK_X] = 0.7f;
            ctx->modelInput[h][MLXR_IN_STICK_CLICK] = 1;
        }
        if (pressed == 2) {
            for (int k = 0; k < MLXR_IN_COUNT; k++) ctx->modelInput[h][k] = 1;
        }
    }
    Cell cell;
    memset(&cell, 0, sizeof(cell));
    cell.w = cell.h = 1728;
    m5_image(&g, ctx, &cell.colour, 2 * cell.w, cell.h, VK_FORMAT_R8G8B8A8_SRGB,
             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    m5_image(&g, ctx, &cell.depth, 2 * cell.w, cell.h, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
             VK_IMAGE_ASPECT_DEPTH_BIT);
    cell.views[0] = m5_view(100.0f, cell.w, cell.h, -0.032f);
    cell.views[1] = m5_view(100.0f, cell.w, cell.h, 0.032f);
    for (int e = 0; e < 2; e++) ctx->modelViews[e] = cell.views[e];
    Cell* cp = &cell;
    VkQueryPool q;
    VkQueryPoolCreateInfo qi = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2;
    vkCreateQueryPool(g.vk.device, &qi, NULL, &q);
    double ms[25];
    ctx->modelShown[0] = ctx->modelShown[1] = 1;
    ctx->modelGrip[0] = (XrPosef){ { 0, 0, 0, 1 }, { -0.18f, -0.22f, -0.45f } };
    ctx->modelGrip[1] = (XrPosef){ { 0, 0, 0, 1 }, { 0.20f, -0.20f, -0.50f } };
    for (int i = 0; i < 27; i++) {
        VkCommandBuffer cmd = m5_begin(&g);
        ctx->frameCmd = cmd;
        vkCmdResetQueryPool(cmd, q, 0, 2);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, q, 0);
        body(ctx, cmd, cp);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, q, 1);
        ctx->frameCmd = VK_NULL_HANDLE;
        m5_submit(&g, cmd, NULL, 0);
        uint64_t t[2];
        vkGetQueryPoolResults(g.vk.device, q, 0, 2, sizeof(t), t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (i >= 2) ms[i - 2] = (t[1] - t[0]) * g.props.limits.timestampPeriod / 1e6;
    }
    vkDestroyQueryPool(g.vk.device, q, NULL);
    m5_image_free(ctx, &cell.colour);
    m5_image_free(ctx, &cell.depth);
    for (int i = 0; i < 25; i++)
        for (int j = i + 1; j < 25; j++)
            if (ms[j] < ms[i]) { double t = ms[i]; ms[i] = ms[j]; ms[j] = t; }
    return ms[12];
}

// A scratch assets dir under OUTDIR holding the generic controller and the
// Frame files of one kind (none, .room or .model); 0 when that kind is there
static int stage(const char* out, const char* name, const char* frameDir, const char* ext, char* assets, size_t n) {
    char cmd[2600];
    snprintf(assets, n, "%s/_%s", out, name);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/models' && cp '%s/models/controller.room' '%s/models/'", assets, assets,
             MLXR_ASSETS_DIR, assets);
    if (system(cmd) != 0) return -1;
    if (!ext) return 0;
    snprintf(cmd, sizeof(cmd), "cp '%s/frame_controller_left.%s' '%s/frame_controller_right.%s' '%s/models/' 2>/dev/null", frameDir,
             ext, frameDir, ext, assets);
    return system(cmd) == 0 ? 0 : -1;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: mlxr-model-snapshot OUTDIR FRAMEMODELSDIR\n");
        return 2;
    }
    const char* dir = argv[1];
    mkdir(dir, 0755);
    const char* kind[KINDS] = { "generic", "painted", "textured" };
    const char* ext[KINDS] = { NULL, "room", "model" };
    char dirs[KINDS][600];
    int have[KINDS];
    for (int k = 0; k < KINDS; k++) {
        have[k] = stage(dir, kind[k], argv[2], ext[k], dirs[k], sizeof(dirs[k])) == 0;
        if (!have[k]) printf("%-8s no frame_controller_*.%s in %s: row left out\n", kind[k], ext[k], argv[2]);
    }
    if (!have[0] || m5_gpu_init(&g) != 0) {
        fprintf(stderr, "could not stage the generic controller, or no Vulkan device\n");
        return 1;
    }
    // rows: each hand, each kind there (left then right)
    int rows = 0;
    for (int k = 0; k < KINDS; k++) rows += have[k] ? 2 : 0;
    uint8_t* sheet = calloc((size_t)VIEWS * CW * rows * CH * 3, 1);
    int rc = 0, row = 0;
    for (int m = 0; m < KINDS; m++) {
        if (!have[m]) continue;
        XrCtx* ctx = newCtx(dirs[m]);
        int wantHand = m > 0, wantTex = m == 2;
        if (!ctx->model || ctx->model->perHand != wantHand || ctx->model->textured != wantTex) {
            fprintf(stderr, "%s: wrong model loaded\n", kind[m]);
            rc = 1;
            continue;
        }
        printf("%-8s %s: %u vertices, %u triangles, %.0f KB on the GPU, loaded in %.1f ms\n", kind[m], ctx->model->source,
               ctx->model->vertexCount, ctx->model->indexCount / 3, ctx->model->gpuBytes / 1024.0, ctx->model->loadMs);
        if (wantTex) {
            printf("         textures %ux%u, %u levels\n", ctx->model->texWidth, ctx->model->texHeight, ctx->model->texLevels);
        }
        for (int h = 0; h < 2; h++) {
            printf("         hand %d: first index %u, %u triangles, vertex offset %d\n", h,
                   ctx->model->handFirstIndex[h], ctx->model->handIndexCount[h] / 3, ctx->model->handVertexOffset[h]);
        }
        Cell cell;
        memset(&cell, 0, sizeof(cell));
        cell.w = CW;
        cell.h = CH;
        m5_image(&g, ctx, &cell.colour, 2 * CW, CH, VK_FORMAT_R8G8B8A8_SRGB,
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        m5_image(&g, ctx, &cell.depth, 2 * CW, CH, VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 VK_IMAGE_ASPECT_DEPTH_BIT);
        uint8_t* rgb = malloc((size_t)CW * CH * 3);
        for (int h = 0; h < 2; h++) {
            for (int v = 0; v < VIEWS; v++) {
                render(ctx, &cell, h, v, rgb);
                for (int y = 0; y < CH; y++) {
                    memcpy(sheet + (((size_t)((row + h) * CH + y)) * VIEWS * CW + (size_t)v * CW) * 3, rgb + (size_t)y * CW * 3,
                           (size_t)CW * 3);
                }
            }
        }
        row += 2;
        free(rgb);
        printf("%-8s models pass, both hands, 2 x 1728x1728: %.3f ms (median of 25)\n", kind[m], timeModels(ctx, 0));
        if (wantTex) {
            printf("%-8s ... trigger, stick moved: %.3f ms; every control down: %.3f ms\n", kind[m], timeModels(ctx, 1),
                   timeModels(ctx, 2));
        }
        m5_image_free(ctx, &cell.colour);
        m5_image_free(ctx, &cell.depth);
        xrWorldDestroy(ctx);
        free(ctx);
    }
    char path[700];
    const char* view = getenv("MLXR_FRAME_MODEL_VIEW");
    snprintf(path, sizeof(path), "%s/models_generic_vs_frame%s%s.png", dir, view && *view ? "_view" : "", view && *view ? view : "");
    stbi_write_png(path, VIEWS * CW, rows * CH, 3, sheet, VIEWS * CW * 3);
    printf("wrote %s (columns: user's side, from +x, from above, three-quarters behind; rows: left then right hand of "
           "each kind there: generic, painted, textured)\n", path);
    free(sheet);
    m5_gpu_free(&g);
    return rc;
}
