# Headset test 8 (batch 4: seventeen lanes merged on 2026-10-30)

For the dev build on main after the wave6-i18n merge (check `app-dev/VERSION`; the first line must name a commit from 2026-10-30 or later), installed with `tools/frame-deploy.sh install` (the dev copy, `~/Games/MoonlightXRNative/app-dev`). [HEADSET_TEST_7.md](HEADSET_TEST_7.md) has never been run and still applies to this build; run it first if you can, or at least its "Before you start" box, which is not repeated here. This document covers only what landed since: Part A is about 35 minutes in the headset with no accessories, plus 5 at a desk before and 10 after to read the logs. Part B needs a Bluetooth gamepad, a second network path (the Wireless Adapter or a 5 GHz and a 6 GHz network), and the PC. Part C is desk work in the flat launcher. Do not paste addresses, host names or Wi-Fi names from the logs into a report.

Items in Part A are in the order to do them. Language (12) and the PC VR switch (13) come last because they end the stream; the burst and failover items (B4, B5) need a bad spot and are in Part B so a good run of Part A is not spoilt by the walk.

The dev build and the main one share `moonlight.log` and `~/.config/moonlight-xr/config.ini`. The log is `~/Games/MoonlightXRNative/moonlight.log`.

## Before you start

- **Stay on 5 GHz for Part A.** The hitching of 2026-10-29 was the Frame on the 6 GHz network; on 5 GHz (80 MHz) it was fine. Put the Frame's home network on 5 GHz, leave the launcher's Network connection on Automatic, and write down which path the log picks (`network path: chose ...`). The Wireless Adapter is not better by name: Automatic measures the paths and the owner has seen the adapter lose to a good 5 GHz link. The 6 GHz network and the adapter are what Part B4 and B5 are for.
- **Launcher stream settings: 2560x1440, 90 fps, 98 Mbps**, as in test 7. The Video bitrate row now goes to 500 Mbps; leave it at 98 until item C2, and write down what you ran at.
- **The host is Vibepollo 2.0.1** (upgraded on 2026-10-30; the first streams on it are these). The Apollo-only items (8, 9) assume it. If the first minute of the log shows `Unrecoverable frame` storms, compare against the published 1.0.0 in the same spot before going on.
- **The per-eye size check from test 7 still holds:** `curved picture: projection layer 3408x3408 per eye`. A 2160 means the dev shortcut has the old block; stop and run `tools/frame-deploy.sh shortcut --sync-settings`.

## 0. Before you put the headset on (5 minutes, not counted)

Check which build you are about to run:

```sh
cat ~/Games/MoonlightXRNative/app-dev/VERSION
tools/frame-deploy.sh status
ls /usr/share/fonts/noto-cjk/
```

Write down whether `NotoSansCJK-Regular.ttc` (and a Medium file) exist in that folder: item 12 depends on it.

Set up the files the items use (all in `~/.config/moonlight-xr/`; the app is closed, so nothing is overwritten):

- `config.ini`: under `[vr]` set `first_run_shown = false` (item 1). Under `[stream]` leave `rfi_idr_ms` and `failover` out (defaults; B4 and B5 set them later). Under `[debug]` nothing yet (`eye_swap`, `depth_threads` and `file_log` come up in items 6, 7 and C2).
- `bindings.ini`: under `[pointer]` add `right.y = app:zoomin`, `right.x = app:zoomout` and `left.dpad_left = app:zoomreset` (item 4). Y no longer opens the keyboard during this run; the bar's keyboard button still does. Take the lines out afterwards.
- The flat launcher, Settings: Connection, "Full-frame requests after lost video" reads 500 ms (default; do not change it yet), "Switch network when video keeps breaking up" is Off; Headset language is Automatic.

