// The pointer's ray, checked a frame at a time against synthetic sequences of
// the four ways the Frame's rays snapped vertical (H1 to H4, see
// docs/dev/ray-debug.md): what a ray is built from, a pose held across a short
// loss of tracking, which source points, and the beam's basis
#include "check.h"
#include "xr_controller.h"
#include "xr_gate.h"
#include "xr_ray.h"
#include "xr_raylog.h"

#include <string.h>

// One frame at 90 Hz, in whole nanoseconds as the recorder writes them
#define FRAME_NS 11111111LL
#define MS 1000000LL

#define LEFT 0
#define RIGHT 1
#define GAZE 2

static const unsigned VALID = GATE_POSITION_VALID | GATE_ORIENTATION_VALID;
static const unsigned ALL = GATE_POSITION_VALID | GATE_ORIENTATION_VALID
        | GATE_ORIENTATION_TRACKED | GATE_POSITION_TRACKED;

// The rule this replaced: joints whenever the aim was not placed
static int oldFallback(int located, int jointsValid) {
    return located ? AIM_POSE : (jointsValid ? AIM_JOINTS : AIM_NONE);
}

// H1: a controller hand whose aim the runtime dropped, with SteamVR's joints
// made up from that controller still there
static void testFallback(void) {
    // The sequence: aim placed, then 20 frames not placed, joints there all along
    int oldJoints = 0, newJoints = 0;
    for (int i = 0; i < 40; i++) {
        int located = i < 10 || i >= 30;
        if (oldFallback(located, 1) == AIM_JOINTS) {
            oldJoints++;
        }
        if (aimFallbackFor(PROFILE_CONTROLLER, located, 1) == AIM_JOINTS) {
            newJoints++;
        }
    }
    // The old rule drew the shoulder to knuckle ray for the whole gap
    CHECK(oldJoints == 20);
    CHECK(newJoints == 0);

    CHECK(aimFallbackFor(PROFILE_CONTROLLER, 1, 1) == AIM_POSE);
    CHECK(aimFallbackFor(PROFILE_CONTROLLER, 0, 1) == AIM_NONE);
    CHECK(aimFallbackFor(PROFILE_CONTROLLER, 0, 0) == AIM_NONE);
    // Tracked hands, and hands on a runtime that refuses the hand profile,
    // still point with the joints
    CHECK(aimFallbackFor(PROFILE_HANDS, 0, 1) == AIM_JOINTS);
    CHECK(aimFallbackFor(PROFILE_NONE, 0, 1) == AIM_JOINTS);
    CHECK(aimFallbackFor(PROFILE_HANDS, 1, 1) == AIM_POSE);
    CHECK(aimFallbackFor(PROFILE_HANDS, 0, 0) == AIM_NONE);
}

// The model and the ray go by one rule
static void testSameRule(void) {
    for (unsigned flags = 0; flags < 16; flags++) {
        for (int active = 0; active < 2; active++) {
            CHECK(controllerModelShown(1, 0, PROFILE_CONTROLLER, active, flags)
                  == controllerPoseLive(active, flags));
        }
    }
    CHECK(controllerPoseLive(1, ALL));
    // Out of the cameras, on its gyro
    CHECK(controllerPoseLive(1, VALID | GATE_ORIENTATION_TRACKED));
    // Valid but neither tracked: SteamVR's extrapolation
    CHECK(!controllerPoseLive(1, VALID));
    CHECK(!controllerPoseLive(1, VALID | GATE_POSITION_TRACKED));
    CHECK(!controllerPoseLive(0, ALL));
}

