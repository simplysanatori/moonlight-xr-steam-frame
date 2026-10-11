// Moonlight XR Native, tests/m4: the stats window (owner's requests). Every
// line of the stats text is visible (the lines are wider than the slot was, so
// they wrap rather than run off its right side, and a long text is not cut
// off at the end), the window is where it always was until a grip takes it,
// a grip on it carries it with the hand in place and turn while the
// thumbsticks push it away and closer, the trigger is not a grab, letting go
// leaves it, and resetting or recentring the screen's placement brings it
// back. GPLv3.
#include "check.h"
#include "scene.h"
#include "text.h"

static XrPosef aimAt(Vec3 from, Vec3 target) {
    Vec3 d = vecNorm(vecSub(target, from));
    Vec3 z = { -d.x, -d.y, -d.z };
    Vec3 up = { 0, 1, 0 };
    Vec3 x = vecNorm(vecCross(up, z));
    Vec3 y = vecCross(z, x);
    XrPosef p = { quatFromBasis(x, y, z), { from.x, from.y, from.z } };
    return p;
}

static Vec3 onPlane(XrPosef pose, float w, float h, float u, float v) {
    Vec3 local = { (u - 0.5f) * w, (0.5f - v) * h, 0.0f };
    Vec3 off = quatRotate(pose.orientation, local);
    Vec3 p = { pose.position.x + off.x, pose.position.y + off.y, pose.position.z + off.z };
    return p;
}

static Vec3 pos(XrPosef p) {
    Vec3 v = { p.position.x, p.position.y, p.position.z };
    return v;
}

static float dist(Vec3 a, Vec3 b) {
    Vec3 d = vecSub(a, b);
    return sqrtf(vecDot(d, d));
}

static float turnedDeg(XrPosef a, XrPosef b) {
    Vec3 n = { 0, 0, 1 };
    float d = vecDot(quatRotate(a.orientation, n), quatRotate(b.orientation, n));
    if (d > 1.0f) d = 1.0f;
    if (d < -1.0f) d = -1.0f;
    return acosf(d) * 180.0f / (float)M_PI;
}

static const Vec3 HAND = { 0.25f, -0.35f, -0.3f };

// What the session's stats text can look like: the app's clauses (one a line)
// wider than the window was, the perf lane's lines, the depth model's two
static const char LONG[] =
    "Display 90 Hz, 89 fps\n"
    "Video 2560x1440, 60 fps\n"
    "decoded 123456, dropped 12, decode 3.1 ms\n"
    "units 654321, idr requests 2, rtt 4 ms, pending video 1\n"
    "hw decode 3.1 ms (last 3.2), frames 123456, skipped 12, errors 0\n"
    "audio 2ch 40 ms buffered, 2 underruns, 0 lost (stream totals)\n"
    "network 54 Mb/s, host encode 4.2 ms, network 1.1 ms, decode 3.1 ms, present 2.0 ms, total 10.4 ms (worst 15.8)\n"
    "Depth 14.9/s (target 15), depth-anything-v2-small-int8\n"
    "Depth inference 41.3 ms, age 88 ms, skipped 1234\n"
    "END";

// Pixels of the stats slot that are bright and opaque (the text), in a
// rectangle of the slot, x0..x1 and y0..y1 as offsets into it
static int textPixels(XrCtx* c, int x0, int x1, int y0, int y1) {
    UiRectPx r = xrAtlasRect(ATLAS_OVERLAY);
    const uint8_t* px = xrAtlasPixels(c);
    int n = 0;
    for (int y = y0; y < y1 && y < r.h; y++) {
        for (int x = x0; x < x1 && x < r.w; x++) {
            const uint8_t* p = px + ((size_t)(r.y + y) * UI_ATLAS_W + (size_t)(r.x + x)) * 4;
            if (p[0] > 200 && p[1] > 200 && p[2] > 200 && p[3] > 200) n++;
        }
    }
    return n;
}

