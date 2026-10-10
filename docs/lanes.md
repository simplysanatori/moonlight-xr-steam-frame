# Worker lanes

Each lane works in its own git worktree at `~/Games/MoonlightXRNative/lanes/<name>` on branch `lane/<name>`, owns one directory, and builds with the top-level CMake into `build-<name>/` inside its worktree. The orchestrator merges a lane into `main` when its milestone starts and its tests pass.

| Lane | Branch | Owns | Model | State |
| --- | --- | --- | --- | --- |
| T toolchain | (main) | build.sh, tools/ldd-audit.sh, third_party | orchestrator | done in M0 (host toolchain, no container needed) |
| V video decode | lane/video | video/, tests/video/ | Opus | started 2026-10-10 |
| B backend | lane/backend | backend/, tests/backend/ | Sonnet | started 2026-10-10 |
| X xr core ports | lane/xrcore | xr/core/, audio/xr_audio*, tests/xrcore/ | Sonnet | started 2026-10-10 |
| A audio | lane/audio | audio/ (except xr_audio*) | Sonnet | after X |
| U ui text | lane/ui | ui/ | Sonnet | after X |
| S shaders | lane/shaders | shaders/ | Opus | wave 2 |
| I input | lane/input | input/ | Sonnet | after the M1 context header |
| R rooms | lane/rooms | rooms/, assets/rooms | Sonnet | after the M1 context header |
| D depth | lane/depth | depth/ | Opus | wave 2 |
| P packaging | lane/packaging | packaging/ (after M1) | Sonnet | M1 |

Pure modules (no GL, Android or `xr_renderer.h`), ported by lane X: xr_atlas, xr_controller, xr_depthmap, xr_gamepad, xr_gate, xr_glow, xr_grade, xr_headaim, xr_keys, xr_layout, xr_math, xr_notice, xr_pinch, xr_rate, xr_roommesh, xr_shared.h, and xr-audio's xr_audio.
Entangled with `xr_renderer.h` (`XrCtx` with GL types): xr_input, xr_ui, xr_layers, xr_display, xr_log, xr_room, xr_model, xr_ambilight; rewritten: xr_session, xr_gl, xr_shaders, xr_assets, xr_depth, xr_debug. The Vulkan `XrCtx` is orchestrator work in M1; I, R and U's panel code wait for it.
