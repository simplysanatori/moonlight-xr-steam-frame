// Native (lane wave2-lightgun-gyro): the Frame controller's or the head's turn
// as a gamepad's gyro (PLAN 2.9, report 15). With pad_gyro on, the gamepad
// slot of the Frame controllers sends motion to the host the way a DualShock 4
// does, and Steam Input on the PC can take gyro aim from it. This file is the
// arithmetic: the angular velocity OpenXR gives, in rad/s about the axes of the
// space it is measured in, turned into what LiSendControllerMotionEvent takes.
//
// Units and axes are moonlight-qt's, which passes SDL's through: the gyroscope
// in deg/s about the device's own x (pitch), y (yaw) and z (roll), right hand
// rule, the accelerometer in m/s^2 including gravity. SDL's game controller
// frame, a pad held in front of you, is x to the right, y up and z toward you,
// which is exactly an OpenXR aim or view space (x right, y up, -z ahead), so
// the axes need no swapping: the device-frame angular velocity in rad/s times
// 57.29578 is the packet. Turning your head to the right is a negative yaw, the
// same as turning a real pad to the right.
//
// The accelerometer is gravity only: a device at rest reads (0, 9.81, 0), one
// held level and still, and the same vector turned into the device's frame as
// it tilts. Linear acceleration of the hand is not sent, which is all that
// Steam Input's gyro aim and its tilt need.
//
// Plain arithmetic over values handed in, no OpenXR calls and no clock. GPLv3.
#ifndef MLXR_XR_PADGYRO_H
#define MLXR_XR_PADGYRO_H

#include "xr_math.h"

// What turns, in the order the pad_gyro setting lists them: nothing (the Frame
// pad advertises no motion), the aim pose of the right controller (the left one
// when the right is not tracked), or the head
#define PAD_GYRO_OFF 0
#define PAD_GYRO_CONTROLLER 1
#define PAD_GYRO_HEAD 2
#define PAD_GYRO_COUNT 3

// When motion goes out, in the order the pad_gyro_ratchet setting lists them:
// always, or only while the right thumb touches the right stick (Steam Input's
// usual gyro ratchet). A Frame without the touch input falls back to always.
#define PAD_GYRO_RATCHET_ALWAYS 0
#define PAD_GYRO_RATCHET_STICK_TOUCH 1
#define PAD_GYRO_RATCHET_COUNT 2

#define PAD_GYRO_RAD_TO_DEG 57.2957795f
// The DualShock 4's gyro tops out at 2000 deg/s; anything past it is a glitch
#define PAD_GYRO_MAX_DEG_S 2000.0f
#define PAD_GYRO_GRAVITY 9.80665f

int padGyroSourceClamp(int source);
int padGyroRatchetClamp(int ratchet);

// The ratchet. touchAvailable says the stick-touch input is bound and readable,
// touching that the thumb is on the stick. Returns 1 when motion goes out.
// stick_touch with no touch input is always (padGyroRatchetFallsBack says so,
// for the log line).
int padGyroGateOpen(int ratchet, int touchAvailable, int touching);
int padGyroRatchetFallsBack(int ratchet, int touchAvailable);

// An angular velocity as xrLocateSpace's XrSpaceVelocity gives it, in rad/s
// about the axes of the base space, for a device whose orientation in that
// space is given: in the device's own axes, deg/s, held to +-2000 and with a
// reading that is not a number as zero
Vec3 padGyroFromVelocity(XrQuaternionf orientation, Vec3 radSBase);

// The same from two orientations dt seconds apart, for a runtime that gives no
// velocity: the turn from one to the other about the device's axes. dt of zero
// or less is no turn.
Vec3 padGyroFromTurn(XrQuaternionf was, XrQuaternionf now, float dt);

// Gravity as the device's accelerometer reads it, m/s^2
Vec3 padGyroGravity(XrQuaternionf orientation);

#endif
