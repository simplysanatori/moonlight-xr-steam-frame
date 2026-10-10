# Moonlight XR Native: session hand-off

Written 2026-10-10 at the end of the first build session (main at the commit that adds this file; 108 test suites green). Paste the prompt at the bottom into a new Claude Code session on the Steam Frame to continue. Everything below is either in this repository or on the Frame at the paths given.

## 1. What this project is

A native Linux Arm64 OpenXR game-streaming client in C for the Valve Steam Frame. It is a port of Sean Gilleece's Moonlight XR (an Android app) with hardware decode on the Frame's Qualcomm Iris decoder, Vulkan on Turnip, and SteamVR as the OpenXR runtime. The flat front end (PC list, app list, pairing, settings) is the real Moonlight UI: a patched moonlight-qt 6.1.0 that hands each stream to the native VR client.

- Original build plan (binding facts, milestones M0–M8, gates): `docs/BUILD_PLAN.md`
- Orchestration plan approved at the start (lanes, models, effort): `docs/ORCHESTRATION_PLAN.md`
- What is confirmed on hardware vs only tested headless: `STATUS.md` (keep it current)
- Lane history and ownership: `docs/lanes.md`
- Per-area reports: `video/REPORT.md`, `backend/REPORT.md`, `xr/core/REPORT.md`, `audio/REPORT.md`, `ui/REPORT.md`, `shaders/REPORT.md`, `depth/REPORT.md`, `input/REPORT.md`, `rooms/REPORT.md`, `packaging/REPORT.md`, `keyboard/REPORT.md`, `launcher/REPORT.md`, `M4_REPORT.md`, `M5_REPORT.md`, `M6_REPORT.md`
- Device facts from the M0 probe: `docs/probe/SUMMARY.md`

## 2. Where things are

