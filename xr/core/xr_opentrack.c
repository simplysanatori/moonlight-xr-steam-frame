// Native (lane wave4-headinput, PLAN 4.5): see xr_opentrack.h
#include "xr_opentrack.h"

#include <math.h>
#include <string.h>

#define OT_DEG (180.0 / 3.14159265358979323846)

static double clampUnit(double v) {
    return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v);
}

void opentrackFromPose(XrPosef head, OpentrackPose* out) {
    out->x = (double)head.position.x * 100.0;
    out->y = (double)head.position.y * 100.0;
    out->z = (double)head.position.z * 100.0;
    double x = head.orientation.x, y = head.orientation.y, z = head.orientation.z,
           w = head.orientation.w;
    double n = sqrt(x * x + y * y + z * z + w * w);
    if (n <= 0.0) {
        out->yaw = out->pitch = out->roll = 0.0;
        return;
    }
    x /= n;
    y /= n;
    z /= n;
    w /= n;
    // The rotation matrix's entries the yaw (about y), pitch (about x), roll (about z) order needs:
    // R = Ry(yaw) Rx(pitch) Rz(roll), so R12 = -sin(pitch), R02 / R22 the yaw, R10 / R11 the roll
    double r02 = 2.0 * (x * z + w * y);
    double r22 = 1.0 - 2.0 * (x * x + y * y);
    double r12 = 2.0 * (y * z - w * x);
    double r10 = 2.0 * (x * y + w * z);
    double r11 = 1.0 - 2.0 * (x * x + z * z);
    out->pitch = asin(clampUnit(-r12)) * OT_DEG;
    out->yaw = atan2(r02, r22) * OT_DEG;
    out->roll = atan2(r10, r11) * OT_DEG;
}

static void putDouble(uint8_t* at, double v) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; i++) {
        at[i] = (uint8_t)(bits >> (8 * i));
    }
}

int opentrackPack(const OpentrackPose* p, uint8_t out[OPENTRACK_PACKET_BYTES]) {
    const double v[6] = { p->x, p->y, p->z, p->yaw, p->pitch, p->roll };
    for (int i = 0; i < 6; i++) {
        putDouble(out + 8 * i, isfinite(v[i]) ? v[i] : 0.0);
    }
    return OPENTRACK_PACKET_BYTES;
}

int opentrackPortClamp(int port) {
    return port >= OPENTRACK_PORT_MIN && port <= OPENTRACK_PORT_MAX ? port : OPENTRACK_PORT_DEFAULT;
}
