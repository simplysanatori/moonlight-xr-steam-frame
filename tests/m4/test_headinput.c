// Moonlight XR Native, tests/m4: head and pointer input as the input pass wires it (lane
// wave4-headinput). The arithmetic is tested in tests/xrcore; this is the glue, driven through the
// seams on the context. Head-look (PLAN 4.4): config.ini to the context, the head's turn to
// relative mouse motion in pointer mode with the screen head locked or a room up, at its own pixels
// a degree, paused by a free grip or app:lookhold as bindings.ini leaves them, and head aim's own
// behaviour unchanged where both could act. Opentrack (PLAN 4.5): config.ini to the context, and the
// head pose out of the input pass to a UDP socket on the loopback standing in for opentrack, only
// while on and streaming with the head placed. GPLv3.
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <math.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "check.h"
#include "opentrack.h"
#include "scene.h"

#define FAKE(n) ((uintptr_t)(n))
#define VIEW ((XrSpace)FAKE(0x303))
#define LOCAL ((XrSpace)FAKE(0x304))
#define PI_F 3.14159265358979f
#define FRAME_NS 11111111LL

static char g_dir[64];
static char g_path[128];

static void writeFile(const char* text) {
    FILE* f = fopen(g_path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

// ---- the fake head --------------------------------------------------------------------------------------

static float g_yawDeg;
static float g_pitchDeg;
static int g_headTracked = 1;

static XrResult headLocate(XrCtx* ctx, XrSpace space, XrSpace base, XrTime time, XrSpaceLocation* out) {
    (void)ctx; (void)base; (void)time;
    if (space != VIEW) {
        return XR_ERROR_RUNTIME_FAILURE;
    }
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    out->locationFlags = g_headTracked ? (XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
                                          | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT
                                          | XR_SPACE_LOCATION_POSITION_VALID_BIT
                                          | XR_SPACE_LOCATION_POSITION_TRACKED_BIT) : 0;
    out->pose.orientation = quatNorm(quatMul(axisAngleQuat(up, g_yawDeg * PI_F / 180.0f),
                                             axisAngleQuat(right, g_pitchDeg * PI_F / 180.0f)));
    out->pose.position = (XrVector3f){ 0.0f, 1.6f, 0.0f };
    return XR_SUCCESS;
}

static void headSetUp(XrCtx* c) {
    c->viewSpace = VIEW;
    c->localSpace = LOCAL;
    c->locateSpace = headLocate;
    c->sessionRunning = 1;
    c->sessionState = XR_SESSION_STATE_FOCUSED;
    c->splash.phase = SPLASH_GONE;
    c->placementValid = 1;
    c->padMode = 0;
    c->roomOverride = -1;
    c->roomStyle = 0;
    c->predictedDisplayTime = 1000000000LL;
    c->headAimDeadZone = 0;
    c->headLookHold = 0;
    xrSetHeadAim(c, 0, HEAD_AIM_SENSITIVITY_DEFAULT, 0);
    xrSetHeadLook(c, 0, HEAD_AIM_SENSITIVITY_DEFAULT);
    headAimReset(&c->headAim);
    g_yawDeg = 0.0f;
    g_pitchDeg = 0.0f;
    g_headTracked = 1;
}

static float g_out[IN_SLOTS];

// One head frame; returns what went to the host's mouse in x and y
static void headFrame(XrCtx* c, int headLocked, int* dx, int* dy) {
    memset(g_out, 0, sizeof(g_out));
    c->predictedDisplayTime += FRAME_NS;
    xrHeadMouseFrame(c, headLocked, g_out);
    *dx = (int)g_out[IN_MOUSE_DX];
    *dy = (int)g_out[IN_MOUSE_DY];
}

// The head turning from where it is by yawDeg (positive left) over 20 frames; what was sent
static void turnBy(XrCtx* c, int headLocked, float yawDeg, float pitchDeg, int* sx, int* sy) {
    int dx, dy;
    *sx = *sy = 0;
    headFrame(c, headLocked, &dx, &dy); // seeds after anything that reset it
    *sx += dx;
    *sy += dy;
    float y0 = g_yawDeg, p0 = g_pitchDeg;
    for (int i = 1; i <= 20; i++) {
        g_yawDeg = y0 + yawDeg * (float)i / 20.0f;
        g_pitchDeg = p0 + pitchDeg * (float)i / 20.0f;
        headFrame(c, headLocked, &dx, &dy);
        *sx += dx;
        *sy += dy;
    }
}

// ---- head-look (PLAN 4.4) ---------------------------------------------------------------------------------

static void headLookSettings(Scene* s) {
    XrCtx* c = s->ctx;
    Settings st;
    settings_defaults(&st);
    xrApplySettings(c, &st);
    CHECK(c->headLookOn == 0 && c->headLookPxDeg == HEAD_AIM_SENSITIVITY_DEFAULT);
    st.head_look = 1;
    st.head_look_px_deg = 12;
    xrApplySettings(c, &st);
    CHECK(c->headLookOn == 1 && c->headLookPxDeg == 12);
    // out of the lane is held to it
    xrSetHeadLook(c, 1, 99);
    CHECK(c->headLookPxDeg == HEAD_AIM_SENSITIVITY_MAX);
    xrSetHeadLook(c, 0, 8);
}

static void headLookTurns(Scene* s) {
    XrCtx* c = s->ctx;
    headSetUp(c);
    int sx, sy;
    // off (the default): a head locked screen sends nothing
    turnBy(c, 1, -10.0f, 0.0f, &sx, &sy);
    CHECK(sx == 0 && sy == 0 && g_out[IN_HEAD_AIM] == 0.0f);

    // on, head locked, pointer mode: 10 degrees to the right at 8 px a degree is 80 px right
    xrSetHeadLook(c, 1, 8);
    turnBy(c, 1, -10.0f, 0.0f, &sx, &sy);
    CHECK(abs(sx - 80) <= 1 && sy == 0);
    CHECK(g_out[IN_HEAD_AIM] == 1.0f && c->headAimActive && c->headMouseWho == HEAD_MOUSE_LOOK);
    // looking up 5 degrees is 40 px up the screen
    turnBy(c, 1, 0.0f, 5.0f, &sx, &sy);
    CHECK(abs(sy + 40) <= 1 && abs(sx) <= 1);
    g_pitchDeg = 0.0f;
    // its own pixels a degree
    xrSetHeadLook(c, 1, 12);
    turnBy(c, 1, 10.0f, 0.0f, &sx, &sy);
    CHECK(abs(sx + 120) <= 1);

    // a screen placed in the world without a room: nothing, and the ray goes back to placing the cursor
    turnBy(c, 0, -10.0f, 0.0f, &sx, &sy);
    CHECK(sx == 0 && !c->headAimActive && c->headMouseWho == HEAD_MOUSE_OFF);
    // a room up: on again, head locked or not
    c->roomOverride = ROOM_STYLE_THEATER;
    turnBy(c, 0, -10.0f, 0.0f, &sx, &sy);
    CHECK(abs(sx - 120) <= 1 && c->headAimActive);
    // gamepad mode: off
    c->padMode = 1;
    turnBy(c, 0, -10.0f, 0.0f, &sx, &sy);
    CHECK(sx == 0 && !c->headAimActive);
    c->padMode = 0;
    // a panel up pauses it, and the head is measured afresh after: no lurch for the turn meanwhile
    c->cogOpen = 1;
    turnBy(c, 0, -10.0f, 0.0f, &sx, &sy);
    CHECK(sx == 0 && c->headAimActive);
    c->cogOpen = 0;
    int dx, dy;
    g_yawDeg -= 30.0f;
    headFrame(c, 0, &dx, &dy);
    CHECK(dx == 0);
    // the head not tracked: nothing
    g_headTracked = 0;
    turnBy(c, 0, -10.0f, 0.0f, &sx, &sy);
    CHECK(sx == 0);
    g_headTracked = 1;
    c->roomOverride = -1;
}

static void headLookAndAim(Scene* s) {
    XrCtx* c = s->ctx;
    headSetUp(c);
    int sx, sy;
    // head aim on, at its own 5 px a degree: head locked, it wins, so nothing changes for it
    xrSetHeadAim(c, 1, 5, 0);
    xrSetHeadLook(c, 1, 12);
    turnBy(c, 1, -10.0f, 0.0f, &sx, &sy);
    CHECK(abs(sx - 50) <= 1 && c->headMouseWho == HEAD_MOUSE_AIM);
    // head aim in gamepad mode too, as it always was; head-look never
    c->padMode = 1;
    turnBy(c, 1, -10.0f, 0.0f, &sx, &sy);
    CHECK(abs(sx - 50) <= 1);
    c->padMode = 0;
    // in a room head aim cannot act, so head-look does, at its rate
    c->roomOverride = ROOM_STYLE_SYNTHWAVE;
    turnBy(c, 1, -10.0f, 0.0f, &sx, &sy);
    CHECK(abs(sx - 120) <= 1 && c->headMouseWho == HEAD_MOUSE_LOOK);
    c->roomOverride = -1;
    xrSetHeadAim(c, 0, 8, 0);
}

// The controls at rest, both controllers Frame controllers in hand
static void rest(XrCtx* c) {
    for (int h = 0; h < HAND_COUNT; h++) {
        memset(&c->padSeen[h], 0, sizeof(c->padSeen[h]));
        c->padSeen[h].frame = 1;
        c->profileKind[h] = PROFILE_CONTROLLER;
        c->triggerDown[h] = 0;
        c->triggerSwallowed[h] = 0;
    }
}

static void bindFrame(XrCtx* c) {
    float out[IN_SLOTS];
    memset(out, 0, sizeof(out));
    out[IN_SETTING] = -1.0f;
    PadState p;
    padRest(&p);
    xrBindFrame(c, 1000000000LL, 1.0f / 90.0f, &p, out);
}

static void headLookHold(Scene* s) {
    XrCtx* c = s->ctx;
    headSetUp(c);
    xrSetHeadLook(c, 1, 8);
    // the defaults: a grip is bound to nothing in pointer mode, so it is the hold
    unlink(g_path);
    CHECK(xrBindLoad(c, g_path) == 0);
    rest(c);
    bindFrame(c);
    CHECK(c->headLookHold == 0);
    c->padSeen[HAND_LEFT].grip = 1.0f;
    bindFrame(c);
    CHECK(c->headLookHold == 1);
    int sx, sy;
    turnBy(c, 1, -10.0f, 0.0f, &sx, &sy);
    CHECK(sx == 0 && c->headAimActive);   // paused, still head-look's
    rest(c);
    bindFrame(c);
    CHECK(c->headLookHold == 0);
    turnBy(c, 1, -10.0f, 0.0f, &sx, &sy);
    CHECK(abs(sx - 80) <= 1);
    // the right grip too
    c->padSeen[HAND_RIGHT].grip = 1.0f;
    bindFrame(c);
    CHECK(c->headLookHold == 1);
    rest(c);
    bindFrame(c);

    // a grip bound to a key of its own is that key's, and app:lookhold makes any control the hold
    writeFile("[pointer]\nleft.grip = key:R\nleft.dpad_left = app:lookhold\n");
    CHECK(xrBindLoad(c, g_path) == 0);
    xrBindSetApp(c, "");
    rest(c);
    c->padSeen[HAND_LEFT].grip = 1.0f;
    bindFrame(c);
    CHECK(c->headLookHold == 0);
    rest(c);
    bindFrame(c);
    c->padSeen[HAND_LEFT].dpadLeft = 1;
    bindFrame(c);
    CHECK(c->headLookHold == 1);
    // a mode switch lets go of it
    c->padMode = 1;
    bindFrame(c);
    CHECK(c->headLookHold == 0);
    c->padMode = 0;
    rest(c);
    bindFrame(c);
    unlink(g_path);
}

// ---- opentrack (PLAN 4.5) ---------------------------------------------------------------------------------

static int udpListener(int* port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = 0 };
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (fd < 0 || bind(fd, (struct sockaddr*)&a, sizeof(a)) != 0) {
        return -1;
    }
    socklen_t n = sizeof(a);
    getsockname(fd, (struct sockaddr*)&a, &n);
    *port = ntohs(a.sin_port);
    return fd;
}

// The six doubles of the packet that arrived within ms; 0 when none did
static int udpPose(int fd, double v[6], int ms) {
    struct pollfd p = { fd, POLLIN, 0 };
    if (poll(&p, 1, ms) <= 0) {
        return 0;
    }
    uint8_t b[64];
    if (recv(fd, b, sizeof(b), 0) != 48) {
        return 0;
    }
    for (int i = 0; i < 6; i++) {
        uint64_t bits = 0;
        for (int k = 0; k < 8; k++) {
            bits |= (uint64_t)b[8 * i + k] << (8 * k);
        }
        memcpy(&v[i], &bits, sizeof(double));
    }
    return 1;
}

static void opentrackWired(Scene* s) {
    XrCtx* c = s->ctx;
    headSetUp(c);
    Settings st;
    settings_defaults(&st);
    xrApplySettings(c, &st);
    CHECK(c->opentrackOn == 0 && c->opentrackPort == 4242 && c->opentrackHost[0] == 0);
    st.opentrack = 1;
    snprintf(st.opentrack_host, sizeof(st.opentrack_host), "%s", "127.0.0.1");
    st.opentrack_port = 70000;
    xrApplySettings(c, &st);
    CHECK(c->opentrackOn == 1 && c->opentrackPort == 4242 && strcmp(c->opentrackHost, "127.0.0.1") == 0);

    int port = 0;
    int fd = udpListener(&port);
    CHECK(fd >= 0);
    double v[6];
    xrSetOpentrack(c, 1, "127.0.0.1", port);
    // not streaming: nothing
    c->streaming = 0;
    xrOpentrackFrame(c);
    CHECK(udpPose(fd, v, 50) == 0);
    // streaming: every frame, the head as it is, in cm and degrees
    c->streaming = 1;
    g_yawDeg = 30.0f;
    g_pitchDeg = -10.0f;
    xrOpentrackFrame(c);
    CHECK(udpPose(fd, v, 1000) == 1);
    CHECK_NEAR(v[0], 0.0, 1e-3);
    CHECK_NEAR(v[1], 160.0, 1e-3);
    CHECK_NEAR(v[2], 0.0, 1e-3);
    CHECK_NEAR(v[3], 30.0, 1e-2);
    CHECK_NEAR(v[4], -10.0, 1e-2);
    CHECK_NEAR(v[5], 0.0, 1e-2);
    for (int i = 0; i < 4; i++) {
        xrOpentrackFrame(c);
    }
    int n = 0;
    while (udpPose(fd, v, 200)) {
        n++;
    }
    CHECK(n == 4 && c->opentrackSent == 5 && c->opentrackLive);
    // the head not placed: that frame sends nothing
    g_headTracked = 0;
    xrOpentrackFrame(c);
    CHECK(udpPose(fd, v, 50) == 0);
    g_headTracked = 1;
    // the stream stops: nothing more, and the sender lets go
    c->streaming = 0;
    xrOpentrackFrame(c);
    CHECK(!c->opentrackLive && c->opentrackSent == 0);
    CHECK(udpPose(fd, v, 50) == 0);
    // switched off while streaming: nothing
    c->streaming = 1;
    xrSetOpentrack(c, 0, "127.0.0.1", port);
    xrOpentrackFrame(c);
    CHECK(udpPose(fd, v, 50) == 0);
    // empty host: the stream's PC as main.c hands it to the sender
    xrSetOpentrack(c, 1, "", port);
    mlxr_opentrack_set_stream_host("127.0.0.1");
    xrOpentrackFrame(c);
    CHECK(udpPose(fd, v, 1000) == 1);
    mlxr_opentrack_set_stream_host("");
    xrOpentrackFrame(c);
    CHECK(udpPose(fd, v, 50) == 0);
    c->streaming = 0;
    xrOpentrackFrame(c);
    xrSetOpentrack(c, 0, "", 4242);
    close(fd);
}

int main(void) {
    snprintf(g_dir, sizeof(g_dir), "/tmp/mlxr-headinput-XXXXXX");
    if (mkdtemp(g_dir) == NULL) {
        perror("mkdtemp");
        return 1;
    }
    snprintf(g_path, sizeof(g_path), "%s/bindings.ini", g_dir);
    Scene* s = scene_new();
    headLookSettings(s);
    headLookTurns(s);
    headLookAndAim(s);
    headLookHold(s);
    opentrackWired(s);
    scene_free(s);
    unlink(g_path);
    rmdir(g_dir);
    return checksDone("m4 headinput");
}
