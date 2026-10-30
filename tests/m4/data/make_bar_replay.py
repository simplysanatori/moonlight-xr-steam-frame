#!/usr/bin/env python3
# Moonlight XR Native, tests/m4: writes bar-replay.csv, ray recorder rows for
# the white beam over the picture while the ray is on the bar (lane
# bar-glare). The right controller points along the bar under the picture,
# through the parts of its row that are no button: under the move pill, and
# just under the buttons. The left controller is held up and moving, its ray
# on the picture. The row is the bar, so the right keeps the pointer and its
# ray all along. Before the fix those parts were the picture's margin, and the
# left took the pointer the moment the right's ray crossed one: its beam ran
# from the left hand across the middle of the picture, the bar hid, and the
# PC's cursor jumped. Same format and scene as make_ray_replay.py. GPLv3.
#
#   python3 tests/m4/data/make_bar_replay.py > tests/m4/data/bar-replay.csv
#
# The scene (test_replay.c sets it up): the picture 2.4 m wide, 16:9, 2.5 m
# ahead at eye height, flat, world locked, the head at the origin, both hands
# on controllers, the pointer's pause off. The bar's row hangs BAR_DROP_FRAC
# of the width under the picture (xr/core/xr_layout.h).
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_ray_replay import HEADER, FRAME_NS, T0, UNCHECKED, aim_at, fmt_hand, hand  # noqa: E402

WIDTH = 2.4
HEIGHT = WIDTH * 9.0 / 16.0
PLANE_Z = -2.5
BAR_HEIGHT_FRAC = 0.14 * 24.0 / 256.0
BAR_DROP_FRAC = 0.035 + BAR_HEIGHT_FRAC * 0.5
ROW_Y = -(HEIGHT * 0.5 + WIDTH * BAR_DROP_FRAC)
# The move pill's own zone used to end this far under the row's middle
# ((BAR_GAP_FRAC + 3 BAR_HEIGHT_FRAC) under the edge), the buttons' zones
# reach COG_BUTTON_FRAC * HOVER_MARGIN / 2 of the width: between the two
UNDER_Y = ROW_Y - WIDTH * 0.0368
# The left of the row: ENV, EXIT, PAD and the fourth place out (head aim's,
# empty with the screen in the world), its middle 0.318 of the width out
ROW_LEFT_X = -0.30 * WIDTH


def main():
    rows = []
    right_at = (0.2, -0.35, -0.3)
    left_at = (-0.2, -0.35, -0.3)

    def row(k, target, chosen, ray):
        t = T0 + k * FRAME_NS
        # The left, held up and moving the whole time, aimed at the picture
        # left of its middle, so it is awake on its own clock
        lt = (-0.2 + 0.3 * math.sin(k * 0.11), 0.1 + 0.25 * math.cos(k * 0.07), PLANE_Z)
        left = hand(left_at, aim_at(left_at, lt))
        right = hand(right_at, aim_at(right_at, target))
        rows.append("%d,0,0xf,0.0000,0.0000,0.0000,0.00000,0.00000,0.00000,1.00000,%s,%s,%d,%d,%d,%d"
                    % (t, fmt_hand(left), fmt_hand(right), UNCHECKED, chosen, ray, UNCHECKED))

    def lerp(a, b, s):
        return tuple(a[i] + (b[i] - a[i]) * s for i in range(3))

    k = 0
    # P0 0..119: the right draws circles on the picture, both wake; the right
    # was first and keeps the pointer (unchecked while the two wake)
    for i in range(120):
        target = (0.3 + 0.3 * math.sin(i * 0.1), -0.2 + 0.2 * math.cos(i * 0.1), PLANE_Z)
        row(k, target, UNCHECKED if i < 60 else 1, UNCHECKED if i < 60 else 1)
        k += 1
    last = (0.3 + 0.3 * math.sin(120 * 0.1), -0.2 + 0.2 * math.cos(120 * 0.1), PLANE_Z)
    # P1: down onto the move pill
    pill = (0.0, ROW_Y, PLANE_Z)
    for i in range(40):
        row(k, lerp(last, pill, (i + 1) / 40.0), 1, 1)
        k += 1
    # P2: just under the pill and the buttons beside it, out to the left and
    # back across to the right of the pill
    under = (0.0, UNDER_Y, PLANE_Z)
    for i in range(10):
        row(k, lerp(pill, under, (i + 1) / 10.0), 1, 1)
        k += 1
    for i in range(90):
        row(k, lerp(under, (-0.45, UNDER_Y, PLANE_Z), (i + 1) / 90.0), 1, 1)
        k += 1
    for i in range(120):
        row(k, lerp((-0.45, UNDER_Y, PLANE_Z), (0.45, UNDER_Y, PLANE_Z), (i + 1) / 120.0), 1, 1)
        k += 1
    # P3: up to the row's middle and along it to the left end
    mid_right = (0.45, ROW_Y, PLANE_Z)
    for i in range(10):
        row(k, lerp((0.45, UNDER_Y, PLANE_Z), mid_right, (i + 1) / 10.0), 1, 1)
        k += 1
    for i in range(120):
        row(k, lerp(mid_right, (ROW_LEFT_X, ROW_Y, PLANE_Z), (i + 1) / 120.0), 1, 1)
        k += 1

    print("# Synthetic ray recorder rows for tests/m4/test_replay.c, written by make_bar_replay.py")
    print(HEADER)
    for r in rows:
        print(r)


if __name__ == "__main__":
    main()
