// The pointer's ray as plain arithmetic, see xr_ray.h
#include "xr_ray.h"
#include "xr_gate.h"

#include <string.h>

int aimFallbackFor(int kind, int located, int jointsValid) {
    if (located) {
        return AIM_POSE;
    }
    return jointsValid && kind != PROFILE_CONTROLLER ? AIM_JOINTS : AIM_NONE;
}

int controllerPoseLive(int poseActive, unsigned flags) {
    const unsigned needed = GATE_POSITION_VALID | GATE_ORIENTATION_VALID
            | GATE_ORIENTATION_TRACKED;
    return poseActive && (flags & needed) == needed;
}

void poseHoldReset(PoseHold* h) {
    memset(h, 0, sizeof(*h));
    h->pose.orientation.w = 1.0f;
}

int poseHoldStep(PoseHold* h, int live, int64_t nowNs, int64_t holdNs) {
    if (live) {
        h->have = 1;
        h->liveAt = nowNs;
        return POSE_LIVE;
    }
    return h->have && nowNs - h->liveAt < holdNs ? POSE_HELD : POSE_GONE;
}

void poseHoldKeep(PoseHold* h, XrPosef drawn) {
    h->pose = drawn;
}

// Awake on its own clock or pressed: a source that may take the pointer
static int keen(const PickIn* in, int h) {
    return in->awake[h] || in->pressed[h];
}

static int choose(PickState* s, const PickIn* in, int h, int64_t nowNs) {
    s->has = 1;
    s->prev = h;
    if (in->hover[h] != PICK_OFF) {
        s->onAt = nowNs;
    }
    return h;
}

int pickHand(PickState* s, const PickIn* in, int64_t nowNs, int64_t holdNs) {
    int n = in->count < PICK_MAX ? in->count : PICK_MAX;
    for (int i = 0; i < n; i++) {
        int h = in->order[i];
        if (in->triggerOnScreen[h]) {
            return choose(s, in, h, nowNs);
        }
    }
    int prev = s->has && s->prev >= 0 && s->prev < n ? s->prev : -1;
    // The last one's ray slipped off everything a moment ago: a press is the
    // only thing that takes the pointer off it until that moment is over
    int grace = prev >= 0 && in->hover[prev] == PICK_OFF && in->valid[prev]
            && nowNs - s->onAt < holdNs;
    static const int passes[2] = { PICK_ON, PICK_NEAR };
    for (int p = 0; p < 2; p++) {
        int pass = passes[p];
        if (prev >= 0 && in->hover[prev] == pass && keen(in, prev)) {
            return choose(s, in, prev, nowNs);
        }
        for (int i = 0; i < n; i++) {
            int h = in->order[i];
            if (h == prev || in->hover[h] != pass) {
                continue;
            }
            // Before anything has pointed any source will do, as it always did
            if (prev < 0 || in->pressed[h] || (!grace && in->awake[h])) {
                return choose(s, in, h, nowNs);
            }
        }
        if (prev >= 0 && in->hover[prev] == pass) {
            return choose(s, in, prev, nowNs);
        }
    }
    return -1;
}

int pickFreeHand(const PickState* s, const PickIn* in, const int* canPoint) {
    int n = in->count < PICK_MAX ? in->count : PICK_MAX;
    if (s->has && s->prev >= 0 && s->prev < n && canPoint[s->prev]) {
        return s->prev;
    }
    for (int i = 0; i < n; i++) {
        int h = in->order[i];
        if (canPoint[h] && (!s->has || keen(in, h))) {
            return h;
        }
    }
    return -1;
}

static Vec3 vecScaled(Vec3 v, float k) {
    Vec3 r = { v.x * k, v.y * k, v.z * k };
    return r;
}

static Vec3 vecAdd(Vec3 a, Vec3 b) {
    Vec3 r = { a.x + b.x, a.y + b.y, a.z + b.z };
    return r;
}

static float vecLen(Vec3 v) {
    return sqrtf(vecDot(v, v));
}

// v with its part along the unit u taken out
static Vec3 squareTo(Vec3 v, Vec3 u) {
    return vecSub(v, vecScaled(u, vecDot(v, u)));
}

// How much of the head's up the face leans to where the head is in line with
// the beam, and the sine of the angle off that line where the lean is gone.
// Further out the face is turned to the head exactly, as it always was.
#define BEAM_LEAN 0.25f
#define BEAM_LEAN_END 0.5f

Vec3 beamBasis(Vec3 start, Vec3 end, Vec3 head, Vec3 headRight) {
    Vec3 y = vecNorm(vecSub(end, start));
    Vec3 right = headRight;
    if (vecLen(right) < 1e-6f) {
        right.x = 1.0f;
        right.y = 0.0f;
        right.z = 0.0f;
    }
    if (vecLen(y) < 1e-6f) {
        return vecNorm(right);
    }
    Vec3 mid = vecScaled(vecAdd(start, end), 0.5f);
    // Toward the head and square to the beam, as long as the sine of the
    // angle between the beam and the line to the head
    Vec3 face = squareTo(vecNorm(vecSub(head, mid)), y);
    float side = vecLen(face);

    // The face that goes with the quad lying along the head's right. None
    // where the beam itself runs along the head's right, which is never in
    // line with the head.
    Vec3 across = squareTo(right, y);
    Vec3 lean = { 0.0f, 0.0f, 0.0f };
    if (vecLen(across) > 1e-4f) {
        lean = vecCross(vecNorm(across), y);
    }
    float t = side / BEAM_LEAN_END;
    t = t > 1.0f ? 1.0f : t;
    float k = BEAM_LEAN * (1.0f - t * t * (3.0f - 2.0f * t));
    Vec3 z = vecAdd(face, vecScaled(lean, k));
    if (vecLen(z) < 1e-6f) {
        z = lean;
    }
    if (vecLen(z) < 1e-6f) {
        // Nothing to go by at all: any side square to the beam
        Vec3 axis = { 1.0f, 0.0f, 0.0f };
        if (fabsf(y.x) > 0.9f) {
            axis.x = 0.0f;
            axis.y = 1.0f;
        }
        z = vecCross(axis, y);
    }
    return vecNorm(vecCross(y, z));
}
