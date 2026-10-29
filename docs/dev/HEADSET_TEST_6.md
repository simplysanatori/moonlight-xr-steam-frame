# Headset test 6 (Wave 0 and Wave 1, 2026-10-29)

For the build on main after the Wave 1 merges, installed with `tools/frame-deploy.sh install` (the dev copy, `~/Games/MoonlightXRNative/app-dev`). About 20 minutes in the headset, then 5 at a desk to read the logs. The order matters: the first minute checks a risk that would make the ray part pointless. Do not paste addresses, host names or Wi-Fi names from the logs into a report.

The dev build and the main one share `moonlight.log` and `~/.config/moonlight-xr/config.ini`. The log is `~/Games/MoonlightXRNative/moonlight.log`.

## 0. Before you put the headset on (2 minutes, not counted in the 20)

Check which build you are about to run:

```sh
cat ~/Games/MoonlightXRNative/app-dev/VERSION
tools/frame-deploy.sh status
```

The ray recorder must be on: `MLXR_RAY_DEBUG=1` has to be in the environment of the dev build (see [ray-debug.md](ray-debug.md); for the dev shortcut it goes in its Launch Options as `MLXR_RAY_DEBUG=1 %command%`). The log then has a line `ray debug: on, a row a frame into ...`.

Write down the VR settings you start with: Hand tracking, Gaze, Pause pointer, Controller model, head locked or room, and the Ray cell (Off, Dot, Ray). Item 2 asks you to say "hands on" or "hands off" for each attempt.

Have the PC ready: Desktop at 2560x1440, audio set to 5.1 or 7.1, and a gamepad tester open (item 4). With the app closed, set `virtual_surround = true` under `[stream]` in `config.ini` (item 5).

## 1. The first minute: does the ray show at all? (1 minute)

Start the stream. In pointer mode, with the picture in front of you:

- Is there a ray (or a dot, if Ray is Dot) from each controller when you aim at the picture?
- Does the controller model show?
- Aim both controllers past the edge of the picture, then back.

Good looks like: a ray from each controller, the model shown, the ray following your hand. If **no ray ever shows** while the model does, skip item 2 and go to the log lines in "Read the logs", checking the `flags` first. SteamVR must set ORIENTATION_TRACKED (`0x4`) on the aim pose; if it does not, the ray hides by design. Say which it was, and carry on with the rest of the script, which does not depend on the ray.

Record: ray shows yes or no, model shows yes or no, and the time.

## 2. The ray glitch, with the recorder on (6 minutes)

Use the pointer the way you normally do: browsing, scrolling, dragging a window. Try to make the glitch happen 3 to 5 times. Vary things between attempts:

- Hand tracking on for two attempts and off for two, if your setting allows it (cog, Display tab). If you cannot change it, say which it was.
- Leave the other controller in your lap for some of the time.
- Aim past the edges of the picture and back, quickly.
- Put a controller down on a table and pick it up again.
- Once, sight straight down a controller's beam.

Each time something glitches, open the Report sheet at once (cog, then Report a problem) and close it without sending. That flushes the CSV and writes a `ray debug: report sheet opened` line into the log, which marks the moment. Say the time aloud or note it.

For each glitch note: did the ray snap vertical or jump, for how long, did the model vanish at the same moment, did the Windows cursor jump, and which hand.

Good looks like (the four fixes):

- H1: with Hand tracking on, a held controller never sends the ray from the shoulder.
- H2: a controller in your lap never takes the pointer, and the desktop cursor does not jump when the ray leaves the picture. The pointing hand keeps the pointer for 300 ms after it leaves everything. Are that hold, and the delay before the other hand can take over, acceptable?
- H3: when tracking is lost, the ray and the controller model vanish together after under 300 ms and return together, with no snap.
- H4: looking straight down the beam, it does not flicker or go edge-on.

Record: the number of attempts, how many glitched, the settings, and the Report sheet times.

## 3. Cog, saved Ray, Notices, exit prompt (4 minutes)

