// The controllers as drawn: when the ray's beam goes up, when a controller's
// model is drawn, where the model goes for each hand, and how wide the cursor
// dot is at each distance
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "xr_controller.h"
#include "xr_gate.h"

static void testRaySwitch(void) {
    // The setting is where a session starts, and each flip turns it over
    CHECK(raySwitchOn(1, 0) == 1);
    CHECK(raySwitchOn(1, 1) == 0);
    CHECK(raySwitchOn(0, 0) == 0);
    CHECK(raySwitchOn(0, 1) == 1);
    // Any non zero reads as set
    CHECK(raySwitchOn(5, 0) == 1);
    CHECK(raySwitchOn(5, 7) == 0);

    // The flip a row's cell asks for lands the switch on that cell, from
    // either setting
    for (int setting = 0; setting < 2; setting++) {
        for (int want = 0; want < 2; want++) {
            CHECK(raySwitchOn(setting, rayFlipFor(setting, want)) == want);
        }
    }
    // Asking for what is already in force asks for no flip
    CHECK(rayFlipFor(1, 1) == 0);
    CHECK(rayFlipFor(0, 0) == 0);
}

static void testRayDrawn(void) {
    // Every combination, against the rule written out: the beam shows while
    // the switch is on, and while any panel is up whatever the switch says
    for (int setting = 0; setting < 2; setting++) {
        for (int flipped = 0; flipped < 2; flipped++) {
            for (int panel = 0; panel < 2; panel++) {
                int on = setting != flipped;
                CHECK(rayDrawn(setting, flipped, panel) == (on || panel));
            }
        }
    }

    // A session started with the setting off: hidden, back while the cog is
    // open, hidden again once it closes
    CHECK(!rayDrawn(0, 0, 0));
    CHECK(rayDrawn(0, 0, 1));
    CHECK(!rayDrawn(0, 0, 0));
    // The bar button turns it on for the session, and a panel changes nothing
    CHECK(rayDrawn(0, 1, 0));
    CHECK(rayDrawn(0, 1, 1));
    // Started on and switched off from the bar, the same as the setting off
    CHECK(!rayDrawn(1, 1, 0));
    CHECK(rayDrawn(1, 1, 1));
}

static void testRayBeamKind(void) {
    // Every combination against the rule written out: whatever rayDrawn
    // allows is the full beam; otherwise the resting stub shows only with the
    // dot off, no panel up and the controller over the bar's band
    for (int setting = 0; setting < 2; setting++) {
        for (int flipped = 0; flipped < 2; flipped++) {
            for (int panel = 0; panel < 2; panel++) {
                for (int dot = 0; dot < 2; dot++) {
                    for (int band = 0; band < 2; band++) {
                        int want = rayDrawn(setting, flipped, panel) ? RAY_BEAM_FULL
                                 : (band && !dot && !panel) ? RAY_BEAM_RESTING : RAY_BEAM_NONE;
                        CHECK(rayBeamKind(setting, flipped, panel, dot, band) == want);
                    }
                }
            }
        }
    }
    // Ray Off, Dot Off (the cog's Off cell) from the start of a session:
    // nothing away from the bar, the stub inside its band, nothing again once
    // a panel opens (the full beam comes back there), and the full beam when
    // the ray is on whatever the band says
    CHECK(rayBeamKind(0, 0, 0, 0, 0) == RAY_BEAM_NONE);
    CHECK(rayBeamKind(0, 0, 0, 0, 1) == RAY_BEAM_RESTING);
    CHECK(rayBeamKind(0, 0, 1, 0, 1) == RAY_BEAM_FULL);
    CHECK(rayBeamKind(1, 0, 0, 1, 0) == RAY_BEAM_FULL);
    CHECK(rayBeamKind(1, 0, 0, 1, 1) == RAY_BEAM_FULL);
    // Ray Off with the Dot on: the dot shows where it points, so no stub
    CHECK(rayBeamKind(0, 0, 0, 1, 1) == RAY_BEAM_NONE);
    // Switched off from the bar for the session: the same as the setting off
    CHECK(rayBeamKind(1, 1, 0, 0, 1) == RAY_BEAM_RESTING);
    CHECK(rayBeamKind(1, 1, 0, 0, 0) == RAY_BEAM_NONE);
}

