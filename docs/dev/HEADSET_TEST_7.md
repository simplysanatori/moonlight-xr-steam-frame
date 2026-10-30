# Headset test 7 (batch 3 and the Wave 1 leftovers, 2026-10-29)

For the dev build on main after the wave2-bind-wire merge (source v1.0.1-1xx or later; check `app-dev/VERSION`), installed with `tools/frame-deploy.sh install` (the dev copy, `~/Games/MoonlightXRNative/app-dev`). Part A is 20 minutes in the headset with no accessories, plus 2 at a desk before and 5 after to read the logs. Part B is optional and needs a Bluetooth gamepad, a USB data cable, and a PC whose keyboard layout you can switch. Do not paste addresses, host names or Wi-Fi names from the logs into a report.

Items in Part A are in the order to do them. The reconnect and presence items (7 and 8) come late because they end or interrupt the stream; the Wave 1 leftovers (9) come last because they finish with the exit prompt.

The dev build and the main one share `moonlight.log` and `~/.config/moonlight-xr/config.ini`. The log is `~/Games/MoonlightXRNative/moonlight.log`.

## Before you start

Read this box first. The last headset run (Wave 1, run 1) was unusable for judging the picture, and the cause was in the setup, not in the code. The full diagnosis is in `research/frame-runs/wave1-run-1/DIAGNOSIS.md`.

- **Launcher stream settings: 2560x1440 at 98 Mbps, 90 fps.** In run 1 the flat launcher's Settings were changed to 3840x2160 at 150 Mbps between two sessions, and the file they are saved in (`Moonlight XR Native.conf`) is shared by the dev and the main shortcuts. 4K/150 made the packet loss clearly worse (video 6 to 50 fps, hundreds of unrecoverable frames in a minute) and the text smeared. Open the flat launcher's Settings and check the resolution and the bitrate before every run. Write down what you ran at.
- **The dev shortcut's SteamVR settings now match the main app: 3408 per-eye resolution at 90 Hz.** Run 1's dev shortcut had `resolutionOverride` 2160 (the main app has 3408), so the picture was redrawn into 40 % of the pixels and small text was a smear. `tools/frame-deploy.sh shortcut` now copies the main shortcut's per-app block to the dev one. Check the first minute of the log for `curved picture: projection layer 3408x3408 per eye (recommended 3408x3408)`. If it says 2160, stop: the dev shortcut still has the old block, and the picture will be soft whatever else you test.
- **The host changed on 2026-10-29 as well.** The PC's streaming host went from Vibepollo 1.18.4-stable.2 to Vibepollo 2.0.1 at 19:03 that day. The client log looks the same either way (the server version string still says 7.1.431). Write the Vibepollo version you are on at the top of your report (the Apollo web UI or the install folder's log directory shows it). Run 1 saw `Unrecoverable frame` storms, ENet rtt of 9 to 46 ms and an audio cushion of 35 to 40 ms; if you see those again, that points at the host or the Wi-Fi and not at the items below, and the answer is to compare against the published 1.0.0 in the same spot rather than to carry on testing.
- **`tools/frame-deploy.sh logs out` fetches everything.** It writes `moonlight.log` (and `.1`, `.previous`), `moonlight-console.log`, `moonlight-launcher.log`, `vrtiming.log`, SteamVR's `vrcompositor.txt`, `status.txt` and the masked bug-report zip into `out/`. The zip holds `ray.csv` when `MLXR_RAY_DEBUG=1` was on. The `Unrecoverable frame` lines are now limited to about one a second (`common-c: Unrecoverable frame ... (+N more)`), so a bad stretch no longer rotates the 5 MB log in minutes. The plain logs hold addresses; only the zip is masked.

## 0. Before you put the headset on (3 minutes, not counted in the 20)

Check which build you are about to run:

```sh
cat ~/Games/MoonlightXRNative/app-dev/VERSION
tools/frame-deploy.sh status
```

Set up the files the items use (all in `~/.config/moonlight-xr/`; the app is closed, so nothing is overwritten):