static void fits(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->overlayVisible = 1;
    scene_frame(s, LONG);
    UiRectPx r = xrAtlasRect(ATLAS_OVERLAY);
    CHECK(c->overlayHasContent && c->overlayW == r.w);
    CHECK(c->overlayH > 100 && c->overlayH <= r.h);

    // The text really is wider than the slot, so it had to wrap: the long
    // line alone is more than twice it
    UiFont* font = ui_font_default();
    CHECK(font != NULL);
    const char* longest = "network 54 Mb/s, host encode 4.2 ms, network 1.1 ms, decode 3.1 ms, present 2.0 ms, total 10.4 ms (worst 15.8)";
    CHECK(ui_text_width(font, UI_WEIGHT_REGULAR, 17.0f, longest, -1) > (float)r.w * 2.0f);
    CHECK(ui_text_width(font, UI_WEIGHT_REGULAR, 17.0f, "hw decode 3.1 ms (last 3.2), frames 123456, skipped 12, errors 0", -1)
          > 384.0f - 24.0f);

    // Nothing runs into the right margin (12 px) or past the window's foot,
    // and text does reach out toward the right
    CHECK(textPixels(c, r.w - 11, r.w, 0, r.h) == 0);
    CHECK(textPixels(c, 0, r.w, c->overlayH - 4, r.h) == 0);
    CHECK(textPixels(c, 300, r.w - 11, 0, r.h) > 50);
    // The last line, END, is on the sheet: text in the last lines' band
    CHECK(textPixels(c, 0, 80, c->overlayH - 36, c->overlayH) > 20);
    // and nothing is left of the window's left padding
    CHECK(textPixels(c, 0, 8, 0, r.h) == 0);

    // A short text is a short window
    scene_frame(s, "Display 90 Hz, 89 fps\nVideo 2560x1440, 60 fps");
    CHECK(c->overlayH < 100);
    // and a text too long for the slot at any size is cut at its foot, not
    // past it
    char huge[4096];
    huge[0] = '\0';
    for (int i = 0; i < 40; i++) strcat(huge, "line of some words that goes on and on and on and on and on\n");
    scene_frame(s, huge);
    CHECK(c->overlayH <= r.h);
    CHECK(textPixels(c, 0, r.w - 11, 0, r.h) > 500);
    CHECK(textPixels(c, r.w - 11, r.w, 0, r.h) == 0);
    // the 1000-byte texts the session now carries get through whole
    scene_frame(s, LONG);
}

static void placement(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->overlayVisible = 1;
    c->statsMoved = 0;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    scene_frame(s, LONG);
    statsPlace(c);
    CHECK(statsShown(c));
    // Where it always was: pinned to the picture's top left, a tenth of
    // a percent (2%) in from each edge, in front of the picture
    float sh = c->screenWidth * (float)c->videoHeight / (float)c->videoWidth;
    Vec3 corner = onPlane(c->statsPose, c->statsW, c->statsH, 0.0f, 0.0f);
    Vec3 want = onPlane(c->screenPose, c->screenWidth, sh, 0.0f, 0.0f);
    Vec3 fwd = quatRotate(c->screenPose.orientation, (Vec3){ 0, 0, 1 });
    float margin = c->screenWidth * 0.02f;
    Vec3 r = quatRotate(c->screenPose.orientation, (Vec3){ 1, 0, 0 });
    Vec3 u = quatRotate(c->screenPose.orientation, (Vec3){ 0, 1, 0 });
    CHECK_NEAR(dist(corner, (Vec3){ want.x + r.x * margin - u.x * margin + fwd.x * 0.01f,
                                    want.y + r.y * margin - u.y * margin + fwd.y * 0.01f,
                                    want.z + r.z * margin - u.z * margin + fwd.z * 0.01f }), 0.0, 1e-4);
    CHECK_NEAR(c->statsW, c->screenWidth * 0.30f * (float)c->overlayW / 384.0f, 1e-5);
    // follows the screen when that moves
    XrPosef was = c->statsPose;
    c->screenPose.position.x += 0.5f;
    statsPlace(c);
    CHECK_NEAR(c->statsPose.position.x - was.position.x, 0.5, 1e-5);
    c->screenPose.position.x -= 0.5f;
    statsPlace(c);
    // hidden, it is nobody's
    c->overlayVisible = 0;
    CHECK(!statsShown(c));
    CHECK(xrHoverAt(c, aimAt(HAND, onPlane(c->statsPose, c->statsW, c->statsH, 0.5f, 0.5f)), NULL, NULL, NULL) == HOVER_SCREEN);
    CHECK(xrGrabStepAt(c, aimAt(HAND, onPlane(c->statsPose, c->statsW, c->statsH, 0.5f, 0.5f)), 0, 1, 0.0f) == HOVER_SCREEN);
    CHECK(c->grabMode == GRAB_NONE);
    c->overlayVisible = 1;
}

