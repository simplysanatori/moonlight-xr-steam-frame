// The Frame controller's or the head's turn as a gamepad's gyro (xr_padgyro.h):
// rad/s to deg/s, the axes of SDL's pad frame, the device frame of a turned
// controller, the finite-difference fallback, gravity-only accelerometer and
// the thumb-on-stick ratchet. New in this fork.
#include <math.h>

#include "check.h"
#include "xr_padgyro.h"

#define DEG 57.2957795
#define DT (1.0f / 90.0f)

static XrQuaternionf identity(void) {
    XrQuaternionf q = { 0.0f, 0.0f, 0.0f, 1.0f };
    return q;
}

static XrQuaternionf about(float x, float y, float z, float angle) {
    Vec3 axis = { x, y, z };
    return axisAngleQuat(axis, angle);
}

static void testSettings(void) {
    CHECK(padGyroSourceClamp(PAD_GYRO_OFF) == PAD_GYRO_OFF);
    CHECK(padGyroSourceClamp(PAD_GYRO_CONTROLLER) == PAD_GYRO_CONTROLLER);
    CHECK(padGyroSourceClamp(PAD_GYRO_HEAD) == PAD_GYRO_HEAD);
    CHECK(padGyroSourceClamp(-1) == PAD_GYRO_OFF && padGyroSourceClamp(PAD_GYRO_COUNT) == PAD_GYRO_OFF);
    CHECK(padGyroRatchetClamp(PAD_GYRO_RATCHET_ALWAYS) == PAD_GYRO_RATCHET_ALWAYS);
    CHECK(padGyroRatchetClamp(PAD_GYRO_RATCHET_STICK_TOUCH) == PAD_GYRO_RATCHET_STICK_TOUCH);
    CHECK(padGyroRatchetClamp(7) == PAD_GYRO_RATCHET_ALWAYS);
}

static void testRatchet(void) {
    // always: motion goes whatever the thumb does
    CHECK(padGyroGateOpen(PAD_GYRO_RATCHET_ALWAYS, 1, 0) == 1);
    CHECK(padGyroGateOpen(PAD_GYRO_RATCHET_ALWAYS, 1, 1) == 1);
    CHECK(padGyroGateOpen(PAD_GYRO_RATCHET_ALWAYS, 0, 0) == 1);
    // stick_touch: only while the thumb is on the stick
    CHECK(padGyroGateOpen(PAD_GYRO_RATCHET_STICK_TOUCH, 1, 1) == 1);
    CHECK(padGyroGateOpen(PAD_GYRO_RATCHET_STICK_TOUCH, 1, 0) == 0);
    // stick_touch with no touch input is always, and says so
    CHECK(padGyroGateOpen(PAD_GYRO_RATCHET_STICK_TOUCH, 0, 0) == 1);
    CHECK(padGyroRatchetFallsBack(PAD_GYRO_RATCHET_STICK_TOUCH, 0) == 1);
    CHECK(padGyroRatchetFallsBack(PAD_GYRO_RATCHET_STICK_TOUCH, 1) == 0);
    CHECK(padGyroRatchetFallsBack(PAD_GYRO_RATCHET_ALWAYS, 0) == 0);
    // a ratchet out of range is always
    CHECK(padGyroGateOpen(9, 1, 0) == 1);
}

static void testUnitsAndAxes(void) {
    // A device at rest in the base space: its axes are the base's. rad/s to deg/s
    // on x (pitch), y (yaw) and z (roll), one axis at a time
    Vec3 px = { 1.0f, 0.0f, 0.0f }, py = { 0.0f, 1.0f, 0.0f }, pz = { 0.0f, 0.0f, 1.0f };
    Vec3 g = padGyroFromVelocity(identity(), px);
    CHECK_NEAR(g.x, DEG, 1e-3);
    CHECK_NEAR(g.y, 0.0, 1e-6);
    CHECK_NEAR(g.z, 0.0, 1e-6);
    g = padGyroFromVelocity(identity(), py);
    CHECK_NEAR(g.x, 0.0, 1e-6);
    CHECK_NEAR(g.y, DEG, 1e-3);
    CHECK_NEAR(g.z, 0.0, 1e-6);
    g = padGyroFromVelocity(identity(), pz);
    CHECK_NEAR(g.z, DEG, 1e-3);
    // Turning to the right (clockwise seen from above) is a negative turn about
    // y, which is a negative yaw: the same as turning a real pad to the right
    Vec3 right = { 0.0f, -0.5f, 0.0f };
    g = padGyroFromVelocity(identity(), right);
    CHECK(g.y < 0.0f);
    CHECK_NEAR(g.y, -0.5 * DEG, 1e-3);
    // 0.25 rad/s is 14.3 deg/s
    Vec3 slow = { 0.0f, 0.0f, 0.25f };
    CHECK_NEAR(padGyroFromVelocity(identity(), slow).z, 14.3239, 1e-3);
}

