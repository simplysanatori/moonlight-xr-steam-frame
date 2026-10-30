// Native (lane wave4-headinput, PLAN 4.5): the head pose for opentrack's "UDP over network" input.
// One packet is six little-endian IEEE doubles, 48 bytes: x, y, z in centimetres, then yaw, pitch
// and roll in degrees. The pose is the head in the session's local space as OpenXR has it (x right,
// y up, z toward the back of the head), the angles taken in yaw, pitch, roll order: yaw about up,
// positive turning left; pitch positive looking up; roll positive tilting the head to the left.
// opentrack centres itself (its Center key or "centre at startup"), and its Output tab inverts any
// axis a game wants the other way. Plain arithmetic, no OpenXR calls and no sockets (the sender is
// backend/opentrack.c), so tests/xrcore reaches all of it. GPLv3.
#ifndef XR_OPENTRACK_H
#define XR_OPENTRACK_H

#include <stdint.h>

#include "xr_math.h"

#define OPENTRACK_PACKET_BYTES 48
#define OPENTRACK_PORT_DEFAULT 4242
#define OPENTRACK_PORT_MIN 1
#define OPENTRACK_PORT_MAX 65535

typedef struct {
    double x, y, z;            // centimetres
    double yaw, pitch, roll;   // degrees
} OpentrackPose;

// The head pose in opentrack's units and order
void opentrackFromPose(XrPosef head, OpentrackPose* out);

// The 48 bytes on the wire, little-endian whatever the host's byte order. Returns the byte count.
int opentrackPack(const OpentrackPose* p, uint8_t out[OPENTRACK_PACKET_BYTES]);

// opentrack_port held to 1..65535, anything else the default
int opentrackPortClamp(int port);

#endif