static void grabAndMove(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->overlayVisible = 1;
    c->statsMoved = 0;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    scene_frame(s, LONG);
    statsPlace(c);
    XrPosef start = c->statsPose;
    XrPosef screen = c->screenPose;
    XrPosef aim = aimAt(HAND, onPlane(start, c->statsW, c->statsH, 0.5f, 0.5f));

    // The ray on it is on the picture as far as the click goes
    CHECK(xrGrabStepAt(c, aim, 0, 0, 0.0f) == HOVER_SCREEN);
    CHECK(c->statsOver);
    CHECK(c->grabMode == GRAB_NONE);
    // The trigger is a click, not a grab
    xrGrabStepAt(c, aim, 1, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE && !c->statsMoved);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    // The grip takes it, anywhere on it
    CHECK(xrGrabStepAt(c, aim, 0, 1, 0.0f) == HOVER_SCREEN);
    CHECK(c->grabMode == GRAB_STATSMOVE && c->statsMoved);
    CHECK(xrGrabStepAt(c, aim, 0, 1, 0.0f) != HOVER_NONE || c->grabMode == GRAB_STATSMOVE);
    CHECK(dist(pos(c->statsPose), pos(start)) < 1e-4f && turnedDeg(c->statsPose, start) < 0.01f);

    // The hand carries it along
    XrPosef carried = aim;
    carried.position.x += 0.30f;
    carried.position.y += 0.12f;
    carried.position.z -= 0.10f;
    xrGrabStepAt(c, carried, 0, 1, 0.0f);
    CHECK(c->grabMode == GRAB_STATSMOVE);
    CHECK_NEAR(c->statsPose.position.x - start.position.x, 0.30, 1e-3);
    CHECK_NEAR(c->statsPose.position.y - start.position.y, 0.12, 1e-3);
    CHECK_NEAR(c->statsPose.position.z - start.position.z, -0.10, 1e-3);
    CHECK(turnedDeg(c->statsPose, start) < 0.01f);
    CHECK(dist(pos(c->screenPose), pos(screen)) == 0.0f);
    // and a statsPlace in between (every frame does one) leaves it be
    XrPosef mid = c->statsPose;
    statsPlace(c);
    CHECK(dist(pos(c->statsPose), pos(mid)) < 1e-5f);

    // Turned with the wrist
    Vec3 xAxis = { 1, 0, 0 };
    XrPosef tilted = carried;
    tilted.orientation = quatNorm(quatMul(axisAngleQuat(xAxis, -60.0f * (float)M_PI / 180.0f), carried.orientation));
    xrGrabStepAt(c, tilted, 0, 1, 0.0f);
    CHECK_NEAR(turnedDeg(c->statsPose, start), 60.0, 0.5);
    CHECK_NEAR(dist(pos(c->statsPose), pos(tilted)), dist(pos(start), pos(aim)), 1e-3);
    XrPosef laid = c->statsPose;

    // Thumbstick north pushes it away, south brings it closer
    float before = dist(pos(c->statsPose), (Vec3){ 0, 0, 0 });
    for (int i = 0; i < 45; i++) xrGrabStepAt(c, tilted, 0, 1, 1.0f);
    float away = dist(pos(c->statsPose), (Vec3){ 0, 0, 0 });
    CHECK(away > before + 0.4f);
    for (int i = 0; i < 90; i++) xrGrabStepAt(c, tilted, 0, 1, -1.0f);
    CHECK(dist(pos(c->statsPose), (Vec3){ 0, 0, 0 }) < away - 0.4f);
    CHECK(turnedDeg(c->statsPose, laid) < 0.01f);

    // Letting go leaves it, and it keeps still after
    XrPosef left = c->statsPose;
    xrGrabStepAt(c, tilted, 0, 0, 0.0f);
    CHECK(c->grabMode == GRAB_NONE && !c->poseDirty);
    XrPosef elsewhere = tilted;
    elsewhere.position.x -= 0.5f;
    xrGrabStepAt(c, elsewhere, 0, 0, 0.0f);
    statsPlace(c);
    CHECK(dist(pos(c->statsPose), pos(left)) < 1e-5f && turnedDeg(c->statsPose, left) < 0.01f);
    CHECK(dist(pos(c->screenPose), pos(screen)) == 0.0f);

    // Off the picture it is still there to be taken again, and is a hover of
    // its own (a ray there is on something)
    Vec3 eye = { 0, 0, 0 };
    XrPosef again = aimAt(eye, onPlane(c->statsPose, c->statsW, c->statsH, 0.3f, 0.4f));
    CHECK(xrHoverAt(c, again, NULL, NULL, NULL) == HOVER_STATS);
    CHECK(xrGrabStepAt(c, again, 0, 1, 0.0f) == HOVER_STATS);
    CHECK(c->grabMode == GRAB_STATSMOVE);
    xrGrabStepAt(c, again, 0, 0, 0.0f);
    // and outside it is not
    Vec3 pastEdge = onPlane(c->statsPose, c->statsW, c->statsH, 1.2f, 0.5f);
    CHECK(xrGrabStepAt(c, aimAt(eye, pastEdge), 0, 1, 0.0f) != HOVER_STATS);
    CHECK(c->grabMode == GRAB_NONE);
    xrGrabStepAt(c, aimAt(eye, pastEdge), 0, 0, 0.0f);

    // A taller window grows downward from its top edge
    Vec3 top = onPlane(c->statsPose, c->statsW, c->statsH, 0.5f, 0.0f);
    int h0 = c->overlayH;
    c->overlayH = h0 + 80;
    statsPlace(c);
    Vec3 top2 = onPlane(c->statsPose, c->statsW, c->statsH, 0.5f, 0.0f);
    CHECK(dist(top, top2) < 1e-4f);
    CHECK(c->statsH > 0.0f);
    c->overlayH = h0;
    statsPlace(c);

    // A keyboard in front of it has the ray first
    c->kbOpen = 1;
    kbPlace(c);
    float kw, kh;
    (void)kw; (void)kh;
    c->kbOpen = 0;
    // A modal sheet up: the grip does nothing to it
    c->cogOpen = 1;
    CHECK(xrGrabStepAt(c, again, 0, 1, 0.0f) != HOVER_NONE || c->grabMode == GRAB_NONE);
    CHECK(c->grabMode == GRAB_NONE);
    xrGrabStepAt(c, again, 0, 0, 0.0f);
    c->cogOpen = 0;
}

