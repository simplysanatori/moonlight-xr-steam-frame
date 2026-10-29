// Moonlight XR Native, tests/m4: the ray recorder's CSV replayed through the
// input code (lane ray-glitch). Every runtime read the pointer's frame makes
// goes through the seams on the context (locateSpace, poseActive, readFloat,
// readBool, readVec2, pfnLocateHandJoints), which here hand back the recorded
// row, and xrPointerStep runs the frame's own readSources, clocks and pick on
// it. Each row's last columns say what the fixed code must decide: the source
// that points, the hand whose ray is drawn, and where each hand's ray came
// from (-9 where not checked). data/ray-replay.csv is synthetic
// (data/make_ray_replay.py), with the rays that snapped vertical on the Frame
// in it: a controller's aim lost with SteamVR's joints there (H1), the lap
// controller taking the pointer and the free beam (H2), a valid but untracked
// pose (H3). A CSV recorded on the headset goes through the same way. GPLv3.
#include "check.h"
#include "scene.h"

#include <stdio.h>
#include <string.h>

#ifndef MLXR_REPLAY_CSV
#define MLXR_REPLAY_CSV "data/ray-replay.csv"
#endif

#define UNCHECKED -9

// Stand ins for the handles, which only the seams ever see
#define FAKE(n) ((uintptr_t)(n))
#define LEFT_AIM ((XrSpace)FAKE(0x201))
#define RIGHT_AIM ((XrSpace)FAKE(0x202))
#define VIEW ((XrSpace)FAKE(0x203))
#define LOCAL ((XrSpace)FAKE(0x204))
#define LEFT_TRACKER ((XrHandTrackerEXT)FAKE(0x301))
#define RIGHT_TRACKER ((XrHandTrackerEXT)FAKE(0x302))

// The row being replayed
static RayLogRow cur;

static XrResult replayLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time,
                             XrSpaceLocation* out) {
    if (space == VIEW) {
        out->locationFlags = cur.headFlags;
        out->pose = cur.head;
        return XR_SUCCESS;
    }
    int h = space == LEFT_AIM ? 0 : (space == RIGHT_AIM ? 1 : -1);
    if (h < 0) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (!cur.hand[h].ok) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    out->locationFlags = cur.hand[h].flags;
    out->pose = cur.hand[h].pose;
    return XR_SUCCESS;
}

static int replayPoseActive(XrCtx* ctx, XrAction action, int hand) {
    return hand >= 0 && hand < 2 && cur.hand[hand].active;
}

static float replayFloat(XrCtx* ctx, XrAction action, int hand) {
    if (hand < 0 || hand > 1) {
        return 0.0f;
    }
    if (action == ctx->triggerAction) {
        return cur.hand[hand].trigger;
    }
    if (action == ctx->grabAction) {
        return cur.hand[hand].grip;
    }
    return 0.0f;
}

static int replayBool(XrCtx* ctx, XrAction action, int hand) {
    if (hand < 0 || hand > 1) {
        return 0;
    }
    unsigned b = cur.hand[hand].buttons;
    if (action == ctx->rightClickAction) {
        return (b & RAYLOG_BTN_RIGHT_CLICK) != 0;
    }
    if (action == ctx->middleClickAction) {
        return (b & RAYLOG_BTN_MIDDLE_CLICK) != 0;
    }
    if (action == ctx->toggleAction) {
        return (b & RAYLOG_BTN_STICK_CLICK) != 0;
    }
    return 0;
}

static XrVector2f replayVec2(XrCtx* ctx, XrAction action, int hand) {
    XrVector2f zero = { 0.0f, 0.0f };
    if (hand < 0 || hand > 1 || action != ctx->scrollAction) {
        return zero;
    }
    return cur.hand[hand].stick;
}

// The joints as SteamVR makes them up from a held controller: only the index
// knuckle the ray is cast through, where the recorder saw it
static XrResult XRAPI_CALL replayJoints(XrHandTrackerEXT tracker,
                                        const XrHandJointsLocateInfoEXT* info,
                                        XrHandJointLocationsEXT* out) {
    int h = tracker == LEFT_TRACKER ? 0 : 1;
    for (uint32_t j = 0; j < out->jointCount; j++) {
        memset(&out->jointLocations[j], 0, sizeof(out->jointLocations[j]));
        out->jointLocations[j].pose.orientation.w = 1.0f;
    }
    out->isActive = cur.hand[h].joints ? XR_TRUE : XR_FALSE;
    if (cur.hand[h].joints && out->jointCount > XR_HAND_JOINT_INDEX_PROXIMAL_EXT) {
        XrHandJointLocationEXT* k = &out->jointLocations[XR_HAND_JOINT_INDEX_PROXIMAL_EXT];
        k->locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT
                | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        k->pose.position = cur.hand[h].knuckle;
    }
    return XR_SUCCESS;
}

