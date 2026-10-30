// Native (lane wave4-headinput, PLAN 4.5): the head pose as opentrack's UDP packet: the units, the
// angles' order and signs, and the 48 bytes on the wire, checked byte for byte
#include <math.h>
#include <string.h>

#include "check.h"
#include "xr_opentrack.h"

#define PI_F 3.14159265358979f
#define RAD(deg) ((deg) * PI_F / 180.0f)

// A head turned by yaw about up, then pitched about its right, then rolled about its back
static XrPosef headAt(float x, float y, float z, float yawDeg, float pitchDeg, float rollDeg) {
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 back = { 0.0f, 0.0f, 1.0f };
    XrQuaternionf q = quatMul(axisAngleQuat(up, RAD(yawDeg)), axisAngleQuat(right, RAD(pitchDeg)));
    q = quatNorm(quatMul(q, axisAngleQuat(back, RAD(rollDeg))));
    XrPosef p = { q, { x, y, z } };
    return p;
}

static void testTheAngles(void) {
    OpentrackPose o;
    opentrackFromPose(headAt(0.1f, 1.6f, -0.25f, 0, 0, 0), &o);
    CHECK_NEAR(o.x, 10.0, 1e-4);
    CHECK_NEAR(o.y, 160.0, 1e-4);
    CHECK_NEAR(o.z, -25.0, 1e-4);
    CHECK_NEAR(o.yaw, 0.0, 1e-4);
    CHECK_NEAR(o.pitch, 0.0, 1e-4);
    CHECK_NEAR(o.roll, 0.0, 1e-4);
    // each alone, both signs: left, up and a tilt to the left are positive
    opentrackFromPose(headAt(0, 0, 0, 30, 0, 0), &o);
    CHECK_NEAR(o.yaw, 30.0, 1e-3);
    CHECK_NEAR(o.pitch, 0.0, 1e-3);
    CHECK_NEAR(o.roll, 0.0, 1e-3);
    opentrackFromPose(headAt(0, 0, 0, -120, 0, 0), &o);
    CHECK_NEAR(o.yaw, -120.0, 1e-3);
    opentrackFromPose(headAt(0, 0, 0, 0, 20, 0), &o);
    CHECK_NEAR(o.pitch, 20.0, 1e-3);
    CHECK_NEAR(o.yaw, 0.0, 1e-3);
    opentrackFromPose(headAt(0, 0, 0, 0, -45, 0), &o);
    CHECK_NEAR(o.pitch, -45.0, 1e-3);
    opentrackFromPose(headAt(0, 0, 0, 0, 0, 15), &o);
    CHECK_NEAR(o.roll, 15.0, 1e-3);
    CHECK_NEAR(o.yaw, 0.0, 1e-3);
    CHECK_NEAR(o.pitch, 0.0, 1e-3);
    // all three together come back apart
    opentrackFromPose(headAt(0, 0, 0, -35, 25, -10), &o);
    CHECK_NEAR(o.yaw, -35.0, 1e-3);
    CHECK_NEAR(o.pitch, 25.0, 1e-3);
    CHECK_NEAR(o.roll, -10.0, 1e-3);
    opentrackFromPose(headAt(0, 0, 0, 170, -60, 40), &o);
    CHECK_NEAR(o.yaw, 170.0, 1e-3);
    CHECK_NEAR(o.pitch, -60.0, 1e-3);
    CHECK_NEAR(o.roll, 40.0, 1e-3);
    // straight up stays finite
    opentrackFromPose(headAt(0, 0, 0, 0, 90, 0), &o);
    CHECK_NEAR(o.pitch, 90.0, 1e-2);
    CHECK(isfinite(o.yaw) && isfinite(o.roll));
    // an unnormalised or zero quaternion
    XrPosef p = headAt(0, 0, 0, 30, 0, 0);
    p.orientation.x *= 3.0f; p.orientation.y *= 3.0f; p.orientation.z *= 3.0f; p.orientation.w *= 3.0f;
    opentrackFromPose(p, &o);
    CHECK_NEAR(o.yaw, 30.0, 1e-3);
    p.orientation = (XrQuaternionf){ 0, 0, 0, 0 };
    opentrackFromPose(p, &o);
    CHECK(o.yaw == 0.0 && o.pitch == 0.0 && o.roll == 0.0);
}

static void testTheBytes(void) {
    // values whose doubles are exact: 1, -2, 0.5, 90, -45.25, 180
    OpentrackPose o = { 1.0, -2.0, 0.5, 90.0, -45.25, 180.0 };
    uint8_t got[OPENTRACK_PACKET_BYTES];
    memset(got, 0xAA, sizeof(got));
    CHECK(opentrackPack(&o, got) == 48);
    static const uint8_t want[48] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,   //   1.0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0,   //  -2.0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE0, 0x3F,   //   0.5
        0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x56, 0x40,   //  90.0
        0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, 0x46, 0xC0,   // -45.25
        0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x66, 0x40,   // 180.0
    };
    CHECK(memcmp(got, want, sizeof(want)) == 0);
    // a non-finite value goes out as 0 rather than poisoning opentrack's filter
    OpentrackPose bad = { NAN, INFINITY, 1.0, 0.0, -INFINITY, 0.0 };
    opentrackPack(&bad, got);
    static const uint8_t zero[8] = { 0 };
    CHECK(memcmp(got, zero, 8) == 0 && memcmp(got + 8, zero, 8) == 0 && memcmp(got + 32, zero, 8) == 0);
    CHECK(memcmp(got + 16, want, 8) == 0);
}

static void testThePort(void) {
    CHECK(opentrackPortClamp(4242) == 4242);
    CHECK(opentrackPortClamp(1) == 1 && opentrackPortClamp(65535) == 65535);
    CHECK(opentrackPortClamp(0) == OPENTRACK_PORT_DEFAULT);
    CHECK(opentrackPortClamp(70000) == OPENTRACK_PORT_DEFAULT);
    CHECK(opentrackPortClamp(-5) == OPENTRACK_PORT_DEFAULT);
}

int main(void) {
    testTheAngles();
    testTheBytes();
    testThePort();
    return checksDone("xr_opentrack");
}
