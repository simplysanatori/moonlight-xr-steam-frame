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
  `scp steamos@192.168.0.168:'~/Games/MoonlightXRNative/reports/ray-*.csv' steamos@192.168.0.168:~/Games/MoonlightXRNative/moonlight.log out/`
  `tools/bug-report.sh` does not bundle the CSV.

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
