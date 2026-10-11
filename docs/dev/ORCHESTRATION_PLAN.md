# Moonlight XR Native on the Steam Frame: execution plan (parallel agents)

## Context

The handoff document "Moonlight XR Native: Steam Frame Build Plan (LLM Handoff)" (Oct 10, 2026) specifies a native Linux Arm64 OpenXR client in C for the Steam Frame, with V4L2 hardware decode, Vulkan on Turnip, SteamVR as the OpenXR runtime, and feature parity with Moonlight XR. Its platform facts, hard rules, milestones M0 to M8 and acceptance gates are binding and are not repeated here. This plan adds what the document leaves open: which model and effort to run, how to split the work across parallel agents on the Frame itself, and the rules that keep parallel work from breaking the milestone gates.

Scope decisions made here:

- **Nothing from the Lepton APK install, Podman/PrimedGun or GalaxyQuest work on this device is used as a template.** This is a fresh native Arm64 Linux project. The only reuse is what the handoff document names: Gilleece's `xr-renderer/*.c` modules, `xr-audio/`, tools, assets and unit tests, fetched by a fresh clone from GitHub during execution (upstream `Gilleece/moonlight-android-xr`, plus only the Frame controller bindings in `xr_input.c` and the passthrough projection-layer trick in `xr_layers.c` from the `steam-frame` branch of `simplysanatori/moonlight-xr-lepton`). None of the fork's Lepton patches, launch wrappers or APK tooling is used.
- **Paths:** `~/Games/MoonlightXR/` is already occupied by the Lepton install, so the native project lives at `~/Games/MoonlightXRNative/` (`src/` repo, `app/` install tree, `moonlight.log`). The Steam shortcut is named "Moonlight XR Native". Everything else follows the document.
- **GitHub:** new repo `simplysanatori/moonlight-xr-native` (later renamed `simplysanatori/moonlight-xr-steam-frame`), branch `main`, GPLv3. The user's token is used only for `git push` and `gh` calls in the implementing session, supplied through `GH_TOKEN` for the duration of a command or the `http.extraheader` basic-auth form. It is never written into the repo, the plan, memory, or `~/.git-credentials`. Creating the repo via API may be blocked by the permission classifier; if so the user creates the empty repo in the browser and the session pushes to it. The token was pasted in chat and should be revoked after the first push and replaced.

## Model and effort (answers to the questions asked)

- **Switch off Fable now.** The plan is the Fable deliverable. Approve this plan, then run `/model`, pick Opus 5.5, and set effort to **high** before any implementation starts. `~/.claude/settings.json` already has `"model": "opus"` and no effort key; if the `/model` picker has no effort control, use the `update-config` skill in the Opus session to add the effort setting there.
- **Effort policy:** `high` is the default for the orchestrating Opus 5.5 session for all milestones. Raise it to `xhigh` only for M2 (V4L2 + dma-buf + Vulkan synchronisation) and for any headset-freeze or `msm_gem` investigation, then drop back to `high`. `max` is not needed anywhere.
- **Parallel agents:** yes. The orchestrating session spawns subagents with the Agent tool, `model: "opus"` for the hard lanes and `model: "sonnet"` for ports, scripts and docs, several per message so they run concurrently, `run_in_background: true`. Pass `effort: "high"` to Opus workers; omit it for Sonnet workers. The Workflow tool is not used: the milestone gates need the owner in the headset, so a human-in-the-loop orchestrator fits better than a scripted fan-out.

## How parallelism respects the gates

The headset-gated integration stays serial: one orchestrator owns the main binary and the milestone order, and never integrates a lane out of order. Library-level work that can be proven with desktop unit tests or standalone test binaries on the Frame runs in parallel worker lanes on their own branches and is merged when its milestone arrives. The decoder lane is the exception that justifies this: the highest-risk code (dma-buf import, source-change handling) is testable headless with `dmesg -w` and no SteamVR, so it starts on day one instead of waiting for M1.

## Repo layout and lane ownership

