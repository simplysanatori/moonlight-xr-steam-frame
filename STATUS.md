# Status

What is confirmed on the Steam Frame versus reasoned from code. The orchestrator keeps this file; lanes report in their own `REPORT.md`.

## Deviations from the handoff plan
- **Host build, no Distrobox.** The SteamOS image on this Frame ships clang 19, lld, cmake, ninja, glslc and the -dev headers for Vulkan, FFmpeg, OpenSSL, curl, Opus, PulseAudio and PipeWire (pacman-owned factory packages, not user installs). Building on the host removes the glibc/SONAME mismatch the container rule guarded against. `build.sh` stops with a clear message if an update removes a tool; a container is the fallback then.
- **OpenXR loader from source.** SteamOS's `libopenxr_loader.a` is an unusable thin archive; the loader is built static from `third_party/OpenXR-SDK`.
- **Vulkan 1.2 instance.** SteamVR 2.18.2 accepts Vulkan 1.0 to 1.2; newer features come in as extensions.
- **Paths.** Project at `~/Games/MoonlightXRNative/` (the Lepton build owns `~/Games/MoonlightXR/`). Log at `~/Games/MoonlightXRNative/moonlight.log`.

## M0 — probe and toolchain
| Item | State | Evidence |
| --- | --- | --- |
| Toolchain on host | confirmed | `build.sh` builds; `tools/ldd-audit.sh` passes (libvulkan, libm, libc only) |
| Probe saved | confirmed | `docs/probe/` and `SUMMARY.md` |
| Upstream unit tests (16 suites) | confirmed | pass on the host unchanged with clang (lane X ports them) |
| FFmpeg v4l2m2m H.264/HEVC decode | confirmed | `docs/probe/ffmpeg-v4l2m2m-test.txt`, no kernel warnings |
| moonlight-common-c static | confirmed | builds against host OpenSSL 3.2 |
| Two-eye triangle, launched from Steam | **confirmed on hardware 2026-10-10** | Owner saw the triangle in the headset from the Steam shortcut "Moonlight XR Native" (appid 2761093165); log: FOCUSED, 72 fps at 72 Hz, clean exit. |
| Native refresh rate | reasoned | SteamVR offers one rate and picks it (90 or 72 seen); the app follows it |

## Milestones
- M0: **gate passed** 2026-10-10 (owner saw the triangle in the headset).
- M1..M8: not started. Worker lanes started early per the plan are listed in `docs/lanes.md`.
