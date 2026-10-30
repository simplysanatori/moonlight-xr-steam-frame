// See xr_padgyro.h. GPLv3.
#include "xr_padgyro.h"

#include <math.h>

int padGyroSourceClamp(int source) {
    return source == PAD_GYRO_CONTROLLER || source == PAD_GYRO_HEAD ? source : PAD_GYRO_OFF;
}

int padGyroRatchetClamp(int ratchet) {
    return ratchet == PAD_GYRO_RATCHET_STICK_TOUCH ? ratchet : PAD_GYRO_RATCHET_ALWAYS;
}

int padGyroRatchetFallsBack(int ratchet, int touchAvailable) {
    return padGyroRatchetClamp(ratchet) == PAD_GYRO_RATCHET_STICK_TOUCH && !touchAvailable;
}

int padGyroGateOpen(int ratchet, int touchAvailable, int touching) {
    if (padGyroRatchetClamp(ratchet) != PAD_GYRO_RATCHET_STICK_TOUCH || !touchAvailable) {
        return 1;
    }
    return touching != 0;
}

// rad/s to deg/s on one axis, held to the DualShock's range
static float degS(float radS) {
    if (!isfinite(radS)) {
        return 0.0f;
    }
    float d = radS * PAD_GYRO_RAD_TO_DEG;
    if (d > PAD_GYRO_MAX_DEG_S) {
        return PAD_GYRO_MAX_DEG_S;
    }
    return d < -PAD_GYRO_MAX_DEG_S ? -PAD_GYRO_MAX_DEG_S : d;
}

Vec3 padGyroFromVelocity(XrQuaternionf orientation, Vec3 radSBase) {
    // The device's own axes: undo the device's orientation in the base space
    Vec3 dev = quatRotate(quatConj(quatNorm(orientation)), radSBase);
    Vec3 out = { degS(dev.x), degS(dev.y), degS(dev.z) };
    return out;
}

Vec3 padGyroFromTurn(XrQuaternionf was, XrQuaternionf now, float dt) {
    Vec3 none = { 0.0f, 0.0f, 0.0f };
    if (!(dt > 0.0f)) {
        return none;
    }
    // now = was * turn, so the turn is in the device's frame of a moment ago,
    // which is its frame to within the angle of one step
    XrQuaternionf turn = quatMul(quatConj(quatNorm(was)), quatNorm(now));
    if (turn.w < 0.0f) {
        turn.x = -turn.x;
        turn.y = -turn.y;
        turn.z = -turn.z;
        turn.w = -turn.w;
    }
    float s = sqrtf(turn.x * turn.x + turn.y * turn.y + turn.z * turn.z);
    // angle = 2 atan2(s, w) about the unit axis (x, y, z) / s
    float k = s < 1e-6f ? 2.0f / (turn.w > 1e-6f ? turn.w : 1.0f) : 2.0f * atan2f(s, turn.w) / s;
    Vec3 out = { degS(turn.x * k / dt), degS(turn.y * k / dt), degS(turn.z * k / dt) };
    return out;
}

Vec3 padGyroGravity(XrQuaternionf orientation) {
    Vec3 up = { 0.0f, PAD_GYRO_GRAVITY, 0.0f };
    return quatRotate(quatConj(quatNorm(orientation)), up);
}