```
~/Games/MoonlightXRNative/src/
  CMakeLists.txt            orchestrator only (superbuild; add_subdirectory per lane)
  build.sh                  creates the Distrobox if missing, installs packages, builds, runs ldd audit
  docs/dev/STATUS.md                 confirmed-on-hardware vs reasoned-from-code (orchestrator keeps it)
  docs/probe/               xrinfo/probe output, v4l2-ctl, vulkaninfo, runtime JSON, os-release
  third_party/              moonlight-common-c (submodule), stb, OpenXR-SDK loader if needed
  backend/                  nvhttp.c pairing.c computer.c + mlxr-cli test tool          (lane B)
  stream/                   session.c (LiStartConnection, callbacks, reconnect)         (orchestrator)
  video/                    v4l2_decoder.c vk_import.c + decode-test harness            (lane V)
  xr/                       xr_session.c xr_vk.c xr_depth.c + ported xr_*.c             (lane X ports, orchestrator rewrites)
  shaders/                  *.glsl -> .spv at build (glslang)                           (lane S)
  depth/                    litert_backend.c + LiteRT build script                      (lane D)
  input/                    xr_input xr_gate xr_pinch xr_headaim xr_gamepad xr_keys pad_out (lane I)
  ui/                       text.c panels.c launcher.c                                  (lane U)
  audio/                    opus_out.c surround.c (port of xr-audio)                    (lane A)
  rooms/                    xr_room xr_roommesh xr_atlas + bake tools                   (lane R)
  assets/                   rooms, atlases, models, HRTF, fonts, licenses               (lane R / lane P)
  packaging/                install.sh moonlight-xr.sh autostart.conf CI workflow       (lane P)
  tests/                    ported desktop C unit tests + new per-lane tests
```

Rules that make concurrent lanes safe on one checkout:

- Each lane owns one directory and its own `CMakeLists.txt` (a static library target plus a `tests/` or a standalone test binary). Only the orchestrator edits the top-level `CMakeLists.txt`, `docs/dev/STATUS.md`, `stream/` and the three rewrites in `xr/`.
- Each lane builds into its own directory, `build-<lane>/`, with `ninja -j2`, so lanes never share a build tree and the orchestrator keeps CPU for headset runs. Heavy builds (LiteRT, FFmpeg static if needed) run with SteamVR off and `nice -n 10`.
- Each lane works on branch `lane/<name>`. The orchestrator merges a lane only when its milestone starts, after its tests pass in the container and (where relevant) on the host.
- At most 4 worker agents run at once on the Frame (8 cores, 16 GB). The orchestrator is the fifth process.
- All compilation happens inside the Distrobox (`distrobox enter mlxr -- ninja -C build-<lane>`); binaries run on the host. Workers never run `pacman`/`apt` on the host and never touch `steamos-readonly`.

## Lanes

