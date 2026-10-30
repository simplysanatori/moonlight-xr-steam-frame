# Developer notes

## Read this first

These are the working notes kept while the project was built, under its working name Moonlight XR Native. The notes use that name. They are history, not instructions. The current state is in [BUILDING.md](../BUILDING.md), [USER_GUIDE.md](../USER_GUIDE.md) and [CHANGELOG.md](../CHANGELOG.md). Users want the [README](../../README.md).

The repository is public and the current release is v1.0.0. Commit hashes quoted in the notes predate a history rewrite and no longer resolve. Paths like `~/Games/MoonlightXRNative/...`, `lanes/` and `ref/` were on the developer's machine. They are not in the repository.

## Plans and status

| File | What it is |
| --- | --- |
| [STATUS.md](STATUS.md) | What was confirmed in the headset and what was only tested without it, by date |
| [HANDOFF.md](HANDOFF.md) | Session hand-off: where things are, how to build and test, open problems, working rules |
| [SESSION_4.md](SESSION_4.md) | The hand-off at the start of the fourth session (2026-10-11): the black screen fix, the lanes that landed after it, the working rules |
| [BUILD_PLAN.md](BUILD_PLAN.md) | The original build plan with the platform facts and milestones M0 to M8. Superseded: it describes a Distrobox/Arch container route that was never used. The build runs on the Frame's own toolchain, see [BUILDING.md](../BUILDING.md) |
| [ORCHESTRATION_PLAN.md](ORCHESTRATION_PLAN.md) | How the work was split into parallel lanes. Superseded for the same reason |
| [lanes.md](lanes.md) | The lanes and their state |

## Headset test scripts

What the developer asked the owner to check in the headset after a session.

| File | What it is |
| --- | --- |
| [HEADSET_TEST_3.md](HEADSET_TEST_3.md) | After session 3 (2026-10-10): the menu panel drawing fix and room judder |
| [HEADSET_TEST_4.md](HEADSET_TEST_4.md) | After the black screen fix (2026-10-11) |
| [HEADSET_TEST_5.md](HEADSET_TEST_5.md) | 2026-10-11: double click, text sharpness, pulling the picture, the flat window, network choice, the Exit button, bitrate |
| [GAMEPAD_CERT.md](GAMEPAD_CERT.md) | 2026-10-29: the 20-minute gamepad certification pass on the Frame with `mlxr-gamepad-probe` (Bluetooth and USB-C pads, Steam Input on and off, Steam Controller gyro routes) |

## Milestone reports

| File | Milestone |
| --- | --- |
| [M4_REPORT.md](M4_REPORT.md) | M4: the in-headset interface |
| [M5_REPORT.md](M5_REPORT.md) | M5: rooms, glow, controller models, click sound, supersampling |
| [M6_REPORT.md](M6_REPORT.md) | M6: real-time 2D to 3D |

## Area reports

| File | Area |
| --- | --- |
| [reports/video.md](reports/video.md) | `video/`: V4L2 hardware decode and Vulkan import |
| [reports/backend.md](reports/backend.md) | `backend/`: pairing, server info, hosts, `mlxr-cli` |
| [reports/xr-core.md](reports/xr-core.md) | `xr/core/`: the ported Moonlight XR modules |
| [reports/audio.md](reports/audio.md) | `audio/`: Opus, PulseAudio, virtual surround |
| [reports/ui.md](reports/ui.md) | `ui/`: text, panels, settings |
| [i18n.md](i18n.md) | The headset UI's translations: where they come from, the language setting, CJK fonts, the overflow test, what is left in English |
| [reports/shaders.md](reports/shaders.md) | `shaders/`: the Vulkan passes |
| [reports/depth.md](reports/depth.md) | `depth/`: LiteRT and the depth models |
| [reports/input.md](reports/input.md) | `input/`: gamepads, mouse, rumble |
| [reports/keyboard.md](reports/keyboard.md) | The headset keyboard and the disconnect chords |
| [reports/rooms.md](reports/rooms.md) | `rooms/`: room and model loading |
| [reports/launcher.md](reports/launcher.md) | `launcher/`: the patched moonlight-qt and every difference from stock |
| [reports/packaging.md](reports/packaging.md) | `packaging/`: installer, release, bug report |
| [reports/frame-models.md](reports/frame-models.md) | The Steam Frame controller model converter |
| [reports/au-scatter.md](reports/au-scatter.md) | The black picture of 2026-10-11: the decoder was given too few parts of each frame |
| [reports/adapter-detect.md](reports/adapter-detect.md) | Detecting whether a Steam Frame Wireless Adapter is present |
| [reports/netpath-quality.md](reports/netpath-quality.md) | Automatic network choice by latency and drops, not only link quality |
| [reports/usb-cable.md](reports/usb-cable.md) | The USB cable as a third network path: the Frame's USB gadget network, candidate addresses, hot-plug, host setup |
| [reports/launcher2.md](reports/launcher2.md) | Bitrate default, larger flat UI, Help link |
| [reports/launcher-return.md](reports/launcher-return.md) | Getting the flat window back after a stream ends |
| [reports/double-click.md](reports/double-click.md) | Making double clicks work with the Frame controller as the mouse |
| [reports/grab-pull.md](reports/grab-pull.md) | Pulling the held picture closer with the stick |
| [reports/eye-swapchains.md](reports/eye-swapchains.md) | One swapchain per eye for every projection layer |
| [reports/refresh.md](reports/refresh.md) | Refresh rates up to 144 Hz |

The platform probe from the first session is in [docs/probe](../probe/SUMMARY.md).