// H3: VALID but untracked poses are held, then hidden
static void testPoseHold(void) {
    PoseHold h;
    poseHoldReset(&h);
    int64_t t = 1000 * MS;
    // Nothing live yet: nothing to hold
    CHECK(poseHoldStep(&h, 0, t, POSE_HOLD_NS) == POSE_GONE);

    XrPosef aimed = { { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.1f, -0.3f, -0.3f } };
    for (int i = 0; i < 10; i++) {
        t += FRAME_NS;
        CHECK(poseHoldStep(&h, controllerPoseLive(1, ALL), t, POSE_HOLD_NS) == POSE_LIVE);
        poseHoldKeep(&h, aimed);
    }
    int64_t lastLive = t;
    // The runtime's extrapolation, valid but untracked, for half a second
    int held = 0, gone = 0, firstGone = -1;
    for (int i = 1; i <= 45; i++) {
        t = lastLive + i * FRAME_NS;
        int s = poseHoldStep(&h, controllerPoseLive(1, VALID), t, POSE_HOLD_NS);
        if (s == POSE_HELD) {
            held++;
            // What is drawn is the last live pose, not the extrapolation
            CHECK_NEAR(h.pose.position.x, 0.1, 1e-6);
            CHECK(firstGone < 0);
        }
        else if (s == POSE_GONE) {
            gone++;
            if (firstGone < 0) {
                firstGone = i;
            }
        }
    }
    // 27 frames come to 299.99 ms, the 28th is past the hold
    CHECK(held == 27);
    CHECK(firstGone == 28);
    CHECK(gone == 45 - 27);
    // Tracking back: live at once
    t += FRAME_NS;
    CHECK(poseHoldStep(&h, 1, t, POSE_HOLD_NS) == POSE_LIVE);
    // A short blip is held through and never hidden
    for (int i = 1; i <= 10; i++) {
        CHECK(poseHoldStep(&h, 0, t + i * FRAME_NS, POSE_HOLD_NS) == POSE_HELD);
    }
}

// The rule this replaced: the trigger on the screen, then the first source in
// order on something, then on the halo; and the free beam from the right if
// it could point, else the left
static int oldPick(const PickIn* in) {
    for (int i = 0; i < in->count; i++) {
        int h = in->order[i];
        if (in->triggerOnScreen[h]) {
            return h;
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < in->count; i++) {
            int h = in->order[i];
            if (in->hover[h] == PICK_OFF || (pass == 0 && in->hover[h] == PICK_NEAR)) {
                continue;
            }
            return h;
        }
    }
    return -1;
}

static int oldFree(const int* canPoint) {
    return canPoint[RIGHT] ? RIGHT : (canPoint[LEFT] ? LEFT : -1);
}

static void pickInit(PickIn* in) {
    memset(in, 0, sizeof(*in));
    in->count = 3;
    in->order[0] = RIGHT;
    in->order[1] = LEFT;
    in->order[2] = GAZE;
    in->valid[LEFT] = 1;
    in->valid[RIGHT] = 1;
}

