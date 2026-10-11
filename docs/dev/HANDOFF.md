# Moonlight XR Native: session hand-off

Written 2026-10-10 at the end of the first build session (main at the commit that adds this file; 108 test suites green). Paste the prompt at the bottom into a new Claude Code session on the Steam Frame to continue. Everything below is either in this repository or on the Frame at the paths given.

## 1. What this project is

A native Linux Arm64 OpenXR game-streaming client in C for the Valve Steam Frame. It is a port of Sean Gilleece's Moonlight XR (an Android app) with hardware decode on the Frame's Qualcomm Iris decoder, Vulkan on Turnip, and SteamVR as the OpenXR runtime. The flat front end (PC list, app list, pairing, settings) is the real Moonlight UI: a patched moonlight-qt 6.1.0 that hands each stream to the native VR client.

- Original build plan (binding facts, milestones M0–M8, gates): `docs/dev/BUILD_PLAN.md`
- Orchestration plan approved at the start (lanes, models, effort): `docs/dev/ORCHESTRATION_PLAN.md`
- What is confirmed on hardware vs only tested headless: `docs/dev/STATUS.md` (keep it current)
- Lane history and ownership: `docs/dev/lanes.md`
- Per-area reports: `docs/dev/reports/video.md`, `docs/dev/reports/backend.md`, `docs/dev/reports/xr-core.md`, `docs/dev/reports/audio.md`, `docs/dev/reports/ui.md`, `docs/dev/reports/shaders.md`, `docs/dev/reports/depth.md`, `docs/dev/reports/input.md`, `docs/dev/reports/rooms.md`, `docs/dev/reports/packaging.md`, `docs/dev/reports/keyboard.md`, `docs/dev/reports/launcher.md`, `docs/dev/M4_REPORT.md`, `docs/dev/M5_REPORT.md`, `docs/dev/M6_REPORT.md`
- Device facts from the M0 probe: `docs/probe/SUMMARY.md`

## 2. Where things are