static void testModelShown(void) {
    const unsigned located = GATE_POSITION_VALID | GATE_ORIENTATION_VALID
            | GATE_ORIENTATION_TRACKED;
    const unsigned all = located | GATE_POSITION_TRACKED;
    // A controller held in view
    CHECK(controllerModelShown(1, 0, PROFILE_CONTROLLER, 1, all));
    // Out of the cameras for a moment: the position is the runtime's guess,
    // and the model stays with it
    CHECK(controllerModelShown(1, 0, PROFILE_CONTROLLER, 1, located));
    // Each condition on its own takes it away: the setting, passthrough,
    // hands or nothing on the hand, the grip not live, the orientation
    // untracked or either part not placed at all
    CHECK(!controllerModelShown(0, 0, PROFILE_CONTROLLER, 1, all));
    CHECK(!controllerModelShown(1, 1, PROFILE_CONTROLLER, 1, all));
    CHECK(!controllerModelShown(1, 0, PROFILE_HANDS, 1, all));
    CHECK(!controllerModelShown(1, 0, PROFILE_NONE, 1, all));
    CHECK(!controllerModelShown(1, 0, PROFILE_UNKNOWN, 1, all));
    CHECK(!controllerModelShown(1, 0, PROFILE_CONTROLLER, 0, all));
    CHECK(!controllerModelShown(1, 0, PROFILE_CONTROLLER, 1,
                                GATE_POSITION_VALID | GATE_ORIENTATION_VALID));
    CHECK(!controllerModelShown(1, 0, PROFILE_CONTROLLER, 1,
                                GATE_ORIENTATION_VALID | GATE_ORIENTATION_TRACKED));
    CHECK(!controllerModelShown(1, 0, PROFILE_CONTROLLER, 1,
                                GATE_POSITION_VALID | GATE_ORIENTATION_TRACKED));
    CHECK(!controllerModelShown(1, 0, PROFILE_CONTROLLER, 1, 0));
}

// A column major matrix times a point
static Vec3 apply(const float* m, Vec3 p) {
    Vec3 r = { m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
               m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
               m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] };
    return r;
}

static float det3(const float* m) {
    return m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2])
            + m[8] * (m[1] * m[6] - m[5] * m[2]);
}

static XrPosef pose(float x, float y, float z, XrQuaternionf q) {
    XrPosef p;
    p.orientation = q;
    p.position.x = x;
    p.position.y = y;
    p.position.z = z;
    return p;
}