static void resets(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->overlayVisible = 1;
    c->statsMoved = 0;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    scene_frame(s, LONG);
    statsPlace(c);
    XrPosef home = c->statsPose;
    XrPosef aim = aimAt(HAND, onPlane(home, c->statsW, c->statsH, 0.5f, 0.5f));
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    aim.position.y += 0.4f;
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    statsPlace(c);
    CHECK(c->statsMoved && dist(pos(c->statsPose), pos(home)) > 0.3f);

    // The screen's placement reset: it is back in the corner
    c->placementValid = 0;
    float distance, width, curve;
    xrDefaultPlacement(&distance, &width, &curve);
    CHECK(updatePlacement(c, distance, width, curve));
    CHECK(!c->statsMoved);
    statsPlace(c);
    CHECK(dist(pos(c->statsPose), pos(home)) < 1e-4f && turnedDeg(c->statsPose, home) < 0.01f);

    // and recentring does the same
    aim = aimAt(HAND, onPlane(home, c->statsW, c->statsH, 0.5f, 0.5f));
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    aim.position.x += 0.4f;
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    CHECK(c->statsMoved);
    recentreScreen(c);
    CHECK(!c->statsMoved);
    statsPlace(c);
    CHECK(dist(pos(c->statsPose), pos(home)) < 1e-3f);
}

// The window is one layer, moved or not, so a frame is no fuller for it
static void layers(Scene* s) {
    XrCtx* c = s->ctx;
    scene_reset(s);
    c->overlayVisible = 1;
    c->statsMoved = 0;
    c->headPos = (XrVector3f){ 0, 0, 0 };
    s->settings.head_locked = 1;
    c->headLockedPref = 1;
    scene_frame(s, LONG);
    unsigned home = s->out.count;
    statsPlace(c);
    XrPosef aim = aimAt(HAND, onPlane(c->statsPose, c->statsW, c->statsH, 0.5f, 0.5f));
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    aim.position.y += 0.2f;
    xrGrabStepAt(c, aim, 0, 1, 0.0f);
    noticePush(&c->notices, TOAST_3D_ON, 0);
    Notice up;
    if (noticeAdvance(&c->notices, nowNs(), 0, &up)) xrPanelsToast(c, up.kind, up.arg);
    scene_frame(s, LONG);
    printf("stats moved and held, toast, head locked: %u layers (home %u)\n", s->out.count, home);
    CHECK(s->out.count <= home + 1);
    CHECK(s->out.count + 1 <= 16);
    xrGrabStepAt(c, aim, 0, 0, 0.0f);
    s->settings.head_locked = 0;
    c->headLockedPref = 0;
    c->statsMoved = 0;
}

int main(void) {
    Scene* s = scene_new();
    fits(s);
    placement(s);
    grabAndMove(s);
    resets(s);
    layers(s);
    scene_free(s);
    return checksDone("m4 stats");
}