- Ray cell (cog, Display tab): set it to Dot, then Off, then Ray. Leave the stream and start it again after each of two of them. The cell must show what you left it on. Then press the bar's Ray button to hide the ray, leave and come back: the bar's choice is for the session only and must not have been kept. The guide says "Bar = this session, cog = saved".
- Notices row (the Display tab now has 14 rows, so the cells are shallower): are the cells still readable and easy to hit? Set All and press the bar's Gamepad button: the toast shows for about 2.5 seconds, 0.22 m below where you look. Set Important: the "Display N Hz" and 3D on/off toasts are gone, but the pointer/gamepad toast and errors still show. Set Off: no toasts. Put it back on All.
- Wording: the hint and the toast for the gamepad chord say "View", not "menu".
- Exit prompt: press the bar's Exit during the stream. It offers Quit app, Disconnect and Cancel. Choose Cancel. At the end of the run use Disconnect once: the PC's app keeps running and the flat window comes back (exit code 11). In the launcher lists (not streaming) the prompt is Exit and Cancel.

Record: Ray persisted yes or no for each value you tried, whether the bar's choice leaked into the next session, Notices cells readable yes or no, toasts at each level as expected, the exit prompt's choices.

## 4. Gamepad mode: Guide, rumble, dead zone (3 minutes)

Switch to gamepad mode (hold left View and the left grip for half a second, or use the bar button). Have the gamepad tester open on the PC.

- Hold left View and right Menu together for about a quarter of a second. The tester must show the Guide (Xbox) button, and Steam's overlay on the PC may open. Let go: Start and Back must not be stuck down afterwards. Do it twice.
- Start, Back, LB and RB together must still disconnect. Do not do this now; use Disconnect from the bar at the end.
- Rumble: use the tester's vibration test or a game that rumbles. Is it felt at all? Is a one-motor effect felt in both hands? Are short pulses felt?
- Dead zone: with the launcher's "Adjust analog stick deadzone" at 0 (the stick uses 7 %), push the left stick slowly in the tester and note where it first moves. Set the slider to 15 in the flat launcher, restart the stream, and repeat. The point where the stick wakes up must move out.
- Optional: set `[vr] pad_grip` to `none`, then to `stick_clicks` in `config.ini`, restart, and see that the grips do nothing, then click the sticks. Put it back to `bumpers` afterwards.

Record: Guide shows yes or no, Start/Back stuck yes or no, rumble felt (both hands, one hand, none), the dead zone figure at 0 and at 15, the `pad_grip` results.

## 5. Surround and mute (3 minutes)

Stream something with 5.1 or 7.1 sound where the centre channel is obvious (a trailer with dialogue, or a surround test clip), with virtual surround on.

- Turn your head 90 degrees to the left: the dialogue (centre) moves to your right ear. Turn right: the reverse. If it is swapped, say so (it is a sign flip in the code).
- Look past the edge of the picture, slowly and then quickly: the sound moves smoothly, with no snap. With the picture head locked, the surround stays fixed.
- Open the SteamVR dashboard: the sound goes silent within a frame or two. Close it: the sound returns. Take the headset off and put it back on: the same. Open and close the dashboard quickly three times: no stutter and no stuck mute.
- Switch the PC to stereo and restart the stream: the sound is as before.

Record: direction right or swapped, smooth or snapping, mute and unmute worked for the dashboard and for headset off, any stuck mute.

## 6. One lossy network run (3 minutes)

Keep streaming. Walk away from the router, or put a wall between you and it, until the picture starts to break up. Stay there about a minute, then come back. This moves the receive-buffer error counters in the log.

Record: where you went, how long, and how the picture behaved (frozen frames, blocks, a drop-out).

## Read the logs (at a desk, 5 minutes)

On the PC, pull the files in one go (the plain logs hold addresses; only the zip is masked):

```sh
tools/frame-deploy.sh logs out
```

That writes `moonlight.log`, `moonlight-console.log`, `moonlight-launcher.log`, `vrtiming.log`, SteamVR's `vrcompositor.txt`, the masked bug-report zip and `status.txt` into `out/`. Copy the ray CSV too:

```sh
scp steamos@<frame>:'~/Games/MoonlightXRNative/reports/ray-*.csv' out/
```

Then, against `out/moonlight.log`:

```sh
# Items 1 and 2: the recorder, the flags and the ray sources
grep "ray debug: on" out/moonlight.log
grep "ray debug: t_ns .* hand " out/moonlight.log | head -40
grep "ray debug: t_ns .* pointing " out/moonlight.log | head -40
grep "ray debug: report sheet opened" out/moonlight.log
grep "ray debug: .* lines held back" out/moonlight.log
# Interaction profile of each hand
grep -E "active interaction profile|Steam Frame profile" out/moonlight.log | tail -8
# Item 4: Guide and rumble
grep "gamepad: Guide button" out/moonlight.log | tail -4
grep -E "rumble: the host's first request|rumble on the (left|right) controller" out/moonlight.log | head -10
# Item 6: network
grep -E "wifi power save:|net: net.core.rmem_max" out/moonlight.log | tail -6
grep -o "xr: latency (stream): .*" out/moonlight.log | grep "udp rcvbuf errors" | tail -24
# Item 3: how each exit ended
grep -E "The VR client exited with|flat window" out/moonlight-launcher.log | tail -6
```

