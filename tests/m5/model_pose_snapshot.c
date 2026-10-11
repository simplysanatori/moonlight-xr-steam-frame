// Moonlight XR Native, tests/m5: mlxr-model-pose-snapshot. The Steam Frame's
// textured controller models (tools/frame-models) drawn through xrModelRecord
// (the client's own call, so the moving parts go through mlxr_model_pose as in
// the headset) at rest and with the trigger and grip pulled, the stick tilted
// and clicked, the face buttons, menu, view, bumper and d-pad pressed. One PNG
// a hand: a row a pose, three close views a row (the face from above and
// behind, and each side). Run by hand:
//
//   mlxr-model-pose-snapshot OUTDIR FRAMEMODELSDIR
//
// FRAMEMODELSDIR holds the converter's frame_controller_{left,right}.model.
// MLXR_FRAME_MODEL_VIEW and MLXR_FRAME_MODEL_GAIN act as in the client.
// GPLv3.
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "m5_gpu.h"
#include "xr_native.h"

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

#define CW 520
#define CH 400
#define VIEWS 3

static M5Gpu g;

typedef struct {
    M5Image colour, depth;
    XrView views[2];
    int w, h;
} Cell;

static void body(XrCtx* ctx, VkCommandBuffer cmd, void* user) {
    Cell* c = user;
    mlxr_barrier(cmd, c->colour.img.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    mlxr_barrier(cmd, c->depth.img.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    xrModelRecord(ctx, cmd, c->colour.img.view, VK_FORMAT_R8G8B8A8_SRGB, c->depth.img.view, c->w, c->h, c->views);
}

// A camera 'dist' from target along the direction given by yaw about +y then pitch about +x
// (pitch negative: from above), looking at the target
static void camera(XrView* v, Vec3 target, float yawDeg, float pitchDeg, float dist, float fov) {
    *v = m5_view(fov, CW, CH, 0.0f);
    Vec3 up = { 0, 1, 0 }, right = { 1, 0, 0 };
    XrQuaternionf q = quatMul(axisAngleQuat(up, yawDeg * 0.01745329f), axisAngleQuat(right, pitchDeg * 0.01745329f));
    Vec3 back = quatRotate(q, (Vec3){ 0, 0, dist });
    v->pose.orientation = q;
    v->pose.position = (XrVector3f){ target.x + back.x, target.y + back.y, target.z + back.z };
}

typedef struct {
    const char* name;
    int hands; // 1 left, 2 right, 3 both
    MlxrModelInput in;
} Pose;

#define IN(...) { { __VA_ARGS__ } }
static const Pose kPoses[] = {
    { "rest", 3, IN(0) },
    { "trigger, grip", 3, IN([MLXR_IN_TRIGGER] = 1, [MLXR_IN_GRIP] = 1) },
    { "trigger half", 3, IN([MLXR_IN_TRIGGER] = 0.5f) },
    { "stick right", 3, IN([MLXR_IN_STICK_X] = 1) },
    { "stick up", 3, IN([MLXR_IN_STICK_Y] = 1) },
    { "stick down-left, click", 3, IN([MLXR_IN_STICK_X] = -0.7f, [MLXR_IN_STICK_Y] = -0.7f, [MLXR_IN_STICK_CLICK] = 1) },
    { "A B", 2, IN([MLXR_IN_A] = 1, [MLXR_IN_B] = 1) },
    { "X Y menu", 2, IN([MLXR_IN_X] = 1, [MLXR_IN_Y] = 1, [MLXR_IN_MENU] = 1) },
    { "bumper", 3, IN([MLXR_IN_BUMPER] = 1) },
    { "view, d-pad up", 1, IN([MLXR_IN_VIEW] = 1, [MLXR_IN_DPAD_UP] = 1) },
    { "d-pad right", 1, IN([MLXR_IN_DPAD_RIGHT] = 1) },
    { "d-pad down-left", 1, IN([MLXR_IN_DPAD_DOWN] = 1, [MLXR_IN_DPAD_LEFT] = 1) },
};
#define POSES ((int)(sizeof(kPoses) / sizeof(kPoses[0])))

static void render(XrCtx* ctx, Cell* cell, int hand, const Pose* pose, int view, uint8_t* rgb) {
    // The grip's origin (0, 0, -0.4); the face plate sits about 5 cm in front of it (-z)
    Vec3 face = { hand ? -0.008f : 0.008f, 0.01f, -0.4f - 0.045f };
    Vec3 whole = { 0.0f, -0.005f, -0.4f - 0.015f };
    if (view == 0) camera(&cell->views[0], face, 0.0f, -48.0f, 0.22f, 27.0f);
    else if (view == 1) camera(&cell->views[0], whole, 90.0f, -8.0f, 0.5f, 15.0f);
    else camera(&cell->views[0], whole, -90.0f, -8.0f, 0.5f, 15.0f);
    cell->views[1] = cell->views[0];
    for (int e = 0; e < 2; e++) ctx->modelViews[e] = cell->views[e];
    ctx->modelShown[hand] = 1;
    ctx->modelShown[1 - hand] = 0;
    ctx->modelGrip[hand] = (XrPosef){ { 0, 0, 0, 1 }, { 0.0f, 0.0f, -0.4f } };
    memcpy(ctx->modelInput[hand], &pose->in, sizeof(pose->in));
    m5_frame(&g, ctx, body, cell);
    uint8_t* px = malloc((size_t)2 * CW * CH * 4);
    m5_readback(&g, cell->colour.img.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 2 * CW, CH, px);
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

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: mlxr-model-pose-snapshot OUTDIR FRAMEMODELSDIR\n");
        return 2;
    }
    const char* dir = argv[1];
    mkdir(dir, 0755);
    char assets[700], cmd[2600];
    snprintf(assets, sizeof(assets), "%s/_assets", dir);
    snprintf(cmd, sizeof(cmd), "rm -rf '%s' && mkdir -p '%s/models' && cp '%s/models/controller.room' '%s/models/' && cp "
             "'%s/frame_controller_left.model' '%s/frame_controller_right.model' '%s/models/'",
             assets, assets, MLXR_ASSETS_DIR, assets, argv[2], argv[2], assets);
    if (system(cmd) != 0 || m5_gpu_init(&g) != 0) {
        fprintf(stderr, "could not stage the models, or no Vulkan device\n");
        return 1;
    }
    XrCtx* ctx = calloc(1, sizeof(XrCtx));
    xrCtxDefaults(ctx, 1920, 1080, 60);
    Settings st;
    settings_defaults(&st);
    xrApplySettings(ctx, &st);
    m5_ctx_gpu(&g, ctx);
    snprintf(ctx->assetsDir, sizeof(ctx->assetsDir), "%s", assets);
    ctx->sessionState = XR_SESSION_STATE_FOCUSED;
    ctx->shouldRender = 1;
    ctx->modelOn = 1;
    m5_frame(&g, ctx, NULL, NULL);
    ctx->modelsShowing = 1;
    if (!ctx->model || !ctx->model->textured) {
        fprintf(stderr, "the textured pair did not load\n");
        return 1;
    }
    for (int h = 0; h < 2; h++) {
        printf("hand %d components:", h);
        for (uint32_t c = 0; c < ctx->model->componentCount[h]; c++) {
            const MlxrModelComponent* mc = &ctx->model->components[h][c];
            printf(" %s[%u %s]", mc->name, mc->motionType, mc->path);
        }
        printf("\n");
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
        int rows = 0, rowOf[POSES];
        for (int p = 0; p < POSES; p++) rowOf[p] = kPoses[p].hands & (1 << h) ? rows++ : -1;
        uint8_t* sheet = calloc((size_t)VIEWS * CW * rows * CH * 3, 1);
        for (int p = 0; p < POSES; p++) {
            if (rowOf[p] < 0) continue;
            for (int v = 0; v < VIEWS; v++) {
                render(ctx, &cell, h, &kPoses[p], v, rgb);
                for (int y = 0; y < CH; y++) {
                    memcpy(sheet + (((size_t)(rowOf[p] * CH + y)) * VIEWS * CW + (size_t)v * CW) * 3, rgb + (size_t)y * CW * 3,
                           (size_t)CW * 3);
                }
            }
            printf("%s hand row %d: %s\n", h ? "right" : "left", rowOf[p], kPoses[p].name);
        }
        char path[700];
        const char* view = getenv("MLXR_FRAME_MODEL_VIEW");
        snprintf(path, sizeof(path), "%s/pose_%s%s%s.png", dir, h ? "right" : "left", view && *view ? "_view" : "",
                 view && *view ? view : "");
        stbi_write_png(path, VIEWS * CW, rows * CH, 3, sheet, VIEWS * CW * 3);
        printf("wrote %s\n", path);
        free(sheet);
    }
    free(rgb);
    m5_image_free(ctx, &cell.colour);
    m5_image_free(ctx, &cell.depth);
    xrWorldDestroy(ctx);
    free(ctx);
    m5_gpu_free(&g);
    return 0;
}
