# Headset test: GPU work for 144 Hz (perf/glow-gpu-133)

For the dev build of `perf/glow-gpu-133` in `app-dev` (check `app-dev/DEV-BUILD.txt`). Seven runs, about a minute each, about 10 minutes in all. What each switch does is in [gpu-144hz.md](gpu-144hz.md). Do not paste addresses, host names or Wi-Fi names from the logs into a report.

Every run, the same way:

- Refresh rate 144 Hz, stream 3440x1440 (or the widest you use).
- Show controllers: Frame. Hold both controllers where you can see them over the picture.
- Put the run's switches in `app-dev/dev.env` (only those lines uncommented), then start the dev build.
- Play or scroll for 60 s with a few head turns, then quit.
- Keep `moonlight-console.log` from that run: its `gpu144:`, `xr: ... gpu ...`, `xr: frames:` and `xr: compositor:` lines.

| Run | `dev.env` | Scene | Look for | Checks the fix |
| --- | --- | --- | --- | --- |
| A | `MLXR_CURVE_ONEPASS=0` `MLXR_GLOW_HOLE=0` `MLXR_ROOM_OCCLUDE=0` | Curved, Glow Wide, no room, passthrough off | The 1.3.1 figures to compare B with | No (switches off) |
| B | none (defaults) | Same as A | A seam or dark ring where the glow meets the picture; controllers drawn wrong | Yes |
| C | A's three lines | Home Theater, 3D on (2D to 3D), Glow Soft | The 1.3.1 figures to compare D with | No (switches off) |
| D | none | Same as C | Room showing through the picture; a ring at the glow; controllers wrong | Yes |
| E | none | Curved, Glow Wide, passthrough on | Smear or a ring round the picture | Yes |
| F | `MLXR_CURVE_REUSE=1` | Curved, a still desktop: head still for 20 s, then slow head turns | Smear or wobble when the head moves | Yes |
| G | `MLXR_FDM_LAYER=0` | Curved, small text at the picture's edges | Text at the edges sharper than in B, or the same | Yes |

"Checks the fix": with the controllers in the picture's layer and ONEPASS on, the controllers are drawn in the same rendering as the picture. Any flicker, a missing controller or a controller drawn through the picture in B, D, E, F or G is worth a note.

Record for each run: the log lines above, and one line on what you saw. In C and D, check the log says `room in the picture's layer`; if it does not, 3D was not on.
