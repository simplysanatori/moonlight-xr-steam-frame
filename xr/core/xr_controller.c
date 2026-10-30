// The controllers as they are drawn, see xr_controller.h
#include "xr_controller.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "xr_gate.h"
#include "xr_ray.h"

int raySwitchOn(int settingOn, int flipped) {
    return (settingOn != 0) != (flipped != 0);
}

int rayFlipFor(int settingOn, int wantOn) {
    return (settingOn != 0) != (wantOn != 0);
}

int rayDrawn(int settingOn, int flipped, int panelOpen) {
    (void)panelOpen;
    return raySwitchOn(settingOn, flipped);
}

int rayBeamKind(int settingOn, int flipped, int panelOpen, int dotOn, int overBarBand) {
    if (rayDrawn(settingOn, flipped, panelOpen)) {
        return RAY_BEAM_FULL;
    }
    return (overBarBand != 0 || panelOpen != 0) && dotOn == 0 ? RAY_BEAM_RESTING : RAY_BEAM_NONE;
}

float pointerDotSize(float distance) {
    float size = distance * (PTR_DOT_SIZE_M / PTR_DOT_REF_M);
    return size < PTR_DOT_MIN_M ? PTR_DOT_MIN_M : (size > PTR_DOT_MAX_M ? PTR_DOT_MAX_M : size);
}

int controllerModelShown(int settingOn, int passthrough, int kind, int poseActive,
                         unsigned flags) {
    // Native (lane ray-glitch): the rule the ray goes by too, so the two
    // hold and hide together
    return settingOn && !passthrough && kind == PROFILE_CONTROLLER
            && controllerPoseLive(poseActive, flags);
}

void controllerModelMatrix(XrPosef grip, int leftHand, float* out16) {
    Vec3 ex = { leftHand ? -1.0f : 1.0f, 0.0f, 0.0f };
    Vec3 ey = { 0.0f, 1.0f, 0.0f };
    Vec3 ez = { 0.0f, 0.0f, 1.0f };
    Vec3 cols[3] = { quatRotate(grip.orientation, ex), quatRotate(grip.orientation, ey),
                     quatRotate(grip.orientation, ez) };
    for (int c = 0; c < 3; c++) {
        out16[c * 4 + 0] = cols[c].x;
        out16[c * 4 + 1] = cols[c].y;
        out16[c * 4 + 2] = cols[c].z;
        out16[c * 4 + 3] = 0.0f;
    }
    out16[12] = grip.position.x;
    out16[13] = grip.position.y;
    out16[14] = grip.position.z;
    out16[15] = 1.0f;
}

Vec3 controllerModelPoint(XrPosef grip, int leftHand, Vec3 local) {
    if (leftHand) {
        local.x = -local.x;
    }
    Vec3 turned = quatRotate(grip.orientation, local);
    Vec3 out = { turned.x + grip.position.x, turned.y + grip.position.y,
                 turned.z + grip.position.z };
    return out;
}

// ---- which model, and from which files --------------------------------------------------------

int controllerModelKind(int on, int kind) {
    if (!on) {
        return CONTROLLER_MODEL_OFF;
    }
    return kind > CONTROLLER_MODEL_OFF && kind < CONTROLLER_MODEL_COUNT ? kind : CONTROLLER_MODEL_FRAME;
}

int controllerModelSpace(int kind) {
    return kind == CONTROLLER_MODEL_GUN ? MODEL_SPACE_AIM : MODEL_SPACE_GRIP;
}

const char* controllerModelKindName(int kind) {
    static const char* const NAMES[CONTROLLER_MODEL_COUNT] = { "off", "frame", "gun", "custom" };
    return kind >= 0 && kind < CONTROLLER_MODEL_COUNT ? NAMES[kind] : "?";
}

const char* controllerModelUserDir(char* buf, size_t cap) {
    const char* dir = getenv("MLXR_CONFIG_DIR");
    if (dir != NULL && *dir != '\0') {
        snprintf(buf, cap, "%s/models", dir);
        return buf;
    }
    const char* home = getenv("HOME");
    if (home == NULL || *home == '\0') {
        return NULL;
    }
    snprintf(buf, cap, "%s/.config/moonlight-xr/models", home);
    return buf;
}

static int readable(const char* path) {
    return access(path, R_OK) == 0;
}

static void addCandidate(ModelCandidate* out, int* n, int format, int kind, const char* a, const char* b) {
    if (*n >= MODEL_CANDIDATES_MAX) {
        return;
    }
    ModelCandidate* c = &out[(*n)++];
    c->format = format;
    c->kind = kind;
    snprintf(c->path[0], MODEL_PATH_MAX, "%s", a);
    snprintf(c->path[1], MODEL_PATH_MAX, "%s", b != NULL ? b : "");
}

// A pair of files dir/<stem>_left.model and dir/<stem>_right.model. Either half there is a try
// (the loader says which is wrong); no half is none
static int addModelPair(ModelCandidate* out, int* n, int kind, const char* dir, const char* stem, int both) {
    char l[MODEL_PATH_MAX], r[MODEL_PATH_MAX];
    snprintf(l, sizeof(l), "%s/%s_left.model", dir, stem);
    snprintf(r, sizeof(r), "%s/%s_right.model", dir, stem);
    int has = both ? (readable(l) && readable(r)) : (readable(l) || readable(r));
    if (has) {
        addCandidate(out, n, MODEL_FILES_TEXTURED_PAIR, kind, l, r);
    }
    return has;
}

// What the Steam Frame's models are: the textured pair, else the painted pair an older converter
// made. Nothing after them (lane fix-polish: the generic controller is gone)
static void addFrameChain(ModelCandidate* out, int* n, const char* assets) {
    char dir[MODEL_PATH_MAX], l[MODEL_PATH_MAX], r[MODEL_PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/models", assets);
    addModelPair(out, n, CONTROLLER_MODEL_FRAME, dir, "frame_controller", 0);
    snprintf(l, sizeof(l), "%s/frame_controller_left.room", dir);
    snprintf(r, sizeof(r), "%s/frame_controller_right.room", dir);
    if (readable(l) && readable(r)) {
        addCandidate(out, n, MODEL_FILES_PAINTED_PAIR, CONTROLLER_MODEL_FRAME, l, r);
    }
}

int controllerModelCandidates(int kind, const char* assetsDir, const char* userDir, ModelCandidate* out) {
    int n = 0;
    char dir[MODEL_PATH_MAX], l[MODEL_PATH_MAX];
    if (assetsDir == NULL) {
        assetsDir = ".";
    }
    snprintf(dir, sizeof(dir), "%s/models", assetsDir);
    switch (kind) {
    case CONTROLLER_MODEL_FRAME:
        addFrameChain(out, &n, assetsDir);
        break;
    case CONTROLLER_MODEL_GUN:
        addModelPair(out, &n, CONTROLLER_MODEL_GUN, dir, "gun", 0);
        addFrameChain(out, &n, assetsDir);
        break;
    case CONTROLLER_MODEL_CUSTOM:
        if (userDir != NULL && *userDir != '\0') {
            // A pair if both halves are there, else the one file, mirrored; never half a pair
            addModelPair(out, &n, CONTROLLER_MODEL_CUSTOM, userDir, "custom", 1);
            snprintf(l, sizeof(l), "%s/custom.model", userDir);
            if (readable(l)) {
                addCandidate(out, &n, MODEL_FILES_TEXTURED_ONE, CONTROLLER_MODEL_CUSTOM, l, NULL);
            }
        }
        addFrameChain(out, &n, assetsDir);
        break;
    default:
        break;
    }
    return n;
}
