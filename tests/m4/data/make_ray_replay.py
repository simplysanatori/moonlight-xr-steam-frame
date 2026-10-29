#!/usr/bin/env python3
# Moonlight XR Native, tests/m4: writes ray-replay.csv, the synthetic ray
# recorder CSV test_replay.c feeds through the input code (lane ray-glitch).
# The rows are in the recorder's format (xr/core/xr_raylog.h); the last
# columns hold what the fixed code must decide, -9 where a column is not
# checked (the frames the pose filter takes to settle after a sudden turn, and
# the frame a controller's own clock wakes on). GPLv3.
#
#   python3 tests/m4/data/make_ray_replay.py > tests/m4/data/ray-replay.csv
#
# The scene (test_replay.c sets it up): the picture 2.4 m wide, 2.5 m ahead at
# eye height, the head at the origin, world locked, both hands on controllers,
# hand tracking on (joints for both hands, as SteamVR makes them up from a
# held controller), the pointer's pause off.
import math

FRAME_NS = 11111111
T0 = 1000000000
CENTRE = (0.0, 0.0, -2.5)
UNCHECKED = -9
# AIM_ from xr/core/xr_ray.h
NONE, POSE, JOINTS, HELD = 0, 1, 2, 3
PROFILE_CONTROLLER = 3