| Lane | Model / effort | Starts | Inputs | Produces | Proven without the headset by | Merged at |
| --- | --- | --- | --- | --- | --- | --- |
| **O** orchestrator | Opus 5.5 / high (xhigh in M2) | M0 | handoff doc, probe | Distrobox, CMake skeleton, probe programs, two-eye Vulkan triangle, `stream/session.c`, `xr/xr_session.c`, `xr/xr_vk.c`, `xr/xr_depth.c`, every merge, `docs/dev/STATUS.md` | n/a (this is the headset-gated lane) | every milestone |
| **T** toolchain | Sonnet | after Distrobox exists | handoff build section | `build.sh`, moonlight-common-c static build, FFmpeg `v4l2m2m` check, `ldd` audit script, `$ORIGIN/lib` rpath rule, glibc check | `ldd` on host shows only SteamOS libs; `ffmpeg -decoders` lists `h264_v4l2m2m`/`hevc_v4l2m2m` | M0 |
| **V** video decode | Opus 5.5 / high | day one, parallel with M0 | parsec-on-frame, Pixel VR Player, moonlight-qt `drm.cpp`/`plvk.cpp`, SteamOS issues #2869 #2882 | `video/v4l2_decoder.c` (FFmpeg `h264_v4l2m2m`/`hevc_v4l2m2m`, DRM_PRIME; direct V4L2 fallback if FFmpeg stalls on the source-change event), `video/vk_import.c` (per-buffer cached import keyed by fd/inode, NV12 ycbcr sampler or two planes, DRM modifier, fence fd to semaphore), `decode-test` harness that decodes an elementary stream into a headless Vulkan image and writes PNGs | 30-minute loop of a 1440p HEVC file with `dmesg -w` clean, decode time under 4 ms, no per-frame import/free, resolution change mid-stream handled | M2 (also used in M1 if ready) |
| **B** backend | Sonnet / high | day one | moonlight-qt `app/backend/` | `backend/nvhttp.c pairing.c computer.c` over libcurl + OpenSSL, client cert under `~/.config/moonlight-xr/`, `mlxr-cli pair/list/launch/quit`, `seed-from-moonlight-qt.sh` | `mlxr-cli` pairs with the user's Sunshine/Apollo host, lists apps, launches and quits one from an SSH shell | M1 |
| **X** xr ports | Sonnet | day one | Gilleece `xr-renderer/*.c`, fork's `xr_input.c` bindings and `xr_layers.c` passthrough trick | the pure-C modules listed in the handoff with file names intact, Android log macros routed to `xr_log.c`, Linux CMake for them, ported desktop unit tests under `tests/` | all ported unit tests pass in the container; `ctest` green | M1 (math, layout, log, rate), M4, M5 |
| **A** audio | Sonnet | day one | moonlight-qt audio renderers, `xr-audio/` | `audio/opus_out.c` (Opus decode to PipeWire via libpulse API), `audio/surround.c` (HRTF convolution port), `surround-test` tone binary, click sample player | 7.1 test tones heard from the host's speakers/headset audio; surround unit test passes | M1 (stereo), M7 (surround) |
| **U** ui text | Sonnet | day one | handoff UI section, README settings tables | `ui/text.c` (stb_truetype SDF atlas, one font), `ui/panels.c` (bar, five-tab settings, picker, keyboard, PC/app lists as quad textures), settings struct persisted as `config.ini` with commented defaults | `panel-test` renders every panel to PNG; config round-trips | M4 |
| **S** shaders | Opus 5.5 / high | after M0 probe (needs Vulkan feature list) | Gilleece `xr_shaders.c` GLSL ES, `xr_gl.c` pass structure | Vulkan GLSL in `shaders/`, SPIR-V at build, pass descriptions (colour + grade + sharpen, depth upsample, offset search, DIBR warp, supersample, glow, UI, room, controller model) as data for `xr_vk.c`, headless `shader-test` that runs each pass on fixtures and writes PNGs | fixtures compare against reference images; `warp_lab.py` capture format honoured | M1 (colour), M4, M5, M6 |
| **D** depth | Opus 5.5 / high | after M0 (long build) | LiteRT CMake, ZipDepth/MiDaS models, `convert_zipdepth.py` | LiteRT Linux arm64 static build, `depth/litert_backend.c` on XNNPACK, `depth-bench` reporting maps/s at 512×288; GPU delegate or Vulkan compute spike only if CPU stays under 12 maps/s | `depth-bench` ≥ 12 maps/s int8 on CPU; output depth map matches the Android reference within tolerance | M6 |
| **I** input | Sonnet | after M0 probe (needs extension list) | `xr_input.c` family, STEAM_FRAME.md layout table | `input/` ports, `pad_out.c` to `LiSendMultiControllerEvent`, rumble callback to `xrApplyHapticFeedback`, suggestBindings that tolerates missing paths, feature flags for hands/gaze | input-rule unit tests; a `bindings-dump` tool prints the accepted profile | M3, M7 |
| **R** rooms | Sonnet | after M0 probe (needs ASTC answer) | `xr_room.c` family, assets, `bake_room.py`, `atlas_astc.py` | `rooms/` ports, ASTC or RGBA8 fallback decided from `docs/probe/`, assets tree, licences | room parser unit tests; `room-test` renders each room headless to PNG | M5 |
| **P** packaging | Sonnet | M1 onwards | handoff packaging section | `packaging/install.sh` (Steam shortcut through Steam's local debugging port), `moonlight-xr.sh` wrapper (unsets `MESA_GL_VERSION_OVERRIDE`/`MESA_GLSL_VERSION_OVERRIDE`, `XR_RUNTIME_JSON` fallback), log rollover, bug-report zip, GitHub Actions on `ubuntu-24.04-arm`, README with settings tables and licences | fresh-install dry run into a temp prefix; CI tarball matches local | M8 |

## Milestone schedule with parallel lanes

Gates are the handoff document's; nothing moves past a gate until the user confirms it in the headset.

1. **M0** Orchestrator: get a shell, check host glibc (`ldd --version`) and pick the newest Distrobox image whose glibc is not newer than the host's (Arch Linux ARM first; else Ubuntu 24.04 arm64; Debian 12 as last resort), create `mlxr`, install the toolchain, write `probe/` (OpenXR extensions, view configs, blend modes, refresh rates, interaction profiles; `v4l2-ctl --all` and formats; `vulkaninfo` with dma-buf, modifier, ycbcr, ASTC lines), save to `docs/probe/`, build the two-eye Vulkan triangle, add the Steam shortcut, ask the user to confirm. Spawn **T, V, B, X, A, U** as soon as the Distrobox exists (T first alone for an hour so the shared static libs exist, then the others; cap 4 at once). Spawn **S, I, R, D** after the probe lands.
2. **M1** Orchestrator merges T, B, X (math/layout/log/rate), A (stereo), S (colour pass): `stream/session.c`, software decode to a staging buffer, one quad layer, mouse from the right trigger. Gate in headset. Spawn **P**.
3. **M2** Orchestrator at `xhigh` merges V. Soak 30 minutes HEVC 1440p90 with `dmesg -w`. Lanes S, D, I, R, U keep working.
4. **M3** merge I. Gate: layout table button by button on a host gamepad tester.
5. **M4** merge U and X (ui/layout rest) and S (UI passes). Gate: every bar button and settings row.
6. **M5** merge R and S (room/glow passes), passthrough projection trick, swapchain budget check (video 1, room 1, passthrough 1, bar 1, panel 1, keyboard 1, picker 1, dot 1, controller 1, glow 1, notice 1 = 11 of 16).
7. **M6** merge D and S (depth passes); governor; stats. Gate: 1440p90 with 3D for 10 minutes.
8. **M7** merge A (surround), I (hands, gaze), headset-off, logging, report.
9. **M8** merge P. `ldd` audit on host, tarball release, CI.

## Worker prompt template (orchestrator pastes this, filled in, to every lane)

```
You are one lane of the Moonlight XR Native project on a Valve Steam Frame (SteamOS, Arm64, user steamos).
Repo: ~/Games/MoonlightXRNative/src, branch lane/<NAME>. You own ONLY <DIR>/ and tests/<NAME>/.
Build in the Distrobox: `distrobox enter mlxr -- cmake -S <DIR> -B build-<NAME> -G Ninja -DCMAKE_C_COMPILER=clang` then `distrobox enter mlxr -- ninja -C build-<NAME> -j2`. Run tests on the HOST (not in the container). Never pacman/apt on the host, never touch steamos-readonly, never run anything with sudo, never start or stop SteamVR.
Hard rules from the handoff document that apply to you: <PASTE the relevant bullets: dma-buf import once and cached; source-change before STREAMON; no VP9; 16-swapchain cap; frame_controller profile with tolerant suggestBindings; gate features on docs/probe/; GPLv3 and licences; log via xr_log.c>.
Reference code (read, port, do not reinvent): <URLs/paths>.
Deliverable: <library target + test binary + what the test must prove>, committed on your branch with a short lane report in <DIR>/REPORT.md: what is confirmed by a test on this device, what is reasoned from code, open questions for the orchestrator.
Do not edit the top-level CMakeLists.txt, docs/dev/STATUS.md, stream/, or xr/xr_session.c, xr/xr_vk.c, xr/xr_depth.c. If you need something there, write it in REPORT.md.
```

Spawn with `Agent(subagent_type: "general-purpose", model: "opus" | "sonnet", effort: "high" for opus lanes, run_in_background: true)`.

## Verification

- **Per lane:** the "proven without the headset" column is the merge requirement; the orchestrator re-runs the lane's test on the host before merging.
- **Per milestone:** the handoff document's gate, confirmed by the user in the headset, recorded in `docs/dev/STATUS.md` as confirmed-on-hardware with the date and the build commit.
- **Decode soak (M2):** 30 minutes HEVC 1440p90 at 80 Mbps with `dmesg -w` in a second shell; zero `msm_gem`/`iris` warnings; `perf top` shows no CPU copies.
- **Link audit (every release build):** `ldd ./moonlight-xr` on the host with `LD_LIBRARY_PATH` unset lists only SteamOS-provided libraries plus `./lib`.
- **Probe drift:** `docs/probe/` is re-run and diffed after any SteamOS update.
- **Unit tests:** `ctest` in every `build-<lane>` and in the superbuild must be green before a merge.

## Open items the orchestrator must settle in M0 (not before)

- Which Distrobox image satisfies the glibc rule; whether `openxr` (loader + `xrinfo`) is packaged for it or the loader is built from OpenXR-SDK.
- Whether FFmpeg's `v4l2m2m` wrapper survives the Iris source-change event; if not, lane V switches to direct V4L2 ioctls.
- Whether `XR_EXT_hand_tracking`, `XR_EXT_eye_gaze_interaction`, `XR_FB_display_refresh_rate`, `XR_FB_passthrough` and `XR_KHR_composition_layer_cylinder` exist natively; lanes I and R and X branch on the answer.
- ASTC support in Turnip (lane R) and `VK_KHR_sampler_ycbcr_conversion` (lane V).
- Disk space in `/home` for the Distrobox plus LiteRT build; if tight, lane D builds last and prunes.
