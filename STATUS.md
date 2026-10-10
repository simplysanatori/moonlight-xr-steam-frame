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
- M1: **video and mouse confirmed on hardware** 2026-10-10 11:37 (owner: "worked well"). Log: DESKTOP-EXAMPLE at 192.0.2.10 (Sunshine 7.1.431), Desktop launched, HEVC 1080p60, software decode 7–12 ms, RTT 2 ms, 0 IDR requests, 72 Hz display; frames shown follow desktop activity (27–54 fps). Owner saw the host cursor trail slightly behind the local pointer dot (expected: the dot is drawn locally at display time, the cursor comes back through encode, network, software decode and the next 72 Hz frame). Sound confirmed in a second run (owner: "audio worked"). **M1 gate passed.** Found in that run: (1) the Bluetooth keyboard (EXAMPLE0 Q1 Pro) did nothing: there was no keyboard path; now read from evdev and forwarded while focused and streaming, grabbed so keys reach only the host (untested in the headset). (2) Single and double clicks failed while press-and-drag worked: trigger pulls jolt the aim past Windows' 4 px click slop; the cursor is now held from the first trigger touch until the aim moves ~38 px (drag) and for 0.35 s after release (double click) (untested in the headset).
- M2: hardware decode integrated (4ccbbe6): V4L2 Iris decoder → cached dma-buf import → warp pass into the quad. Headless 1080p60 H.264 file: 1.5 ms decode, 0 errors, kernel log clean. **Owner accepted M2 2026-10-10 ("looks good")** after a 77 s in-headset run at 2560x1440 90 fps HEVC 80 Mbps: hardware decode 3.7–3.8 ms (gate < 4 ms), 0 decoder errors, 0 IDR requests from the decoder, kernel log clean (no iris/msm_gem/smmu/hangcheck lines), no freeze. Not met as written: the 30-minute run was only done headless by lane V (162001 frames clean); in the headset some packets were lost at 80 Mbps at 72 s and the owner ended the session from the PC at 76 s. Defaults are now 2560x1440x90 at 50 Mbps. Stream frames above the 72 Hz display rate are skipped (891 of 4975); M4's rate logic should request the display rate.
- Order changed by the owner: M4 (UI) before M3 (gamepad). Owner asked: drop the click hold (done, the One Euro filter fixes the jitter); gamepad toggle as in Moonlight XR (hold grip + menu/View); UI options to hide the dot (the host cursor is drawn into the video by Sunshine and the Moonlight protocol cannot hide it); screen expand/shrink; reuse Moonlight XR's look and assets.
- M3..M8: not started. Worker lanes are listed in `docs/lanes.md`.