static void testModelMatrix(void) {
    float m[16];
    XrQuaternionf none = { 0.0f, 0.0f, 0.0f, 1.0f };

    // At the origin unturned, the right hand is the model as authored and the
    // left its mirror image in x, and nothing else changes
    controllerModelMatrix(pose(0, 0, 0, none), 0, m);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(m[i], i % 5 == 0 ? 1.0 : 0.0, 1e-6);
    }
    controllerModelMatrix(pose(0, 0, 0, none), 1, m);
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(m[i], i == 0 ? -1.0 : (i % 5 == 0 ? 1.0 : 0.0), 1e-6);
    }

    // The grip's position is the last column and nothing else
    controllerModelMatrix(pose(0.2f, -0.3f, -0.5f, none), 0, m);
    CHECK_NEAR(m[12], 0.2, 1e-6);
    CHECK_NEAR(m[13], -0.3, 1e-6);
    CHECK_NEAR(m[14], -0.5, 1e-6);
    CHECK_NEAR(m[15], 1.0, 1e-6);
    CHECK_NEAR(m[3], 0.0, 1e-6);
    CHECK_NEAR(m[7], 0.0, 1e-6);
    CHECK_NEAR(m[11], 0.0, 1e-6);

    // A quarter turn to the left about up takes the model's right to straight
    // ahead and its forward to the left, as quatRotate has them
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    XrQuaternionf quarter = axisAngleQuat(up, (float)M_PI * 0.5f);
    controllerModelMatrix(pose(0, 0, 0, quarter), 0, m);
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 forward = { 0.0f, 0.0f, -1.0f };
    Vec3 r = apply(m, right);
    Vec3 f = apply(m, forward);
    CHECK_NEAR(r.x, 0.0, 1e-6);
    CHECK_NEAR(r.z, -1.0, 1e-6);
    CHECK_NEAR(f.x, -1.0, 1e-6);
    CHECK_NEAR(f.z, 0.0, 1e-6);

    // Any grip: the matrix and the point worked without it agree, for both
    // hands, on points across the model, and the left hand's point is the
    // right hand's for the point mirrored in x
    Vec3 axis = vecNorm((Vec3){ 0.3f, -0.8f, 0.5f });
    XrPosef grip = pose(-0.12f, 1.05f, -0.4f, axisAngleQuat(axis, 2.1f));
    Vec3 points[] = { { 0.0f, 0.0f, 0.0f }, { 0.006f, 0.093f, -0.024f },
                      { 0.0f, -0.055f, 0.022f }, { -0.008f, 0.054f, -0.016f },
                      { 0.03f, -0.01f, -0.06f } };
    for (int hand = 0; hand < 2; hand++) {
        controllerModelMatrix(grip, hand, m);
        for (unsigned i = 0; i < sizeof(points) / sizeof(points[0]); i++) {
            Vec3 a = apply(m, points[i]);
            Vec3 b = controllerModelPoint(grip, hand, points[i]);
            CHECK_NEAR(a.x, b.x, 1e-6);
            CHECK_NEAR(a.y, b.y, 1e-6);
            CHECK_NEAR(a.z, b.z, 1e-6);
            Vec3 mirrored = { -points[i].x, points[i].y, points[i].z };
            Vec3 c = controllerModelPoint(grip, !hand, mirrored);
            CHECK_NEAR(b.x, c.x, 1e-6);
            CHECK_NEAR(b.y, c.y, 1e-6);
            CHECK_NEAR(b.z, c.z, 1e-6);
        }
        // A turn keeps its handedness and the left hand's mirror flips it,
        // with every column still a unit length apart and square to the rest
        CHECK_NEAR(det3(m), hand ? -1.0 : 1.0, 1e-5);
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                float dot = m[a * 4] * m[b * 4] + m[a * 4 + 1] * m[b * 4 + 1]
                        + m[a * 4 + 2] * m[b * 4 + 2];
                CHECK_NEAR(dot, a == b ? 1.0 : 0.0, 1e-5);
            }
        }
    }

    // The points that lie on the grip's own centre plane are where both hands
    // put them, and the two hands' models are each other's reflection there
    controllerModelMatrix(grip, 0, m);
    float left[16];
    controllerModelMatrix(grip, 1, left);
    Vec3 onPlane = { 0.0f, 0.04f, -0.03f };
    Vec3 a = apply(m, onPlane);
    Vec3 b = apply(left, onPlane);
    CHECK_NEAR(a.x, b.x, 1e-6);
    CHECK_NEAR(a.y, b.y, 1e-6);
    CHECK_NEAR(a.z, b.z, 1e-6);
    // Only the first column differs, and by its sign
    for (int i = 0; i < 16; i++) {
        CHECK_NEAR(left[i], i < 3 ? -m[i] : m[i], 1e-6);
    }
}

// The angle the dot fills, seen from distance metres
static double dotAngle(float distance) {
    return 2.0 * atan(0.5 * pointerDotSize(distance) / distance);
}

