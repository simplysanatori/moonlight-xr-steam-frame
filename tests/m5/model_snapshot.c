// Moonlight XR Native, tests/m5: mlxr-model-snapshot. The controller models
// drawn through xrModelRecord on the Frame's GPU, the generic one beside the
// Steam Frame's own (tools/frame-models) at the same grip poses, from the
// front, the side and above, one hand at a time, tiled into one PNG; and the
// models pass timed for each. The grip is identity-oriented (trigger forward,
// -z; top face up, +y), so a model that sits in the hand as the generic one
// does shows the same way up and the same way round in both rows.
//
//   mlxr-model-snapshot OUTDIR FRAMEMODELSDIR
//
// FRAMEMODELSDIR holds frame_controller_{left,right}.room (the converter's
// output). GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "m5_gpu.h"
#include "xr_native.h"

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

#define CW 480
#define CH 360
#define VIEWS 3

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
    xrModelRecord(ctx, cmd, c->colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, c->depth.img.view, c->w, c->h, c->views);
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
// front (the user's own side, +z), the side (+x) and above (+y)
static void camera(int view, XrView* v) {
    *v = m5_view(40.0f, CW, CH, 0.0f);
    Vec3 up = { 0, 1, 0 }, right = { 1, 0, 0 };
    if (view == 1) {
        v->pose.orientation = axisAngleQuat(up, 1.5707963f);
        v->pose.position = (XrVector3f){ 0.45f, 0.0f, -0.4f };
    }
    else if (view == 2) {
        v->pose.orientation = axisAngleQuat(right, -1.5707963f);
        v->pose.position = (XrVector3f){ 0.0f, 0.45f, -0.4f };
    }
    else {
        v->pose.position = (XrVector3f){ 0.0f, 0.0f, 0.05f };
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
static double timeModels(XrCtx* ctx) {
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

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: mlxr-model-snapshot OUTDIR FRAMEMODELSDIR\n");
        return 2;
    }
    const char* dir = argv[1];
    mkdir(dir, 0755);
    // A scratch assets dir: the generic controller and the Frame's pair
    char assets[600], cmd[2400];
    snprintf(assets, sizeof(assets), "%s/_assets", dir);
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/models' && cp '%s/models/controller.room' '%s/models/' && cp '%s'/frame_controller_*.room "
             "'%s/models/'", assets, MLXR_ASSETS_DIR, assets, argv[2], assets);
    if (system(cmd) != 0) {
        fprintf(stderr, "could not stage the models\n");
        return 1;
    }
    char noFrame[600];
    snprintf(noFrame, sizeof(noFrame), "%s/_generic", dir);
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s/models' && cp '%s/models/controller.room' '%s/models/'", noFrame, MLXR_ASSETS_DIR,
             noFrame);
    if (system(cmd) != 0 || m5_gpu_init(&g) != 0) {
        fprintf(stderr, "no Vulkan device\n");
        return 1;
    }
    const char* kind[2] = { "generic", "frame" };
    const char* dirs[2] = { noFrame, assets };
    // rows: generic left, frame left, generic right, frame right
    uint8_t* sheet = calloc((size_t)VIEWS * CW * 4 * CH * 3, 1);
    int rc = 0;
    for (int m = 0; m < 2; m++) {
        XrCtx* ctx = newCtx(dirs[m]);
        if (!ctx->model || ctx->model->perHand != m) {
            fprintf(stderr, "%s: wrong model loaded\n", kind[m]);
            rc = 1;
            continue;
        }
        printf("%-8s %s: %u vertices, %u triangles, %.0f KB\n", kind[m], ctx->model->source, ctx->model->vertexCount,
               ctx->model->indexCount / 3, ctx->model->gpuBytes / 1024.0);
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
                int row = h * 2 + m;
                for (int y = 0; y < CH; y++) {
                    memcpy(sheet + (((size_t)(row * CH + y)) * VIEWS * CW + (size_t)v * CW) * 3, rgb + (size_t)y * CW * 3,
                           (size_t)CW * 3);
                }
            }
        }
        free(rgb);
        printf("%-8s models pass, both hands, 2 x 1728x1728: %.3f ms (median of 25)\n", kind[m], timeModels(ctx));
        m5_image_free(ctx, &cell.colour);
        m5_image_free(ctx, &cell.depth);
        xrWorldDestroy(ctx);
        free(ctx);
    }
    char path[700];
    snprintf(path, sizeof(path), "%s/models_generic_vs_frame.png", dir);
    stbi_write_png(path, VIEWS * CW, 4 * CH, 3, sheet, VIEWS * CW * 3);
    printf("wrote %s (columns: user's side, from +x, from above; rows: generic left, Frame left, generic right, "
           "Frame right)\n", path);
    free(sheet);
    m5_gpu_free(&g);
    return rc;
}
