# xr/core report (lane X, branch lane/xrcore)

Base: Gilleece/moonlight-android-xr master 75d92f2. Fork compared: simplysanatori/moonlight-xr-lepton (branch steam-frame).

## Modules ported (library `mlxr_xrcore`, static, C11, -Wall -Wextra)
xr_atlas, xr_controller, xr_depthmap, xr_gamepad, xr_gate, xr_glow, xr_grade, xr_headaim, xr_keys, xr_layout, xr_math, xr_notice, xr_pinch, xr_rate, xr_roommesh (.c/.h each), xr_shared.h, and xr_audio.c/.h (from xr-audio/).
File names and logic are as upstream, except the fork hunks below. `xr_audio.c` is compiled at -O2 (as upstream builds it; test_audio prints kernel cost). The JNI half of xr_audio.c is behind `#ifdef __ANDROID__` and is simply not compiled.

- OpenXR types: the real headers via `OpenXR::headers` work; upstream's `stubs/openxr/openxr.h` is not used (only xr_math.h includes openxr). The stub was needed upstream just to avoid the SDK.
- `xr_platform.h`: NOT created. No pure module logs through an Android macro (the only `android/log.h` use is inside the `__ANDROID__` block of xr_audio.c). Add the shim when a lane needs LOGI/LOGW (xr_input, xr_session etc. do; see notes below).
- Warnings: none. The library is warning-free with clang and gcc at `-std=c11 -Wall -Wextra -O2`, even with `-Wunused-parameter` (which the top-level CMake disables). No warning fixes were needed.

## Fork hunks (diffed every ported file against the fork; only these three differ)
Taken (all Steam Frame controller/pad work, commit "take the fork's Steam Frame pad work"):
1. xr_shared.h: `PAD_UP/DOWN/LEFT/RIGHT` (0x1/2/4/8) and `PAD_BACK` (0x20) added, PAD_BUTTONS extended to 14 bits, comment updated. Taken: Frame d-pad and Back.
2. xr_gamepad.h: `PadHand` gains `frame, faceX, faceY, dpadUp, dpadLeft, bumper, start`. Taken: Frame controller layout.
3. xr_gamepad.c hunk 1 (padMap face buttons): on a Frame left hand lower/upper become PAD_DOWN/PAD_RIGHT, dpadUp/dpadLeft give UP/LEFT, bumper LB; Touch left keeps X/Y; Frame right hand adds X, Y, RB (bumper), Start. Taken: Frame mapping.
4. xr_gamepad.c hunk 2 (padMap menu): left menu button is PAD_BACK on a Frame left, PAD_START otherwise. Taken: Frame view button is Back.

Skipped: none in the pure modules. All other fork changes to the xr-renderer dir are outside my lane and are the unwanted Lepton/SteamVR-workaround kind or belong to entangled modules (see notes): xr_assets.c (`fewSwapchains` swapchain trimming, `uploadArt` made non-static), xr_layers.c (transparent passthrough projection layer), xr_session.c (`fewSwapchains` detection, XR_VALVE_frame_controller_interaction enabling), xr_renderer.h (those fields + Frame action fields), xr_input.c (Frame interaction profile `suggestFrameBindings`, `onFrame[]`). Of those, the xr_input.c / xr_renderer.h Frame-controller parts ARE wanted and feed the new PadHand fields; the rest is not.
Note: "flip-face-buttons for the pad" is not in any pure module of the fork's C tree (the grep for flip finds only the unrelated ray/head-aim switch); it is presumably Java-side or in xr_input.c, so it is not ported here.

The fork's tests are byte-identical to upstream's, so the fork's PAD_BUTTONS change leaves upstream's test_gamepad failing (3 checks). I updated test_gamepad.c (see below) and added a new suite.

## Tests (tests/xrcore, ctest names `xrcore_<module>`, built with -Werror)
Counts printed by upstream (`make -C app/src/test/cpp test CC=clang`) before porting, and by the ported ctest binaries after:

| suite | upstream | ported |
|---|---|---|
| xr_math | 169 | 169 |
| xr_depthmap | 240 | 240 |
| xr_atlas | 79 | 79 |
| xr_roommesh | 87 | 87 |
| xr_layout | 1260 | 1260 |
| xr_audio | 22628 | 22628 |
| xr_rate | 3432 | 3432 |
| xr_gate | 49 | 49 |
| xr_pinch | 125 | 125 |
| xr_notice | 8160 | 8160 |
| xr_glow | 124 | 124 |
| xr_grade | 9058 | 9058 |
| xr_keys | 57 | 57 |
| xr_controller | 193 | 193 |
| xr_headaim | 1598 | 1598 |
| xr_gamepad | 674 | 674 |
| xr_gamepad_frame (new) | n/a | 23 |