// H2: the left controller aiming, the right lying in a lap, asleep, its ray
// across the picture or pointing up
static void testPick(void) {
    PickState s;
    memset(&s, 0, sizeof(s));
    PickIn in;
    pickInit(&in);
    int canPoint[3] = { 1, 1, 0 };
    int64_t t = 1000 * MS;

    // The left aims at the picture, awake; the right's ray points up at nothing
    in.awake[LEFT] = 1;
    in.hover[LEFT] = PICK_ON;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == LEFT);

    // The lap controller's ray comes to lie across the bar: the old rule gave
    // it the pointer outright, being first in order
    in.hover[RIGHT] = PICK_ON;
    for (int i = 0; i < 90; i++) {
        t += FRAME_NS;
        CHECK(oldPick(&in) == RIGHT);
        CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == LEFT);
    }

    // The aiming ray slips past the edge of the picture for a second
    in.hover[LEFT] = PICK_OFF;
    for (int i = 0; i < 90; i++) {
        t += FRAME_NS;
        CHECK(oldPick(&in) == RIGHT);
        CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == -1);
        // and the beam runs on from the left, not out of the lap
        CHECK(pickFreeHand(&s, &in, canPoint) == LEFT);
    }
    // The old free beam came out of the lap whenever nothing hovered at all
    in.hover[RIGHT] = PICK_OFF;
    CHECK(oldFree(canPoint) == RIGHT);
    CHECK(pickFreeHand(&s, &in, canPoint) == LEFT);
    in.hover[RIGHT] = PICK_ON;

    // Back on the picture: the left again
    in.hover[LEFT] = PICK_ON;
    t += FRAME_NS;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == LEFT);

    // Within the hold, the other being awake is not enough
    in.hover[LEFT] = PICK_OFF;
    in.awake[RIGHT] = 1;
    int64_t lost = t;
    for (int i = 1; i <= 27; i++) {
        CHECK(pickHand(&s, &in, lost + i * FRAME_NS, PICK_HOLD_NS) == -1);
    }
    // After it, an awake controller in the other hand takes over
    CHECK(pickHand(&s, &in, lost + 28 * FRAME_NS, PICK_HOLD_NS) == RIGHT);
    t = lost + 28 * FRAME_NS;

    // A button down takes it at once, hold or not, asleep or not
    in.awake[RIGHT] = 0;
    in.hover[LEFT] = PICK_ON;
    t += FRAME_NS;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == LEFT);
    in.hover[LEFT] = PICK_OFF;
    in.pressed[RIGHT] = 1;
    t += FRAME_NS;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == RIGHT);
    in.pressed[RIGHT] = 0;

    // A trigger held on the picture wins outright, as before
    in.hover[LEFT] = PICK_ON;
    in.triggerOnScreen[LEFT] = 1;
    t += FRAME_NS;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == LEFT);
    in.triggerOnScreen[LEFT] = 0;

    // Something under the ray still beats the halo: an awake hand on the
    // picture takes it from one only near it
    memset(&s, 0, sizeof(s));
    pickInit(&in);
    in.awake[LEFT] = in.awake[RIGHT] = 1;
    in.hover[RIGHT] = PICK_ON;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == RIGHT);
    in.hover[RIGHT] = PICK_NEAR;
    in.hover[LEFT] = PICK_ON;
    CHECK(pickHand(&s, &in, t + FRAME_NS, PICK_HOLD_NS) == LEFT);

    // Before anything has pointed any source will do, in the old order
    memset(&s, 0, sizeof(s));
    pickInit(&in);
    in.hover[LEFT] = in.hover[RIGHT] = PICK_ON;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == RIGHT);
    // and an awake left hand then takes it off the sleeping right one
    in.awake[LEFT] = 1;
    CHECK(pickHand(&s, &in, t + FRAME_NS, PICK_HOLD_NS) == LEFT);

    // The last one gone (its pose lost) gives no hold: an awake other takes
    // over at once, a sleeping one never does, nor draws the free beam
    memset(&s, 0, sizeof(s));
    pickInit(&in);
    in.awake[LEFT] = 1;
    in.hover[LEFT] = PICK_ON;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == LEFT);
    in.valid[LEFT] = 0;
    in.hover[LEFT] = PICK_OFF;
    in.hover[RIGHT] = PICK_ON;
    CHECK(pickHand(&s, &in, t + FRAME_NS, PICK_HOLD_NS) == -1);
    canPoint[LEFT] = 0;
    CHECK(pickFreeHand(&s, &in, canPoint) == -1);
    in.awake[RIGHT] = 1;
    CHECK(pickHand(&s, &in, t + 2 * FRAME_NS, PICK_HOLD_NS) == RIGHT);
    CHECK(pickFreeHand(&s, &in, canPoint) == RIGHT);

    // Gaze, last in order, keeps the pointer like any other source
    memset(&s, 0, sizeof(s));
    pickInit(&in);
    in.valid[GAZE] = 1;
    in.awake[GAZE] = 1;
    in.hover[GAZE] = PICK_ON;
    CHECK(pickHand(&s, &in, t, PICK_HOLD_NS) == GAZE);
    in.hover[RIGHT] = PICK_NEAR;
    CHECK(pickHand(&s, &in, t + FRAME_NS, PICK_HOLD_NS) == GAZE);
}

static float angleDeg(Vec3 a, Vec3 b) {
    float d = vecDot(a, b);
    d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
    return acosf(d) * 180.0f / (float)M_PI;
}