static void testDotSize(void) {
    // The default screen, 3 m off, keeps the 2.2 cm it always had
    CHECK_NEAR(pointerDotSize(3.0f), 0.022, 1e-6);
    // A metre away it is a third of that and looks the same size, and so it
    // does on Synthwave's wall 14 m off and the Grand Cinema's further still
    CHECK_NEAR(pointerDotSize(1.0f), 0.022 / 3.0, 1e-6);
    CHECK_NEAR(pointerDotSize(14.0f), 0.022 * 14.0 / 3.0, 1e-6);
    CHECK_NEAR(dotAngle(1.0f), dotAngle(3.0f), 1e-6);
    CHECK_NEAR(dotAngle(14.0f), dotAngle(3.0f), 1e-6);
    CHECK_NEAR(dotAngle(16.0f), dotAngle(3.0f), 1e-6);
    // Everywhere between the floor and the ceiling the angle holds
    float floorAt = PTR_DOT_MIN_M * PTR_DOT_REF_M / PTR_DOT_SIZE_M;
    float ceilingAt = PTR_DOT_MAX_M * PTR_DOT_REF_M / PTR_DOT_SIZE_M;
    CHECK(floorAt < 1.0f && ceilingAt > 16.0f);
    int off = 0;
    float last = 0.0f;
    for (float d = 0.05f; d < 40.0f; d += 0.05f) {
        float size = pointerDotSize(d);
        if (size < last) off++;
        last = size;
        if (size < PTR_DOT_MIN_M || size > PTR_DOT_MAX_M) off++;
        if (d > floorAt + 0.01f && d < ceilingAt - 0.01f
                && fabs(dotAngle(d) - dotAngle(3.0f)) > 1e-5) {
            off++;
        }
    }
    CHECK(off == 0);
    // Up close it stops at the floor rather than shrinking to a speck, far
    // off at the ceiling, and nothing measured is the floor
    CHECK(pointerDotSize(0.2f) == PTR_DOT_MIN_M);
    CHECK(pointerDotSize(100.0f) == PTR_DOT_MAX_M);
    CHECK(pointerDotSize(0.0f) == PTR_DOT_MIN_M);
    CHECK(pointerDotSize(-1.0f) == PTR_DOT_MIN_M);
}

// ---- which model: the kind, its space and the files it reads ---------------------------------

static void touch(const char* dir, const char* name) {
    char path[1100];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "wb");
    if (f != NULL) {
        fputs("x", f);
        fclose(f);
    }
}

// A fresh folder tree: root/assets/models (the bundled and the Frame's files) and root/user
static void scratch(char* root, char* assets, char* user) {
    strcpy(root, "/tmp/mlxr-model-kind-XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    char path[1100];
    snprintf(assets, 600, "%.500s/assets", root);
    snprintf(user, 600, "%.500s/user", root);
    CHECK(mkdir(assets, 0755) == 0);
    snprintf(path, sizeof(path), "%.500s/models", assets);
    CHECK(mkdir(path, 0755) == 0);
    CHECK(mkdir(user, 0755) == 0);
}

static void rmTree(const char* root) {
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "rm -rf '%.1000s'", root);
    CHECK(system(cmd) == 0);
}

// The candidates as "format:kind" pairs, for one comparison
static void listed(int kind, const char* assets, const char* user, char* out, size_t cap) {
    ModelCandidate c[MODEL_CANDIDATES_MAX];
    int n = controllerModelCandidates(kind, assets, user, c);
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        size_t at = strlen(out);
        snprintf(out + at, cap - at, "%s%d:%d", i ? " " : "", c[i].format, c[i].kind);
    }
}

static int endsWith(const char* s, const char* tail) {
    size_t a = strlen(s), b = strlen(tail);
    return a >= b && strcmp(s + a - b, tail) == 0;
}

static void testModelKind(void) {
    // On with no kind (the first version of the row) is the Frame's; off is off whatever was kept
    CHECK(controllerModelKind(0, CONTROLLER_MODEL_GUN) == CONTROLLER_MODEL_OFF);
    CHECK(controllerModelKind(1, 0) == CONTROLLER_MODEL_FRAME);
    CHECK(controllerModelKind(1, CONTROLLER_MODEL_FRAME) == CONTROLLER_MODEL_FRAME);
    CHECK(controllerModelKind(1, CONTROLLER_MODEL_GENERIC) == CONTROLLER_MODEL_GENERIC);
    CHECK(controllerModelKind(1, CONTROLLER_MODEL_GUN) == CONTROLLER_MODEL_GUN);
    CHECK(controllerModelKind(1, CONTROLLER_MODEL_CUSTOM) == CONTROLLER_MODEL_CUSTOM);
    CHECK(controllerModelKind(1, CONTROLLER_MODEL_COUNT) == CONTROLLER_MODEL_FRAME);
    CHECK(controllerModelKind(1, -3) == CONTROLLER_MODEL_FRAME);
    // The settings' order is the enum's
    CHECK(CONTROLLER_MODEL_OFF == 0 && CONTROLLER_MODEL_FRAME == 1 && CONTROLLER_MODEL_GENERIC == 2 &&
          CONTROLLER_MODEL_GUN == 3 && CONTROLLER_MODEL_CUSTOM == 4 && CONTROLLER_MODEL_COUNT == 5);
    CHECK(strcmp(controllerModelKindName(CONTROLLER_MODEL_GUN), "gun") == 0);
    CHECK(strcmp(controllerModelKindName(CONTROLLER_MODEL_CUSTOM), "custom") == 0);
    CHECK(strcmp(controllerModelKindName(9), "?") == 0);
    // Only the gun is drawn at the aim pose
    for (int k = 0; k < CONTROLLER_MODEL_COUNT; k++) {
        CHECK(controllerModelSpace(k) == (k == CONTROLLER_MODEL_GUN ? MODEL_SPACE_AIM : MODEL_SPACE_GRIP));
    }
}