| What | Where |
| --- | --- |
| Main checkout (branch `main`) | `~/Games/MoonlightXRNative/src` |
| GitHub (private) | `https://github.com/simplysanatori/moonlight-xr-native` (owner login `simplysanatori`) |
| Installed app (what the Steam shortcut runs) | `~/Games/MoonlightXRNative/app` |
| Steam shortcut | "Moonlight XR for Frame", appid 2761093165, runs `app/moonlight-xr-launcher.sh` (the flat Moonlight UI). `VR_ONLY=1 ./packaging/install.sh` points it back at `app/moonlight-xr.sh` (the VR client's own in-VR PC/app lists) |
| VR client config, pairing, hosts | `~/.config/moonlight-xr/` (`config.ini` with every setting, `client.pem/key`, `hosts.ini`, `servercerts/`); `file_log = verbose` is set for testing |
| Flat launcher config | `~/.config/Moonlight XR Native/Moonlight XR Native.conf` (copied once from the owner's flatpak `Moonlight.conf`, which is never modified) |
| Logs | `~/Games/MoonlightXRNative/moonlight.log` (+ `.1` after 5 MB) and `moonlight-console.log`; bug report zip: `app/bug-report.sh` → `~/Games/MoonlightXRNative/reports/` |
| Reference sources (read-only clones) | `~/Games/MoonlightXRNative/ref/{gilleece,fork,moonlight-qt,parsec-on-frame,pixel-vr-player}` |
| LiteRT build (outside git, 1.8 GB) | `~/Games/MoonlightXRNative/third-party-build` (rebuilt by `depth/tools/build-litert.sh` if missing) |
| Test clip | `~/Games/MoonlightXRNative/test-1080p60.mp4` |
| Lane worktrees (all merged, can be removed) | `~/Games/MoonlightXRNative/lanes/*` |
| The owner's PC | a Windows PC on the home LAN running Sunshine 7.1.431 (later Apollo); its name and addresses are kept out of this repository |

## 3. Build, test, install, push

```
cd ~/Games/MoonlightXRNative/src
./build.sh                       # host toolchain (no container); builds LiteRT if missing, the VR client, mlxr-cli and the flat launcher; ldd audit
env -u LD_LIBRARY_PATH ctest --test-dir build -E audio_pulse      # never run audio_pulse: it loads a PulseAudio null sink
./packaging/install.sh           # installs to ~/Games/MoonlightXRNative/app and updates the Steam shortcut (VR_ONLY=1, NO_SHORTCUT=1 available)
tools/release.sh                 # tarball + SHA256SUMS in ~/Games/MoonlightXRNative/releases (must be built on the Frame; the CI artefact cannot run on it: FFmpeg 6.1 vs 7.0)
```

- Run binaries from SSH with `env -u LD_LIBRARY_PATH` (the shell inherits Steam's runtime path).
- Headless runs: `build/moonlight-xr --file ~/Games/MoonlightXRNative/test-1080p60.mp4 --seconds N`. SteamVR keeps sessions started from SSH in the SYNCHRONIZED state, so nothing is drawn headless; visual checks need the owner in the headset, or the offscreen snapshot targets (`ninja -C build ui-snapshots`, `kb-snapshots`).
- Pushing: the owner pastes a GitHub token when asked. Use it for one command only: `git -c http.extraheader="Authorization: Basic $(printf 'simplysanatori:%s' <token> | base64 -w0)" push origin main`. Never store it (not in git config, memory, files). Fetching the private repo needs the same header. Advise the owner to revoke tokens after use.

## 4. State by milestone (2026-10-10)

| Milestone | State |
| --- | --- |
| M0 probe + triangle | **Passed in the headset** |
| M1 pair + stream flat | **Passed** (video, mouse, audio confirmed by the owner) |
| M2 hardware decode | **Accepted by the owner** (1440p90 HEVC 80 Mbps, 3.8 ms decode, kernel clean; the 30-minute soak was done headless only) |
| M4 in-VR UI | **Owner: "the whole in-VR UI looks good"** |
| M3 gamepad mode | Wired; **owner's gamepad-tester run pending** |
| M5 rooms, controller models, glow, click, supersample | Merged; **owner's headset run pending** (script in `docs/dev/M5_REPORT.md`) |
| Keyboard (Full TKL, Hacker's Keyboard modifiers) + Start+Select+L1+R1 chord | Merged; **untested in the headset** |
| Flat Moonlight launcher | Merged, installed as the shortcut; **untested in the headset** (Game Mode display of the flat window, switch into VR and back) |
| M6 realtime 3D | Merged; **owner's headset run pending** (script in `docs/dev/M6_REPORT.md`); see section 6 |
| M7 hands, gaze, surround, headset-off, logging, report | Partly done: logging with rollover, report sheet + bug-report.sh, surround renderer exists (5.1/7.1 KEMAR, numerically tested, not heard yet). Open: see section 5 |
| M8 packaging + release | Groundwork merged (installer, release tarball, CI, README, CREDITS). Open: see section 5 |

Owner-confirmed behaviour to keep:
- Pointer: One Euro smoothing (min cutoff 1 Hz, beta 0.01); no click hold (the owner asked to remove it).
- A physical Bluetooth keyboard works via evdev, grabbed while focused and streaming.
- Exit from the VR bar ends the host's session (quit); the chord Start+Select+L1+R1 and, on a physical keyboard, Moonlight's own Ctrl+Alt+Shift+Q only disconnect (the game keeps running; the flat UI shows Resume). The flat UI's tip picks between them as stock does (gamepad connected or not).
- Default stream 2560x1440, 90 fps, 80 Mbps HEVC (the owner says 1440p needs ~80 Mbps).
- Settings and defaults follow Moonlight XR (upstream `PreferenceConfiguration.java`, `MidasDepthSource.java`) unless the owner chose otherwise. The owner's own choices (2026-10-10): 3D on by default, supersample Normal, sharpening Off. Native deviations still in place: gaze and hand tracking off until tested on the Frame (M7), the pointer dot, the remembered 3D switch.

## 4b. Session 2 (2026-10-10 afternoon): where it stopped

The owner stopped the session at their usage limit. main is pushed; two lanes are pushed unmerged.

**Owner's latest headset report (the open problems, most important first):**
1. **Rooms and the in-VR menu lag/judder with head movement** ("the fix didn't do anything"). Evidence in moonlight.log: with the cog menu open, "N panel drawings ~200 ms avg" and up to 461 missed refreshes per 10 s (frames 20–70 ms); without the menu, rooms run 2–3 ms a frame with 0 missed, yet the owner still reports judder in rooms with head tracking. So: (a) finish lane `panels-perf` (panel rasterisation off the frame loop; one commit f9a7a3c so far: panels drawn into cached memory, only touched slots copied), (b) then debug the room judder separately (rooms are projection layers redrawn every frame; the picture in a room is a quad unless 3D is on; SteamVR draws quads over projection layers whatever the order, which may make the picture and room disagree under reprojection; check SteamVR's `~/.local/share/Steam/logs/vrcompositor.txt` "presents / dropped / reprojected" summary per session).
2. **Picture quality in VR is poor; text barely legible.** Done so far: 3D picture through a projection layer (SteamVR drops one-eye quads), glow inside that layer, mip chain + trilinear/anisotropic sampling for the 3D/curved picture (6c79b4d), defaults sharpen Off / supersample Normal (owner). Not yet tested by the owner after 6c79b4d. Physics: a 2560-wide desktop on a 3 m screen at 3 m is ~2x the display's pixels per degree; the owner can now pull the screen closer (stick on the held bar). Compare 3D off (single quad, sampled once by SteamVR) vs on.
3. **Flat UI does not come back after leaving VR the second time** (first time it did). The launcher asks Steam `SteamClient.OpenVR.VROverlay.ShowDashboard('valve.steam.desktopgame.<appid>')` via `app/steam_cef.py` at 0, 1 and 2.5 s after the client exits (5911d12); not yet tested by the owner. Launcher log: `moonlight-launcher.log` ("Asked Steam to show the flat screen again ...").
4. **Controller models**: owner wants full parity with the real Frame controllers (indented grip etc., as in SteamVR passthrough). Lane `models-tex` was stopped mid-task: WIP commit 0974e93 does NOT build (converter keeps all triangles + textures; textured model shaders and pass API started; loader in rooms/room_gpu.c, xr_model.c wiring, tests not done). Finish it (Opus).

**Done and installed in session 2 (headset-tested where noted):** defaults 3D on (as upstream), supersample Normal, sharpening Off, glow Off (owner); Ctrl+Alt+Shift+Q (keyboard) and Start+Select+L1+R1 (gamepad, bumpers, as Moonlight; owner-tested the chord) disconnect; flat window hidden while streaming and shown when the headset has the picture (MLXR_STREAMING on the client's stdout); the VR client shows no lists of its own under the flat UI; SteamVR per-app settings for appid 2761093165 (90 Hz confirmed by the owner, motion smoothing off, resolution 2160, sharpening off) via vrcmd; Steam game resolution 1920x1080 for the flat window (owner: 1440p too small); Moonlight's Fullscreen UI mode set once in Game Mode (black bars); move bar + thumbstick north/south pushes the screen away/closer (both controllers; untested); Frame controller models v1 (vertex-coloured, decimated; owner: "looks terrible").

**Tools for headset debugging:** `env -u LD_LIBRARY_PATH python3 -I ~/mlxr-tools/vrshot.py /tmp/x` takes a SteamVR screenshot of what the owner sees (x_preview.png, x_vr.png); read it with the Read tool. `vrcmd --set-settings-int steam.app.<appid>.<key> <value>` sets SteamVR per-app settings; `python3 -I packaging/steam_cef.py "<js>"` runs Steam client JS (SteamClient.Apps.SetAppResolutionOverride etc.).

## 4c. Session 3 (2026-10-10 evening, Fable, planning only): what was checked and the plan

Fable is for planning (owner's rule), so this session verified, merged one finished lane and wrote this plan. Implementation is for an Opus 5.5 session at effort `high`.

**Verified:** main cbacaaf builds on the host and passes 110 of 110 suites. Lane `panels-perf` (f9a7a3c, one commit, xr_assets.c + xr_native.h only) was merged into main, rebuilt, retested and installed; see the commit log. Its finding is the root cause of the menu lag: the panels were rasterised straight into the atlas staging buffer, whose memory is host-visible **uncached (write-combined)** on Turnip, so the read-modify-write blending ran about 40x slower than in cached memory (cog 180-200 ms instead of 4-5 ms). Panels now draw into plain memory and a touched slot is copied into the staging buffer in one sequential write. Expected: "N panel drawings ~200 ms avg" becomes ~5 ms. **Not headset-tested.**

**Facts from the owner's last run (16:52-16:54, moonlight.log) that shape the plan:**
- Menu open: 2-18 panel drawings per 5 s at 146-236 ms each, 431 and 418 missed refreshes per 10 s, 17-70 fps. Fully explained by the write-combined memory above.
- Rooms, menu closed: frame 2.2-4.2 ms, 0-2 missed refreshes per 10 s, 90 fps. Yet the owner sees judder. "Missed refreshes" (xr_display.c displayFrameBegun) only counts jumps in xrWaitFrame's predictedDisplayTime, i.e. frames *our loop* was late for. It says nothing about frames the **compositor** dropped or reprojected, and SteamVR's own per-session summary in `~/.local/share/Steam/logs/vrcompositor.txt` was lost when SteamVR restarted at 20:43 (the log is per SteamVR run). Compositor evidence is the missing piece.
- Layer sizes: curved/3D picture projection layer 5280x2640 (two eyes), UI atlas 2048x2960 with 19 slots and 3 images; up to 11 layers with the menu open.
- Flat UI return: the retry fix 5911d12 **was** in the installed build (16:51) for the 16:52 run; `moonlight-launcher.log` shows the three ShowDashboard asks (0, 1000, 2500 ms) ran and Steam answered `null` (no error). Whether the owner saw the window is unknown (the owner listed it as untested).

**Plan, in order (each step = one Opus agent or the orchestrator itself; one worktree per lane; Sonnet only where marked):**

1. **Menu lag, part 2 (Opus, lane `panels-perf`, owns xr/xr_panels.c, xr/xr_assets.c, xr/xr_native.h glue in xr_session.c).** The 40x fix is merged; the remaining ~5 ms per cog redraw still runs inside the 11.1 ms frame. Move rasterisation to a worker thread: `xrPanelsUpdate` computes the per-slot keys on the frame thread (cheap, as now) and hands changed slots to one worker that draws into a per-slot back buffer; the frame thread, after the fence wait where it already calls `xrAtlasTouch`, copies finished slots into the staging buffer and bumps `version[slot]`. Keep the stats line ("panel drawings ... ms avg") measuring the worker's draw time, and add the frame thread's wait time if any. Tests: `tests/ui` panel tests + a new threaded test (TSan target as in `tests/input`). Acceptance (owner): with the cog open, 0-2 missed refreshes per 10 s at 90 Hz.

2. **Room judder, diagnosis first (Opus, orchestrator-level, no lane).** Do not guess; measure the compositor:
   a. Write `~/mlxr-tools/vrtiming.py` after `vrshot.py`'s ctypes pattern: `VR_InitInternal2(…, 3 = Background)`, `FnTable:IVRCompositor_028`, call `GetFrameTimings(Compositor_FrameTiming*, nFrames)` once a second for N seconds and print per second: m_nNumFramePresents, m_nNumMisPresented, m_nNumDroppedFrames, m_nReprojectionFlags, m_flCompositorRenderGpuMs, m_flPresentCallCpuMs, and m_flNewFrameReadyMs - m_flWaitGetPosesCalledMs. Run it (over SSH, `env -u LD_LIBRARY_PATH python3 -I`) while the owner stands in a room with the menu closed, 3D on and then 3D off. Also copy `vrcompositor.txt` straight after the run (per-session "presents/dropped/reprojected" summary). Take the `Compositor_FrameTiming` layout from the openvr.h matching SteamVR 2.18 (set `m_nSize`), and check the FnTable index of GetFrameTimings against that header before trusting the numbers.
   b. Classify: (i) compositor drops or reprojection while our loop reports 0 missed means our frames arrive too late for SteamVR's running start: the loop submits GPU work and calls xrEndFrame straight away, and the next frame begins with `vkWaitForFences` on the previous frame; try submitting the room/model/curve work earlier in the frame (before the video import wait) or splitting the submission so the layers' images are done sooner; measure with (a) again. (ii) No drops, judder only between room and picture: a pose/time mismatch between the room's projection layer (drawn from `xrLocateViews` at `predictedDisplayTime`, pose = the drawn views, `xr_layers.c addRoomLayer`) and the picture quad SteamVR positions itself; compare room+3D on (picture also a projection layer, `curveRecord`) with room+3D off (quad) in the headset; if only the quad case judders, draw the flat picture into the picture projection layer too when a room is up (SteamVR drops one-eye quads and draws quads over projection layers regardless of order, so this also fixes ordering). (iii) Judder of the room alone, with no picture: check `xr_room.c` renders at the recommended eye size (`roomEyeWidth`/`Height`, logged) with the FOV from the same `xrLocateViews` call, and that `ctx->roomViews` are the views of *this* frame, not the previous one (`roomViewsValid` timing).
   c. Only then fix. Record the measurements in docs/dev/STATUS.md.

3. **Text quality (owner test, no code until results).** 6c79b4d (mip chain + trilinear/anisotropic sampling for the 3D/curved picture) is installed and untested. Owner script: Desktop at 2560x1440, (1) 3D on, default distance; (2) pull the screen closer with the held bar + stick; (3) 3D off (single quad, SteamVR samples it once); (4) supersample Quality. Say which reads best. If the quad (3) beats the projection layer, the projection layer's eye resolution (2640 wide per eye at SteamVR's 2160 setting) or its filtering is the limit: measure it: screenshot both with `vrshot.py` and compare the same text region; then try a larger picture layer (SteamVR per-app `supersampleScale` through `vrcmd`) and, for 3D, sampling the picture texture at its own resolution (no mip bias) before deciding.

4. **Flat UI return (owner test; Sonnet if a fix is needed).** Owner: start a stream, leave VR with the bar's Exit or the chord, repeat once. Report whether the flat Moonlight window comes back each time. If not: `moonlight-launcher.log` has the ShowDashboard lines; next candidates are re-showing/raising the Qt window itself (`MLXR_LAUNCHER_HIDE_WINDOW` hides it for the stream) before the ShowDashboard asks, and `vrshot.py` right after exit to see what SteamVR shows.

5. **Textured Frame controller models (Opus, lane `models-tex`, WIP 0974e93 does not build).** Rebase on main first. Left to do per the WIP message: loader in `rooms/room_gpu.c` for the textured format the converter now writes (every triangle, UVs, normals, textures), `xr_model.c` wiring to the new `model_tex` pass, tests in `tests/rooms`, headless render check, then the owner's look (hold angle, colour; `MLXR_FRAME_MODEL_GAIN`). Keep the ~4k-triangle vertex-coloured models as the fallback. Never redistribute Valve's files (read from `/opt/steamvr` at install).

6. Then HANDOFF section 5 items 3-5 (M6 follow-ups, M7, M8) as before.

**Push:** main after step 1 and after each merged fix; lanes `panels-perf` (now merged) and `models-tex` stay pushed.

### 4c progress (same evening, Opus 5.5)

- **Step 1 done (8e108d8), headless only.** Measured at -O2 in cached memory: cog 4-7 ms, keyboard ~10 ms, picker ~11 ms per drawing, too much of an 11.1 ms frame. The panels are now drawn on one worker thread (`xr/xr_panels.c`): the frame loop computes the keys, hands a job (a full copy of what the panel is drawn from) to the worker, and copies finished drawings into the atlas after a later frame's fence. Newest job per slot wins; cog, keyboard and exit go first. Launcher hits, the cog's last view and the report/toast/overlay flags change when the drawing lands. CPU-only atlases (tests, snapshots) still draw inline; `MLXR_PANELS_INLINE=1` forces inline in the session. The stats line now says "N panel drawings X ms avg (worst wait Y ms)". Test `m4_panels_async` (worker vs inline byte-identical over 13 UI states, burst, report reopen), clean under ThreadSanitizer in a separate build. 111/111 suites.
- **Step 2 prepared (e26cd33).** `tools/vrtiming.py` reads SteamVR's compositor frame timing (GetFrameTimings/GetCumulativeStats via ctypes, read-only background app). The flat launcher's wrapper runs it for every run into `~/Games/MoonlightXRNative/vrtiming.log` (previous run `.1`, `MLXR_VRTIMING=0` disables), `bug-report.sh` collects it. Columns: compositor frames, app frames reused (presents > 1), mispresented, dropped, reprojected cpu/gpu, async and motion flags, compositor GPU ms, app frame interval, poses-to-frame-ready ms, vsyncs to photons. Code review of the room path found nothing wrong: room views come from `xrLocateViews` at the predicted display time and the layer reports the poses it was drawn with; room textures are mipmapped, trilinear, anisotropic; room at 1760x1760 per eye (env res standard). **Next:** read vrtiming.log against moonlight.log from the owner's run of `docs/dev/HEADSET_TEST_3.md` step 2, then classify as in step 2b above.
- Steps 3 and 4 need the owner (`docs/dev/HEADSET_TEST_3.md` steps 3 and 4).
- Step 5 done and merged (da373fc). Owner's later requests done and merged: keyboard move bar, live 3D slider thumbs. Room judder: suspected SteamVR downsampling of projection textures over 3458 px wide (see docs/dev/STATUS.md), room capped; confirm with `docs/dev/HEADSET_TEST_3.md` step 2, then apply the same to the 5280-wide 3D/curved picture layer (array swapchain or a cap), which may also fix the text quality.

## 5. What is left (do these next, in this order unless the owner says otherwise)

1. **Get the owner's headset results** for the flat launcher, M3, M5, M6 and the keyboard (each report has a test script) and fix what they find. Read `moonlight.log` after each run.
2. **Done 2026-10-10 (headless; see `docs/dev/reports/frame-models.md`; owner check of angle and colour pending; animation not done).** Steam Frame controller models from the device, the way another Steam Frame port by the owner does it: SteamVR's Frame controller driver ships per-hand render models at `/opt/steamvr/drivers/frame_controller/resources/rendermodels/frame_controller_{left,right}/` (OBJ + MTL per component: body, bumper, dpad, grip_button, sticks, buttons, trigger; `*_color.png`, `*_ao.png`). That converter (GitHub `simplysanatori/Wiicompiled_VR-PLUS-steam-frame`, branch `steam-frame`, commit 23d95f5) triangulates every visible component into OpenXR grip space using the model's `openxr_grip` component, bakes AO into the base colour at 1024², and simplifies with a pure-Python QEM to ~4000 triangles per hand (full-detail models cost the owner 30 fps in that game; ~4k/hand was "perfect"). Do the same here: convert at install time (or first run) into the format `rooms/room_gpu` loads for the controller model (`assets/models/controller.room` today, a generic Quest-style controller), never redistribute Valve's files (read them from the local SteamVR install only), and fall back to the generic model if the directory is missing. Optional: animate the trigger, grips, sticks and buttons from the controller's input state, as the fork does with SteamVR's component motions.
3. **M6 follow-ups**: the owner's answers to the decisions in section 6, and the items not ported yet.
4. **M7**: hand tracking with pinch click and triple-pinch lock, and eye-gaze pointing (both present natively per the probe, ported in `xr_input.c`, **off by default** on the Frame because gaze takes the pointer from the controllers; let the owner turn them on and test); headset-off: mute and pause via `XR_EXT_user_presence` and session state, resume within a minute; virtual surround: owner listening test (5.1/7.1 at the host, `virtual_surround` setting), and the open question of SteamOS's own spatializer (the renderer bypasses it by node name; `systemSpatializer` switches); Stats overlay rows.
5. **M8**: tag `v0.1.0` when the owner is happy, `tools/release.sh`, GitHub release (private repo for now; public is the owner's call), Steam library art (done: `assets/branding`), drop `assets/rooms/*_lo_*` (~7.5 MB, unused on the Frame) and maybe MiDaS (33 MB) if the owner agrees, CI caveat stays documented.
6. **Physical gamepads (lane/gamepads, headless only):** SDL2 GameController source in `input/gamepads*.c`, same gating as the keyboard, shares controller numbers with gamepad mode, Start+Select+L1+R1 on a physical pad disconnects; see the last section of `docs/dev/reports/input.md` for design and the owner test (pair a pad over Bluetooth, stream, check buttons, rumble and the chord). Needs the owner's headset check.
7. **Small known items**: the Caps Lock light on the Full keyboard follows our own presses only; supersample defaults to Normal and sharpening to Off (the owner's choices, 2026-10-10; upstream has Off and Quality); the flat window stays visible during a stream (stock hides it; `MLXR_LAUNCHER_HIDE_WINDOW=1`); 144 fps from Moonlight settings passes through and the client skips frames above the display rate (SteamVR offers one rate, 72 or 90, and picks it); the generic controller model until item 2.

## 6. M6 (realtime 3D)

Merged 2026-10-10 (lane/m6 499b972). The whole chain runs headless on the Frame's GPU; nothing seen in the headset yet. Details and the owner's test script: `docs/dev/M6_REPORT.md`.

- Pipeline (`xr/xr_depth.c`, rewrite of upstream's xr_depth.c and the 3D half of renderVideoFrame): when a capture is due, downscale to 512x288 and copy into one of 2 host-visible readback buffers keyed by a timeline value; a stage thread waits and calls `mlxr_depth_submit`; the newest map goes into the next of 3 depth textures (per-slot staging, nothing allocated per frame); then upsample, offset search and one warp per eye into a double-wide swapchain (disparity ±separation, convergence, edge fade, held source). Curve and rooms work in 3D (each eye samples its half). Mono path unchanged with 3D off.
- Engine: starts the first time 3D is turned on (or at session start if the saved switch is on), ZipDepth 512x288 fp16, 2 threads, 20 maps/s; on failure stays flat with a toast and greys the 3D tab. `depth_source` = `zipdepth` (default), `zipdepth-int8`, `midas` (also `shifttest`, `eyetest` patterns). Without the LiteRT build, 3D shows "not installed".
- Numbers (headless, synthetic scene, hardware decode, 90 Hz loop): ZipDepth fp16 20.2 maps/s, model 36 ms avg, capture→display 78 ms, 1.5 cores; int8 19.8 maps/s, 14 ms, 45 ms age, 0.56 cores; MiDaS 45 ms, 82 ms age, 1.8 cores. GPU at 1080p: warp both eyes 1.9 ms, downscale + readback 0.35, upsample 0.26, offset 0.20. Frame loop p99 3.6–4.1 ms. CPU pinning did not help (unpinned default; `MLXR_DEPTH_CPUS` exists).
- Budget: worst 3D case 16 of 16 layers, nothing dropped; at most 8 swapchains. The double-wide picture swapchain exists whenever a model is configured (~44 MB at 1440p).
- Owner's decisions (2026-10-10): the 3D switch is remembered and **defaults to on** (as upstream); defaults follow Moonlight XR: ZipDepth fp16, its own 0.6 % separation, convergence 50 (int8 stays an option; upstream uses it only on Gen 1 headsets). Still open: is 78 ms depth age acceptable on motion.
- Not ported yet: upstream's debug capture dump, the depth-scale preference, the splash's wait for the first map.

## 7. How to work (the owner's standing rules)

- **Native Arm64 Linux only.** Do not use the old Lepton/Android install (`~/Games/MoonlightXR`) or the owner's other ports on this Frame as templates or references.
- **Models.** The session runs on Opus 5.5 at effort `high` (raise to `xhigh` only for kernel/decoder/headset-freeze debugging). Fable is for planning only (expensive). For subagents: Opus where it clearly pays (core integration in `xr/` and the session/frame loop, Vulkan synchronisation, threading, headset debugging); Sonnet for self-contained ports, asset loaders, scripts, packaging, docs. Be mindful of usage limits; if an agent stops on a limit, resume it with SendMessage after the reset instead of starting over. Never run two agents on the same files at once; give each lane its own git worktree (`git worktree add ../lanes/<name> -b lane/<name> main`) and owned directories, and merge only after rerunning its tests yourself.
- **Flat UI = stock Moonlight.** The launcher must stay exactly like moonlight-qt 6.1.0; every difference is listed in `docs/dev/reports/launcher.md`. Use Moonlight's own wording, e.g. "Tip: Press Start+Select+L1+R1 to disconnect your session" (Moonlight calls bumpers L1/R1 and triggers L2/R2).
- **In-VR UI = Moonlight XR.** Port upstream's code and assets (`ref/gilleece`, `ref/fork` for Frame-specific features only), keep upstream names and logic, mark native changes in comments.
- **Verify before claiming.** docs/dev/STATUS.md separates hardware-confirmed from headless-tested. The owner is the only one who can see the headset.
- **Safety.** No sudo/pacman/steamos-readonly; never restart Steam or SteamVR without asking; no launches against the owner's PC without the owner (read-only serverinfo/applist is fine); `--wake` sends Wake-on-LAN and is only used when asked.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` (or the model that did the work). Keep the README plain; the owner does not want it to read as LLM-written.

## 8. Prompt for the next session

```
Continue building Moonlight XR Native on this Steam Frame. Read, in order:
~/Games/MoonlightXRNative/src/docs/dev/HANDOFF.md (sections 4b and 4c first: the open problems and the plan),
docs/dev/STATUS.md, docs/dev/lanes.md, docs/dev/BUILD_PLAN.md, and the reports it lists, plus your memory
files about this project. Then:
1. Run ./build.sh and the test suite to confirm main is green.
2. Ask me for my headset results on the flat launcher, M3 gamepad, M5 rooms/models/glow, M6 3D,
   the Full keyboard and the Start+Select+L1+R1 chord (or tell me which test script to run),
   and fix what I report.
3. Then do HANDOFF.md section 5 items 2-5: Steam Frame controller models from SteamVR's
   rendermodels, M6 follow-ups, M7, M8.
Use Opus for core integration and debugging, Sonnet for self-contained work, one worktree
per lane, and keep docs/dev/STATUS.md honest about what is confirmed on hardware. I will give you a
GitHub token when it is time to push.
```