// The basis the beam had before: square to the line to the head, and the
// world's up within about 9 degrees of it
static Vec3 oldBasis(Vec3 start, Vec3 end, Vec3 head) {
    Vec3 y = vecNorm(vecSub(end, start));
    Vec3 mid = { (start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f, (start.z + end.z) * 0.5f };
    Vec3 x = vecCross(y, vecNorm(vecSub(head, mid)));
    if (sqrtf(vecDot(x, x)) < 0.15f) {
        Vec3 up = { 0.0f, 1.0f, 0.0f };
        x = vecCross(y, up);
    }
    return vecNorm(x);
}

// H4: no flip of the quad while the head sights down the beam
static void testBeamBasis(void) {
    Vec3 start = { 0.2f, -0.3f, -0.25f };
    Vec3 end = { 0.2f, -0.3f, -4.25f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };

    // The head swept sideways through the line of the beam, behind its start
    float worstNew = 0.0f, worstOld = 0.0f;
    Vec3 prevNew = { 0, 0, 0 }, prevOld = { 0, 0, 0 };
    for (int i = -200; i <= 200; i++) {
        Vec3 head = { 0.2f + i * 0.002f, -0.3f, 0.05f };
        Vec3 xNew = beamBasis(start, end, head, right);
        Vec3 xOld = oldBasis(start, end, head);
        CHECK_NEAR(sqrtf(vecDot(xNew, xNew)), 1.0, 1e-4);
        // Square to the beam always
        CHECK_NEAR(xNew.z, 0.0, 1e-4);
        if (i > -200) {
            float a = angleDeg(xNew, prevNew);
            float b = angleDeg(xOld, prevOld);
            worstNew = a > worstNew ? a : worstNew;
            worstOld = b > worstOld ? b : worstOld;
        }
        prevNew = xNew;
        prevOld = xOld;
    }
    // The old one turned a quarter at the threshold, and back
    CHECK(worstOld > 80.0f);
    CHECK(worstNew < 2.0f);

    // From above, the usual way a hand is held below the eyes, down through
    // the line: no step at all, and the quad lies along the head's right
    worstNew = 0.0f;
    for (int i = 0; i <= 300; i++) {
        Vec3 head = { 0.2f, -0.3f + 0.6f - i * 0.002f, 0.05f };
        Vec3 x = beamBasis(start, end, head, right);
        if (i > 0) {
            float a = angleDeg(x, prevNew);
            worstNew = a > worstNew ? a : worstNew;
        }
        prevNew = x;
    }
    CHECK(worstNew < 2.0f);
    CHECK(fabsf(prevNew.x) > 0.99f);

    // Well off the line it faces the head exactly, as before
    // (a beam across the view, the head well off its line)
    Vec3 head = { 0.0f, 0.0f, 0.0f };
    Vec3 from = { -1.0f, -0.3f, -2.0f };
    Vec3 aside = { 1.0f, 0.2f, -2.5f };
    Vec3 xNew = beamBasis(from, aside, head, right);
    Vec3 xOld = oldBasis(from, aside, head);
    CHECK(angleDeg(xNew, xOld) < 0.1f);
    // and its face, x cross y, is toward the head
    Vec3 y = vecNorm(vecSub(aside, from));
    Vec3 z = vecCross(xNew, y);
    Vec3 mid = { (from.x + aside.x) * 0.5f, (from.y + aside.y) * 0.5f, (from.z + aside.z) * 0.5f };
    CHECK(vecDot(z, vecSub(head, mid)) > 0.0f);

    // A beam of no length and an unknown head's right still give a unit x
    Vec3 none = { 0.0f, 0.0f, 0.0f };
    Vec3 x0 = beamBasis(start, start, head, none);
    CHECK_NEAR(sqrtf(vecDot(x0, x0)), 1.0, 1e-4);
    Vec3 x1 = beamBasis(start, end, start, none);
    CHECK_NEAR(sqrtf(vecDot(x1, x1)), 1.0, 1e-4);
}

// Native (lane beam-streak): how far p is in front of the head along forward
static float aheadOf(Vec3 p, Vec3 head, Vec3 forward) {
    return vecDot(vecSub(p, head), vecNorm(forward));
}

// Native (lane beam-streak): the beam is cut off in front of the eyes. A quad that reaches the
// eye plane is projected through the eye and shows as a white streak across the whole view.
static void testBeamClip(void) {
    Vec3 head = { 0.0f, 0.0f, 0.0f };
    Vec3 fwd = { 0.0f, 0.0f, -1.0f };
    Vec3 s, e;

    // A normal beam, from a controller at the chest down to the keyboard: untouched
    Vec3 hand = { 0.25f, -0.35f, -0.3f }, key = { 0.1f, -0.6f, -1.6f };
    CHECK(beamClipAhead(hand, key, head, fwd, BEAM_CLIP_AHEAD_M, &s, &e) == BEAM_CLIP_NONE);
    CHECK(s.x == hand.x && s.y == hand.y && s.z == hand.z && e.x == key.x && e.y == key.y && e.z == key.z);
    // A controller held at the face, its tip 8 cm in front of the eyes, pointing away: untouched
    Vec3 face = { 0.04f, -0.05f, -0.08f }, far = { 0.0f, 0.1f, -2.5f };
    CHECK(beamClipAhead(face, far, head, fwd, BEAM_CLIP_AHEAD_M, &s, &e) == BEAM_CLIP_NONE);
    // and the same pointing across the view, square to the head's forward
    Vec3 across = { 1.5f, -0.05f, -0.08f };
    CHECK(beamClipAhead(face, across, head, fwd, BEAM_CLIP_AHEAD_M, &s, &e) == BEAM_CLIP_NONE);

    // The glitch: the start at the local origin, 0.3 m under and 0.8 m behind a head looking down
    // -z (and turned a little), the end on the keyboard in front
    Vec3 head2 = { 0.05f, 0.3f, -0.8f };
    Vec3 fwd2 = vecNorm((Vec3){ 0.2f, -0.3f, -1.0f });
    Vec3 origin = { 0.0f, 0.0f, 0.0f }, kb = { 0.15f, -0.2f, -1.9f };
    int clip = beamClipAhead(origin, kb, head2, fwd2, BEAM_CLIP_AHEAD_M, &s, &e);
    CHECK(clip == BEAM_CLIP_START);
    // the new start on the plane 5 cm in front, on the old line; the end where it was
    CHECK_NEAR(aheadOf(s, head2, fwd2), BEAM_CLIP_AHEAD_M, 1e-4);
    Vec3 a = vecNorm(vecSub(kb, origin)), b = vecNorm(vecSub(kb, s));
    CHECK_NEAR(vecDot(a, b), 1.0, 1e-5);
    CHECK(e.x == kb.x && e.y == kb.y && e.z == kb.z);
    // Turned round: the end behind the head is cut, the start kept
    clip = beamClipAhead(kb, origin, head2, fwd2, BEAM_CLIP_AHEAD_M, &s, &e);
    CHECK(clip == BEAM_CLIP_END);
    CHECK_NEAR(aheadOf(e, head2, fwd2), BEAM_CLIP_AHEAD_M, 1e-4);
    CHECK(s.x == kb.x && s.y == kb.y && s.z == kb.z);
    // Both ends behind the eyes, or just in front of them inside the 5 cm: nothing to draw
    Vec3 behind1 = { 0.1f, -0.2f, 0.3f }, behind2 = { -0.3f, 0.1f, 0.6f };
    CHECK(beamClipAhead(behind1, behind2, head, fwd, BEAM_CLIP_AHEAD_M, &s, &e) == BEAM_CLIP_GONE);
    Vec3 near1 = { 0.02f, -0.01f, -0.03f }, near2 = { -0.02f, 0.01f, -0.04f };
    CHECK(beamClipAhead(near1, near2, head, fwd, BEAM_CLIP_AHEAD_M, &s, &e) == BEAM_CLIP_GONE);
    // Not finite: gone. A head that is not known yet (no forward): nothing is cut
    Vec3 bad = { NAN, 0.0f, -1.0f };
    CHECK(beamClipAhead(bad, key, head, fwd, BEAM_CLIP_AHEAD_M, &s, &e) == BEAM_CLIP_GONE);
    Vec3 none = { 0.0f, 0.0f, 0.0f };
    CHECK(beamClipAhead(origin, kb, head, none, BEAM_CLIP_AHEAD_M, &s, &e) == BEAM_CLIP_NONE);

    // Swept: starts and ends all round the head, a forward that turns. What is drawn never comes
    // nearer the eyes than the plane, lies on the beam as it was, and is the beam whole whenever
    // both ends were in front
    unsigned seed = 12345u;
    int cut = 0, whole = 0, gone = 0;
    for (int i = 0; i < 20000; i++) {
        float r[9];
        for (int k = 0; k < 9; k++) {
            seed = seed * 1664525u + 1013904223u;
            r[k] = (float)(seed >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
        }
        Vec3 f = vecNorm((Vec3){ r[0] * 0.5f, r[1] * 0.5f, -1.0f });
        Vec3 st = { r[2] * 1.2f, r[3] * 1.2f, r[4] * 1.5f };
        Vec3 en = { r[5] * 2.0f, r[6] * 2.0f, r[7] * 3.0f };
        clip = beamClipAhead(st, en, head, f, BEAM_CLIP_AHEAD_M, &s, &e);
        float ds = aheadOf(st, head, f), de = aheadOf(en, head, f);
        if (ds >= BEAM_CLIP_AHEAD_M && de >= BEAM_CLIP_AHEAD_M) {
            CHECK(clip == BEAM_CLIP_NONE);
            whole++;
            continue;
        }
        if (clip & BEAM_CLIP_GONE) {
            CHECK(ds < BEAM_CLIP_AHEAD_M && de < BEAM_CLIP_AHEAD_M);
            gone++;
            continue;
        }
        cut++;
        CHECK(aheadOf(s, head, f) >= BEAM_CLIP_AHEAD_M - 1e-4f);
        CHECK(aheadOf(e, head, f) >= BEAM_CLIP_AHEAD_M - 1e-4f);
        // on the old line: the cross product of the two directions vanishes
        Vec3 d0 = vecSub(en, st), d1 = vecSub(e, s);
        Vec3 off = vecCross(d0, vecSub(s, st));
        CHECK(sqrtf(vecDot(off, off)) < 1e-3f * (1.0f + sqrtf(vecDot(d0, d0))));
        CHECK(vecDot(d0, d1) >= 0.0f);
    }
    CHECK(cut > 1000 && whole > 1000 && gone > 1000);
}

// The recorder's rows read back as they were written
static void testRayLog(void) {
    char header[1024];
    int n = rayLogHeader(header, sizeof(header));
    CHECK(n > 0 && header[0] == 't');
    RayLogRow back;
    CHECK(!rayLogParse(header, &back));
    CHECK(!rayLogParse("# a comment", &back));
    CHECK(!rayLogParse("", &back));

    RayLogRow r;
    memset(&r, 0, sizeof(r));
    r.t = 1234567890123LL;
    r.toHead = 1;
    r.headFlags = 0xf;
    r.head.orientation.w = 1.0f;
    r.head.position.y = 1.6f;
    r.hand[1].kind = PROFILE_CONTROLLER;
    r.hand[1].ok = 1;
    r.hand[1].flags = 0x3;
    r.hand[1].active = 1;
    r.hand[1].pose.position.x = 0.25f;
    r.hand[1].pose.orientation.x = -0.5f;
    r.hand[1].pose.orientation.w = 0.8660f;
    r.hand[1].joints = 1;
    r.hand[1].knuckle.z = -0.3f;
    r.hand[1].trigger = 0.75f;
    r.hand[1].stick.y = -1.0f;
    r.hand[1].buttons = RAYLOG_BTN_RIGHT_CLICK | RAYLOG_BTN_STICK_CLICK;
    r.hand[1].src = AIM_HELD;
    r.hand[1].awake = 1;
    r.pointer = 1;
    r.chosen = -1;
    r.ray = 1;
    r.hover = 6;
    char line[1024];
    CHECK(rayLogFormat(&r, line, sizeof(line)) > 0);
    CHECK(rayLogParse(line, &back));
    CHECK(back.t == r.t);
    CHECK(back.toHead == 1 && back.headFlags == 0xf);
    CHECK_NEAR(back.head.position.y, 1.6, 1e-4);
    CHECK(back.hand[1].kind == PROFILE_CONTROLLER && back.hand[1].ok && back.hand[1].active);
    CHECK(back.hand[1].flags == 0x3);
    CHECK_NEAR(back.hand[1].pose.position.x, 0.25, 1e-4);
    CHECK_NEAR(back.hand[1].pose.orientation.x, -0.5, 1e-4);
    CHECK_NEAR(back.hand[1].knuckle.z, -0.3, 1e-4);
    CHECK_NEAR(back.hand[1].trigger, 0.75, 1e-3);
    CHECK_NEAR(back.hand[1].stick.y, -1.0, 1e-3);
    CHECK(back.hand[1].buttons == (RAYLOG_BTN_RIGHT_CLICK | RAYLOG_BTN_STICK_CLICK));
    CHECK(back.hand[1].src == AIM_HELD && back.hand[1].awake == 1);
    CHECK(back.hand[0].kind == 0 && back.hand[0].ok == 0);
    CHECK(back.pointer == 1 && back.chosen == -1 && back.ray == 1 && back.hover == 6);
    // A row cut short is not a row
    line[strlen(line) / 2] = '\0';
    CHECK(!rayLogParse(line, &back));
    // and one too small a buffer is not written at all
    CHECK(rayLogFormat(&r, line, 40) == 0);
}

int main(void) {
    testRayLog();
    testFallback();
    testSameRule();
    testPoseHold();
    testPick();
    testBeamBasis();
    testBeamClip();
    return checksDone("xr_ray");
}
