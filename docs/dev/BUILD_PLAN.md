# Moonlight XR Native: Steam Frame Build Plan (LLM Handoff)

Oct 10, 2026 · simplysanatori

## Mission and scope

Build **Moonlight XR Native**: a native Linux Arm64 OpenXR client for the Steam Frame, written in C (with C++ where a library needs it), that reaches feature parity with Moonlight XR as it stands in the `steam-frame` branch of [simplysanatori/moonlight-xr-lepton](https://github.com/simplysanatori/moonlight-xr-lepton), but decodes video on the Frame's Qualcomm Iris hardware decoder instead of in software. Everything is built, compiled and run on the Steam Frame itself, in SteamOS desktop mode or over SSH, with no x86 PC in the loop.

Why native rather than the Lepton APK: Lepton gives Android apps no hardware decoder, so the APK is capped at about 40 Mbps at 1440p with H.264 only, the depth model runs on the CPU, and SteamVR's 16-swapchain cap forced the splash, toasts, glow and clock out. A native app gets V4L2 hardware decode (about 2 ms per 1080p frame), GPU depth inference, Vulkan on Turnip, and the full set of OpenXR extensions SteamVR exposes to Linux apps.

In scope:

- Pairing, PC list, app list and stream start (moonlight-common-c)
- Hardware H.264/HEVC 8-bit decode via V4L2 M2M with zero-copy dma-buf import into Vulkan
- OpenXR session on SteamVR with the Steam Frame controller interaction profile
- Virtual screen: flat or curved, world-locked or head-locked, 6DOF move bar, corner resize, recentre
- Environments: passthrough, void, and the three rooms (Home Theater, Grand Cinema, Synthwave)
- Realtime 2D-to-3D (ZipDepth and MiDaS) with the depth-image-based warp
- Pointer mode (controller as mouse), gamepad mode (controllers as one Xbox pad with rumble), head aim, hand tracking with pinch, eye-gaze pointing
- In-VR bar, five-tab settings panel, keyboard with Fn sheet, environment picker
- Picture grading (brightness, contrast, gamma, saturation), sharpen, supersample, glow/ambilight, screen light on rooms
- Audio via PipeWire including virtual surround (KEMAR HRTF)
- Headset-off pause, performance stats overlay, file log and bug report
- Install as a Steam library shortcut (non-Steam game) launched from the Frame's Game Mode

Out of scope for v1: HDR and 10-bit (the Frame's kernel decodes only 8-bit H.264/HEVC), AV1, YUV 4:4:4, the Android 2D settings screens (replaced by a minimal native launcher UI), Check for updates and the report collector, and the nineteen translations (English first, strings table kept separate so they can return).

## Target platform facts (as of 10 Oct 2026)

The Frame is an ordinary Arm64 Arch-based Linux box with SteamVR as its OpenXR runtime; treat it as a PC, not a Quest. Everything below is confirmed by a linked source unless marked *verify*.

| Area | Fact | Source |
| --- | --- | --- |
| SoC / RAM / GPU | Snapdragon 8 Gen 3, 16 GB LPDDR5X, Adreno 750 | [Steamworks](https://partner.steamgames.com/doc/steamhardware/steamframe/compatibility) |
| Display | 2160×2160 per eye LCD, 72–144 Hz; SteamVR offered the Lepton APK only 90 Hz, but a native GL app got rates via `XR_FB_display_refresh_rate` | [STEAM\_FRAME.md](https://raw.githubusercontent.com/simplysanatori/moonlight-xr-lepton/steam-frame/STEAM_FRAME.md), [OpenCE #85](https://github.com/OpenCommunityEdition/OpenCE/pull/85) |
| OS | SteamOS (Arch-based), immutable root, kernel 6.18, KDE Plasma desktop mode, Flatpak and Distrobox/Podman available as on Steam Deck | [parsec-on-frame](https://github.com/SaberMage/parsec-on-frame) |
| GPU driver | Mesa Turnip (Vulkan) and Freedreno/Zink (GL); Vulkan is the native API | [Phoronix](https://www.phoronix.com/news/Steam-Frame-Turnip-Vulkan) |
| OpenXR runtime | SteamVR 2.18.x; native Arm64 apps open it with `XR_KHR_vulkan_enable2` (Pixel VR Player) or `XR_KHR_opengl_enable` + `XR_MNDX_egl_enable` (OpenCE). Runtime JSON is normally at `~/.config/openxr/1/active_runtime.json`; *verify* the path and library name on the device | [Pixel VR Player](https://github.com/Pixelplanet/Pixel-VR-Player), [OpenCE #85](https://github.com/OpenCommunityEdition/OpenCE/pull/85) |
| OpenXR extensions seen inside Lepton | `XR_VALVE_frame_controller_interaction` present; `XR_KHR_composition_layer_cylinder`, `XR_KHR_composition_layer_color_scale_bias`, `XR_FB_composition_layer_settings` absent; 16 swapchains per session; passthrough composites only behind a projection layer. *Verify* each on the native runtime with `xrinfo` or a probe, since the Lepton runtime may be a subset | [STEAM\_FRAME.md](https://raw.githubusercontent.com/simplysanatori/moonlight-xr-lepton/steam-frame/STEAM_FRAME.md) |
| Controllers | `/interaction_profiles/valve/frame_controller`; grips are `squeeze/value`; a binding path the profile lacks must not abort suggestBindings | [STEAM\_FRAME.md](https://raw.githubusercontent.com/simplysanatori/moonlight-xr-lepton/steam-frame/STEAM_FRAME.md), [OpenCE #85](https://github.com/OpenCommunityEdition/OpenCE/pull/85) |
| Eye tracking | `XR_EXT_eye_gaze_interaction` supported by Valve | [Steamworks](https://partner.steamgames.com/doc/steamhardware/steamframe) |
| Video decode | Qualcomm Iris V4L2 M2M at `/dev/video-dec0` (iris driver, firmware `vpu33_4v.mbn`); H.264 and HEVC 8-bit only; no 10-bit, AV1, VA-API or Vulkan Video; about 2 ms per 1080p frame | [parsec-on-frame](https://github.com/SaberMage/parsec-on-frame), [WiVRn #1130](https://github.com/WiVRn/WiVRn/pull/1130) |
| Iris quirk 1 | No `V4L2_FMT_FLAG_DYN_RESOLUTION`; CAPTURE format stays 384×256 until `V4L2_EVENT_SOURCE_CHANGE` fires | [SteamOS #2869](https://github.com/ValveSoftware/SteamOS/issues/2869) |
| Iris quirk 2 | Importing and freeing dma-bufs every frame can hit a NULL deref in `msm_gem_import()` that freezes the headset (upstream fix e6863b085606 not in 6.18); cache imports per buffer | [SteamOS #2882](https://github.com/ValveSoftware/SteamOS/issues/2882) |
| Iris quirk 3 | VP9 second session crashes the firmware; do not use VP9 | [SteamOS #2865](https://github.com/ValveSoftware/SteamOS/issues/2865) |
| Steam launch env | Steam sets `MESA_GL_VERSION_OVERRIDE=4.3` for games; unset it if using GL. SteamVR's `xrEndFrame` blocks for the GPU (4–5 ms), so end the previous frame at the start of the next | [OpenCE #85](https://github.com/OpenCommunityEdition/OpenCE/pull/85) |
| Audio | PipeWire (PulseAudio API works) | [Pixel VR Player](https://github.com/Pixelplanet/Pixel-VR-Player) |
| Known Flatpak quirks | Missing cursor needs `KWIN_FORCE_SW_CURSOR=1`; xdg portal crashes in Game Mode | [SteamOS #2904](https://github.com/ValveSoftware/SteamOS/issues/2904) |
| Depth layers | SteamVR lists depth swapchain formats for GL but creates none; skip `XR_KHR_composition_layer_depth` | [OpenCE #85](https://github.com/OpenCommunityEdition/OpenCE/pull/85) |

## Reference codebases and what to reuse

Most of the XR logic already exists as plain C in Gilleece's `app/src/main/jni/xr-renderer/` and ports almost unchanged; the work is replacing the Android, GL ES and MediaCodec edges with Linux, Vulkan and V4L2. All four repos are GPLv3, so the result is GPLv3.

| Repo | Clone | Reuse | Replace |
| --- | --- | --- | --- |
| [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) | `git clone --recursive` (bundles enet, reed-solomon; needs OpenSSL/libcrypto) | Whole library as-is: `LiStartConnection`, `DECODER_RENDERER_CALLBACKS`, `AUDIO_RENDERER_CALLBACKS`, `LiSendMouse*`, `LiSendControllerEvent`, `LiSendKeyboardEvent`, `LiRequestIdrFrame` | Nothing |
| [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) | `git clone --recursive` | Read only: `app/backend/nvhttp.cpp`, `nvpairingmanager.cpp`, `computermanager.cpp` (pairing, HTTPS to port 47984/47989, app list, launch/resume, client cert); `app/streaming/video/ffmpeg.cpp` (decoder selection, low-latency AVCodecContext flags, frame pacing); `ffmpeg-renderers/drm.cpp` (V4L2 M2M + `AV_PIX_FMT_DRM_PRIME`), `plvk.cpp` (libplacebo Vulkan import); `app/streaming/audio/renderers/` (opus decode, SDL/Pulse out); `app/streaming/input/` (mouse, keyboard, gamepad to LiSend\*) | Qt, QML UI, SDL window |
| [Gilleece/moonlight-android-xr](https://github.com/Gilleece/moonlight-android-xr) (and the `steam-frame` branch of [simplysanatori/moonlight-xr-lepton](https://github.com/simplysanatori/moonlight-xr-lepton)) | `git clone --recursive -b steam-frame` | Port verbatim, as C: `xr_math.c`, `xr_layout.c`, `xr_ui.c`, `xr_input.c` (Frame controller bindings already in the fork), `xr_gate.c`, `xr_pinch.c`, `xr_headaim.c`, `xr_gamepad.c`, `xr_keys.c`, `xr_layers.c` (the passthrough projection-layer trick), `xr_depthmap.c`, `xr_rate.c`, `xr_display.c`, `xr_grade.c`, `xr_ambilight.c`, `xr_glow.c`, `xr_room.c`, `xr_roommesh.c`, `xr_atlas.c`, `xr_controller.c`, `xr_model.c`, `xr_notice.c`, `xr_log.c`, `xr_shared.h`, `xr_renderer.h`; `xr-audio/` convolution; `tools/` (bake\_room.py, atlas\_astc.py, convert\_zipdepth.py, quantize\_depth.py, warp\_lab.py); the room meshes, ASTC atlases, HRTF set and depth models under `app/src/main/assets/`; the desktop C unit tests in `app/src/test/cpp/` | `xr_session.c` (JNI lifecycle, GL ES swapchains → Vulkan swapchains), `xr_gl.c` and `xr_shaders.c` (GL ES → Vulkan + GLSL compiled to SPIR-V at build), `xr_assets.c` (Java bitmap uploads → native text/raster), `xr_depth.c` (SurfaceTexture read-back → Vulkan staging), `XrRenderer.java`, `XrPanels.java`, `MidasDepthSource.java`, `XrPad.java`, `MediaCodecHelper`, all of `com.limelight` Java |
| [awesome-steam-frame](https://github.com/kosantosbik/awesome-steam-frame) | browse | Index of Frame tooling: sideloaders, SSH/devkit notes, Lepton, community apps | - |

Also read before writing a line of decode code: [parsec-on-frame](https://github.com/SaberMage/parsec-on-frame) (V4L2 + dma-buf import on this exact decoder), [Pixel VR Player](https://github.com/Pixelplanet/Pixel-VR-Player) (OpenXR + Vulkan + FFmpeg `v4l2m2m` on the Frame, with `tools/fetch-sysroot.sh` and a Devkit deploy flow), and [OpenCE #85](https://github.com/OpenCommunityEdition/OpenCE/pull/85) (frame pacing and Steam launch pitfalls on SteamVR).

## Build environment on the Frame itself

Build inside a Distrobox container on the Frame and run the binary on the host; the container holds the compilers and -dev headers that SteamOS's immutable root lacks, and the app links only against libraries the host already ships. This is the same approach OpenCE used ("the arm64 container") and it survives SteamOS updates, which wipe anything installed with `steamos-readonly disable` and `pacman`.

1. **Get a shell.** Desktop mode → Konsole, or enable SSH (`sudo systemctl enable --now sshd`, user `steamos`, set a password with `passwd` first). Claude Code already runs on the Frame over SSH this way, so an agent can work there directly.
2. **Make the container.** Podman and Distrobox ship with SteamOS. Use an Arch Linux ARM image so library versions track the host closely: `distrobox create -n mlxr -i docker.io/menci/archlinuxarm:latest --home ~/mlxr-home` (if that image is unavailable, `quay.io/fedora/fedora:41` or `docker.io/ubuntu:24.04` arm64 work; they only change package names). Enter with `distrobox enter mlxr`. The container shares `$HOME`, `/dev`, `/run/user`, and the network with the host, so `/dev/video-dec0`, `/dev/dri`, PipeWire and the SteamVR runtime JSON are all visible from inside.
3. **Install the toolchain (Arch names).** `pacman -Syu base-devel clang cmake ninja git pkgconf python glslang shaderc vulkan-headers vulkan-icd-loader vulkan-tools openssl libdrm pipewire libpulse mesa libxkbcommon wayland wayland-protocols v4l-utils libva ffmpeg openxr`. `openxr` gives `libopenxr_loader` and `xrinfo`; if it is missing build the loader from [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) with CMake (`-DBUILD_LOADER=ON`, static).
4. **FFmpeg.** The distro FFmpeg has `h264_v4l2m2m` and `hevc_v4l2m2m` with `AV_PIX_FMT_DRM_PRIME` output; confirm with `ffmpeg -decoders | grep v4l2m2m`. If the host's `libavcodec.so` SONAME differs from the container's, either build FFmpeg static inside the container (`--enable-v4l2-m2m --enable-libdrm --disable-everything --enable-decoder=h264,hevc,h264_v4l2m2m,hevc_v4l2m2m,opus --enable-parser=h264,hevc`) or ship the `.so` files next to the binary with an rpath of `$ORIGIN/lib`. Shipping next to the binary is the default plan.
5. **moonlight-common-c.** `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C build`; link `libmoonlight-common-c.a`, `libenet.a`, `libreed-solomon.a` plus `libcrypto`.
6. **Depth models.** LiteRT (TensorFlow Lite) Linux Arm64: build `tensorflow/lite` with CMake (`-DTFLITE_ENABLE_GPU=ON` for the OpenCL/GL delegate; the GL delegate failed under Lepton, so expect to run on XNNPACK CPU first and test the GPU delegate on native Turnip, which has OpenCL via rusticl only if enabled). Fallback: export ZipDepth to ONNX and run with onnxruntime-arm64, or write the inference as Vulkan compute (biggest effort, best result).
7. **Linking rules.** `ldd ./moonlight-xr` run on the **host** (not in the container) must show only libraries present on SteamOS: `libc`, `libm`, `libpthread`, `libvulkan.so.1`, `libdrm.so.2`, `libpipewire-0.3.so.0` or `libpulse.so.0`, `libcrypto.so.3`, `libxkbcommon`, `libwayland-client`. Everything else is static or in `./lib`. Run with `LD_LIBRARY_PATH` unset to prove it.
8. **Run from Game Mode.** Add `~/Games/MoonlightXR/moonlight-xr.sh` as a non-Steam game (the Lepton fork's `steam-frame/install.sh` already does this through Steam's local debugging port; reuse it). The wrapper unsets `MESA_GL_VERSION_OVERRIDE` and `MESA_GLSL_VERSION_OVERRIDE`, exports `XR_RUNTIME_JSON` only if the default lookup fails, and launches the binary. Alternatively, the SteamOS Devkit Client with the *Steam Linux Runtime 3.0 ARM64 (Sniper)* runtime, as Pixel VR Player documents.
9. **Iterate.** Edit in the container, `ninja`, then from a host shell `~/Games/MoonlightXR/moonlight-xr.sh` with SteamVR running, or launch from the library. Logs go to `~/Games/MoonlightXR/moonlight.log` and journald.

Common pitfalls: the container's `/tmp` is separate from the host's, so sockets must live under `/run/user/1000`; `distrobox` may need `--init` off; after a SteamOS update re-run `distrobox enter` once to re-link host bind mounts; CMake must be told `-DCMAKE_C_COMPILER=clang` because gcc on Arch ARM lags.

## Architecture

One process, five threads, no copies of video on the CPU. The decoded NV12 dma-buf is imported into Vulkan once per buffer and sampled directly by the colour pass; only the depth thread reads a 512×288 downscale back.

&#91;embedded content: native pipeline · 4 lanes: stream, GPU, depth, input\]

The rows are lanes: the stream lane (moonlight-common-c receive thread and the V4L2 decode thread) hands finished buffers to the GPU lane (the OpenXR frame-loop thread), which also feeds the depth lane (stage thread plus inference thread) and composes its result back into the warp. The input lane runs inside the frame loop's `xrSyncActions` pass and sends to the host; audio is its own thread off moonlight-common-c's audio callback, through the HRTF convolution from `xr-audio/` into PipeWire. In the fourth row the audio box is tied to the pipeline by that callback rather than by an arrow.

Module plan (new code is marked, the rest ports from Gilleece's `xr-renderer/`):

| Module | Role | Status |
| --- | --- | --- |
| `main.c`, `app_config.c` | argv, config file at `~/.config/moonlight-xr/`, log setup, launches the launcher UI or autostart | new |
| `backend/nvhttp.c`, `pairing.c`, `computer.c` | HTTPS to 47984, pairing with client cert, app list, launch/resume; port of moonlight-qt's `app/backend/` in C with libcurl or mbedTLS, or reuse `libcurl` + OpenSSL | new |
| `stream/session.c` | `LiStartConnection`, callbacks, reconnect, `LiRequestIdrFrame` on decoder error | new (modelled on moonlight-qt `session.cpp`) |
| `video/v4l2_decoder.c` | FFmpeg `h264_v4l2m2m`/`hevc_v4l2m2m` with `AV_PIX_FMT_DRM_PRIME`, or direct V4L2 ioctls if FFmpeg's wrapper mishandles the Iris source-change event; outputs `AVFrame` with `AVDRMFrameDescriptor` | new |
| `video/vk_import.c` | dma-buf → `VkImage` (NV12, `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM` or two planes), `VkImportMemoryFdInfoKHR`, DRM format modifier, cache keyed by fd/inode, `vkQueueWaitIdle`-free sync via `VkSemaphore` from the fence fd | new |
| `xr/xr_session.c` | instance with `XR_KHR_vulkan_enable2`, system, session, reference spaces, swapchains (one per layer, under the 16 cap), frame loop | rewrite |
| `xr/xr_vk.c`, `shaders/*.glsl → .spv` | render passes: colour pass, depth upsample, offset search, warp, UI panels, rooms, controller model; GPU timestamps | rewrite of `xr_gl.c`, `xr_shaders.c` |
| `xr/xr_depth.c` | staging readback via `vkCmdBlitImage` to a host-visible buffer, inference thread | rewrite |
| `depth/litert_backend.c` | ZipDepth fp16 on GPU delegate, int8 on XNNPACK; MiDaS optional | new (replaces `MidasDepthSource.java`) |
| `xr/xr_assets.c`, `ui/text.c` | panel textures rasterised natively (stb\_truetype or FreeType) instead of Java bitmaps | rewrite |
| `ui/panels.c`, `ui/launcher.c` | the bar, five-tab settings panel, picker, keyboard, PC list and app list as OpenXR quad panels | new (replaces `XrPanels.java`, `com.limelight` UI) |
| `input/xr_input.c`, `xr_gate.c`, `xr_pinch.c`, `xr_headaim.c`, `xr_gamepad.c`, `xr_keys.c` | Frame controller profile, hands (`XR_EXT_hand_tracking`), gaze (`XR_EXT_eye_gaze_interaction`), pointer, gamepad, head aim, keyboard | port |
| `input/pad_out.c` | gamepad events to `LiSendMultiControllerEvent`, rumble back via `AUDIO/CONTROLLER` callbacks to `xrApplyHapticFeedback` | new (replaces `XrPad.java`) |
| `audio/opus_out.c`, `audio/surround.c` | Opus decode, HRTF convolution, PipeWire stream | new + port of `xr-audio/` |
| everything else in `xr-renderer/` | layout, layers, rooms, glow, grade, rate governor, notice, math, log | port |

## Feature parity matrix

Every feature in the Steam Frame fork's README, with how the native app delivers it. "Port" means the C module moves over with its logic intact; "Rewrite" means the platform edge changes; "New" means it only existed in Java.

| Feature (Moonlight XR) | Native implementation | Kind |
| --- | --- | --- |
| PC list, pairing, app list, launch/resume, Open Desktop automatically | `backend/` in C over libcurl + OpenSSL, client cert stored under `~/.config/moonlight-xr/`; `seed-from-moonlight-qt.sh` imports moonlight-qt's pairing | New |
| Stream in VR (immersive OpenXR session) | `xr_session.c` on `XR_KHR_vulkan_enable2`; the runtime's first view config `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO` | Rewrite |
| Hardware decode (H.264/HEVC 8-bit) | V4L2 M2M via FFmpeg, DRM\_PRIME out, cached dma-buf import | New |
| Default 2560×1440 at the display rate; the frame-rate list offers every rate the headset reports | `xr_rate.c` + `XR_FB_display_refresh_rate`; request the stream rate or its lowest whole multiple, floor 72 | Port |
| Flat or curved screen, distance, width, height, tilt, rotate, curve, size | `xr_layout.c`; curve via a tessellated quad in a projection layer (no cylinder layer extension on this runtime) | Port + Rewrite |
| Head-locked vs world-locked | reference space `VIEW` vs `LOCAL`/`STAGE`; layer pose per `xr_layout.c` | Port |
| Move bar (6DOF drag), corner resize handles, grip or trigger holds | `xr_ui.c`, `xr_layout.c` hit tests | Port |
| Recentre keeps distance, size, curve, head lock | handle `XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING`; OpenCE's "recentre again on focus" fix | Port |
| Realtime 3D: ZipDepth (GPU fp16 512×288) and MiDaS, Comfort/Balanced/Strong presets, depth and convergence tracks, separation defaults 0.6 % / 0.5 % | LiteRT Linux build; XNNPACK CPU first, GPU delegate if OpenCL (rusticl) or Vulkan compute port later; `xr_depthmap.c` temporal average and scene-cut detector | New backend + Port |
| Depth-rate governor (halve depth rate before dropping display rate, never below 72) | `xr_rate.c` | Port |
| DIBR occlusion-aware gather warp, depth upsample guided by colour, offset search | GLSL ported to Vulkan GLSL, compiled with glslang to SPIR-V at build | Rewrite |
| Environments: passthrough, void, Home Theater, Grand Cinema, Synthwave; per-room brightness, glow, light, picture size; screen light over the room | `xr_room.c`, `xr_roommesh.c`, `xr_atlas.c` with ASTC textures (Adreno supports ASTC; confirm `VK_FORMAT_ASTC_*` in Turnip); passthrough via `XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND` plus the 16×16 transparent projection layer trick from `xr_layers.c` | Port |
| Pointer mode: laser, trigger click, A/X right click, stick scroll, wake on motion, 5 s sleep, stick-click off, ray hide, dot stays | `xr_input.c`, `xr_gate.c`, `xr_controller.c` → `LiSendMousePositionEvent`, `LiSendMouseButtonEvent`, `LiSendScrollEvent` | Port |
| Gamepad mode: one Xbox pad with d-pad and Back (Frame layout table in STEAM\_FRAME.md), rumble, shortcut hold View+grip, flip face buttons | `xr_gamepad.c` + `pad_out.c` → `LiSendMultiControllerEvent`; rumble from the `ConnectionListenerRumble` callback → `xrApplyHapticFeedback` | Port + New |
| Head aim (8 px/°, 2 °/s dead zone) | `xr_headaim.c` | Port |
| Hand tracking with pinch click and triple-pinch lock | `XR_EXT_hand_tracking` in `xr_pinch.c`; *verify* SteamVR exposes it on the Frame | Port |
| Look to point (eye gaze) | `XR_EXT_eye_gaze_interaction` in `xr_input.c` | Port |
| Show the controllers (model at each grip) | `xr_model.c` baked mesh | Port |
| The bar: head aim, gamepad, exit, environment, move, cog, keyboard, ray, 3D | `xr_ui.c`, `xr_layout.c`, panels rasterised natively | Port + Rewrite |
| Settings panel: Screen, Display, 3D, Picture, About, Room tabs; applies live and saves | `ui/panels.c` with a settings struct persisted as INI/JSON | New |
| Keyboard with Fn sheet, held Ctrl/Alt/Win | `xr_keys.c` → `LiSendKeyboardEvent` | Port |
| Sharpen (Quality), supersample | extra passes in `xr_vk.c` | Rewrite |
| Picture brightness, contrast, gamma, saturation | `xr_grade.c` in the colour pass | Port |
| Glow / ambilight, letterbox detection | `xr_ambilight.c`, `xr_glow.c`; costs one swapchain, so keep a count | Port |
| Click sound on panels | small PCM sample through PipeWire | New |
| Virtual surround (5.1/7.1 KEMAR HRTF, held to the screen) | `audio/surround.c` port of `xr-audio/` convolution; 37 HRTF files shipped | Port |
| Headset-off: mute and wait up to a minute | `XR_SESSION_STATE_VISIBLE`/`FOCUSED` transitions + `XR_EXT_user_presence` if present | Port |
| Performance stats overlay (display rate, warp GPU time, depth time/rate/age/skips, audio underruns, clock, battery) | GPU timestamps via `VK_QUERY_TYPE_TIMESTAMP`; battery from `/sys/class/power_supply/` | Rewrite |
| File log with rollover (5 MB, two files), verbosity setting | `xr_log.c` to `~/Games/MoonlightXR/moonlight.log` | Port |
| Report a problem (bundle settings + logs) | writes a `.zip` beside the log; no collector | Simplified |
| Debug knobs (`debug.moonlight.*` setprop, warp capture for `warp_lab.py`) | environment variables `MLXR_DEBUG_*` and a capture flag | Port |
| Desktop C unit tests (maths, depthmap, rate, input rules, layout, room parsers, surround) | `tests/` built with the same CMake, run on the Frame | Port |
| Languages (19) | strings table kept; English only shipped in v1 | Deferred |
| Check for updates, Ko-fi sheet | dropped | Out of scope |

## Milestones and acceptance gates

Each milestone ends with a test in the headset; nothing moves to the next until its gate passes. Estimates are for one agent working on the Frame with the owner launching builds; they are guesses, not commitments.

1. **M0: Probe and toolchain (1–2 days).** Distrobox up, CMake project skeleton, `xrinfo` output saved to `docs/runtime-probe.txt`, `v4l2-ctl --all -d /dev/video-dec0` and `v4l2-ctl --list-formats-out` saved beside it, hello-OpenXR Vulkan triangle in both eyes launched from the Steam library. *Gate:* triangle visible at the runtime's native refresh rate; the probe lists which of `XR_KHR_vulkan_enable2`, `XR_EXT_hand_tracking`, `XR_EXT_eye_gaze_interaction`, `XR_FB_display_refresh_rate`, `XR_FB_passthrough`, `XR_KHR_composition_layer_cylinder`, `XR_VALVE_frame_controller_interaction` exist natively.
2. **M1: Pair and stream flat (3–5 days).** `backend/` pairs with Sunshine/Apollo, lists apps, launches one; moonlight-common-c delivers NALs; software decode (FFmpeg `h264`) into a staging buffer; one quad layer shows the stream. Audio through PipeWire. *Gate:* 1080p60 flat stream with sound, mouse moves from the right controller trigger.
3. **M2: Hardware zero-copy decode (1–2 weeks, highest risk).** `v4l2_decoder.c` with DRM\_PRIME, `vk_import.c` with the per-buffer cache, NV12 sampling in the colour pass, source-change handling, decoder reset and `LiRequestIdrFrame` on error. *Gate:* 1440p90 HEVC at 80 Mbps for 30 minutes with no headset freeze, decode time under 4 ms in the stats overlay, zero CPU copies in `perf top`.
4. **M3: Input parity (1 week).** Port `xr_input.c`, `xr_gate.c`, `xr_gamepad.c`, `xr_headaim.c`, `xr_keys.c`, `pad_out.c`, rumble. *Gate:* the Frame layout table in STEAM\_FRAME.md passes button by button in a gamepad tester on the host; keyboard types; View+grip toggles modes.
5. **M4: Screen, bar and panels (1–2 weeks).** `xr_layout.c`, `xr_ui.c`, native text rasteriser, bar buttons, settings panel tabs with persistence, picker, environment void, corner resize, recentre. *Gate:* every bar button and settings row from the README does what it says.
6. **M5: Rooms, passthrough, glow, grade (1 week).** `xr_room.c` family with ASTC atlases, passthrough projection-layer trick, `xr_glow.c`, `xr_grade.c`, screen light. *Gate:* all three rooms load under the swapchain cap with passthrough still available; the fork's Lepton limitations list is empty.
7. **M6: Realtime 3D (2–4 weeks).** LiteRT build, ZipDepth on XNNPACK, `xr_depth.c` readback, warp shaders in Vulkan, presets, governor, stats. Then the GPU delegate or a Vulkan-compute port if CPU inference stays under 12 maps/s. *Gate:* 3D on at 1440p90 holds 90 Hz for 10 minutes; depth age under 100 ms on GPU inference.
8. **M7: Hands, gaze, surround, headset-off, logging, report (1 week).** *Gate:* triple pinch locks and unlocks; gaze click works with pinch; surround places 7.1 test tones around the head; doffing pauses and resumes within a minute.
9. **M8: Packaging and release (3–5 days).** `install.sh` modelled on the fork's, Steam shortcut, `ldd` audit, tarball release on GitHub, README with the settings tables carried over, CI build on a GitHub ARM runner. *Gate:* a fresh Frame goes from download to streaming in under five minutes with no SSH.

## Hard risks and mitigations

The decoder and the kernel are the project's real risk; everything else is engineering time.

| Risk | Why it bites | Mitigation |
| --- | --- | --- |
| Headset freeze from dma-buf import churn | `msm_gem_import()` NULL deref on 6.18 when imports are freed per frame ([SteamOS #2882](https://github.com/ValveSoftware/SteamOS/issues/2882)) | Import each V4L2 CAPTURE buffer once, key the cache by buffer index and fd, free only on decoder teardown; never recreate on resolution change without a full stop |
| Decoder never produces frames | Iris lacks `DYN_RESOLUTION`; CAPTURE stays 384×256 until `V4L2_EVENT_SOURCE_CHANGE` ([SteamOS #2869](https://github.com/ValveSoftware/SteamOS/issues/2869)) | Subscribe to the event before `STREAMON`, allocate CAPTURE buffers only after it, handle it again on IDR after a host resolution change; if FFmpeg's wrapper stalls, drive V4L2 directly (parsec-on-frame shows the sequence) |
| FFmpeg SONAME mismatch with the host | Container FFmpeg differs from SteamOS's | Ship `libav*.so` in `./lib` with `$ORIGIN` rpath, or static link |
| `xrEndFrame` blocks 4–5 ms | SteamVR waits for the GPU inside it | End the previous frame at the start of the next, as OpenCE does; keep the decode thread independent so a blocked frame loop never stalls the decoder |
| Swapchain cap of 16 | Each quad, panel, room, glow and the passthrough projection layer costs one | Budget in `xr_layers.c`: video 1, room 1, passthrough 1, bar 1, panel 1, keyboard 1, picker 1, pointer dot 1, controller model 1, glow 1, notice 1 = 11; pack small UI textures into one atlas swapchain |
| Hand tracking or gaze missing natively | Only confirmed inside Lepton as untested | Feature-flag both behind extension checks from M0's probe; the app must run without them |
| GPU depth inference | LiteRT's GL delegate failed under Lepton; Turnip has no vendor OpenCL | Start on XNNPACK CPU (ZipDepth int8 ran 12 maps/s on XR2 Gen 1, so the 8 Gen 3 CPU should reach 20); then port the network to Vulkan compute, which is the only path with no driver dependency |
| Latency parity with Steam Link | Valve's stack has foveated streaming and a dedicated 6 GHz link | Measure decode-to-photon with a frame counter overlay; accept that Moonlight wins on hosts and features, not on raw latency |
| ASTC on Turnip | Rooms ship ASTC | Check `VK_FORMAT_ASTC_4x4_UNORM_BLOCK` support; fall back to the `tools/atlas_astc.py` input PNGs as RGBA8 |
| SteamOS updates | 0.4.x betas change kernel and SteamVR | Pin the probe outputs in the repo and diff them after each update; keep the Lepton APK as the fallback |
| Text rendering | Java drew every panel | stb\_truetype into an atlas; one font (Inter or Noto Sans), SDF for crisp VR text |
| GPLv3 compliance | All sources are GPLv3 | Publish the repo with the full source and the licence notices for OpenXR SDK, LiteRT, ZipDepth, MiDaS, KEMAR, and the CC BY 4.0 rooms and controller model credited to Sean Gilleece / Woodford XR |

## Verification tests on the headset

Run these in order on the Frame before writing application code; their outputs go into the repo under `docs/probe/` so the implementing LLM reasons from facts, not from this document.

- [ ] `uname -r; cat /etc/os-release; steamos-readonly status`: kernel and SteamOS build
- [ ] `ls -l /dev/video* /dev/dri/*; v4l2-ctl -d /dev/video-dec0 --all; v4l2-ctl -d /dev/video-dec0 --list-formats-out; --list-formats`: decoder node, codecs, pixel formats (expect `NV12` or `QC08C`/`QC10C` compressed; a UBWC-only CAPTURE format changes the import plan)
- [ ] `vulkaninfo --summary` on the host: Turnip version, `VK_EXT_external_memory_dma_buf`, `VK_EXT_image_drm_format_modifier`, `VK_KHR_sampler_ycbcr_conversion`, ASTC formats
- [ ] `cat ~/.config/openxr/1/active_runtime.json` and `ls ~/.local/share/Steam/steamapps/common/SteamVR/`: runtime library name and path
- [ ] `xrinfo` (or a 40-line C probe that enumerates `xrEnumerateInstanceExtensionProperties`, view configs, blend modes, display refresh rates, interaction profiles) with SteamVR running: the extension list
- [ ] `ffmpeg -decoders | grep v4l2m2m` and `ffmpeg -f lavfi -i testsrc=size=1920x1080:rate=60 -c:v libx264 -t 5 t.mp4 && ffmpeg -c:v h264_v4l2m2m -i t.mp4 -f null -`: hardware decode works through FFmpeg
- [ ] `pw-cli info 0` and `pactl info`: audio server reachable from inside the container
- [ ] Launch the Flathub Moonlight (aarch64) in desktop mode and read its stats overlay: does stock moonlight-qt already get `v4l2m2m` on this device (useful baseline for decode latency)
- [ ] Run the Lepton APK (v0.3) and note its stats overlay numbers at 1440p: the baseline the native build must beat
- [ ] Steam Link to the same host, same game: the latency and quality bar to compare against
- [ ] After M2: `dmesg -w` in a second terminal during a 30-minute HEVC stream: no `msm_gem` or `iris` warnings
- [ ] After M3: on the host, open a gamepad tester and tick every row of the Frame layout table

## Packaging, launching and licensing

Ship a tarball that unpacks to `~/Games/MoonlightXR/` and a Steam shortcut; a Flatpak is a later nicety, not a requirement.

- **Layout:** `moonlight-xr` (binary, rpath `$ORIGIN/lib`), `lib/` (FFmpeg, LiteRT, anything not on the host), `assets/` (rooms, atlases, models, HRTF, fonts, licences), `moonlight-xr.sh` (wrapper: unset `MESA_GL_VERSION_OVERRIDE`, `MESA_GLSL_VERSION_OVERRIDE`; `cd` to its own dir; exec the binary), `install.sh` (copies the tree and adds the shortcut through Steam's local debugging port, as the fork's `steam-frame/install.sh` does), `autostart.conf` (optional host UUID and app name).
- **Launching:** from the Steam library in Game Mode with SteamVR started by Steam; from desktop mode or SSH for debugging with SteamVR already running. The binary must behave when launched without a runtime: print the reason and exit 1.
- **Config:** `~/.config/moonlight-xr/config.ini` with every setting from the parity matrix, written commented-out at its default so new defaults reach old installs (OpenCE's approach).
- **Logs:** `~/Games/MoonlightXR/moonlight.log` plus `moonlight.previous.log`, 5 MB rollover.
- **Build reproducibility:** `build.sh` creates the Distrobox if missing, installs packages, builds, and runs `ldd` against the host; CI on `ubuntu-24.04-arm` GitHub runners produces the same tarball.
- **Licence:** GPLv3 (`LICENSE.txt`), with `assets/licenses/` for OpenXR SDK (Apache 2.0), LiteRT (Apache 2.0), ZipDepth (MIT), MiDaS (MIT), KEMAR HRTF (cite the MIT Media Lab report), rooms and controller model (CC BY 4.0, Sean Gilleece / Woodford XR), moonlight-common-c and moonlight-qt (GPLv3). Credit Gilleece's Moonlight XR as the origin of every XR module in the README.

## Ready-to-paste prompt for the implementing LLM

Paste this as the first message to Claude Code (or any coding agent) running on the Frame over SSH, with this document attached or linked.

```markdown
You are building **Moonlight XR Native**: a native Linux Arm64 OpenXR game-streaming client for the Valve Steam Frame, in C (C++ only where a library demands it), with feature parity to Moonlight XR as described in the `steam-frame` branch of https://github.com/simplysanatori/moonlight-xr-lepton (README.md and STEAM_FRAME.md). You are running ON the Steam Frame itself (SteamOS, Arm64, user `steamos`). I launch builds in the headset and report back; you read logs and change code.

Read first, in this order, before writing code:
1. The attached plan "Moonlight XR Native: Steam Frame Build Plan (LLM Handoff)": its platform facts, risks and milestones are binding.
2. https://raw.githubusercontent.com/simplysanatori/moonlight-xr-lepton/steam-frame/STEAM_FRAME.md and the fork's README.md (feature list, settings tables, code layout).
3. `app/src/main/jni/xr-renderer/*.c` in that repo: port these, do not reinvent them.
4. https://github.com/SaberMage/parsec-on-frame (V4L2 Iris decode + dma-buf on this device), https://github.com/Pixelplanet/Pixel-VR-Player (OpenXR + Vulkan + FFmpeg v4l2m2m on the Frame), https://github.com/OpenCommunityEdition/OpenCE/pull/85 (SteamVR pacing and Steam launch pitfalls).
5. moonlight-qt `app/backend/` and `app/streaming/video/ffmpeg-renderers/drm.cpp`, `plvk.cpp` for the protocol and decode patterns.

Hard rules:
- Everything builds and runs on this device. Set up a Distrobox (Arch Linux ARM image, fall back to Fedora/Ubuntu arm64) for the toolchain; the binary runs on the host and `ldd` on the host must show only SteamOS-provided libraries plus `./lib`.
- Video decode is V4L2 M2M on `/dev/video-dec0` with `AV_PIX_FMT_DRM_PRIME` output, imported into Vulkan via dma-buf. Import each buffer ONCE and cache it; never import/free per frame (kernel bug, SteamOS issue #2882). Subscribe to `V4L2_EVENT_SOURCE_CHANGE` before STREAMON and allocate CAPTURE buffers only after it (issue #2869). Never use VP9.
- OpenXR via `XR_KHR_vulkan_enable2` on SteamVR. Respect the 16-swapchain cap. Passthrough needs a transparent projection layer under the quads. End the previous frame at the start of the next, because SteamVR's xrEndFrame blocks.
- Controllers via `/interaction_profiles/valve/frame_controller`; grips are squeeze/value; a missing binding path must not fail the whole suggestBindings call.
- Gate every feature on an extension or capability check captured in `docs/probe/`; the app must run with any of hand tracking, eye gaze, refresh-rate control or passthrough absent.
- Work milestone by milestone (M0–M8 in the plan). Do not start the next until I confirm the gate in the headset. Keep a `docs/dev/STATUS.md` with what is confirmed on hardware versus reasoned from code.
- Reuse Gilleece's C modules with their file names and logic; replace only the Android, GL ES, MediaCodec and Java edges. Keep his unit tests building.
- GPLv3 throughout; carry every third-party licence into `assets/licenses/`.
- Log to `~/Games/MoonlightXR/moonlight.log`; on any decoder error call `LiRequestIdrFrame` and log the V4L2 ioctl that failed.

Start with M0: create the Distrobox, install the toolchain, write the OpenXR/V4L2/Vulkan probe, save its output under `docs/probe/`, and build a two-eye Vulkan triangle launched from the Steam library. Report the probe results and ask me to confirm the triangle before moving to M1.
```

## Sources

- [simplysanatori/moonlight-xr-lepton README](https://github.com/simplysanatori/moonlight-xr-lepton) and [STEAM\_FRAME.md](https://raw.githubusercontent.com/simplysanatori/moonlight-xr-lepton/steam-frame/STEAM_FRAME.md)
- [Gilleece/moonlight-android-xr](https://github.com/Gilleece/moonlight-android-xr)
- [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c), [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt), [moonlight-android](https://github.com/moonlight-stream/moonlight-android)
- [awesome-steam-frame](https://github.com/kosantosbik/awesome-steam-frame)
- [Steamworks: Steam Frame compatibility](https://partner.steamgames.com/doc/steamhardware/steamframe/compatibility), [Steam Frame overview](https://partner.steamgames.com/doc/steamhardware/steamframe), [Custom engines](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom), [Loading games](https://partner.steamgames.com/doc/steamhardware/steamframe/loadgames)
- [OpenCE PR #85: native OpenXR on the Steam Frame](https://github.com/OpenCommunityEdition/OpenCE/pull/85)
- [Pixel VR Player](https://github.com/Pixelplanet/Pixel-VR-Player)
- [parsec-on-frame](https://github.com/SaberMage/parsec-on-frame)
- [SteamOS #2882 msm\_gem\_import freeze](https://github.com/ValveSoftware/SteamOS/issues/2882), [#2869 Iris DYN\_RESOLUTION](https://github.com/ValveSoftware/SteamOS/issues/2869), [#2865 VP9 firmware crash](https://github.com/ValveSoftware/SteamOS/issues/2865), [#2904 portal crash in Game Mode](https://github.com/ValveSoftware/SteamOS/issues/2904)
- [WiVRn PR #1130 Steam Frame support](https://github.com/WiVRn/WiVRn/pull/1130)
- [Phoronix: Steam Frame uses Turnip](https://www.phoronix.com/news/Steam-Frame-Turnip-Vulkan)
- [Lepton open-sourced (GamingOnLinux)](https://www.gamingonlinux.com/2026/09/lepton-from-valve-to-run-android-games-on-linux-is-now-open-source/)
