# Ray glitch: the recorder and the 10-minute headset run

The bug: a controller's pointer ray snaps vertical, or glitches, for about a second. Research report 12 (`research/reports/12-bug-ray-glitch.md`) named four suspects. Lane ray-glitch fixed all four in code, because nobody yet knew whether Hand tracking was on. The rules live in `xr/core/xr_ray.c` and are tested in `tests/xrcore/test_ray.c`:

| | What it was | What the code does now |
|---|---|---|
| H1 | With Hand tracking on, a controller hand whose aim pose dropped out took the ray from the joints. SteamVR makes those joints up from the held controller, so the ray ran from the shoulder through the knuckle, 35 to 70 degrees off. | `aimFallbackFor`: a hand holding a controller never uses the joint ray. Tracked hands still do. |
| H2 | Every frame the pointing hand was picked fresh, the right hand first. A controller lying in the lap took the pointer, and the free beam came out of it whenever the other ray left the picture. | `pickHand` / `pickFreeHand`: the hand that pointed last keeps the pointer. Another hand takes it only with a button down, or when it is awake on its own clock (it moved recently) and the last hand has been off everything for more than 300 ms. The free beam comes from the hand that pointed last. |
| H3 | A pose that was VALID but not TRACKED (SteamVR guessing where the controller is) was drawn as it came, and the ray snapped back when tracking returned. The controller model vanished at the same moment, because it went by a stricter rule. | `controllerPoseLive` and `poseHoldStep`: one rule for the ray and the model. The pose action is live, the pose is placed, and its orientation is tracked (position may be a guess). When tracking is lost, the last tracked pose is held for under 300 ms, then the ray and the model hide together. The pose filter is reset. |
| H4 | When the head looked within about 9 degrees of straight down the beam, the beam quad switched to the world's up. It was then seen edge-on and flickered. | `beamBasis`: the quad still faces the head, and leans smoothly toward the head's right as the line of sight closes in. It never switches. |

## Running the recorder (owner, about 10 minutes)

1. In Steam, open the Moonlight XR shortcut's Properties. Put this in Launch Options:
   `MLXR_RAY_DEBUG=1 %command%`
   (Without the shortcut: `MLXR_RAY_DEBUG=1 ~/Games/MoonlightXRNative/app/moonlight-xr.sh`.)
2. Write down the VR settings: Hand tracking, Gaze, Pause pointer, Controller model, and head locked or room.
3. Start a stream and use the pointer the way you normally do. Try to make the glitch happen 3 to 5 times. Leave the other controller in your lap sometimes, and aim past the edges of the picture.
4. Each time it glitches, open the Report sheet right away (cog, then Report a problem). You don't have to send anything. Opening the sheet flushes the CSV and writes a `ray debug: report sheet opened at t_ns ...` line into `moonlight.log`, which marks the moment.
5. Quit the stream. Remove the launch option afterwards: it costs a little disk and log space.

### Where things land