static void testDeviceFrame(void) {
    // The device yawed 90 degrees to the left: its z axis (toward the user) now
    // lies along the base's x. A turn about the base's x is a roll of the device
    XrQuaternionf left90 = about(0.0f, 1.0f, 0.0f, (float)M_PI / 2.0f);
    Vec3 aboutBaseX = { 1.0f, 0.0f, 0.0f };
    Vec3 g = padGyroFromVelocity(left90, aboutBaseX);
    CHECK_NEAR(g.x, 0.0, 1e-3);
    CHECK_NEAR(g.y, 0.0, 1e-3);
    CHECK_NEAR(g.z, DEG, 1e-3);
    // A turn about the vertical is the device's yaw however it is yawed
    Vec3 aboutBaseY = { 0.0f, 1.0f, 0.0f };
    g = padGyroFromVelocity(left90, aboutBaseY);
    CHECK_NEAR(g.y, DEG, 1e-3);
    CHECK_NEAR(g.x, 0.0, 1e-3);
    // The device pitched up (its -z, the way it points, toward the ceiling): a
    // turn of the world about the vertical is now a turn about the device's z
    XrQuaternionf up90 = about(1.0f, 0.0f, 0.0f, (float)M_PI / 2.0f);
    g = padGyroFromVelocity(up90, aboutBaseY);
    CHECK_NEAR(g.y, 0.0, 1e-3);
    CHECK_NEAR(fabs(g.z), DEG, 1e-3);
    // An orientation that is not quite unit length is taken as its direction
    XrQuaternionf big = left90;
    big.x *= 3.0f;
    big.y *= 3.0f;
    big.z *= 3.0f;
    big.w *= 3.0f;
    g = padGyroFromVelocity(big, aboutBaseX);
    CHECK_NEAR(g.z, DEG, 1e-3);
}

static void testLimits(void) {
    Vec3 fast = { 100.0f, -100.0f, 100.0f };
    Vec3 g = padGyroFromVelocity(identity(), fast);
    CHECK_NEAR(g.x, PAD_GYRO_MAX_DEG_S, 1e-3);
    CHECK_NEAR(g.y, -PAD_GYRO_MAX_DEG_S, 1e-3);
    CHECK_NEAR(g.z, PAD_GYRO_MAX_DEG_S, 1e-3);
    Vec3 bad = { NAN, INFINITY, 0.5f };
    g = padGyroFromVelocity(identity(), bad);
    CHECK(g.x == 0.0f && g.y == 0.0f);
    CHECK_NEAR(g.z, 0.5 * DEG, 1e-3);
    // A bad axis must not spill into the good ones once the device is turned
    // and the rotation mixes them: yawed 90 degrees left, a clean turn about
    // the vertical is still a clean yaw
    XrQuaternionf left90 = about(0.0f, 1.0f, 0.0f, (float)M_PI / 2.0f);
    Vec3 badX = { NAN, 1.0f, 0.0f };
    g = padGyroFromVelocity(left90, badX);
    CHECK(isfinite(g.x) && isfinite(g.y) && isfinite(g.z));
    CHECK_NEAR(g.x, 0.0, 1e-3);
    CHECK_NEAR(g.y, DEG, 1e-3);
    CHECK_NEAR(g.z, 0.0, 1e-3);
}