On the Frame itself, use `~/Games/MoonlightXRNative/moonlight.log` in place of `out/moonlight.log`.

What each should show:

- `ray debug: t_ns ... hand ... flags`: while a controller is in view and its model shows, the flags include `0x4` (for example `0x7` or `0xf`). Around a glitch they drop to `0x3` or `0x0`, the ray source reads `held` for under 300 ms and then `none`, and never `joints` on a hand that holds a controller (`joints 1` on a controller hand is the Hand tracking case, H1). `pointing` lines must not flip between hands while the other controller lies in your lap.
- `active interaction profile`: for hand 0 and hand 1, `/interaction_profiles/valve/frame_controller_valve` (or `.../frame_controller`). `Steam Frame profile <path> accepted` or `not accepted by this runtime`, once per path.
- `rumble: the host's first request: low motor N ...`: absent means the PC sent no rumble. `rumble on the left controller ... runtime said XR_SUCCESS` (and right) for each hand that buzzed.
- `wifi power save: wlan0 is off` (already off on this Frame, so no "turned off for the stream" or "restored" lines), then `net: net.core.rmem_max is 2097152 bytes`.
- `udp rcvbuf errors +N (system total N), in errors +N` in the 5 second `xr: latency (stream):` line: +0 in the calm part, growing while you were far from the router.
- `The VR client exited with 11` for Disconnect, `0` for Quit app.

Optional, with `[debug] file_log = verbose` set before the run and a bitrate over 300 Mbps: `grep "OUTPUT .* sizeimage" out/moonlight-console.log` shows the buffer size, and `grep -i "E_TOO_BIG" out/moonlight-console.log` should find nothing (PLAN item 1.12). Skip it if you do not raise the bitrate that far.

## Results

Copy this table into your report and fill it in.

| # | Item | Result (fine, or what was wrong) | Time of the run |
|---|---|---|---|
| 1 | Ray shows at all (ORIENTATION_TRACKED risk) | | |
| 2a | Ray glitch: attempts, glitched, Hand tracking on or off | | |
| 2b | H1: no ray from the shoulder with Hand tracking on | | |
| 2c | H2: lap controller never takes the pointer; cursor does not jump | | |
| 2d | H3: ray and model vanish and return together, under 300 ms hold | | |
| 2e | H4: no flicker looking down the beam | | |
| 2f | The 300 ms hold and the hand-switch delay are acceptable | | |
| 3a | Ray cog setting survives a restart (Off, Dot, Ray) | | |
| 3b | Bar Ray button is session only | | |
| 3c | Notices cells readable and hittable; All, Important, Off behave | | |
| 3d | "View" in the hint and the toast | | |
| 3e | Exit prompt: Quit app, Disconnect, Cancel | | |
| 4a | Guide chord shows Guide; Start and Back not stuck | | |
| 4b | Rumble felt (both hands, one hand, none) | | |
| 4c | Dead zone slider moves the stick dead zone | | |
| 4d | `pad_grip` none and stick_clicks (optional) | | |
| 5a | Surround turns the right way, no snap | | |
| 5b | Mute with the dashboard and with the headset off | | |
| 5c | Stereo unaffected | | |
| 6 | Lossy run: counters rose; how the picture behaved | | |
| L | Log lines from the greps above (only if they carry no addresses or names) | | |

## Owner questions this run answers (PLAN section 6)

Question 1, the ray glitch:

- Is Hand tracking on or off? (Items 0 and 2; the recorder shows it as `joints` on the controller hands.)
- Does the Windows cursor jump too? (Item 2.)
- Does the other controller lie on your lap when it happens? (Item 2.)
- Does the controller model vanish at the same time? (Item 2.)
- Is Gaze on? (Item 0.)

Question 2: with Controller model Off, do you still see a controller? Set it Off for a minute during item 1 and say what you see. A controller that is still drawn is SteamVR's own rendering, not the client's.

## What to report

- The results table.
- The time of each run, so it can be found in the log, and which item each Report sheet time belongs to.
- The ray CSV and the masked bug-report zip from `tools/frame-deploy.sh logs`, only if the ray still glitched.