- CSV: `~/Games/MoonlightXRNative/reports/ray-<date>-<time>.csv`, one file per session. If `MLXR_LOG_DIR` is set, it goes to `$MLXR_LOG_DIR/reports/` instead. It holds one row per input frame and grows by about 2 MB a minute. It stops at 96 MB.
- Log lines: `~/Games/MoonlightXRNative/moonlight.log`, all starting `ray debug:`, at most 20 a second. A line is written when a hand's location flags or ray source change (`aim`, `joints`, `held`, `none`), when its ray goes steeper than 80 degrees, or when the pointing source changes. The pointing-source line includes both hands' hovers and awake state.
- Copy both off the headset:
  `scp steamos@<frame>:'~/Games/MoonlightXRNative/reports/ray-*.csv' steamos@<frame>:~/Games/MoonlightXRNative/moonlight.log out/`
  `tools/bug-report.sh` (and the Report sheet's Save and zip) bundles the newest CSV as `ray.csv`: its header and the last 20000 rows. Opening the sheet flushes the CSV first.

## What the run has to confirm

- **H1:** with Hand tracking on, lines with `kind 3` and `joints 1` around the glitch confirm that SteamVR supplies joints for a held controller. The ray source then reads `held` or `none`, never `joints`.
- **H2:** look for `pointing` lines that flip between hands while the hovers show the lap controller on the bar or halo. They should no longer appear. Also check whether the desktop cursor still jumps when the ray leaves the picture.
- **H3:** look for flag changes from `0xf` or `0x7` to `0x3` (valid, untracked) or `0x0` around the glitch. The ray source should read `held` for under 300 ms, then `none`. The ray and the controller model should vanish and come back together.
- **H4:** sight straight down a controller's beam. The beam must not flicker or go edge-on. Nothing is logged for this one; it is a look check only.
- **A risk to check first:** the aim's flags must include ORIENTATION_TRACKED (`0x4`) whenever the controller is in view. If SteamVR never set that bit on the aim pose, the ray would never show. The bit is already required for the controller model, which does show on the Frame.

## Replaying a CSV

`tests/m4/test_replay.c` reads `tests/m4/data/ray-replay.csv` through the same input code. It sets the seams on the context (`locateSpace`, `poseActive`, `readFloat`, `readBool`, `readVec2`, `pfnLocateHandJoints`) and calls `xrPointerStep`. For each row it checks the pointing source, the hand whose ray is drawn, and each hand's ray source. `-9` means the column is not checked. The committed CSV is synthetic: `tests/m4/data/make_ray_replay.py` writes it, and the comments in that script describe each phase. To turn a headset CSV into a regression test:

1. Copy it into `tests/m4/data/`, keeping just the rows around the glitch.
2. Set the `hand`, `ray`, `l_src` and `r_src` columns to what the fixed code should decide, or `-9` where they are not checked.
3. Point a second `MLXR_REPLAY_CSV` test at it.

The replay scene is world locked, with the picture 2.4 m wide and 2.5 m ahead. A recording made with a different placement replays its poses correctly, but its hovers will differ.

## The white streak on the keyboard (lane beam-streak)

The bug: with the keyboard up, a large white streak, like a reflected ray, crossed the middle of the view for a frame, most often while the ray went back and forth over the keys. The owner's recording of 2026-10-31 has one such frame. In it the dot sits where the ray hits the keyboard, but the streak does not reach the dot: it runs from the bottom of the view to the top, wide at the bottom, as a 1 cm beam quad looks only when it passes within a few centimetres of the eye. The input code builds the beam's start and its end from the same aim pose, so the end being right means the start was too. The frame before it had the ring round the Compact key, and the glitch frame is the first without it, the dot having moved into the gap beside the key.

- **The cause, most likely:** the ring round the key under the ray is a layer of its own. It came and went as the ray crossed the gaps between keys, and each time the beam and the dot moved a place up or down the list of layers. On the frame the ring went, SteamVR drew the beam, now in the ring's place, as that streak. Every layer ahead of the beam that comes and goes with the hover now keeps its place while it is not shown, as a clear patch of the ring's art a millimetre across (`holdPlace` in `xr/xr_layers.c`): the picture's bar and corner handles, the bar's buttons, the key ring, the room grid's rings, the hand hint's and the Ko-fi sheet's button rings, the controller models, the dwell's mark and a beam too short to draw. The bar's buttons hold their places only while the bar's row could come up (no panel open, no card up), since a card takes only the room the rest leave. The frame is then as full as it was with the hover on its fullest: 13 layers on the picture (14 with the stats window), 8 with the keyboard, 9 with the room grid, against SteamVR's 16. Over 16, the toast goes first and then the held places, so the beam still keeps its place. `tests/m4/test_kbbar.c` sweeps the hover over the picture's furniture, the keyboard and the room grid and checks the beam's place and the count never change.
- **A guard whatever the cause:** the layers never submit a beam that reaches the eyes. `beamClipAhead` (`xr/core/xr_ray.c`, tested in `tests/xrcore/test_ray.c`) keeps only the part more than 5 cm in front of the head, along the way it faces. No beam is drawn when nothing is left, and no dot where the end itself was cut. A beam from a controller held at the face and pointing away is never touched.
- **The log line:** each time the guard cuts a beam, at most once a second, `moonlight.log` gets a `ray debug: the beam reached the head and was ...` line. It gives the hand, what the ray was over, which code set the beam (the picture, a panel or the keyboard, a held handle, the free beam), the start and end, that hand's aim, the head and the way it faces, and each hand's ray source and whether it was placed. A streak with no such line around it was not ours to cut, and points at the compositor again.

### Since 1.3.1: the pointer ahead of the hover layers (lane beam-order)

1.3.1 took the held places away (issue #5: a stack of clear quads made SteamVR judder at 144 Hz), and the streak came back as the ray went onto and off the keys and the bar. Rather than hold places, the pointer now goes in ahead of everything that comes and goes with the hover. Each step is a switch read once at the start and said in `moonlight.log` (`beam streak switches: ...`):

- `MLXR_POINTER_ORDER` (default 1): the layers that only change as a panel or a card opens or shuts, then the pointer (the dwell's mark, the beam, the dot), then the move bar or corner, the bar's buttons, the rings and the controller models (`addHoverLayers` in `xr/xr_layers.c`). A panel's own bar button stays ahead, under its panel. The cost: a bar button or the move bar is drawn over the dot while the ray is on it. 2 puts the dwell's mark and the dot last instead, over everything but the toast. 0 is 1.3.1's order. Over the limit the toast goes first, then the held places, the models, the hover ring, the cog button and the clock as before, then the last hover layers after the beam; nothing ahead of the beam.
- `MLXR_BEAM_BLANK=1` (default off): the beam and the dot are drawn clear for one frame when the number of layers ahead of the pointer changed, keeping their places in the list.
- The log: a `beam place:` line on each frame the beam's place or the count changes (the beam's place old -> new, -1 for none, the count, the hover, whether the UI atlas was taken from its swapchain that frame, and whether the beam was blanked). At most 8 a second, 2 of them for a count alone; the rest go into one line at the end of the second with the worst move of the beam written in full. The 5 s `xr: frames:` line counts the frames the beam moved in the list.

`tests/m4/test_kbbar.c` sweeps the hover over the picture's furniture, the keyboard and the room grid; ctest runs the sweep with the held places off and each order (`m4_kbbar_pointer_order`, `m4_kbbar_dot_last`, `m4_kbbar_order0_held`), and checks the beam's place never changes, nothing hover-only is ahead of it, and the count stays within 16.