def norm(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def quat_from_basis(x, y, z):
    # Columns x, y, z of a rotation matrix, as xr_math.c's quatFromBasis
    m00, m11, m22 = x[0], y[1], z[2]
    tr = m00 + m11 + m22
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        return ((y[2] - z[1]) / s, (z[0] - x[2]) / s, (x[1] - y[0]) / s, 0.25 * s)
    if m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2
        return (0.25 * s, (y[0] + x[1]) / s, (z[0] + x[2]) / s, (y[2] - z[1]) / s)
    if m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2
        return ((y[0] + x[1]) / s, 0.25 * s, (z[1] + y[2]) / s, (z[0] - x[2]) / s)
    s = math.sqrt(1.0 + m22 - m00 - m11) * 2
    return ((z[0] + x[2]) / s, (z[1] + y[2]) / s, 0.25 * s, (x[1] - y[0]) / s)


def look(direction):
    # A pose points down its own -z
    z = norm(tuple(-c for c in direction))
    x = norm(cross((0.0, 1.0, 0.0), z))
    y = cross(z, x)
    return quat_from_basis(x, y, z)


def aim_at(pos, target):
    return look(tuple(target[i] - pos[i] for i in range(3)))


def pitched(deg):
    r = math.radians(deg)
    return look((0.0, math.sin(r), -math.cos(r)))


def turned_left(deg):
    r = math.radians(deg)
    return look((-math.sin(r), 0.0, -math.cos(r)))


def hand(pos, quat, flags=0xF, ok=1, trigger=0.0, joints=1, src=POSE):
    return dict(pos=pos, quat=quat, flags=flags, ok=ok, trigger=trigger, joints=joints, src=src)


def fmt_hand(h):
    k = (h["pos"][0], h["pos"][1] - 0.03, h["pos"][2] - 0.05)
    return ("%d,%d,0x%x,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f,%d,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%u,%d,%d"
            % (PROFILE_CONTROLLER, h["ok"], h["flags"], 1, h["pos"][0], h["pos"][1], h["pos"][2],
               h["quat"][0], h["quat"][1], h["quat"][2], h["quat"][3], h["joints"], k[0], k[1], k[2],
               h["trigger"], 0.0, 0.0, 0.0, 0, h["src"], UNCHECKED))


HEADER = ("t_ns,to_head,head_flags,hx,hy,hz,hqx,hqy,hqz,hqw"
          + "".join(",%s_%s" % (side, c) for side in "lr" for c in
                    ("kind", "ok", "flags", "active", "x", "y", "z", "qx", "qy", "qz", "qw",
                     "joints", "kx", "ky", "kz", "trigger", "grip", "sx", "sy", "buttons", "src",
                     "awake"))
          + ",pointer,hand,ray,hover")


def main():
    rows = []
    l_still = (-0.15, -0.3, -0.3)
    r_lap = (0.25, -0.55, -0.2)

    def row(k, left, right, chosen, ray):
        t = T0 + k * FRAME_NS
        rows.append("%d,0,0xf,0.0000,0.0000,0.0000,0.00000,0.00000,0.00000,1.00000,%s,%s,%d,%d,%d,%d"
                    % (t, fmt_hand(left), fmt_hand(right), UNCHECKED, chosen, ray, UNCHECKED))

    def settle(k, start, frames, value):
        return UNCHECKED if k < start + frames else value

    # P0 0..59: the left drifts sideways (waking it), aimed at the picture;
    # the right lies in the lap pointing up at nothing
    for k in range(0, 60):
        pos = (-0.25 + 0.1 * k / 90.0, -0.3, -0.3)
        row(k, hand(pos, aim_at(pos, CENTRE)), hand(r_lap, pitched(85.0)), 0, 0)
    # P1 60..89: the lap controller's ray comes to lie across the picture; the
    # left, awake, keeps the pointer (the old rule gave it to the right)
    for k in range(60, 90):
        row(k, hand(l_still, aim_at(l_still, CENTRE)), hand(r_lap, aim_at(r_lap, CENTRE)), 0, 0)
    # P2 90..134: the left aims past the picture; nothing points and the beam
    # runs on from the left, not out of the lap (H2)
    for k in range(90, 135):
        row(k, hand(l_still, turned_left(70.0)), hand(r_lap, aim_at(r_lap, CENTRE)),
            settle(k, 90, 10, -1), settle(k, 90, 10, 0))
    # P3 135..149: a trigger on the lap controller takes the pointer at once
    for k in range(135, 150):
        row(k, hand(l_still, turned_left(70.0)), hand(r_lap, aim_at(r_lap, CENTRE), trigger=1.0),
            settle(k, 135, 2, 1), settle(k, 135, 2, 1))
    # P4 150..179: let go, the left back on the picture takes it back
    for k in range(150, 180):
        row(k, hand(l_still, aim_at(l_still, CENTRE)), hand(r_lap, aim_at(r_lap, CENTRE)),
            settle(k, 150, 10, 0), settle(k, 150, 10, 0))
    # P5 180..224 (H1): the left's aim is not placed while SteamVR's joints
    # are: held 27 frames (under 300 ms) where it was, then gone, never the
    # shoulder to knuckle ray
    for k in range(180, 225):
        heldf = k <= 206
        row(k, hand(l_still, aim_at(l_still, CENTRE), flags=0x0, src=HELD if heldf else NONE),
            hand(r_lap, aim_at(r_lap, CENTRE)), 0 if heldf else -1, 0 if heldf else -1)
    # P6 225..239: tracked again, at once
    for k in range(225, 240):
        row(k, hand(l_still, aim_at(l_still, CENTRE)), hand(r_lap, aim_at(r_lap, CENTRE)), 0, 0)
    # P7 240..284 (H3): valid but untracked, the runtime's extrapolation
    # pitched 60 degrees down: the last tracked pose held, then hidden
    for k in range(240, 285):
        heldf = k <= 266
        row(k, hand(l_still, pitched(-60.0), flags=0x3, src=HELD if heldf else NONE),
            hand(r_lap, aim_at(r_lap, CENTRE)), 0 if heldf else -1, 0 if heldf else -1)
    # P8 285..299: out of the cameras, orientation tracked on the gyro: live
    for k in range(285, 300):
        row(k, hand(l_still, aim_at(l_still, CENTRE), flags=0x7),
            hand(r_lap, aim_at(r_lap, CENTRE)), 0, 0)
    # P9 300..379: the left aims away and the right is picked up and moved,
    # aimed at the picture: it takes over once awake on its own clock
    for k in range(300, 380):
        rpos = (0.25 - 0.1 * (k - 299) / 90.0, -0.55, -0.2)
        if k < 310:
            chosen = ray = UNCHECKED
        elif k <= 335:
            chosen, ray = -1, 0
        elif k <= 355:
            chosen = ray = UNCHECKED
        else:
            chosen = ray = 1
        row(k, hand(l_still, turned_left(70.0)), hand(rpos, aim_at(rpos, CENTRE)), chosen, ray)

    print("# Synthetic ray recorder rows for tests/m4/test_replay.c, written by make_ray_replay.py")
    print(HEADER)
    for r in rows:
        print(r)


if __name__ == "__main__":
    main()