// The scene the synthetic rows were made for: the picture 2.4 m wide, 2.5 m
// ahead at eye height, both hands on controllers, hand tracking on, and the
// pointer's pause off
static void setUp(XrCtx* c) {
    float place[10] = { 0.0f, 0.0f, -2.5f, 0.0f, 0.0f, 0.0f, 1.0f, 2.4f, 2.5f, -1.0f };
    xrSetScreenPose(c, place);
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->aimSpaces[0] = LEFT_AIM;
    c->aimSpaces[1] = RIGHT_AIM;
    c->aimAction = (XrAction)FAKE(0x401);
    c->triggerAction = (XrAction)FAKE(0x402);
    c->grabAction = (XrAction)FAKE(0x403);
    c->scrollAction = (XrAction)FAKE(0x404);
    c->rightClickAction = (XrAction)FAKE(0x405);
    c->middleClickAction = (XrAction)FAKE(0x406);
    c->toggleAction = (XrAction)FAKE(0x407);
    c->jointTracking = 1;
    c->handTrackers[0] = LEFT_TRACKER;
    c->handTrackers[1] = RIGHT_TRACKER;
    c->pfnLocateHandJoints = replayJoints;
    c->locateSpace = replayLocate;
    c->poseActive = replayPoseActive;
    c->readFloat = replayFloat;
    c->readBool = replayBool;
    c->readVec2 = replayVec2;
    c->pointerSleepOn = 0;
    c->padMode = 0;
    c->eyeGaze = 0;
}

int main(void) {
    FILE* in = fopen(MLXR_REPLAY_CSV, "r");
    if (in == NULL) {
        fprintf(stderr, "replay: cannot read %s\n", MLXR_REPLAY_CSV);
        return 1;
    }
    Scene* s = scene_new();
    XrCtx* c = s->ctx;
    setUp(c);

    char line[2048];
    int rows = 0, mismatched = 0;
    while (fgets(line, sizeof(line), in) != NULL) {
        RayLogRow want;
        if (!rayLogParse(line, &want)) {
            continue;
        }
        cur = want;
        rows++;
        c->profileKind[0] = want.hand[0].kind;
        c->profileKind[1] = want.hand[1].kind;
        c->predictedDisplayTime = (XrTime)want.t;
        xrPointerStep(c, want.t, want.toHead);
        const RayLogRow* got = &c->rayRow;

        int bad = 0;
        if (want.chosen != UNCHECKED) {
            CHECK(got->chosen == want.chosen);
            bad |= got->chosen != want.chosen;
        }
        if (want.ray != UNCHECKED) {
            CHECK(got->ray == want.ray);
            bad |= got->ray != want.ray;
        }
        for (int h = 0; h < 2; h++) {
            if (want.hand[h].src != UNCHECKED) {
                CHECK(got->hand[h].src == want.hand[h].src);
                bad |= got->hand[h].src != want.hand[h].src;
            }
            // Whatever the row, a controller's ray never comes out of the joints
            if (want.hand[h].kind == PROFILE_CONTROLLER) {
                CHECK(got->hand[h].src != AIM_JOINTS);
            }
            // and the recorder kept the row as it was handed in
            CHECK(got->hand[h].flags == want.hand[h].flags);
            CHECK(got->hand[h].joints == want.hand[h].joints);
        }
        if (bad && mismatched++ < 20) {
            fprintf(stderr, "  row %d t %lld: pointing %d (want %d), ray %d (want %d), "
                    "src %d %d (want %d %d), hovers %d, awake %d %d\n", rows,
                    (long long)want.t, got->chosen, want.chosen, got->ray, want.ray,
                    got->hand[0].src, got->hand[1].src, want.hand[0].src, want.hand[1].src,
                    got->hover, got->hand[0].awake, got->hand[1].awake);
        }
    }
    fclose(in);
    // The whole file was read
    CHECK(rows >= 300);
    printf("replay: %d rows from %s\n", rows, MLXR_REPLAY_CSV);
    scene_free(s);
    return checksDone("m4_replay");
}