| What | Where |
| --- | --- |
| Main checkout (branch `main`) | `~/Games/MoonlightXRNative/src` |
| GitHub (private) | `https://github.com/simplysanatori/moonlight-xr-native` (owner login `simplysanatori`) |
| Installed app (what the Steam shortcut runs) | `~/Games/MoonlightXRNative/app` |
| Steam shortcut | "Moonlight XR Native", appid 2761093165, runs `app/moonlight-xr-launcher.sh` (the flat Moonlight UI). `VR_ONLY=1 ./packaging/install.sh` points it back at `app/moonlight-xr.sh` (the VR client's own in-VR PC/app lists) |
| VR client config, pairing, hosts | `~/.config/moonlight-xr/` (`config.ini` with every setting, `client.pem/key`, `hosts.ini`, `servercerts/`); `file_log = verbose` is set for testing |
| Flat launcher config | `~/.config/Moonlight XR Native/Moonlight XR Native.conf` (copied once from the owner's flatpak `Moonlight.conf`, which is never modified) |
| Logs | `~/Games/MoonlightXRNative/moonlight.log` (+ `.1` after 5 MB) and `moonlight-console.log`; bug report zip: `app/bug-report.sh` → `~/Games/MoonlightXRNative/reports/` |
| Reference sources (read-only clones) | `~/Games/MoonlightXRNative/ref/{gilleece,fork,moonlight-qt,parsec-on-frame,pixel-vr-player}` |
| LiteRT build (outside git, 1.8 GB) | `~/Games/MoonlightXRNative/third-party-build` (rebuilt by `depth/tools/build-litert.sh` if missing) |
| Test clip | `~/Games/MoonlightXRNative/test-1080p60.mp4` |
| Lane worktrees (all merged, can be removed) | `~/Games/MoonlightXRNative/lanes/*` |
| The owner's PC | `DESKTOP-EXAMPLE`, Sunshine 7.1.431, LAN `192.0.2.10` (Tailscale `203.0.113.5` fallback) |

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
- Memory: edit `~/.claude/projects/-home-steamos/memory/*.md` with the Edit/Write tools; Bash heredocs into memory are blocked by the permission classifier.

## 4. State by milestone (2026-10-10)

| Milestone | State |
| --- | --- |
| M0 probe + triangle | **Passed in the headset** |
| M1 pair + stream flat | **Passed** (video, mouse, audio confirmed by the owner) |
| M2 hardware decode | **Accepted by the owner** (1440p90 HEVC 80 Mbps, 3.8 ms decode, kernel clean; the 30-minute soak was done headless only) |
| M4 in-VR UI | **Owner: "the whole in-VR UI looks good"** |
| M3 gamepad mode | Wired; **owner's gamepad-tester run pending** |
| M5 rooms, controller models, glow, click, supersample | Merged; **owner's headset run pending** (script in `M5_REPORT.md`) |
| Keyboard (Full TKL, Hacker's Keyboard modifiers) + Start+Select+L2+R2 chord | Merged; **untested in the headset** |
| Flat Moonlight launcher | Merged, installed as the shortcut; **untested in the headset** (Game Mode display of the flat window, switch into VR and back) |
| M6 realtime 3D | Merged; **owner's headset run pending** (script in `M6_REPORT.md`); see section 6 |
| M7 hands, gaze, surround, headset-off, logging, report | Partly done: logging with rollover, report sheet + bug-report.sh, surround renderer exists (5.1/7.1 KEMAR, numerically tested, not heard yet). Open: see section 5 |
| M8 packaging + release | Groundwork merged (installer, release tarball, CI, README, CREDITS). Open: see section 5 |

Owner-confirmed behaviour to keep:
- Pointer: One Euro smoothing (min cutoff 1 Hz, beta 0.01); no click hold (the owner asked to remove it).
- Physical Bluetooth keyboard (EXAMPLE0 Q1 Pro) works via evdev, grabbed while focused and streaming.
- Exit from the VR bar ends the host's session (quit); the chord Start+Select+L2+R2 and, on a physical keyboard, Moonlight's own Ctrl+Alt+Shift+Q only disconnect (the game keeps running; the flat UI shows Resume). The flat UI's tip picks between them as stock does (gamepad connected or not).
- Default stream 2560x1440, 90 fps, 80 Mbps HEVC (the owner says 1440p needs ~80 Mbps).
- Settings and defaults follow Moonlight XR (upstream `PreferenceConfiguration.java`, `MidasDepthSource.java`) unless the owner chose otherwise. The owner's own choices (2026-10-10): 3D on by default, supersample Normal, sharpening Off. Native deviations still in place: gaze and hand tracking off until tested on the Frame (M7), the pointer dot, the remembered 3D switch.

## 5. What is left (do these next, in this order unless the owner says otherwise)

1. **Get the owner's headset results** for the flat launcher, M3, M5, M6 and the keyboard (each report has a test script) and fix what they find. Read `moonlight.log` after each run.
2. **Done 2026-10-10 (headless; see `tools/frame-models/REPORT.md`; owner check of angle and colour pending; animation not done).** Steam Frame controller models from the device, the way the owner's Mario Kart Wii fork does it: SteamVR's Frame controller driver ships per-hand render models at `/opt/steamvr/drivers/frame_controller/resources/rendermodels/frame_controller_{left,right}/` (OBJ + MTL per component: body, bumper, dpad, grip_button, sticks, buttons, trigger; `*_color.png`, `*_ao.png`). The fork's converter `~/Games/Wiicompiled_VR-PLUS-steam-frame/steam-frame/frame-controller-models.py` (GitHub `simplysanatori/Wiicompiled_VR-PLUS-steam-frame`, branch `steam-frame`, commit 23d95f5) triangulates every visible component into OpenXR grip space using the model's `openxr_grip` component, bakes AO into the base colour at 1024², and simplifies with a pure-Python QEM to ~4000 triangles per hand (full-detail models cost the owner 30 fps in that game; ~4k/hand was "perfect"). Do the same here: convert at install time (or first run) into the format `rooms/room_gpu` loads for the controller model (`assets/models/controller.room` today, a generic Quest-style controller), never redistribute Valve's files (read them from the local SteamVR install only), and fall back to the generic model if the directory is missing. Optional: animate the trigger, grips, sticks and buttons from the controller's input state, as the fork does with SteamVR's component motions.
3. **M6 follow-ups**: the owner's answers to the decisions in section 6, and the items not ported yet.
4. **M7**: hand tracking with pinch click and triple-pinch lock, and eye-gaze pointing (both present natively per the probe, ported in `xr_input.c`, **off by default** on the Frame because gaze takes the pointer from the controllers; let the owner turn them on and test); headset-off: mute and pause via `XR_EXT_user_presence` and session state, resume within a minute; virtual surround: owner listening test (5.1/7.1 at the host, `virtual_surround` setting), and the open question of SteamOS's own spatializer (the renderer bypasses it by node name; `systemSpatializer` switches); Stats overlay rows.
5. **M8**: tag `v0.1.0` when the owner is happy, `tools/release.sh`, GitHub release (private repo for now; public is the owner's call), Steam library art (none yet; the owner has used SteamGridDB art before: `~/.local/bin/steam-art`, key in `~/.config/example/key`), drop `assets/rooms/*_lo_*` (~7.5 MB, unused on the Frame) and maybe MiDaS (33 MB) if the owner agrees, CI caveat stays documented.
6. **Small known items**: the Caps Lock light on the Full keyboard follows our own presses only; supersample defaults to Normal and sharpening to Off (the owner's choices, 2026-10-10; upstream has Off and Quality); the flat window stays visible during a stream (stock hides it; `MLXR_LAUNCHER_HIDE_WINDOW=1`); 144 fps from Moonlight settings passes through and the client skips frames above the display rate (SteamVR offers one rate, 72 or 90, and picks it); the generic controller model until item 2.

## 6. M6 (realtime 3D)

Merged 2026-10-10 (lane/m6 499b972). The whole chain runs headless on the Frame's GPU; nothing seen in the headset yet. Details and the owner's test script: `M6_REPORT.md`.

- Pipeline (`xr/xr_depth.c`, rewrite of upstream's xr_depth.c and the 3D half of renderVideoFrame): when a capture is due, downscale to 512x288 and copy into one of 2 host-visible readback buffers keyed by a timeline value; a stage thread waits and calls `mlxr_depth_submit`; the newest map goes into the next of 3 depth textures (per-slot staging, nothing allocated per frame); then upsample, offset search and one warp per eye into a double-wide swapchain (disparity ±separation, convergence, edge fade, held source). Curve and rooms work in 3D (each eye samples its half). Mono path unchanged with 3D off.
- Engine: starts the first time 3D is turned on (or at session start if the saved switch is on), ZipDepth 512x288 fp16, 2 threads, 20 maps/s; on failure stays flat with a toast and greys the 3D tab. `depth_source` = `zipdepth` (default), `zipdepth-int8`, `midas` (also `shifttest`, `eyetest` patterns). Without the LiteRT build, 3D shows "not installed".
- Numbers (headless, synthetic scene, hardware decode, 90 Hz loop): ZipDepth fp16 20.2 maps/s, model 36 ms avg, capture→display 78 ms, 1.5 cores; int8 19.8 maps/s, 14 ms, 45 ms age, 0.56 cores; MiDaS 45 ms, 82 ms age, 1.8 cores. GPU at 1080p: warp both eyes 1.9 ms, downscale + readback 0.35, upsample 0.26, offset 0.20. Frame loop p99 3.6–4.1 ms. CPU pinning did not help (unpinned default; `MLXR_DEPTH_CPUS` exists).
- Budget: worst 3D case 16 of 16 layers, nothing dropped; at most 8 swapchains. The double-wide picture swapchain exists whenever a model is configured (~44 MB at 1440p).
- Owner's decisions (2026-10-10): the 3D switch is remembered and **defaults to on** (as upstream); defaults follow Moonlight XR: ZipDepth fp16, its own 0.6 % separation, convergence 50 (int8 stays an option; upstream uses it only on Gen 1 headsets). Still open: is 78 ms depth age acceptable on motion.
- Not ported yet: upstream's debug capture dump, the depth-scale preference, the splash's wait for the first map.

## 7. How to work (the owner's standing rules)

- **Native Arm64 Linux only.** Do not use the old Lepton/Android install (`~/Games/MoonlightXR`), `~/PrimedGun-podman` or `~/Games/GalaxyQuest` as templates or references.
- **Models.** The session runs on Opus 5.5 at effort `high` (raise to `xhigh` only for kernel/decoder/headset-freeze debugging). Fable is for planning only (expensive). For subagents: Opus where it clearly pays (core integration in `xr/` and the session/frame loop, Vulkan synchronisation, threading, headset debugging); Sonnet for self-contained ports, asset loaders, scripts, packaging, docs. Be mindful of usage limits; if an agent stops on a limit, resume it with SendMessage after the reset instead of starting over. Never run two agents on the same files at once; give each lane its own git worktree (`git worktree add ../lanes/<name> -b lane/<name> main`) and owned directories, and merge only after rerunning its tests yourself.
- **Flat UI = stock Moonlight.** The launcher must stay exactly like moonlight-qt 6.1.0; every difference is listed in `launcher/REPORT.md`. Use Moonlight's own wording, e.g. "Tip: Press Start+Select+L2+R2 to disconnect your session" (Moonlight calls bumpers L1/R1 and triggers L2/R2).
- **In-VR UI = Moonlight XR.** Port upstream's code and assets (`ref/gilleece`, `ref/fork` for Frame-specific features only), keep upstream names and logic, mark native changes in comments.
- **Verify before claiming.** STATUS.md separates hardware-confirmed from headless-tested. The owner is the only one who can see the headset.
- **Safety.** No sudo/pacman/steamos-readonly; never restart Steam or SteamVR without asking; no launches against the owner's PC without the owner (read-only serverinfo/applist is fine); `--wake` sends Wake-on-LAN and is only used when asked.
- **Commits** end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` (or the model that did the work). Keep the README plain; the owner does not want it to read as LLM-written.

## 8. Prompt for the next session

```
Continue building Moonlight XR Native on this Steam Frame. Read, in order:
~/Games/MoonlightXRNative/src/docs/HANDOFF.md (state, paths, rules, what is left),
STATUS.md, docs/lanes.md, docs/BUILD_PLAN.md, and the reports it lists, plus your memory
files about this project. Then:
1. Run ./build.sh and the test suite to confirm main is green.
2. Ask me for my headset results on the flat launcher, M3 gamepad, M5 rooms/models/glow, M6 3D,
   the Full keyboard and the Start+Select+L2+R2 chord (or tell me which test script to run),
   and fix what I report.
3. Then do HANDOFF.md section 5 items 2-5: Steam Frame controller models from SteamVR's
   rendermodels (as my Mario Kart Wii fork does), M6 follow-ups, M7, M8.
Use Opus for core integration and debugging, Sonnet for self-contained work, one worktree
per lane, and keep STATUS.md honest about what is confirmed on hardware. I will give you a
GitHub token when it is time to push.
```
