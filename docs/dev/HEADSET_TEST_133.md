# Headset test: the 1.3.3 candidate (release/1.3.3)

For the combined dev build of `release/1.3.3` in `app-dev` (check `app-dev/DEV-BUILD.txt`): the 144 Hz GPU savings ([gpu-144hz.md](gpu-144hz.md)), the Aspect ratio setting and the strict layer gate. Nine short runs, about 15 minutes in all. Do not paste addresses, host names or Wi-Fi names from the logs into a report.

## Before the first run

- The dev build shares `config.ini` with `app/` (1.3.2). Its first start converts the saved screen widths to sizes once (a 16:9 picture's width at the same height) and saves the file. 1.3.2 does not know that and would show a 21:9 screen narrower, then the next dev start would convert again. So first back the file up:
  `cp ~/.config/moonlight-xr/config.ini ~/.config/moonlight-xr/config.ini.pre133`
  Use only the dev build until the test is done, then put the backup back before you start `app/` again.
- Leave the launcher on Ultrawide (21:9), 3440x1440, for runs 1 to 7: the conversion uses the shape of the first stream, so run 1 must be the 21:9 one you normally use.
- `app-dev/dev.env`: every line commented out (defaults) unless the run says otherwise. Only the run's lines uncommented.

Every run, unless it says otherwise:

- Refresh rate 144 Hz, stream 3440x1440.
- Show controllers: Frame. Hold both controllers where you can see them over the picture for a few seconds.
- About 60 s of play or scrolling with a few head turns, then quit.

## The runs

| Run | `dev.env` | Scene | Look for |
| --- | --- | --- | --- |
| 1 | none (defaults) | Curved, Glow Wide, no room, passthrough off | **Screen size:** the screen is as wide and as tall as it was in 1.3.2 (the Size row may show a smaller number, that is the 16:9 size now). **GPU:** a seam or dark ring where the glow meets the picture; controllers flickering, missing or drawn through the picture. **Beam sweep** (below). |
| 2 | `MLXR_CURVE_ONEPASS=0` `MLXR_GLOW_HOLE=0` `MLXR_ROOM_OCCLUDE=0` | Same as 1 | Nothing to look for: the 1.3.1 figures to compare run 1 with |
| 3 | run 2's three lines | Home Theater, 3D on (2D to 3D), Glow Soft | Nothing to look for: the 1.3.1 figures to compare run 4 with |
| 4 | none | Same as 3 | The room showing through the picture; a ring at the glow; controllers wrong |
| 5 | none | Curved, Glow Wide, passthrough on | Smear or a ring round the picture |
| 6 | `MLXR_CURVE_REUSE=1` | Curved, a still desktop: head still for 20 s, then slow head turns | Smear or wobble when the head moves |
| 7 | `MLXR_FDM_LAYER=0` | Curved, small text at the picture's edges | Text at the edges sharper than in run 1, or the same |

In runs 3 and 4 the log must say `room in the picture's layer`; if it does not, 3D was not on.

### Beam sweep (in run 1)

Open the keyboard. Sweep the controller's ray slowly across the keys and the gaps between them, then over the keyboard's bar and its buttons, then off the keyboard and back, for about 20 s. Look for the white streak up through the view. Any streak is worth a note, with roughly where the ray was.

### Aspect ratio (runs 8 and 9, defaults)

| Run | In the launcher's Settings | Look for |
| --- | --- | --- |
| 8 | Aspect ratio to Widescreen (16:9). Video resolution should move to 1440p (2560x1440) by itself. Start a stream. | The screen keeps its height and is narrower than in run 1. |
| 9 | Aspect ratio back to Ultrawide (21:9). Video resolution should be 1440p (3440x1440) again. Start a stream. | The screen is the same as in run 1, as wide as in 1.3.2. |

Then quit and put the backup back:
`cp ~/.config/moonlight-xr/config.ini.pre133 ~/.config/moonlight-xr/config.ini`

## What to send back

One line per run on what you saw, and these lines from `~/Games/MoonlightXRNative/moonlight.log` (each run starts at its `== moonlight-xr starting` line):

```
grep -E "== moonlight-xr starting|config: screen widths are sizes now|gpu144:|one projection layer|xr: frames:|xr: compositor:|beam streak switches|beam place:" ~/Games/MoonlightXRNative/moonlight.log ~/Games/MoonlightXRNative/moonlight-console.log
```

- `config: screen widths are sizes now (3440x1440 picture): width_cm A -> B, ...` must appear once, in run 1 only. B should be about A / 1.31.
- `xr: frames: ... beam moved in the list on 0` in run 1 (the beam sweep). Any `beam place:` line that says `(moved)` is worth sending in full.
- `gpu144:`, `xr: gpu144:` and `xr: compositor: ... gpu` lines in every run, for the A/B of run 1 against 2 and of run 4 against 3.