- `config.ini`: `[display] follow_leash_deg` stays at its default 25 for now; `[display] stats_style = hud` is set in item 6. Leave `[vr] scroll_speed` at 100.
- `bindings.ini` must exist (the first run writes it with every default commented out) and must still be all comments: item 2 tests the defaults first and edits the file during the run, so have an editor or SSH session ready.
- `macros.ini`: create it with, for example, `Snip = win+shift+s` and `Paste and send = ctrl+v, wait:150, enter` (USER_GUIDE "Macros", [host.md](host.md)).
- A custom controller model, if you have one: put it in `models/` as `custom.model` (or a `custom_left.model` and `custom_right.model` pair), made as [CUSTOM_MODELS.md](../CUSTOM_MODELS.md) says. Without one, skip the Custom part of item 1.
- The ray recorder: `MLXR_RAY_DEBUG=1` in the dev shortcut's Launch Options as `MLXR_RAY_DEBUG=1 %command%` ([ray-debug.md](ray-debug.md)). The log then has `ray debug: on, a row a frame into ...`. Item 6 needs it for the report zip.
- The flat launcher: Settings, 2560x1440, 98 Mbps (see "Before you start").

On the PC: Desktop at 2560x1440, a gamepad tester open (https://hardwaretester.com/gamepad), the volume mixer visible and a text editor window to type into. Have a second app you can stream for the per-app layout in item 5, and audio set to 5.1 or 7.1 for item 9.

Write down the VR settings you start with: Hand tracking, Gaze, Controller model, head locked or room, Ray, Notices.

# Part A: no accessories (about 20 minutes in the headset)

## 1. Controller model kinds (2 minutes)

Source: PLAN 2.1 and 2.4, lane wave2-model-kind; USER_GUIDE "Controllers"; [CUSTOM_MODELS.md](../CUSTOM_MODELS.md).

Start the stream. Open the cog, Display tab. The "Show controllers" row has four cells: Off, Frame, Generic, Gun.

- Press each cell in turn, with the controllers in view. They must swap at once: no restart, no hitch in the picture or the sound.
- Gun: the barrel lies along the ray and the muzzle sits on the dot; the trigger part rotates when you pull the trigger; the left hand has its own gun mesh (not the right one mirrored). Compare the hold angle and the fit with the real handle. If the model floats away from your hand, note how far (a hand's width, a few centimetres) and in which direction.
- Custom (only if you did the setup): close the stream, set `[vr] controller_model = custom` (or in the flat launcher, Settings, Input Settings, "Show controllers", press the row until it says Your own model), start again. The right hand shows the model, the left is its mirror (or its own file, for a pair), and the trigger part moves on a pull. Close the stream, delete or rename the file, start again: the Frame models come back and the log says why.
- Tracking loss: put one controller down and pick it up (or cover it). The ray and its model must vanish together and return together.

Good looks like: four cells that all work live, a gun that sits on the ray, the custom model drawn and mirrored, a clean fall back.

Record: each kind, swaps live yes or no, Gun fit (floating, how far), custom shown and mirrored, fall back worked, ray and model vanish together yes or no.

## 2. Bindings: defaults, a custom remap, errors (3 minutes)

Source: PLAN 2.5, 2.6 and 2.7, lane wave2-bind-wire; [bindings.md](bindings.md); USER_GUIDE "Changing the buttons".

First the defaults, in pointer mode, with a text editor on the PC and `bindings.ini` still all comments:

- Trigger clicks and drags. A (and d-pad down) right click, B (and d-pad right) middle click. Stick clicks right click.
- Y opens the keyboard, X toggles 3D, d-pad up recentres (the screen turns to the head's heading), Menu opens the cog, the bumpers go back and forward in the PC's browser.
- Gamepad mode (hold left View and left grip for half a second): the gamepad tester sees the Frame pad. Hold left View and right Menu for a quarter of a second: Guide shows. Start+Back+LB+RB is the disconnect chord: do not press it now. `pad_grip` still decides what the grips do.

Then the remap. Leave the stream (Disconnect) and edit `bindings.ini` on the Frame: under `[pointer]` add `right.y = key:F`; add `[app "<the app you stream, as the app list names it>"]` with `pointer.right.x = key:G`; and add one wrong line on purpose, `right.q = key:H`. Start the stream again.

- The headset shows "bindings.ini: 1 errors, see log" (or "N errors") once, when the stream starts.
- Y types F into the editor (and no longer opens the keyboard). X types G in that app, and toggles 3D in other apps.
- Open the flat launcher's Settings, Controller bindings page (PLAN 2.6), change one control there, restart the stream, and check the new binding is live: the page edits the same file the headset reads.
- Take the wrong line out afterwards.

Good looks like: defaults unchanged, your remap in force after the stream restart, the app section only in its app, the error toast once.

Record: each default worked yes or no, Y typed F, the app section scoped, the error toast text, the launcher page changed the same file, Guide shows.

## 3. Scroll and click steadiness (1 minute)

Source: PLAN 2.7 and 2.8; USER_GUIDE "Pointer mode".

- Scroll a long web page with the thumbstick: smooth, not stepped. Lean sideways in a wide page or a spreadsheet: it scrolls sideways. In the flat launcher's Settings the "Scroll speed" row (`[vr] scroll_speed`, 25 to 400 %) changes how fast; try 50 and 200 if you have the time.
- Cog, Display tab, "Click steadiness": Off, Normal, Steady. The Display tab now has 15 rows: are they still readable and easy to hit? Click a small target (a checkbox, a window close button) with each setting. Off sends the aim as it is, Normal sends the click where you aimed before the pull moved the controller, Steady also keeps the cursor still from the first touch of the trigger. Double click a file name with each.

Good looks like: smooth scroll both ways, three distinct feels, no clipped rows.

Record: vertical scroll smooth yes or no, sideways yes or no, three steadiness feels distinct yes or no and which you prefer, Display tab readable yes or no.

## 4. Host sheet, macros, sticky modifiers, media keys (2 minutes)

Source: PLAN 4b.1, 4b.2, 4b.13 and 3.11, lane wave4b-host; [host.md](host.md); USER_GUIDE "Host: keys for the PC".

The Host button is the outermost one on the right of the bar; it opens the Host sheet. Press each cell and see that Windows does the right thing:

- Esc, Win (Start menu), Win+D (desktop), Alt+Enter (full screen in a game or video), Ctrl+Alt+Tab (the switcher stays up), Shift+Tab (Steam overlay, if Steam is running), Win+G (Game bar), Win+Shift+Left (moves a window to the other monitor), Ctrl+V (paste), Ctrl+Shift+Esc (task manager).
- Alt+F4 asks first: Send or Cancel. Cancel it, then send it at the end on a throwaway window.
- Send keys...: opens the keyboard waiting for one combo. Tap Ctrl, then a letter: it goes once, as a chord, and not again.
- Your macros: each cell fires (Snip opens the snipping bar, Paste and send pastes then presses Enter after 150 ms).

On the Compact keyboard: tap Shift once and type a letter (one capital, then it lets go); hold Shift for half a second (it turns blue with an underline and locks) and type several letters (capitals stay); tap it again to release. Do the same with Ctrl and a letter (a locked Ctrl is held on the PC).

Media row, on both keyboards (Compact and Full): previous, play/pause, next, mute, volume down, volume up. With the Windows volume mixer visible, mute, vol- and vol+ must change the PC's volume. If vol- or mute do nothing, say so: the keys have to be sent as extended keys for Windows to take them.

Good looks like: every cell does what its name says, chord once, lock visible, volume moves.

Record: any Host cell that did nothing or the wrong thing, macros fire yes or no, one-shot and lock yes or no, volume keys change the PC's volume yes or no on each keyboard.

## 5. Layout: presets, Bring here, Remember per app, Follow, angular lock (3 minutes)

Source: PLAN 3.1, 3.2, 3.3 and 3.12, lane wave3-layout; [layout.md](layout.md) "Headset checks"; USER_GUIDE "Screen" and "Follow and comfort".

Cog, Screen tab:

- Preset row: Desk, Couch, Cinema. Each sets the distance and the width only (Desk 120 cm and 160 cm, Couch 250 and 260, Cinema 400 and 500) and leaves tilt, roll and curve alone. Does the ring on the row follow when you move the screen by hand to about the same size?
- Bring here: look hard to one side, press the cell. The screen arrives where you look, at the same distance and the same size as before, facing you.
- Remember per app: lay out the current app (move it, resize it), stream the second app and lay it out differently, quit and relaunch each. Each comes back with its own layout. Press Forget on one: it starts from what is in force the next time. Off: it stops remembering.
- Angular-size lock (default On): grip the move pill and push or pull with the thumbstick; the picture should keep the same size in your view. Set it Off and compare: the picture shrinks as it goes away.

Display tab, "Head locked": its third cell is Follow. With it on, the screen stays where it is until your head is past the leash (25 degrees, in yaw or pitch), then eases after your gaze over about 0.6 s and stays. Look around inside the leash: nothing moves. Open a panel or hold the screen: it waits. **Comfort:** leave Follow on for the next ten minutes of the run (through items 6 to 8) and note how you feel. Close the stream and set `follow_leash_deg` to 40 and then 60 for a later stretch if the 25 is too busy.

Good looks like: presets that look right at a desk, on a couch and for a film; Bring here that lands in front of you; two apps that each keep their own; a size lock that holds the apparent size; a Follow that does not make you queasy.

Record: preset sizes right yes or no, Bring here yes or no, two apps remembered yes or no (and Forget worked), angular lock yes or no (and Off differs), Follow comfort over the ten minutes (fine, uneasy, how long to uneasy), the leash you prefer.

## 6. HUD, battery, slow connection badge, report zip (2 minutes)

Source: PLAN 3.4, 3.7, 3.10, 3.14 and 4b.15, lane wave3-hud; USER_GUIDE "Stats styles, battery and the slow connection badge" and "About".

- Stats. Cog, Display tab, Stats On. With `stats_style = hud` (or `stats = hud`) in `config.ini`: do the numbers look plausible next to the flat launcher's stats overlay from an earlier run (latency with its parts, round trip, loss, bit rate, frame rate, compositor frame times)? With `stats_style = lite` you get one line: `54 Mb/s | 11 ms | dec 3.1 | loss 0.2% | 90 fps`.
- Battery. Open the cog: the clock strip shows the battery cell, the percentage and a bolt while charging. Compare the percentage with the one in the SteamVR dashboard or Steam overlay.
- Slow connection badge. It comes up after 5 seconds of a poor link or more than 1.5 % of frames lost. If it does not come up by itself, walk toward the edge of the Wi-Fi range for a few seconds (item 7 will do this too); the HUD line starts with `!` or says slow connection. It goes after 5 clean seconds.
- Report a problem. Cog, About, Report a problem, type a note, Save and zip. A toast shows the zip path (`~/Games/MoonlightXRNative/reports/mlxr-report-<date>.zip`); the zip holds `ray.csv` when `MLXR_RAY_DEBUG=1` is set.
- Compositor counters. With stats on, `perf-<date>-<time>.csv` next to `vrtiming.log` should fill every 5 seconds. The log line `performance metrics, first reading:` lists the compositor counter names; they have not yet been seen on SteamVR.

Good looks like: HUD numbers that agree with the launcher's, a battery that matches SteamVR's, a badge that comes and goes, a zip with the toast.

Record: HUD style plausible yes or no, lite line yes or no, battery % and bolt match yes or no, badge up and down yes or no, zip made and toast shown yes or no, `ray.csv` in the zip yes or no.

## 7. Reconnect: pull the Wi-Fi (2 minutes)

Source: PLAN 3.5, lane wave3-session; USER_GUIDE "When the connection drops".

During the stream, with a game or a video running on the PC, cut the Frame's connection: turn Wi-Fi off in SteamOS, or take the router's cable out, or walk out of range. Leave it off for about 3 seconds, then give it back.

- A toast reads "Connection lost, reconnecting (1/2)..." and the picture freezes. The room and the screen stay where they are. The first try comes 2 seconds after the drop; the second 5 seconds after the first fails.
- When a try works, the picture comes back and nothing more is said. Check that the PC's app was not started a second time.
- Repeat, and this time keep the Wi-Fi off for 15 seconds: both tries fail and the flat window comes back (`not trying again ... back to the launcher`). Resume Game in the flat launcher goes back in.
- Repeat once more and press Exit, then Disconnect while it is trying: it stops (`reconnect: cancelled`).

Good looks like: the toast, the room and screen held, a silent return, a clean give-up, a clean cancel.

Record: toast shown, the screen and room stayed put yes or no, the stream came back on try 1 or 2 or not, how long the picture was away, cancel worked, no second copy of the app.

## 8. Presence: headset off 10 seconds and 70 seconds (3 minutes)

Source: PLAN 3.6, lane wave3-session; USER_GUIDE "Taking the headset off".

Stream something with sound. Take the headset off for 10 seconds, put it back on.

- The sound mutes at once, whatever you held on the PC is let go (a key from the headset keyboard, a mouse button), and the stream carries on when you put the headset back. The audio returns.

Take it off again, this time for 70 seconds (put it somewhere it will sense "off": on a table, facing up).

- After 60 seconds (`doff_disconnect`) the stream disconnects and the flat window comes back; the PC's app is still running. Put the headset on and press Resume Game: the stream returns.
- This is the first time `XR_EXT_user_presence` has been tried on SteamVR, so also note whether the headset's off state was seen at all. If the log has no `user presence:` line, the headset-off handling may be coming only from the other path; say so.

Good looks like: mute and release at once, unmute on return, disconnect at 60 s with the PC's app alive, Resume Game working.

Record: the 10 s run: muted yes or no, input released yes or no, stream continued yes or no; the 70 s run: disconnect time (seconds), app still on the PC yes or no, Resume Game yes or no; `user presence:` lines in the log yes or no.

## 9. Wave 1 leftovers (3 minutes)

Source: HEADSET_TEST_6 items 3, 4, 5 and 7, not covered in run 1 because of the stream quality. Run these in the stream from the Resume Game of item 8.

- **Surround and mute.** Stream something with a centre channel that is obvious (a trailer with dialogue), virtual surround on (`virtual_surround = true` under `[stream]`). Turn your head 90 degrees left: the dialogue moves to your right ear; right: the reverse. Look past the edge of the picture, slowly and quickly: no snap. Open the SteamVR dashboard: silent within a frame or two; close it: the sound returns. Open and close it three times fast: no stutter, no stuck mute.
- **Guide chord and rumble.** In gamepad mode, hold left View and right Menu for a quarter of a second: the tester shows Guide and Start/Back are not stuck afterwards. Use the tester's vibration test: is rumble felt at all? In both hands for a one-motor effect? Short pulses?
- **Notices row.** The Display tab's Notices cells (All, Important, Off): readable and easy to hit? All and the bar's Gamepad button: a toast for about 2.5 s, 0.22 m below where you look. Important: "Display N Hz" and 3D toasts gone, mode and error toasts stay. Off: none. Put it back on All.
- **Faint resting beam.** Set Ray to Off in the cog and close it: the beam vanishes. Point below the screen at the bar band: the buttons appear and a faint full-length beam shows; move away and it goes. Nothing shows while pointing at the picture. Open a panel: the full beam is back. With Ray set to Dot, no faint beam.
- **Cog opens on the last tab.** Press the 3D tab, close the cog, reopen: it opens on 3D. After a restart (`[display] cog_tab`) it does again.
- **Exit prompt.** Press the bar's Exit during the stream: Quit app, Disconnect, Cancel. Cancel first. Then Disconnect (the PC's app keeps running and the flat window returns; exit code 11), Resume Game, Exit again, Quit app (the PC's app closes; exit code 0).

Record: surround direction right or swapped, smooth or snapping, mute and unmute for the dashboard worked, any stuck mute; Guide shows, Start/Back stuck yes or no; rumble felt (both hands, one hand, none); Notices cells readable and each level as expected; faint beam only in the bar band; cog last tab kept across a restart; exit prompt: the three choices and both outcomes.

# Part B: accessories (optional, 15 to 20 minutes)

Do any of these that you have the hardware for. They are independent.

## B1. Bluetooth gamepad (8 minutes)

Source: PLAN 2b.2, 2b.4, 2b.5 and 2b.7, lane wave2b-padmouse; USER_GUIDE "A gamepad as the PC's mouse" and "Frame controllers and a gamepad together"; [input.md](reports/input.md) "Owner test".

Pair the pad in SteamOS (a DualSense for the gyro part). Start a stream. Have the gamepad tester open on the PC.

- **Mouse emulation.** Hold Start for a second: a toast says the gamepad is a mouse. The left stick moves the PC's cursor (slow start, fast sweep), the right stick scrolls, A/B/X click left/right/middle, LB/RB go back/forward, the d-pad is the arrow keys, Y opens the headset keyboard. The tester shows the pad at rest meanwhile. Hold Start for a second again: "Gamepad is a gamepad again" and the sticks work in a game. Take the headset off or open the dashboard while a button is held: nothing stays down on the PC. With Notices at Off, no toast. Try `pad_mouse_chord = start_back` if you have time.
- **Arbitration.** Put the Frame controllers in gamepad mode: the tester shows one pad. Press a button on the Bluetooth pad: the Frame's pad disappears and the Bluetooth pad is the only one (`pad_arbitration = pointer`, the default). Switch gamepad mode off and on: the Frame pad is back until the pad is used again. Set `pad_arbitration = sleep`: the Frame pointer also rests for 20 seconds after the pad was used. Set `also_gamepad`: both stay.
- **Flip face buttons.** Turn "Flip face buttons" on: the physical pad's A and B swap live, with no restart.
- **Gyro caps (2b.2).** With a DualSense or a Switch Pro: the arrival line in `moonlight.log` has caps with `0x30` set (accelerometer and gyroscope). The PC sees a PlayStation pad (Apollo's DS4 emulation with ViGEmBus). Enable gyro in Steam Input on the PC: the log says `the host asks for the gyroscope at N Hz`; tilting the pad moves the PC's gyro. Battery and the touchpad also reach the PC. The Frame controllers in gamepad mode never ask for motion.

Record: mouse mode toast, each control in mouse mode, Start again gives it back, release on focus loss, arbitration (pointer, sleep, also_gamepad), flip live yes or no, caps value, PC sees a PlayStation pad yes or no, gyro rate asked, battery and touchpad reach the PC.

## B2. USB data cable (7 minutes, plus 30 for the drift)

Source: PLAN 4c.2 and 4c.4, lane wave4c-usb; [usb-cable.md](reports/usb-cable.md) "Headset test"; USER_GUIDE "USB cable".

A charge-only cable has no data lines and gives no carrier. Plug the data cable from the Frame's USB-C port into a USB port on the PC.

- On the PC a new USB Ethernet adapter appears, with an address next to the Frame's. Set its profile to Private. `ping` the Frame's `usb0` address.
- The flat launcher, Settings, Connection, Network connection: the "USB cable" entry says "Cable detected" within 10 seconds.
- Start a stream (Automatic or USB cable). The headset shows "Connected over USB cable." On the Frame, `moonlight.log` has the `network path:` lines (below).
- Pull the cable mid-stream: `USB cable link lost while streaming over it`, and the reconnect of item 7 kicks in (the digest expects it; the lane's own notes still say no reconnect, so say which happened).
- Choose "USB cable" with the cable out and start a stream: it uses Wi-Fi and shows the "USB cable not reachable (...). Using Wi-Fi." notice.
- **Battery drift (4c.4).** With the stream up on the cable, note the battery % (cog clock strip, or `status` on the Frame) at the start and after 30 minutes. Does it charge, hold or drain?

Record: adapter appeared, ping answered, "Cable detected" in under 10 s, the toast, the lines, round trip against Wi-Fi if the log shows both scores, cable pull behaviour, battery at start and after 30 minutes.

## B3. AZERTY and QWERTZ (5 minutes)

Source: PLAN 2.10, lane wave2-azerty; USER_GUIDE "AZERTY and QWERTZ".

The PC has to be on the matching Windows keyboard layout (Settings, Time and language, Language and region, Typing). Set it to French (France), then in `config.ini` `[vr] keyboard_keymap = azerty` (or the flat launcher's "Headset keyboard layout"), restart the stream.

- Compact: type `é`, `a`, `m`, a digit through Shift (the number row is symbols; Shift gives digits), `ù` from the symbols sheet. Full: the `<` key beside the left Shift, and right Alt followed by `à` gives `@`.
- Switch the PC to German (Germany) and `keyboard_keymap = qwertz`: type `ß ä ö ü Ä Ö Ü`, check that z and y are swapped, Shift+7 gives `/`.
- The report sheet's note field always shows QWERTY keys; that is expected.

Record: each character that came out right or wrong on each layout, with the layout the PC was on.

## Read the logs (at a desk, 5 minutes)

On the PC, pull the files in one go (the plain logs hold addresses; only the zip is masked):

```sh
tools/frame-deploy.sh logs out
```

Copy the ray CSV and the stats CSV too, which the command does not fetch:

```sh
scp steamos@<frame>:'~/Games/MoonlightXRNative/reports/ray-*.csv' steamos@<frame>:'~/Games/MoonlightXRNative/perf-*.csv' out/
```

Then, against `out/moonlight.log` (on the Frame use `~/Games/MoonlightXRNative/moonlight.log` instead):

```sh
# Before you start: the per-eye size and what the stream ran at
grep -E "curved picture: projection layer|stream: [0-9]+x[0-9]+" out/moonlight.log | tail -4
grep -E "udp rcvbuf errors|Unrecoverable frame" out/moonlight.log | tail -6
# Item 1: model kinds
grep -E "controller model .* from the panel|controller model ready:|controller models: left|is not a mesh the renderer reads" out/moonlight.log | tail -20
# Item 2: bindings
grep "bindings" out/moonlight.log | tail -12
# Item 3: click steadiness
grep "click steadiness" out/moonlight.log | tail -6
# Item 4: Host sheet and macros
grep -E "macros.ini|host sheet open|host sends|host: send keys" out/moonlight.log | tail -20
# Item 5: layout
grep -E "screen brought here|follow: the head is past the leash|layout kept for this app" out/moonlight.log | tail -20
# Item 6: HUD, battery, slow connection, compositor counters
grep -E "^.*battery: [0-9]+%|slow connection badge|performance metrics" out/moonlight.log | tail -12
# Item 7: reconnect
grep "reconnect:" out/moonlight.log | tail -20
# Item 8: presence
grep "presence:" out/moonlight.log | tail -12
# Item 9: Guide, rumble, exits
grep "gamepad: Guide button" out/moonlight.log | tail -4
grep -E "rumble: the host's first request|rumble on the (left|right) controller" out/moonlight.log | head -10
grep -E "The VR client exited with|flat window" out/moonlight-launcher.log | tail -6
# Part B
grep -E "gamepad #[0-9]+ (arrived|is in use)|asks for the (gyroscope|accelerometer)|controller pointer is parked|its pointer resumes" out/moonlight.log | tail -20
grep -E "network path:|USB cable" out/moonlight.log | tail -20
grep "MLXR_LAUNCHER_USB" out/moonlight-launcher.log | tail -4
```

What each should show:

- `curved picture: projection layer 3408x3408 per eye (recommended 3408x3408)`. A 2160 here means the dev shortcut's SteamVR block is wrong (see "Before you start"). `stream: 2560x1440 90 fps, 98000 kbps` for the settings above.
- Item 1: `controller model ready: <kind> asked, <kind> drawn ...` after each cell; for Custom, `custom asked, custom drawn`, and after the file is gone the asked kind is custom and the drawn kind is frame. `controller models: left drawn, right drawn`.
- Item 2: `bindings: pointer mode right.y = key:F; gamepad mode as the defaults` and one `bindings: <file>:<line>:<col>: what` for the wrong line.
- Item 3: `click steadiness <off|normal|steady>: press stabilise N, double click ...` for each cell you pressed.
- Item 4: `macros.ini: N macros`, `host sheet open`, `host sends <label>`, `host: send keys, waiting for a combo`.
- Item 5: `screen brought here: N m away, N m wide`, `follow: the head is past the leash, the screen comes after it`, and `layout kept for this app: N cm away, N cm wide` followed by `layout kept for this app put back: ...` when an app is streamed again.
- Item 6: `battery: N% (charging|not charging)`, `slow connection badge up (loss N%, rtt N ms, status ...)` then `slow connection badge down`, and `performance metrics, first reading: <names>` (or `none valid yet`, or `performance metrics: the runtime lists no counters`).
- Item 7: `reconnect: connection lost (code N), try 1 of 2 in 2 s`, then `reconnect: try 1 of 2: resuming app N on ...`, then `reconnect: the stream is back after try 1`, or `reconnect: try 1 failed: ...` followed by `reconnect: not trying again (code N...): back to the launcher`. `reconnect: cancelled` after Exit during a try.
- Item 8: `presence: headset off: audio muted, input released; disconnecting in 60 s if it stays off`, then `presence: headset back on after 10.x s: audio unmuted`. For the 70 s run: `presence: headset off for 60 s (doff_disconnect): disconnecting, the host's app keeps running`. Also `user presence: XR_EXT_user_presence enabled, the system has the sensor` (or `not offered by this runtime`) and `user presence: headset on|off`. The last has never been seen on SteamVR.
- Item 9: `gamepad: Guide button`; `rumble: the host's first request: low motor N ...` (absent means the PC sent no rumble) and `rumble on the left controller ... runtime said XR_SUCCESS` for each hand that buzzed; `The VR client exited with 11` for Disconnect, `0` for Quit app.
- Part B1: `gamepad #N arrived: ... caps 0x...` with `0x30` set for a pad with a gyroscope; `gamepad #N is in use: gamepad mode's pad #M is taken off the host (pad_arbitration)`; `gamepad #N: the host asks for the gyroscope at N Hz`; `mouse used: the controller pointer is parked` and `controller aim moved ...: its pointer resumes`.
- Part B2: `network path: USB cable: usb0 up, carrier on, N peer candidates`, `network path: chose USB cable: score ... ms against Wi-Fi ... ms`, `network path: using USB cable <address> via usb0 (setting ...)`; after the pull `USB cable link lost while streaming over it: ...`; `MLXR_LAUNCHER_USB: cable on usb0` in the launcher log.

The sources of these lines: [ray-debug.md](ray-debug.md) (the recorder), [layout.md](layout.md), [host.md](host.md), [bindings.md](bindings.md), [reports/input.md](reports/input.md) and [reports/usb-cable.md](reports/usb-cable.md).

## Results

Copy this table into your report and fill it in. Start it with the settings: stream size and bitrate, SteamVR per-eye size from the log, and the Vibepollo version.

| # | Item | Result (fine, or what was wrong) | Time of the run |
|---|---|---|---|
| 0 | Settings: stream size/bitrate, per-eye size in the log, Vibepollo version | | |
| 1a | Show controllers Off/Frame/Generic/Gun swap live, no hitch | | |
| 1b | Gun: barrel on the ray, muzzle on the dot, trigger rotates, left is its own mesh; fit (how far off) | | |
| 1c | Custom model: right drawn, left mirrored, trigger moves; fall back after delete with a log line | | |
| 1d | Ray and model vanish and return together | | |
| 2a | Defaults unchanged (trigger, A/B, d-pad, stick clicks, Y, X, d-pad up, Menu, bumpers) | | |
| 2b | `right.y = key:F` types F; `[app]` section only in its app | | |
| 2c | Error toast "bindings.ini: N errors, see log" | | |
| 2d | Launcher Controller bindings page edits the same file | | |
| 2e | Gamepad mode, Guide chord from the bindings | | |
| 3a | Smooth vertical and sideways scroll; scroll_speed | | |
| 3b | Click steadiness Off/Normal/Steady feel distinct; Display tab (15 rows) readable | | |
| 4a | Host sheet: each cell does the right thing; Alt+F4 asks first | | |
| 4b | macros.ini fires; Send keys chord once | | |
| 4c | Sticky modifiers: one-shot, lock (blue, underline), release; locked Shift keeps capitals | | |
| 4d | Media row on both keyboards changes the PC volume | | |
| 5a | Presets Desk/Couch/Cinema; Bring here | | |
| 5b | Remember per app: two apps, Forget, Off | | |
| 5c | Follow: leash, ease, pause under panels; 10 minute comfort; leash 40 and 60 | | |
| 5d | Angular-size lock On versus Off | | |
| 6a | HUD and lite numbers plausible | | |
| 6b | Battery % and bolt match SteamVR; `battery:` line | | |
| 6c | Slow connection badge up and down | | |
| 6d | Report a problem, Save and zip: toast, `ray.csv` in the zip | | |
| 6e | Compositor counter names on SteamVR; perf CSV fills every 5 s | | |
| 7a | Wi-Fi pulled 3 s: toast, room and screen stay, silent return | | |
| 7b | Wi-Fi pulled 15 s: give up, flat window, Resume Game | | |
| 7c | Exit during retry: `reconnect: cancelled` | | |
| 8a | Headset off 10 s: mute, release, back on, stream continues | | |
| 8b | Headset off 70 s: disconnect at 60 s, PC app running, Resume Game | | |
| 8c | `user presence:` lines on SteamVR | | |
| 9a | Surround turns the right way, no snap; mute with the dashboard and with the headset off | | |
| 9b | Guide chord shows Guide; Start and Back not stuck | | |
| 9c | Rumble felt (both hands, one hand, none) | | |
| 9d | Notices cells readable; All, Important, Off | | |
| 9e | Faint resting beam with Ray Off, only in the bar band | | |
| 9f | Cog opens on the last tab, also after a restart | | |
| 9g | Exit prompt: Quit app, Disconnect, Cancel, exit codes 0 and 11 | | |
| B1a | Pad as mouse: Start 1 s, each control, back again | | |
| B1b | Arbitration: pointer, sleep, also_gamepad; one pad on the PC | | |
| B1c | Flip face buttons applies live to the physical pad | | |
| B1d | Gyro caps `0x30`, PC sees a PlayStation pad, `asks for the gyroscope`, battery and touchpad | | |
| B2a | USB adapter on the PC, Private, ping the Frame | | |
| B2b | Launcher "Cable detected" in 10 s; `MLXR_LAUNCHER_USB: cable on usb0` | | |
| B2c | Stream log lines and toast "Connected over USB cable." | | |
| B2d | Cable pulled mid-stream: link lost line, reconnect | | |
| B2e | Battery over 30 minutes wired: charges, holds or drains (start and end %) | | |
| B3a | AZERTY: é a m, digit via Shift, ù, `<`, right Alt + à = @ | | |
| B3b | QWERTZ: ß ä ö ü Ä Ö Ü, z/y swapped, Shift+7 = / | | |
| L | Log lines from the greps above (only if they carry no addresses or names) | | |

## What to report

- The results table, and the three settings from item 0 at the top.
- The time of each run, so it can be found in the log.
- The output folder of `tools/frame-deploy.sh logs` (the masked zip is the one safe to share), and the ray CSV only if the ray glitched.
- If the stream was choppy (`Unrecoverable frame` storms, rtt over 10 ms in the 5 second `xr: latency (stream):` lines): say so first, with the resolution, the bitrate and the Vibepollo version, before any of the item results.