// Lists read "format:kind": format 1 textured pair, 2 textured one, 3 painted pair, 4 generic;
// kind 1 the Frame's, 2 generic, 3 gun, 4 custom
static void testModelCandidates(void) {
    char root[100], assets[600], user[600], models[700], got[200];
    scratch(root, assets, user);
    snprintf(models, sizeof(models), "%.500s/models", assets);
    ModelCandidate c[MODEL_CANDIDATES_MAX];

    // Off has nothing to read
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_OFF, assets, user, c) == 0);

    // Nothing of the Frame's installed: the Frame kind is the generic controller, tried even if the
    // file is gone (the loader says so)
    listed(CONTROLLER_MODEL_FRAME, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "4:2") == 0);
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_FRAME, assets, user, c) == 1);
    CHECK(endsWith(c[0].path[0], "/models/controller.room"));

    // The Frame's textured pair is first; one half there is still the pair (the loader refuses it)
    touch(models, "frame_controller_left.model");
    listed(CONTROLLER_MODEL_FRAME, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 4:2") == 0);
    touch(models, "frame_controller_right.model");
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_FRAME, assets, user, c) == 2);
    CHECK(c[0].format == MODEL_FILES_TEXTURED_PAIR && endsWith(c[0].path[0], "frame_controller_left.model") &&
          endsWith(c[0].path[1], "frame_controller_right.model"));

    // The painted pair from an older converter needs both halves, and comes after the textured one
    touch(models, "frame_controller_left.room");
    listed(CONTROLLER_MODEL_FRAME, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 4:2") == 0);
    touch(models, "frame_controller_right.room");
    listed(CONTROLLER_MODEL_FRAME, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 3:1 4:2") == 0);

    // Generic is the generic controller whatever the Frame's files are
    listed(CONTROLLER_MODEL_GENERIC, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "4:2") == 0);

    // Gun: its pair, then the Frame's chain when it is missing or broken
    listed(CONTROLLER_MODEL_GUN, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 3:1 4:2") == 0);
    touch(models, "gun_left.model");
    touch(models, "gun_right.model");
    listed(CONTROLLER_MODEL_GUN, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:3 1:1 3:1 4:2") == 0);
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_GUN, assets, user, c) == 4);
    CHECK(endsWith(c[0].path[0], "/models/gun_left.model") && endsWith(c[0].path[1], "/models/gun_right.model"));

    // Custom with no folder, or an empty one: the Frame's chain
    listed(CONTROLLER_MODEL_CUSTOM, assets, NULL, got, sizeof(got));
    CHECK(strcmp(got, "1:1 3:1 4:2") == 0);
    listed(CONTROLLER_MODEL_CUSTOM, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 3:1 4:2") == 0);
    // One half of a pair is not used
    touch(user, "custom_right.model");
    listed(CONTROLLER_MODEL_CUSTOM, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 3:1 4:2") == 0);
    touch(user, "custom_left.model");
    listed(CONTROLLER_MODEL_CUSTOM, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:4 1:1 3:1 4:2") == 0);
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_CUSTOM, assets, user, c) == 4);
    CHECK(strstr(c[0].path[0], "/user/custom_left.model") != NULL &&
          strstr(c[0].path[1], "/user/custom_right.model") != NULL);
    // A single custom.model: the right hand's, mirrored. A pair beside it is tried first
    touch(user, "custom.model");
    listed(CONTROLLER_MODEL_CUSTOM, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:4 2:4 1:1 3:1 4:2") == 0);
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_CUSTOM, assets, user, c) == 5);
    CHECK(c[1].format == MODEL_FILES_TEXTURED_ONE && endsWith(c[1].path[0], "/user/custom.model"));
    char gone[1200];
    snprintf(gone, sizeof(gone), "%.600s/custom_left.model", user);
    CHECK(remove(gone) == 0);
    listed(CONTROLLER_MODEL_CUSTOM, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "2:4 1:1 3:1 4:2") == 0);
    // The Frame kind never looks in the user's folder, nor the gun for custom files
    listed(CONTROLLER_MODEL_FRAME, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 3:1 4:2") == 0);
    listed(CONTROLLER_MODEL_GUN, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:3 1:1 3:1 4:2") == 0);
    // A gun of the user's own: both halves, then it comes before the bundled pair
    touch(user, "gun_right.model");
    listed(CONTROLLER_MODEL_GUN, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:3 1:1 3:1 4:2") == 0);
    touch(user, "gun_left.model");
    listed(CONTROLLER_MODEL_GUN, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:3 1:3 1:1 3:1 4:2") == 0);
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_GUN, assets, user, c) == 5);
    CHECK(strstr(c[0].path[0], "/user/gun_left.model") != NULL && strstr(c[0].path[1], "/user/gun_right.model") != NULL);
    CHECK(endsWith(c[1].path[0], "/models/gun_left.model") && strstr(c[1].path[0], "/user/") == NULL);
    // Without a user folder, or for the Frame kind, it is not read
    listed(CONTROLLER_MODEL_GUN, assets, NULL, got, sizeof(got));
    CHECK(strcmp(got, "1:3 1:1 3:1 4:2") == 0);
    listed(CONTROLLER_MODEL_FRAME, assets, user, got, sizeof(got));
    CHECK(strcmp(got, "1:1 3:1 4:2") == 0);

    // A kind out of range reads nothing
    CHECK(controllerModelCandidates(CONTROLLER_MODEL_COUNT, assets, user, c) == 0);
    CHECK(controllerModelCandidates(-1, assets, user, c) == 0);
    rmTree(root);

    // The files the build ships: the gun's pair is where the loader looks for it
    listed(CONTROLLER_MODEL_GUN, MLXR_ASSETS_DIR, NULL, got, sizeof(got));
    CHECK(strncmp(got, "1:3 ", 4) == 0);
    listed(CONTROLLER_MODEL_GENERIC, MLXR_ASSETS_DIR, NULL, got, sizeof(got));
    CHECK(strcmp(got, "4:2") == 0);
}

static void testModelUserDir(void) {
    char buf[600];
    const char* oldCfg = getenv("MLXR_CONFIG_DIR");
    const char* oldHome = getenv("HOME");
    char cfg[700] = "", home[700] = "";
    if (oldCfg) snprintf(cfg, sizeof(cfg), "%.600s", oldCfg);
    if (oldHome) snprintf(home, sizeof(home), "%.600s", oldHome);
    setenv("MLXR_CONFIG_DIR", "/tmp/mlxr-cfg-test", 1);
    CHECK(controllerModelUserDir(buf, sizeof(buf)) == buf && strcmp(buf, "/tmp/mlxr-cfg-test/models") == 0);
    unsetenv("MLXR_CONFIG_DIR");
    setenv("HOME", "/tmp/mlxr-home-test", 1);
    CHECK(controllerModelUserDir(buf, sizeof(buf)) == buf &&
          strcmp(buf, "/tmp/mlxr-home-test/.config/moonlight-xr/models") == 0);
    unsetenv("HOME");
    CHECK(controllerModelUserDir(buf, sizeof(buf)) == NULL);
    if (oldCfg) setenv("MLXR_CONFIG_DIR", cfg, 1);
    if (oldHome) setenv("HOME", home, 1);
}

int main(void) {
    testModelKind();
    testModelCandidates();
    testModelUserDir();
    testRaySwitch();
    testRayDrawn();
    testRayBeamKind();
    testModelShown();
    testModelMatrix();
    testDotSize();
    return checksDone("xr_controller");
}