Changes to the ported tests:
- test_atlas.c, test_roommesh.c: asset directories are `MLXR_ASSETS_DIR "/rooms/"` and `"/models/"` (compile definition from tests/xrcore/CMakeLists.txt, default `${PROJECT_SOURCE_DIR}/assets`, cache var `MLXR_ASSETS_DIR`), instead of the relative `../../main/assets/`.
- test_gamepad.c: with the fork's PAD_BUTTONS (14 bits, 0xf3ff) three checks failed (the bit-set check, the 0xf3d0 constant, and "all Touch buttons at once == PAD_BUTTONS"). Updated them to the 14-bit value and a local `TOUCH_BUTTONS` mask (the old nine bits) for the Touch-hands case; the check count is unchanged (674).
- test_gamepad_frame.c: NEW, 23 checks for the Frame layout (d-pad, Back, X/Y/bumpers/Start, mixed Touch/Frame hands). The only addition beyond the 16 suites; delete if unwanted.
- All other suites are unchanged copies; check.h unchanged. The tests link the whole `mlxr_xrcore` rather than per-module sources.
- xr/core/CMakeLists.txt does the `add_subdirectory` of tests/xrcore (so xr/CMakeLists.txt is only `add_subdirectory(core)`).

## Assets
Copied from upstream app/src/main/assets into assets/ keeping layout: rooms/ (3 rooms, 6 ASTC atlases, 3 thumbs), hrtf/kemar/ (37 WAV + LICENSE.txt), models/controller.room, images/ (2 PNG), licenses/zipdepth_LICENSE.txt. No .tflite. Plus the new assets/licenses/README.md.
Size added: 41,750,327 bytes in 55 files (rooms 41,654,849; hrtf 21,795; models 58,300; images 12,428; licenses 2,955). The ASTC atlases do not compress, so expect ~41.7 MB in .git. The three 9.6 MB 4096-px `_0.atlas` files (one per room) dominate; the `_lo_0` ones (2.4 MB each) are for XR2 Gen 1 headsets and a Frame likely does not need them (could be dropped to save ~7.5 MB if the orchestrator agrees).
tools/xr/ holds upstream tools/ unchanged (minus report-worker): bake_room.py, atlas_astc.py, convert_*.py, quantize_depth.py, warp_lab.py, make_controller.py, make_qr.py, measure_logo.py, check_strings.py, plus models/ and rooms/ NOTE.txt files. Not run. NOTE: their commands still refer to `app/src/main/assets/...` and `tools/...` paths; adjust when used. The room source .glb files are not in upstream either.

## Surprises
- The fork's git history does not contain upstream 75d92f2, so I diffed the file trees directly. The fork is identical to upstream in all pure modules except the pad work above.
- Upstream `xr_math.h` is the only pure header that includes `<openxr/openxr.h>`; xr_shared.h has no OpenXR or GL dependency and defines the JNI-ish shared constants (IN_PAD_*, PAD_*), which every UI-ish lane will include.
- Even strict `-std=c11` builds with no `_GNU_SOURCE` fine; test_audio uses nothing beyond libc.

## Notes for the entangled-module lanes
- Everything is reachable with `target_link_libraries(x PRIVATE mlxr_xrcore)`; the include dir (xr/core) and OpenXR::headers are PUBLIC.
- Header coupling of pure modules: xr_headaim.h, xr_layout.h, xr_keys.h, xr_grade.h, xr_notice.h, xr_rate.h, xr_depthmap.h, xr_gamepad.h include xr_shared.h; xr_controller.h/gate.h/pinch.h/headaim.h/layout.h include xr_math.h. None include xr_renderer.h. Upstream xr_renderer.h is the XrCtx god-header the entangled modules all include; it in turn includes these (so the XrCtx port should keep including them, and the fork's added XrCtx fields for Frame input are: padXAction, padYAction, padUpAction, padLeftAction, padBumperAction, padStartAction, frameProfile, frameBound, onFrame[HAND_COUNT], frameInteraction).
- xr_input: calls xr_gate (who may point, controller clocks), xr_pinch (hand pinch/press/triple pinch), xr_gamepad (padMap, padHoldBack, padRest etc.; fills `PadHand` including the Frame fields: frame, faceX, faceY, dpadUp, dpadLeft, bumper, start), xr_controller (ray drawn, model draw/placement), xr_headaim, xr_math. The fork's Frame bindings (xr_input.c `suggestFrameBindings`, XR_VALVE_frame_controller_interaction, left dpad_down/right = pointer right/middle click, view = menu) are the wanted part; `lower/upper` on a Frame left hand mean d-pad down/right.
- xr_ui: xr_layout (hover zones, stand-in, Room/3D tabs), xr_keys (keyboard sheets and modifier state), xr_notice (panel fades, splash timing), xr_grade, xr_rate, xr_headaim; xr_shared.h constants.
- xr_layers / xr_display: xr_notice, xr_rate (refresh choice, frame budget, depth governor), xr_layout, xr_math, xr_glow (glow luma and edge pass), xr_depthmap. Skip the fork's passthrough projection layer and `fewSwapchains` trimming (Lepton/SteamVR workarounds) unless needed natively.
- xr_room / xr_model: xr_roommesh (.room parser) and xr_atlas (.atlas parser); assets from assets/rooms and assets/models. xr_ambilight: xr_glow, xr_grade, xr_math.
- xr_log: not needed by the pure modules; if xr_platform.h is added later it should define LOGI/LOGW/LOGE over stderr and keep xr_audio's android block untouched.
- xr_audio: only the C kernels are built; whoever wires audio supplies HRTF from assets/hrtf/kemar (37 WAVs, elevation 0) and calls the C API in xr_audio.h directly instead of the JNI entry points.