static void testFromTurn(void) {
    // 0.01 rad about the device's x in one frame at 90 Hz: 0.9 rad/s
    XrQuaternionf was = about(0.0f, 1.0f, 0.0f, 0.5f);
    XrQuaternionf now = quatMul(was, about(1.0f, 0.0f, 0.0f, 0.01f));
    Vec3 g = padGyroFromTurn(was, now, DT);
    CHECK_NEAR(g.x, 0.01 / DT * DEG, 0.01);
    CHECK_NEAR(g.y, 0.0, 0.01);
    CHECK_NEAR(g.z, 0.0, 0.01);
    // the same turn about y and z, negative ones too
    now = quatMul(was, about(0.0f, 1.0f, 0.0f, -0.02f));
    g = padGyroFromTurn(was, now, DT);
    CHECK_NEAR(g.y, -0.02 / DT * DEG, 0.01);
    CHECK_NEAR(g.x, 0.0, 0.01);
    now = quatMul(was, about(0.0f, 0.0f, 1.0f, 0.03f));
    g = padGyroFromTurn(was, now, DT);
    CHECK_NEAR(g.z, 0.03 / DT * DEG, 0.02);
    // the same rotation written as the negated quaternion is the same turn
    XrQuaternionf neg = now;
    neg.x = -neg.x;
    neg.y = -neg.y;
    neg.z = -neg.z;
    neg.w = -neg.w;
    Vec3 h = padGyroFromTurn(was, neg, DT);
    CHECK_NEAR(h.z, g.z, 1e-3);
    // not turned, no time, or time running backwards: no turn
    g = padGyroFromTurn(was, was, DT);
    CHECK(g.x == 0.0f && g.y == 0.0f && g.z == 0.0f);
    g = padGyroFromTurn(was, now, 0.0f);
    CHECK(g.x == 0.0f && g.y == 0.0f && g.z == 0.0f);
    g = padGyroFromTurn(was, now, -1.0f);
    CHECK(g.x == 0.0f && g.y == 0.0f && g.z == 0.0f);
    // it agrees with the velocity form for the same motion
    XrQuaternionf a = about(0.3f, 0.5f, 0.8f, 0.7f);
    Vec3 axis = { 0.0f, 1.0f, 0.0f };
    XrQuaternionf b = quatMul(about(0.0f, 1.0f, 0.0f, 0.004f), a);
    Vec3 viaTurn = padGyroFromTurn(a, b, DT);
    Vec3 baseSpin = axis;
    baseSpin.y *= 0.004f / DT;
    Vec3 viaVel = padGyroFromVelocity(a, baseSpin);
    CHECK_NEAR(viaTurn.x, viaVel.x, 0.05);
    CHECK_NEAR(viaTurn.y, viaVel.y, 0.05);
    CHECK_NEAR(viaTurn.z, viaVel.z, 0.05);
}

static void testGravity(void) {
    // Level and still: (0, 9.81, 0), the accelerometer reading of a pad lying flat
    Vec3 g = padGyroGravity(identity());
    CHECK_NEAR(g.x, 0.0, 1e-5);
    CHECK_NEAR(g.y, PAD_GYRO_GRAVITY, 1e-5);
    CHECK_NEAR(g.z, 0.0, 1e-5);
    CHECK_NEAR(PAD_GYRO_GRAVITY, 9.81, 0.01);
    // Pointed at the ceiling, the up of the world is the device's -z
    g = padGyroGravity(about(1.0f, 0.0f, 0.0f, (float)M_PI / 2.0f));
    CHECK_NEAR(g.x, 0.0, 1e-4);
    CHECK_NEAR(g.y, 0.0, 1e-4);
    CHECK_NEAR(g.z, -PAD_GYRO_GRAVITY, 1e-4);
    // Rolled a quarter turn (a side down), the up of the world is along the
    // device's x, and the length is always gravity's
    g = padGyroGravity(about(0.0f, 0.0f, 1.0f, (float)M_PI / 2.0f));
    CHECK_NEAR(g.y, 0.0, 1e-4);
    CHECK_NEAR(fabs(g.x), PAD_GYRO_GRAVITY, 1e-4);
    // Upside down
    g = padGyroGravity(about(1.0f, 0.0f, 0.0f, (float)M_PI));
    CHECK_NEAR(g.y, -PAD_GYRO_GRAVITY, 1e-4);
    // Yaw does not change it
    g = padGyroGravity(about(0.0f, 1.0f, 0.0f, 1.2f));
    CHECK_NEAR(g.y, PAD_GYRO_GRAVITY, 1e-4);
    CHECK_NEAR(g.x, 0.0, 1e-4);
}

int main(void) {
    testSettings();
    testRatchet();
    testUnitsAndAxes();
    testDeviceFrame();
    testLimits();
    testFromTurn();
    testGravity();
    return checksDone("xr_padgyro");
}
