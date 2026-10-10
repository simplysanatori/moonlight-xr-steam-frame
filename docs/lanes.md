# Worker lanes

Each lane works in its own git worktree at `~/Games/MoonlightXRNative/lanes/<name>` on branch `lane/<name>`, owns one directory, and builds with the top-level CMake into `build-<name>/` inside its worktree. The orchestrator merges a lane into `main` when its milestone starts and its tests pass.

| Lane | Branch | Owns | Model | State |
| --- | --- | --- | --- | --- |
| T toolchain | (main) | build.sh, tools/ldd-audit.sh, third_party | orchestrator | done in M0 (host toolchain, no container needed) |
| V video decode | lane/video | video/, tests/video/ | Opus | started 2026-10-10 |
| B backend | lane/backend | backend/, tests/backend/ | Sonnet | merged 2026-10-10: 7/7 suites (fake Sunshine host incl. pairing); live host untested (PC off) |
| X xr core ports | lane/xrcore | xr/core/ (incl. xr_audio), tests/xrcore/, assets/, tools/xr/ | Sonnet | done 2026-10-10: 17/17 suites pass (16 upstream with equal check counts + Frame pad suite); 41.7 MB assets |
| A audio | lane/audio (from lane/xrcore) | audio/, tests/audio/ | Sonnet | merged 2026-10-10: 6 suites; ~15 ms own buffering (PulseAudio reports 60 ms on the speakers incl. SteamOS chain); bypasses the SteamOS spatializer by node name |
| U ui text | lane/ui | ui/, tests/ui/, assets/fonts/ | Sonnet | started 2026-10-10 |
| S shaders | lane/shaders | shaders/, tests/shaders/ | Opus | merged 2026-10-10: 12 upstream programs + supersample/panel, 17 test groups; 4K stereo all passes ~4.5 ms GPU; not yet wired into xr_session (M2/M4) |
| I input | lane/input | input/ | Sonnet | after the M1 context header |
| R rooms | lane/rooms | rooms/, assets/rooms | Sonnet | after the M1 context header |
| D depth | lane/depth | depth/ | Opus | wave 2 |
| P packaging | lane/packaging | packaging/ (after M1) | Sonnet | M1 |

Pure modules (no GL, Android or `xr_renderer.h`), ported by lane X: xr_atlas, xr_controller, xr_depthmap, xr_gamepad, xr_gate, xr_glow, xr_grade, xr_headaim, xr_keys, xr_layout, xr_math, xr_notice, xr_pinch, xr_rate, xr_roommesh, xr_shared.h, and xr-audio's xr_audio.
Entangled with `xr_renderer.h` (`XrCtx` with GL types): xr_input, xr_ui, xr_layers, xr_display, xr_log, xr_room, xr_model, xr_ambilight; rewritten: xr_session, xr_gl, xr_shaders, xr_assets, xr_depth, xr_debug. The Vulkan `XrCtx` is orchestrator work in M1; I, R and U's panel code wait for it.