On the PC: Desktop at 2560x1440, a text editor, Notepad, a web page with small controls and a long scroll, a gamepad tester (https://hardwaretester.com/gamepad), a half-width side-by-side 3D video if you have one (item 7; a top-and-bottom one too if you have it), a game or emulator with a mouse cursor for the lightgun (item 3; the desktop cursor will do). In Apollo's web UI, Clients, check the Frame has Server Command allowed and that at least one harmless server command exists (Notepad is a good one).

Write down the VR settings you start with: Hand tracking, Gaze, Controller model, head locked or room, Ray, Notices.

# Part A: no accessories (about 35 minutes in the headset)

## 1. First-run card (2 minutes)

Source: PLAN 3.9 and 3.15, lane wave3-cards; USER_GUIDE "The first-run card".

Start the stream. Once the picture is up, a card "Controls at a glance" hangs over the middle of the picture.

- Check each row against the real controllers: Menu on the right opens the cog, Y opens the keyboard (not during this run: it zooms, item 4), left View + left grip switches to gamepad mode, the bar's Host button, Ctrl+Alt+Shift+Q.
- Leave it: it goes by itself after about 20 seconds. Set `first_run_shown = false` again later (or the launcher's "Controls card on the next stream" switch) and on that run open the cog while the card is up: it goes at once.
- Pull the Wi-Fi for 3 seconds (test 7 item 7) later in the run: the card does not come back after the reconnect. `config.ini` ends with `first_run_shown = true`.

Good looks like: one card, readable at the stream's size, correct rows, gone after 20 s, gone at once on the cog, never twice.

Record: placement and readability, any row that does not match the controller, time until it went, whether it came back after a reconnect.

## 2. Splash: connecting and reconnecting (2 minutes)

Source: PLAN 6.3, lane wave6-splash; USER_GUIDE "Ending a stream", "When the connection drops".

You saw the start already: between the app list going away and the first frame, the Moonlight XR icon with "Connecting to <PC name>…" on a dark pill hangs about 1.5 m ahead, head-locked, and fades out over about a third of a second when the picture appears.

- Disconnect and start again from the flat window (the hand-off): the logo and line show from stream-up until the first frame, then fade. Note whether there is a long empty void before the logo (the connect handshake itself is not covered).
- Open the cog (or the bar and the keyboard) while the logo is up and before the first frame: nothing flickers; the splash hides neither the pointer nor the panels.
- During the stream, pull the PC's network (not the Frame's) for about 3 s: the picture goes, the room and the screen place stay, "Reconnecting…" shows with the logo, plus the usual "Connection lost, reconnecting (1/2)…" toast when Notices allow it; on resume the logo fades. Pull it for 20 s: both tries fail, the flat window comes back and no splash is left behind.
- With the busiest layout (a room, glow on, controller models on): nothing in the log about more than 16 composition layers during the fade.

Good looks like: a logo while nothing else is there, a visible fade, never a logo over a live picture.

Record: logo seen at stream start and at hand-off yes or no, ever shown after the picture was there, fade visible (it is a 5-step fade) yes or no, how long "Reconnecting…" stayed, what showed when both tries failed.

## 3. Lightgun (3 minutes)

Source: PLAN 2.3, lane wave2-lightgun-gyro; USER_GUIDE "Lightgun mode".

Cog, Display tab, Show controllers = Gun. With `lightgun = auto` (the default) that turns the lightgun policy on. Point at a game or the desktop cursor.

- The ray never sleeps between shots (with Frame it retires after 5 s still). Clicks land at the dot with no wait. Every shot gives a short, strong buzz in that hand.
- A trigger pull on the pill under the picture does not move the screen; a grip pull does.
- Set `[vr] lightgun_reload = true` (config.ini, or the launcher's Input Settings) and restart the stream: a pull started off the picture is a right click while held (the reload); one started on the picture is a left click even if you swing off it.
- Set `lightgun = off` with the Gun model still chosen: the old pointer behaviour is back (sleeping ray, click steadiness). Put it back to `auto`.

Good looks like: a gun that always shows its aim, shots that land where the dot is, a buzz per shot, reload only from off-screen.

Record: ray stayed awake yes or no, click lag felt yes or no, buzz per shot (both hands), pill behaviour with trigger and grip, reload behaviour, `lightgun pointer on: ...` and `recoil on the right controller: runtime said ...` from the log.

## 4. Zoom (3 minutes)

Source: PLAN 4b.6, lane wave4b-zoom; USER_GUIDE "Zoom and pan".

With the bindings from item 0 (Y zoom in, X zoom out, left d-pad left reset), in pointer mode, Frame model (or Off):

- Each press is one rung: 100, 125, 150, 175, 200, 250, 300, 350, 400 %, smooth, and reset shows the whole picture again. The zoom is centred on where you point.
- Point at the top-right corner of the desktop and zoom in four times: the corner fills the screen, the host cursor lands under the dot, and clicks hit what you see. Look at the edge pixels. The click-hold and double-click radii at 400 % may feel four times too wide: that is known (they are in host pixels), note whether it bothers you.
- 300 % in the middle of a letterboxed film with glow on: the glow takes the on-screen colours, shifts on zoom and reset, and no black band shows; a corner that is all letterbox bar gives no black or flicker and settles at once.
- Zoom in app A, quit it, start it again: still zoomed. Start app B the first time: the zoom stays in force. Screen tab, Forget: the entry is gone; `config.ini` has `[screen.<hash>]` with `zoom`, `zoom_x`, `zoom_y` while it is remembered.
- With the SBS video of item 7 at 200 % in the middle: both eyes show the same part at the right depth, no bleed from the other eye; at the convergence ends no seam; the pointer lands right. Note the softness (1080p at 400 % is 480x270 real pixels). With the depth model at 200 %: the depth lines up and does not slide.

Good looks like: nine rungs that feel like one motion, a corner that fills the screen with the cursor under the dot, no black bands, zoom kept per app.

Record: rungs smooth yes or no, any host-pixel offset at 400 %, glow and letterbox behaviour, per-app memory across a restart, SBS and depth at 200 %.

## 5. Picture tab: Background dim, Void colour, Glow edge (3 minutes)

Source: PLAN 3.8, 3.13 and 4b.20, lane wave3-visual; USER_GUIDE "Picture".

Cog, Picture tab.

- Background (0 to 100 %) in a room (Cinema): the walls, floor and the screen's spill fall to black as you drag; the picture stays at full brightness; at 100 only the picture is left. Reset is 0. In passthrough: 50 % about half as bright, 100 % hidden behind black, the picture not dimmed, the hand and controller rays still show. In the void: 50 % half as bright, 100 % black.
- Void colour: set `[vr] void_color = #204060` (config.ini, or the launcher's Void colour row), void environment: a solid blue; `#000000` is as before; Background 50 % darkens it. Look for banding or flicker at the screen's edge.
- Glow edge (glow on, a bright picture with a dark border): Off is exactly as before; Soft fades the outer edge into the glow with no hard border; Wide a wider band. Look for a sliding glow, a seam or a dark ring, also at a low glow level.
- Set the three rows, restart the stream: they come back. The launcher's rows read and write `[vr] background_dim`, `[vr] void_color` and `[display] glow_feather`.

Good looks like: a dimmer that only touches the surroundings, a blue void with no bands, a glow edge with no ring.

Record: the Background value that looks right in a room and in passthrough, banding yes or no, Soft or Wide and any artefact, persistence yes or no.

## 6. 3D tab: ten rows and the depth model (5 minutes)

Source: PLAN 5.1, 5.2, 5.4, 5.6 and 3.13, lanes wave5-depth, wave3-tidy and wave3-visual; USER_GUIDE "3D", "Edges".

Cog, 3D tab, 3D on with the depth model running (source Depth).

- The tab has ten rows and Reset clear under the last; the readouts left of the new tracks read "0%", "100%", "100%", "0%", "0%" at the defaults; nothing overlaps.
- Edge: drag it and step it with the chevrons (0, 25, 50, 75, 100). Hair or a thin outline against a distant background holds its own depth at 75 to 100, with no new flicker or blocky edges; the picture changes while you drag. Record the value you prefer.
- Depth detail and Depth shape are live during the drag: shape flattens the scene toward the screen plane with the detail kept; detail changes the separation. Depth detail 150 with Depth shape 100: a face pops further from the wall; shape 50: the whole picture flattens.
- Motion reset at 10 in a fast pan or a fast scroll: no trail of old depth behind moving edges, and still areas do not flicker more than at 0. Record whether 5 flickers.
- Eye balance +100: only one eye moves, the total depth is the same; -100 the other way; cover each eye to check. Record the comfortable value and any double vision.
- One-eye fuzzy edge: look at a thin dark object on a bright page (the text cursor, a thin window border). Close each eye in turn: the edge is about equally sharp in both eyes, no several-pixel smear on one side in one eye. Then put `MLXR_HOLE_FILL=0 %command%` in the dev shortcut's Launch Options and restart: the old smear is back on one side in one eye. Record side and eye, and any halo or tearing next to the object. Take it out again.
- Threads: `[debug] depth_threads = 2`, then 3 (restart the 3D view each time). With stats on, record map rate and age, CPU load and temperature, and whether motion feels less laggy at 3.
- Restart the stream: `edge_sharpness`, `depth_scale`, `depth_global`, `motion_reset` and `eye_balance` persist under `[3d]`.

Good looks like: ten readable rows, live drags, thin things that keep their depth, no trail, equal edges in both eyes.

Record: layout fine yes or no, the Edge, Motion reset and Eye balance values you settled on, the hole-fill side and eye, map rate at 2 and 3 threads.

## 7. SBS and Top-bottom sources (3 minutes, needs a 3D video)

Source: PLAN 4.3, lane wave4-sbs; USER_GUIDE "SBS 3D".

Play the half-width side-by-side video on the PC (left eye on the left).

- Press the bar's 3D button until its face reads SBS. Good: real depth, no doubled or ghosted picture, nothing from the other half at the seam. Record the video size and the aspect.
- Cog, 3D tab, the "3D source" row (Depth, SBS, Top-bottom): each cell switches at once, no restart. Depth starts the model; SBS and Top-bottom show the PC's pair. The bar's 3D button off shows the flat left half. In SBS the preset and depth rows are dim with no thumbs; Convergence, the 3D switch and the source row stay live.
- Convergence with SBS on: the halves move apart and together smoothly, centred at 0.5.
- Top-bottom with a top-and-bottom film (left eye on top): the same checks; the face reads TB.
- `[debug] eye_swap = 1` and restart: the depth inverts (near things sit behind the screen); back to 0 it is right again.
- `[3d] sbs_full = 1` with a 3840x1080 full-size pair (an Apollo virtual display at that size, USER_GUIDE recipe): the picture is 16:9, the pointer lands where aimed, letterbox crop and glow not odd. Record what Apollo actually gives.
- The pointer on an SBS picture hits the correct spot.

Good looks like: a stereo film with no ghosting, three sources that switch live, the right rows dimmed.

Record: ghosting yes or no, the seam, each source cell live yes or no, eye swap inverts yes or no, the full-size pair's aspect, pointer accuracy.

## 8. Host sheet: PC clipboard and commands (4 minutes, Apollo)

Source: PLAN 4.6, 4b.4 and 4b.5, lane wave4-apollo, plus lane wave3-tidy; USER_GUIDE "Clipboard and PC commands (Apollo)".

- The log at stream start has `host sheet: Apollo, N server commands, clipboard read 1 set 1`; record N.
- Open the Host sheet from the bar: the header row reads "Clipboard, commands", "PC VR", "Close", none cut off. Press "Clipboard, commands": the title is "PC clipboard, commands", the page has Back, PC VR, Close.
- Paste from PC: copy a two-line text on the PC, focus Notepad, press "Paste from PC": the text appears with its line break. Record the toast.
- Send text to PC: the Compact keyboard opens with no "Full" chip and the space bar reads "Type, Enter sends". Type `hello 123`: nothing appears on the PC. Enter, then Ctrl+V on the PC pastes it. Repeat with Hide instead of Enter: the PC clipboard is unchanged.
- Server command: press a harmless cell (Notepad): "Run X on the PC?"; Cancel does nothing; Run runs it, toast `Sent "X" to the PC`. Record whether it ran and how long it took.
- No permission: turn Server Command off for the Frame in Apollo's Clients page and restart the stream: the page says "This host has not allowed server commands for the Frame" and has no cells. Turn it back on.
- On a Sunshine host (if you have one): Paste and Send text are dimmed with "Not supported by this host: commands and the PC clipboard need Apollo"; Paste gives the toast "Not supported by this host".
- PC VR from the PC page: "Switch to PC VR? This stream ends and Steam Link takes over." with Switch and Cancel; the question reads whole; Cancel returns to the PC page with the stream still running. Do not press Switch yet (item 13). The Host button on the bar matches the new look.

Good looks like: a header with nothing cut off, a paste with its line break, text that goes only on Enter, a command that asks first.

Record: N commands, paste with line break yes or no, Hide leaves the clipboard alone yes or no, command ran and after how long, the no-permission text, Cancel on the PC VR question.

## 9. Ko-fi sheet (1 minute)

Source: PLAN 6.14, lane wave6-kofi; USER_GUIDE "About".

- Cog, About tab, press **Support simplysanatori on Ko-fi**: the sheet shows only that title and the buttons Open page and Not now (no QR code, no address, no other lines; lane fix-polish); the hover ring follows each; the text fits.
- Not now, or a press off the buttons: it closes and nothing opens.
- Open page: the sheet closes and Steam's browser shows ko-fi.com/sana748653 (this port's page, not upstream's) in the SteamVR dashboard within a few seconds. Press it twice quickly: one page only. Record whether the dashboard comes forward by itself, and what happens if Steam's browser was already open.

Record: sheet readable yes or no, the page opened and after how long, one page for two presses yes or no, dashboard behaviour.

## 10. Reconnect regression (1 minute)

Source: HEADSET_TEST_7 item 7, re-run because lanes wave1-burst, wave1-failover and wave6-splash touched the paths around it.

Pull the Frame's Wi-Fi for 3 seconds: the toast and the splash of item 2, the room and screen kept, a silent return, the first-run card not back. That is all; the 15 s and cancel cases are in test 7.

Record: came back on try 1 or 2, how long the picture was away.

## 11. Gaze with closed eyes (2 minutes, the accessibility pre-check)

Source: the head and eye only operation plan, not yet built; it decides whether eye gestures are possible on the Frame.

Set `[vr] gaze = true` and restart the stream (Gaze on in the Display tab does the same). Turn the stats on (any style that shows the gaze state; if none does, the log is the source: look for the gaze pose's tracked or valid state around the time you closed your eyes).

- Close both eyes for 1 s, open; for 2 s; for 3 s. Each time note whether the gaze pose loses its tracked bit, after roughly how long, and whether it is back within half a second of opening.
- Blink normally for 10 s: the bit should not drop for a blink.

Record: drops at 1, 2, 3 s yes or no, the delay to drop and to return, blink immunity. If it never drops, say so: the eye-closure gestures then drop out of the plan and the rest stands.

## 12. Headset language (4 minutes; restarts the stream)

Source: PLAN 6.1, lane wave6-i18n; USER_GUIDE "Language".

Disconnect. Flat Settings page, at the end: Headset language = Deutsch. Start the stream.

- The log has `UI language de (Deutsch), from [ui] language`.
- Every panel readable in German: cog Screen (with Head locked on, so the aim rows show), Display, 3D, Picture, About, Room; the keyboard in both layouts; the exit prompt; the Host sheet; the PC and app lists; a gamepad-mode toast. Nothing cut off or overlapping. Photograph anything wrong.
- Change the language in the launcher mid-stream: nothing changes; the next stream start shows it.
- Русский: on the Display tab "Контроллеры" and its "Shortcut…" line do not touch.
- 日本語, then 한국어: with the Noto CJK fonts present (item 0), Japanese everywhere translated, no boxes; record the `its characters from ...` log line and how long the cog takes to open the first time. If the fonts are absent: English, and the log says `needs Noto Sans CJK`.
- Automatic: record the Frame's `LANG` and which language appears.

Good looks like: a German cog that fits, Cyrillic that does not collide, CJK with real glyphs.

Record: each language tried, anything cut off (photo), CJK fonts found yes or no, the first-open delay, what Automatic chose.

## 13. PC VR: the switch (3 minutes; ends the stream)

Source: PLAN 4.1, 4.2 and 4.9, lane wave4-pcvr; USER_GUIDE "PC VR".

Set the language back to Automatic. Start a stream from the flat launcher with a VR game chosen on its Settings page's PC VR row (the row lists the paired PCs and their games, VR ones marked, and gives a clear reason if the port is closed or a call is missing).

- Host sheet, PC VR, Switch: the Moonlight stream ends, the PC session keeps running, toast "Switching to PC VR: the app is still running on the PC". Within a few seconds the Frame's own Steam Link starts that game from the PC. Record whether Steam accepts the call while the Moonlight shortcut was the running game.
- With no game chosen: Steam's library opens, and "Stream from your PC" on a game works.
- When Valve's stream ends, the flat window comes back as after any other stream.
- A stream started from the headset's own PC list instead of the flat launcher: the toast reads "PC VR needs the Moonlight XR launcher: start the stream from it".
- At a desk afterwards, run the read-only queries in [host.md](host.md) "PC VR hand-off: what to confirm on the Frame" (`steam_cef.py --probe`, the `typeof` checks, `--devices`, `--apps --client <id>`, `GetRunningApps`) and record their output.

Record: Steam took the call yes or no and after how long, library case, the way back to the flat window, the queries' output.

# Part B: accessories and the bad spot (about 25 minutes)

## B1. Gamepad gyro and the ratchet (6 minutes; Bluetooth pad not needed, this is the Frame controllers)

Source: PLAN 2.9, lane wave2-lightgun-gyro; USER_GUIDE "Gyro in gamepad mode".

On the PC: Apollo with ViGEmBus, gamepad type Automatic or DS4, Steam Input on. Set `[vr] pad_gyro = controller`. Start the stream, gamepad mode.

- Log: `gamepad gyro from the controller`, then `gamepad: the host asks for the gyroscope at N Hz` (record N and whether the accelerometer is asked for too). If no motion request arrives, record the Apollo version and its gamepad setting.
- On the PC a DualShock 4 appears in Steam's controller list. Turning the controller right: yaw negative; pitch up: pitch; still: zero, no drift. Try Steam Input's gyro-to-mouse: it moves the right way.
- `pad_gyro = head`: the same with the head.
- Ratchet: `pad_gyro_ratchet = stick_touch`. The startup Frame bindings line says `with the right stick's touch` (or record the `rejected ...` line plus the accepted one). Thumb on the right stick: gyro moves; lift: stops dead; rest again: resumes. Switch the right hand to hand tracking and back: a `not read (... not on the Steam Frame controller profile)` warning, then `read again`.

Record: N Hz, the axis signs, drift yes or no, gyro-to-mouse direction, ratchet stops dead yes or no, the hand-tracking warning pair.

## B2. The gamepad card (3 minutes, Bluetooth pad)

Source: PLAN 2b.6, lane wave3-cards; USER_GUIDE "The gamepad card".

Connect a Bluetooth pad (a DualSense if you have one) and press a button.

- The card appears in your lap with the right title for the pad. Each control lights the matching part: face buttons with A at the bottom, d-pad, LB and RB, LT and RT as bars, stick caps and rings, stick clicks, Back and Start, the touchpad dot, the Gyro mark while you tilt the pad.
- Let go: it hides about 4 s later. Turn your body: the card stays in the lap and turns with you.
- The launcher's row: On (it stays up while the pad is in use) and Off (never shows). Put it back to the default.

Record: pad type, anything lit wrong, whether redraws keep up while you wiggle a stick, `pad card up` and `pad card away` in the log.

## B3. Bitrate per path (3 minutes)

Source: PLAN 4b.8, lane wave4b-abr; USER_GUIDE "Bitrate for each connection".

Launcher Settings, Connection: Bitrate over Wi-Fi = 40 Mbps, Bitrate over the Steam Frame Wireless Adapter = 150 Mbps, Video bitrate at its default. Leave the page to save.

- Network connection = Wi-Fi, start a stream: the log has `bitrate for Wi-Fi: 40000 kbps (override)`, then `starting stream ... 40000 kbps`; the HUD's rate line reads "N of 40 Mb/s". Record the received rate.
- Adapter forced: `bitrate for Steam Frame Wireless Adapter: 150000 kbps (override)`, "of 150 Mb/s"; if it fell back to Wi-Fi the line names Wi-Fi and 40000.
- Automatic: the bitrate line names the same path as `network path: chose ...`.
- A USB cable the same way, and "Wired network" if you are on ethernet.
- One override back to "Same as above": the line says `(launcher)` with the Video bitrate.

Record: each line, the received rate against the ceiling, Automatic's path.

## B4. Burst loss: the IDR watchdog (5 minutes, in the bad spot)

Source: PLAN 1.15, lane wave1-burst; USER_GUIDE "Connection".

Put the Frame on the 6 GHz network (or stand where the picture used to freeze), HEVC, 80 Mbps or more, defaults (no `rfi_idr_ms` in config.ini). Stream for about 3 minutes through the freezes. Keep the log.

- At stream start: `video: RFI watchdog: an IDR frame after 100 ms ... up to 1000 ms`; `video: IDR frame requests of our own at most one per 500 ms ([stream] rfi_idr_ms)`; `video: receive drops ... are read for wlan0` (note it if it says "no interface").
- After each visible freeze: one `video: burst at <time> for <N> ms: ...` line, and inside it no run of repeated "Waiting for RFI frame" or "Invalidate reference frame request sent" lines (each kind once, then "N per-frame lines folded"). Record per burst: duration, IDR requests sent, held back, recovered by.
- The storm is gone when: per burst `IDR requests sent` is at most duration/500 ms + 1; the 5 s line's IDR requests per window is a few, not dozens; `put off by a burst` is more than 0; the freezes feel shorter than 1.0.0 did at the same spot.
- The 5 s line: `rx drop misc +0 (wlan0 drop +0 ...)` with a large `frames lost on the network` means the loss is on the radio; a non-zero drop, fifo or own-socket count means the headset drops packets. Record which.
- Optional A/B: `rfi_idr_ms = 0` (launcher row "Full-frame requests after lost video" = No limit), the same scene, compare burst durations and IDR requests sent. Pressing the launcher row steps 500 to 1000 and config.ini gets `rfi_idr_ms = 1000` under `[stream]`.

Record: per burst the four numbers, radio or headset loss, and the A/B if done.

## B5. Path failover (5 minutes, two paths)

Source: lane wave1-failover (an addition to PLAN 1.15); USER_GUIDE "Switching links when video keeps breaking up".

`[stream] failover = on` (launcher: "Switch network when video keeps breaking up" under Connection), Network connection = Automatic. You need the 6 GHz link to be the one Automatic picks and the adapter (or the 5 GHz network) up as the other candidate; the start-of-stream `network path:` lines show both answering.

- Walk into the bad spot. After about 3 s of lost video inside 10 s: `network path: switching from Wi-Fi to Steam Frame Wireless Adapter after N ms of loss (...)`; just before it a `video: burst at ...; network path switched from ... to ...` line; a toast naming the new path; then `reconnect: try 1 of 2`, `network path: using ...`, and the picture back within a few seconds with the room and screen kept.
- No flapping: no second switch for 5 minutes; walking back does not switch; a new stream re-measures every path.
- Failover on with a path set by hand (adapter only): it never switches. Adapter unplugged: nothing is logged, it stays.
- The thresholds (15 ms margin, 20 s settle, 3 s burst, 5 min gap) are reasoned, not measured: say whether they felt right.

Record: the switch line (loss ms, percent, both scores), time from the switch line to the picture, toast shown yes or no, any second switch. Turn failover off afterwards unless you want to keep it.

# Part C: the flat launcher at a desk (about 15 minutes, plus one install)

## C1. Stream profiles, preflight, UUID, search (8 minutes)

Source: PLAN 4.7, 4.8 and 4b.17, lane wave4-launcher; USER_GUIDE "Stream profiles", "Check before a stream", "Search and jump list".

- Profiles: Settings at 1080p/60 Hz, Profiles, save as "Desktop"; then 1440p/120 Hz, 3D on, Stats on, save as "Game". In the app menu, Desktop, "Stream settings profile...", pick Desktop; the game gets Game. Stream Desktop: the Stats show 1080p 60. Stream the game: 1440p 120, 3D and Stats on. After each stream Settings shows your own values and config.ini is unchanged. Change a 3D value from the cog during a profile stream: it survives after the stream. Record the `MLXR_LAUNCHER_PROFILE:` lines from `moonlight-launcher.log`.
- Preflight: Use Virtual Display on with Apollo's driver disabled (or a Sunshine host): the "Starting..." screen shows "Use Virtual Display is on, but ...", and the profiles page's "Last check" says the same. Record `MLXR_LAUNCHER_PREFLIGHT:` and `MLXR_LAUNCHER_HINT:`. A second paired client with only List and View permissions: the "does not let this client launch apps" hint, then the same text in the error dialog.
- UUID: stream an app from Apollo: the launcher log's PREFLIGHT line shows an app uuid (not "none"); Apollo's verbose log shows "Launching app [id] with UUID [uuid]". With the app running, Resume: no hint, it resumes.
- Search: Game Mode at 1.5 scale, type "refresh": only the Refresh rate row under Video Settings; clear with the cross or Esc; the Audio jump button scrolls to and focuses its first row; a gamepad reaches the field and the buttons.

Record: profiles applied per app yes or no and config.ini untouched, the preflight texts, the uuid, search in Game Mode with a pad.

## C2. The 500 Mbps ceiling (3 minutes)

Source: lane wave4b-abr (the Video bitrate row's new ceiling); USER_GUIDE "Video Settings".

- Video bitrate slider to 400 Mbps: the row shows 400; the stream starts with `400000 kbps`. Record whether the link holds it (the received rate on the HUD, freezes).
- With `[debug] file_log = verbose` and a bitrate over 300 Mbps: `grep "OUTPUT .* sizeimage"` and `grep E_TOO_BIG` on the log; record sizeimage; no freeze at an IDR.
- Put the bitrate back to 98.

## C3. Help in Game Mode (1 minute)

Source: lane wave6-kofi (the launcher half); USER_GUIDE "The flat window".

- In Game Mode, the launcher's Help (question mark): the readme opens in Steam's browser in the dashboard; `moonlight-launcher.log` says `open page: ... with steam steam://openurl`. Record whether Qt's old openUrl worked in Game Mode before.
- In Desktop mode: Help opens the normal browser.

## C4. Package: Frametanium and the zip (at the next release; optional now)

Source: PLAN 6.13, lane wave6-package; INSTALL docs.

These need a release after 1.0.1 that carries the zip beside the tar.gz. When it exists:

- Frametanium, fresh prefix: move `~/Games/MoonlightXRNative` aside, delete `~/.local/share/frametanium/apps/moonlight-xr-steam-frame`, add `simplysanatori/moonlight-xr-steam-frame` in Frametanium, install. Good: status "Installed <tag>. Moonlight XR for Frame <ver> installed. Steam shortcut ready (appid ...)", the shortcut with artwork in the library, `app/moonlight-xr` executable. Record the status text, `~/Games/MoonlightXRNative/install.log`, which asset it picked (expected tar.gz); pick the zip by hand once too.
- Frametanium with Steam closed: install reports success with "Steam shortcut skipped (Steam not reachable): start Steam, then run ...install.sh again"; the files are in place. Start Steam, run the printed command in Konsole: the shortcut appears.
- Tar.gz by hand: as before; `install.log` now exists.
- Zip by hand: unzip on the Frame, `./install.sh` is executable; record whether `unzip` exists on SteamOS.
- FrameDrop: drop the zip in; record whether it runs install.sh (unverified).

Then move your real install back.

## Read the logs (at a desk, 10 minutes)

On the PC, pull the files in one go (the plain logs hold addresses; only the zip is masked):

```sh
tools/frame-deploy.sh logs out
```

Then, against `out/moonlight.log` (on the Frame use `~/Games/MoonlightXRNative/moonlight.log` instead):

```sh
# Before you start: the per-eye size, what the stream ran at, the path
grep -E "curved picture: projection layer|stream: [0-9]+x[0-9]+|network path: chose" out/moonlight.log | tail -6
# Item 1: first-run card
grep "first-run card" out/moonlight.log | tail -4
# Item 2: splash
grep -E "splash:|composition layers" out/moonlight.log | tail -12
# Item 3: lightgun
grep -E "lightgun pointer|recoil on the" out/moonlight.log | tail -8
# Item 4: zoom
grep -E "^.*zoom [0-9]+%" out/moonlight.log | tail -12
# Item 5: Picture tab
grep -E "background dim|void colour|void color|glow edge" out/moonlight.log | tail -8
# Item 6: depth
grep -E "depth: |hole fill|depth threads|eye balance" out/moonlight.log | tail -20
# Item 7: SBS
grep -E "3D source|sbs|eye swap|top-bottom" out/moonlight.log | tail -12
# Item 8: Host sheet (Apollo)
grep "host sheet:" out/moonlight.log | tail -12
# Item 9: Ko-fi
grep "Ko-fi" out/moonlight.log | tail -4
# Item 10: reconnect
grep "reconnect:" out/moonlight.log | tail -8
# Item 11: gaze
grep -iE "gaze" out/moonlight.log | tail -12
# Item 12: language
grep "UI language" out/moonlight.log | tail -6
# Item 13: PC VR
grep -E "Steam CEF preflight|Asked Steam|PC VR" out/moonlight.log out/moonlight-launcher.log | tail -10
# B1: gyro
grep -E "gamepad gyro from|asks for the (gyroscope|accelerometer)|right stick's touch|rejected|not read \(|read again" out/moonlight.log | tail -12
# B2: pad card
grep "pad card" out/moonlight.log | tail -6
# B3: bitrate per path
grep -E "bitrate for|starting stream" out/moonlight.log | tail -10
# B4: bursts
grep -E "RFI watchdog|IDR frame requests of our own|receive drops|video: burst at|rx drop misc" out/moonlight.log | tail -30
# B5: failover
grep -E "network path:" out/moonlight.log | tail -20
# C1 to C3: the launcher
grep -E "MLXR_LAUNCHER_(PROFILE|PREFLIGHT|HINT)|open page:" out/moonlight-launcher.log | tail -20
```

What each should show:

- Item 1: `first-run card up` once per fresh `first_run_shown = false`, never after a reconnect.
- Item 2: `splash: <state> (<reason>)` at stream start and at a reconnect, `splash: gone` after the fade; no `frame needs more than 16 composition layers`.
- Item 3: `lightgun pointer on (lightgun auto, controller model gun)` when the Gun kind is chosen, `off` when the policy is off; `recoil on the right controller: runtime said XR_SUCCESS` for the buzz.
- Item 4: `zoom N% centred X,Y` for each rung; the Forget and the per-app lines from the Screen tab as in test 7 item 5.
- Item 5: `background dim N% from the panel`, `glow edge N from the panel`.
- Item 8: `host sheet: Apollo, N server commands, clipboard read 1 set 1`, `host sheet: X of Y bytes of the PC clipboard typed`, `host sheet: N bytes sent to the PC clipboard`, `host sheet: server command i (X) sent`.
- Item 9: `Ko-fi page opened with steam`, and `Ko-fi page: already opened for this press` for the quick second press.
- Item 12: `UI language de (Deutsch), from [ui] language`; for Japanese `UI language ja (日本語), its characters from <font> (face N)`, or `needs Noto Sans CJK, which is not under ...: English instead`.
- Item 13: `Steam CEF preflight: ...` then `Asked Steam for PC VR: done`; afterwards `Asked Steam to show the flat screen again`.
- B1: `gamepad gyro from the controller, motion sent ...`, `gamepad: the host asks for the gyroscope at N Hz`.
- B2: `pad card up` on the first press, `pad card away` about 4 s after the last.
- B3: `bitrate for Wi-Fi: 40000 kbps (override)`, `(launcher)` once the override is off.
- B4: one `video: burst at <time> for <N> ms: ... IDR requests sent N, held back N, recovered by ...` per freeze; the 5 s line with `rx drop misc`.
- B5: `network path: switching from X to Y after N ms of loss (...)`, then `network path: using Y ...`.

The sources of these lines: [reports/input.md](reports/input.md), [reports/burst.md](reports/burst.md), [reports/netpath-quality.md](reports/netpath-quality.md), [host.md](host.md), [i18n.md](i18n.md), [layout.md](layout.md) and the per-lane sections of [lanes.md](lanes.md).

## Results

Copy this table into your report and fill it in. Start it with the settings: stream size and bitrate, SteamVR per-eye size from the log, the network path Automatic chose, and the Vibepollo version.

| # | Item | Result (fine, or what was wrong) | Time of the run |
|---|---|---|---|
| 0 | Settings: stream size/bitrate, per-eye size, path chosen, Vibepollo version, CJK fonts present | | |
| 1a | First-run card: placement, readability, rows correct | | |
| 1b | Gone after ~20 s; gone at once on the cog; not back after a reconnect | | |
| 2a | Splash at start and at hand-off; fade visible; never over a live picture | | |
| 2b | "Reconnecting…" with the logo; clean give-up; no layer-count warning | | |
| 3a | Lightgun: ray awake, clicks land at the dot, buzz per shot | | |
| 3b | Pill: trigger does not move the screen, grip does; reload right click off-screen; `lightgun = off` restores | | |
| 4a | Zoom rungs smooth; corner fills the screen, cursor under the dot; offset at 400 % | | |
| 4b | Glow and letterbox at 300 %; per-app memory; Forget | | |
| 4c | SBS and depth at 200 % | | |
| 5a | Background dim in a room, passthrough, void; value that looks right | | |
| 5b | Void colour blue, no banding; Glow edge Soft/Wide, no ring | | |
| 5c | Picture rows persist; launcher rows read/write them | | |
| 6a | 3D tab: ten rows, readouts, Reset clear, no overlap | | |
| 6b | Edge, Depth detail, Depth shape, Motion reset, Eye balance: live and the values you settled on | | |
| 6c | Hole fill: equal edges both eyes; the smear back with MLXR_HOLE_FILL=0 (side, eye) | | |
| 6d | depth_threads 2 vs 3: map rate, age, CPU, temperature | | |
| 7a | SBS: depth, no ghosting, seam clean; video size and aspect | | |
| 7b | 3D source cells live; rows dimmed in SBS; Top-bottom; eye_swap; sbs_full aspect; pointer accuracy | | |
| 8a | Host sheet header whole; N commands in the log | | |
| 8b | Paste from PC with line break; Send text on Enter only | | |
| 8c | Server command asks first, runs, time taken; no-permission text; Sunshine dimming | | |
| 8d | PC VR question whole; Cancel returns | | |
| 9 | Ko-fi sheet; page opened; one page for two presses | | |
| 10 | Reconnect regression: try 1 or 2, time away | | |
| 11 | Gaze tracked bit with eyes closed 1/2/3 s: drops yes or no, delay, blink immunity | | |
| 12a | German everywhere, nothing cut off (photos) | | |
| 12b | Russian Display tab; CJK glyphs and first-open delay; Automatic's choice | | |
| 13 | PC VR switch: Steam took the call, time, library case, way back; the five queries | | |
| B1 | Gyro: N Hz, axis signs, drift, gyro-to-mouse, head; ratchet; hand-tracking warning | | |
| B2 | Pad card: pad type, parts lit, hide after 4 s, lap-locked, launcher On/Off | | |
| B3 | Bitrate per path: each line, received rate, Automatic's path | | |
| B4 | Bursts: per burst duration / IDR sent / held back / recovered by; radio or headset loss; A/B | | |
| B5 | Failover: switch line, time to picture, toast, no flapping, hand-set path never switches | | |
| C1 | Profiles per app; preflight texts; uuid; search in Game Mode | | |
| C2 | 400 Mbps: held or not; sizeimage; no freeze at an IDR | | |
| C3 | Help opens in Steam's browser in Game Mode | | |
| C4 | Frametanium and zip (next release) | | |
