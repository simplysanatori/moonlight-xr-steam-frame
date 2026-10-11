# Developer notes

These are the working notes from building the client, kept for anyone who works on the code. They were written during development and describe the state at the time. Users want the [README](../../README.md) and the [user guide](../USER_GUIDE.md).

The project was called Moonlight XR Native while it was built. The notes use that name.

## Plans and status

| File | What it is |
| --- | --- |
| [STATUS.md](STATUS.md) | What was confirmed in the headset and what was only tested without it, by date |
| [HANDOFF.md](HANDOFF.md) | Session hand-off: where things are, how to build and test, open problems, working rules |
| [BUILD_PLAN.md](BUILD_PLAN.md) | The original build plan with the platform facts and milestones M0 to M8 |
| [ORCHESTRATION_PLAN.md](ORCHESTRATION_PLAN.md) | How the work was split into parallel lanes |
| [lanes.md](lanes.md) | The lanes and their state |
| [HEADSET_TEST_3.md](HEADSET_TEST_3.md) | A headset test script from the third session |

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
| [reports/shaders.md](reports/shaders.md) | `shaders/`: the Vulkan passes |
| [reports/depth.md](reports/depth.md) | `depth/`: LiteRT and the depth models |
| [reports/input.md](reports/input.md) | `input/`: gamepads, mouse, rumble |
| [reports/keyboard.md](reports/keyboard.md) | The headset keyboard and the disconnect chords |
| [reports/rooms.md](reports/rooms.md) | `rooms/`: room and model loading |
| [reports/launcher.md](reports/launcher.md) | `launcher/`: the patched moonlight-qt and every difference from stock |
| [reports/packaging.md](reports/packaging.md) | `packaging/`: installer, release, bug report |
| [reports/frame-models.md](reports/frame-models.md) | The Steam Frame controller model converter |

The platform probe from the first session is in [docs/probe](../probe/SUMMARY.md).
