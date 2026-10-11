# grab-pull report (lane grab-pull, branch lane/grab-pull)

The owner's report: holding the picture by its move bar with the left
controller's grip, the stick pushed north sends the picture away, but pulled
south it does not bring it closer.

## What was checked

- `pushHeldScreen()` (xr/xr_input.c) and its caller `applyGrab()`. The math is
  symmetric. North and south add the same metres a second for the same tilt.
  The clamp keeps nothing back, so a pull at the near end is not stored up.
- `GRAB_PUSH_NEAR_M` is 0.3 m, far below the owner's 3.0 to 3.45 m. The clamp
  does not eat the pull.
- Nothing undoes the pull later in the frame. Outside a room nothing else
  writes `screenPose` during a hold. The curve, the bar frame, the default
  placement (`updatePlacement` gets the fixed default, so it never reseeds) and
  the save on letting go all keep it.
- The sticks reach the function raw for both hands (`seenStick`). Only a chord
  gamepad shortcut with a stick click zeroes them, and the owner's shortcut is
  the menu button with the grip.
- SteamVR's binding for the app (`~/.config/openvr/config/openxr/`, Frame
  controller) puts both thumbsticks on the scroll action in joystick mode.
  Nothing filters south.
- A new unit test drives the real hover pass and `applyGrab()` for the picture
  (flat and curved) and the keyboard. Pull and push match to the millimetre.

## The cause

The owner's own log of today shows it. The "cursor dot" lines trace the ray's
distance during each hold, and the placement saved at each letting go gives the
net move.

- Pushes ran near full speed: 3.56 m to 5.76 m in one hold of under two and a
  half seconds.
- Pulls with only the left controller in use (07:33 to 07:34, the right one
  asleep) came to 0.1 to 0.6 m per hold of two to six seconds.
- Pulls once the right controller was picked up ran at about 1.1 m a second
  (3.7 m to 0.75 m in under three seconds).

So the pull reached the code, but weakly. The code read only the stick's
north-south part, took a 0.2 dead zone off it and squared what was left. A
thumb drawn back toward the palm goes south on a slant and short of the rim. A
slant at 45 degrees with a tilt of 0.88 gives y = 0.62, which came out at 0.28
of full speed: 0.41 m a second against 1.5 m a second north. A shorter pull of
0.4 gave 0.09 m a second, which looks like nothing. North is a thumb stretched
straight out, which reaches the rim.

This is an inference from the log. The log has no stick values, so the new
diagnostic line below is there to prove it on the next headset test.

## The fix

- `stickPushAmount()` reads the whole tilt, as long as the stick leans north
  or south (within 60 degrees, `GRAB_PUSH_SECTOR_COS`). East and west still do
  nothing.
- A tilt of 0.85 (`GRAB_PUSH_FULL`) already counts as full tilt. The curve is
  still squared below that, for fine moves.
- The stronger of the two sticks wins, either way, as before.
- The near limit stays at 0.3 m. It was not the cause, and the keyboard and
  the stats window take the same push and want to come that close.
- New line on letting go of any held window (picture, keyboard, stats):
  `move bar let go: 3.00 m from the head, sticks pushed it -1.50 m; stick y
  seen left -0.62 to 0.00, right 0.00 to 0.00`. It gives the most south and
  most north each stick reached during the hold.

Files: xr/xr_input.c, xr/xr_renderer.h (constants, two fields),
xr/xr_native.h (`xrGrabStepStick` test hook), tests/m4/test_kbbar.c,
docs/USER_GUIDE.md.

## Tests

`pictureBarPushPull` in tests/m4/test_kbbar.c, the owner's picture at 3.0 m
and 3.6 m wide, flat and curved:
- a stick inside the dead zone, or pushed east or west, moves nothing;
- one second north moves it 1.5 m out, one second south brings it back by the
  same, then on past the start;
- a slanted pull short of the rim (-0.62, -0.62) pulls at full speed;
- half a tilt is slower than full, the same both ways;
- held at the near limit for three seconds it stays at 0.3 m, and one frame
  north moves it out at once; the same at the far end;
- the keyboard held by its bar takes the same push and pull.

## What the owner should try

1. Hold the picture by its bar with the left grip, pull the left stick down
   the way that feels natural. The picture should come in as fast as north
   sends it out.
2. Do the same with the right controller, and with the keyboard's bar.
3. If a pull still feels slow, send the log: each letting go now says how far
   south each stick reached (`move bar let go: ... stick y seen left ...`).
   A left value that never gets past about -0.3 would mean the stick itself
   stops short, and the next step would be the stick's calibration in SteamVR.

## Open risks

- The cause is read from timings in the log, not from stick values. The new
  log line settles it.
- A slanted push now moves the picture where it did little before. A stick
  pushed mostly sideways (more than 60 degrees off north or south) still does
  nothing.
- The cylinder's radius does not follow a push or pull (the Distance slider
  scales it). A curved picture pushed far out wraps more than it did at its
  set distance. That is unchanged by this lane.
