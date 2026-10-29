// The pointer's ray: what a hand's ray is built from, how long a controller
// pose that has lost its tracking is held before the ray goes, which source
// points, and the basis the beam's quad is drawn in. Plain arithmetic over
// values handed in, no OpenXR calls and no context, so the host tests reach
// all of it (tests/xrcore/test_ray.c).
//
// Native (lane ray-glitch): the rules behind the rays that snapped vertical
// for about a second on the Frame (docs/dev/ray-debug.md has the four
// suspects, H1 to H4, and the recorder that tells them apart).

#ifndef XR_RAY_H
#define XR_RAY_H

#include <stdint.h>

#include "xr_math.h"

// Where a hand's ray comes from this frame: nothing, the runtime's aim pose,
// the ray cast from the shoulder through the joints, or the last tracked pose
// held across a short loss of tracking
#define AIM_NONE   0
#define AIM_POSE   1
#define AIM_JOINTS 2
#define AIM_HELD   3

// The aim pose where the runtime placed it. Otherwise the ray built out of the
// joints stands in, which is what makes hand pointing work on runtimes that
// refuse the hand profile, but never on a hand holding a controller: SteamVR
// makes joints up from a held controller, and that ray, from the shoulder
// through the knuckle, is pitched 35 to 70 degrees off the aim (H1). kind is
// a PROFILE_ value from xr_gate.h.
int aimFallbackFor(int kind, int located, int jointsValid);

// A controller pose that is drawn as it is: its pose action live, placed (both
// VALID bits) and its orientation tracked. Position only needs to be valid,
// so a controller out of the cameras' view for a moment, on its gyro, keeps
// its ray and its model where the runtime puts them. The model and the ray
// both go by this, and hold and hide together. flags are the GATE_ bits.
int controllerPoseLive(int poseActive, unsigned flags);

// How long a controller pose that stopped being live is held where it last
// was, before its ray and its model are hidden (H3). Short, so a real loss
// never leaves a ray standing in the wrong place.
#define POSE_HOLD_NS 300000000LL

#define POSE_LIVE 0
#define POSE_HELD 1
#define POSE_GONE 2

typedef struct {
    // A live pose has been seen, when the last one was, and the pose to hold
    int have;
    int64_t liveAt;
    XrPosef pose;
} PoseHold;

void poseHoldReset(PoseHold* h);

// One frame. live is controllerPoseLive this frame. Says POSE_LIVE when it is,
// POSE_HELD within holdNs of the last live frame (draw h->pose), POSE_GONE
// after that and before the first live frame.
int poseHoldStep(PoseHold* h, int live, int64_t nowNs, int64_t holdNs);

// The pose drawn on a live frame, which is what a loss of tracking holds
void poseHoldKeep(PoseHold* h, XrPosef drawn);

// Which source points (H2). The one that pointed last keeps the pointer while
// its ray is on something. Another takes it only when a button is down on it,
// or when it is awake on its own clock (moved lately, so in a hand) and the
// last one has been off everything, or gone, for longer than PICK_HOLD_NS. A
// controller lying in a lap is asleep, so its ray, however it lies across the
// picture or the bar, never takes the pointer, and a ray that slips off the
// edge of the picture keeps it.
#define PICK_MAX 4
#define PICK_HOLD_NS 300000000LL

// What a source's ray is over: nothing, the halo round the picture, or
// something it can act on
#define PICK_OFF  0
#define PICK_NEAR 1
#define PICK_ON   2

typedef struct {
    // A source has pointed, which one, and when its ray was last on something
    int has;
    int prev;
    int64_t onAt;
} PickState;

typedef struct {
    int count;
    // The order sources are tried in where nothing else decides, by index
    int order[PICK_MAX];
    // The rest by source
    int hover[PICK_MAX];
    int valid[PICK_MAX];
    int triggerOnScreen[PICK_MAX];
    int awake[PICK_MAX];
    int pressed[PICK_MAX];
} PickIn;

// The source that points this frame, or -1. A trigger held on the picture
// wins outright, so a drag is never stolen; then something under the ray
// beats the halo. Updates the state.
int pickHand(PickState* s, const PickIn* in, int64_t nowNs, int64_t holdNs);

// Whose ray runs free into the room while no source is on anything: the one
// that pointed last if it can still point, else the first in order that can
// and is awake or pressed (any, before anything has pointed). canPoint by
// source. -1 for none.
int pickFreeHand(const PickState* s, const PickIn* in, const int* canPoint);

// The beam's quad: returns its x, across the beam; the beam runs up its y
// (start to end) and its face, x cross y, is turned to the head. Facing the
// head exactly spins the quad round the beam as the head comes in line with
// it, so close to that line the face leans toward the head's up (the quad
// lying along the head's right) and never switches to the world's up, which
// is what flickered within about 9 degrees of sighting down the beam (H4).
// headRight is the head's +x in the same space, zero for unknown.
Vec3 beamBasis(Vec3 start, Vec3 end, Vec3 head, Vec3 headRight);

#endif
