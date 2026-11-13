# GPU work for 144 Hz (lane gpu-144, branch perf/glow-gpu)

At 144 Hz a display frame is 6.94 ms. Owner logs from 1.3.x put the curved picture's layer at:

- 144 Hz, 3440x1440, no room, Glow on: warp 1.2 ms, models 0.8 ms, curve 2.0 ms (glow 0.09 ms), app GPU 3.3 to 4.0 ms. Fits.
- 90 Hz, a room in the picture's layer: room 5.7 ms, curve 7.7 ms, warp 6 ms. The curve figure already contains the room (its timer is opened before the room is drawn), so the curve's own part is about 2 ms. With a room the frame does not fit 144 Hz.

So the room is the largest cost, then the curve's own draws (glow cylinder and picture), at 3408 x 3408 per eye (23 Mpx a frame).

Every change below keeps the picture the same pixel for pixel, or only leaves out pixels that something opaque covers later in the same image. The one exception is the glow hole, which shifts a few glow pixels by at most 2/255 (see 2 below). Each has a switch, read once at start and logged as `gpu144: switches ...`. Put them in `app-dev/dev.env` for an A/B.

## Switches

| Switch | Default | What it does |
| --- | --- | --- |
| `MLXR_CURVE_ONEPASS` | 1 | Room, glow, picture and controller models go into the curved layer's image in one rendering per eye. Before, each was its own rendering, so the whole eye image was written out and read back between them. 0 = one rendering each, as in 1.3.1. |
| `MLXR_GLOW_HOLE` | 1 | The glow cylinder (or the flat glow in 3D) leaves out the part the picture's opaque middle covers. 0 = full glow surface, as before. |
| `MLXR_ROOM_OCCLUDE` | 1 | Before the room is drawn into the picture's layer, the picture's opaque middle goes into the depth, so room pixels hidden behind the picture are not shaded. 0 = off. |
| `MLXR_CURVE_REUSE` | 0 | When there is no new picture or glow, nothing in the layer changed, and each eye is within `MLXR_CURVE_REUSE_DEG` (0.3) degrees and `MLXR_CURVE_REUSE_MM` (2) mm of where the image was drawn from, the curved layer goes up again with its last image and poses, and SteamVR turns it the small way to the eyes. At most `MLXR_CURVE_REUSE_MAX` (3) frames in a row. Off by default: it needs a headset check. |
| `MLXR_FDM_LAYER` | unset | 0 turns off Valve's `fdm_injection` layer (sets `DISABLE_VULKAN_FDM_INJECTION_LAYER=1` before the OpenXR instance). See Foveation below. |

Earlier switches still apply: `MLXR_GLOW_WIDE=0` (Wide glow held to the Soft size), `MLXR_PICTURE_BLEND=0`, `MLXR_HOLD_PLACES`.

## Log lines

- At start: `gpu144: Valve fdm_injection layer ...` and `gpu144: switches ...`.
- Every 5 s, the existing `xr: ... gpu warp/glow/curve/room/models` line. With `MLXR_CURVE_ONEPASS=1` there is no separate room or models figure for the curved layer: a timestamp inside a rendering on a tiled GPU only times its last tile, so `curve` is the whole layer (room, glow, picture, models). For the 3D rate budget the whole layer stands in for the room (a little more than the room alone, so it errs toward a lower refresh, never a higher one). Compare `curve` with ONEPASS=1 against `room + curve + models` with ONEPASS=0 (in 1.3.1 `curve` already contained `room`, so the old sum is `curve + models`).
- Every 5 s, new: `xr: gpu144: waited for the last frame's GPU X ms avg (max Y), blocked on N of M frames; curved layer drawn D (chained C), sent again unchanged K`.
- `xr: frames: ... display frames skipped by the loop` and `xr: compositor: ... app gpu` as before.

## Expected savings, and why

These are estimates. I have no Adreno timings yet; the headset run below gives the numbers.

1. **One rendering per eye (ONEPASS).** Valve's FDM layer foveates every rendering 3408 px tall, and Turnip only does foveation in tiled (GMEM) mode, so each of our curved-layer renderings is tiled. Every boundary between two renderings then stores the whole eye image to memory and the next one loads it back: about 46 MB each way per eye uncompressed (UBWC compresses smooth areas well). Glow + picture removes one boundary per eye; room + glow + picture + models removes three. Expected: 0.3 to 0.8 ms with the glow only, 1 to 2 ms with a room and controllers in the layer. Output is unchanged: same draws, same order, same blending.
2. **Glow hole.** The glow is drawn under the picture and the picture's opaque middle covers it. The hole is the middle inside the border fade (4 % Soft, 8 % Wide), shrunk by how far each eye sees the glow (5 cm proud of the picture) beside the point behind it, times 1.25 plus 1 cm, worked out each frame from the eyes. If an eye is near grazing, there is no hole. This saves 15 to 30 % of the glow's pixels; the glow costs about 0.3 to 0.6 ms at 144 Hz, so about 0.05 to 0.15 ms. It is small, but it costs nothing. It does not change the picture: nothing under the picture's opaque middle changes. Cutting the glow surface at the hole's edges (three rows of quads a segment, and 64 segments for the flat glow instead of one) moves its vertices, so the glow's own rounding shifts at a few pixels outside the picture and under its border fade: at most 2/255, in under 0.1 % of an eye's pixels (desktop A/B of 64 scenes, NVIDIA). A unit test (`tests/xrcore/test_glow.c`, testGlowHoleStaysUnderThePicture) ray-casts every left-out glow point to the picture from three seats and checks it lands in the opaque middle.
3. **Room occluder.** The room is fill-bound (5.7 ms at 23 Mpx with depth). The picture covers 20 to 35 % of each eye, so 1 to 1.5 ms less room at the owner's sizes. The occluder is the picture's own surface, so there is no parallax margin, only the fade and the filter's 0.5 %. Room geometry in front of the picture is still drawn, and the picture covers it as before.
4. **Reuse (off).** Nothing at a 144 fps stream (every refresh has a new picture). On a 60 fps stream or a still desktop with a still head, up to 3 of 4 refreshes skip the whole curved layer (2 to 8 ms each).

### Looked at, not done

- **Tiny glow, upsampled.** The glow image is already 256 x 256 and only redrawn on new pictures (0.09 ms). Its cost is the full-resolution draw into the eye, one bilinear read per pixel. An upsample pass into the eye is the same fill, so drawing smaller does not help.
- **Trimming the Wide cylinder to the visible angle.** Columns outside the view are already clipped before any pixel is shaded; only vertices would be saved. The Wide glow's alpha stays above zero almost to its rim, so trimming by alpha changes nothing.
- **Cheaper blur taps.** glow.frag runs on 256 x 256 once per new picture (0.09 ms). Not worth touching the look.
- **Lobs1994's fork** (60db840, b5e72a8, 8a796b6, f116ca6, dbfe6d6, cabe51c): these add the edge feather, the wide glow scale, the raw-colour glow and the picture's alpha. Our borderless Glow edge (1.3.0) already contains all of it in its later form. Nothing cheaper there. On Android they draw the glow as a cylinder layer, which leads to the next point.
- **The glow as its own cylinder layer.** If `XR_KHR_composition_layer_cylinder` is offered (it is in the SteamVR beta, 2.19.1; this Frame's `vrclient.so` already contains the name), the curved glow could be a cylinder layer from the glow swapchain the flat glow uses. It would sit among the static layers right before the curved picture's projection layer, with pose = picture pose moved by the radius along +z, `centralAngle`, `aspectRatio` and the `rectX/rectWidth` sub-image from `glowCylinderForScale`. Then nothing in our eye passes draws the glow, and the compositor reprojects it. It only works when the curved layer is see-through around the picture (no room in the layer and no passthrough backdrop in it). Not done, by the owner's decision: stable SteamVR does not have the extension yet.

## Foveation

SteamVR on the Frame offers `XR_FB_foveation` and `_vulkan`, and Valve's implicit layer `XR_APILAYER_VALVE_fdm_injection` already loads into the app (`moonlight-console.log`: "added swapchain size 4048 / 1440 / 3408"). It hooks `vkCmdBeginRendering` and puts a gaze-driven density map on each of our renderings whose height matches a registered swapchain height. So our curved layer passes are already foveated, and adding `XR_FB_foveation` ourselves would not help.

One thing to check: the video swapchain is created at the stream's size in every mode (`videoSwapchain` runs before `videoTarget`, curved or not), so the stream's height is registered whenever it is over the threshold (about 1000 px; the exact value comes from SPD13's commit, not the binary). Our renderings of that height are then foveated in the picture's own texture space, which makes no sense for a texture and could soften parts of the picture:

| Stream height | Registered | Renderings that match it |
| --- | --- | --- |
| 720, 800, 900, 960 | no | none |
| 1080, 1200 | yes | the warp into the picture's own image (curved or 3D) or into the video swapchain (flat quad) |
| 1440 (2560x1440, 3440x1440, 5120x1440) | yes (seen in `moonlight-console.log`) | same |
| 1600 (2560x1600, 3840x1600), 2160 (3840x2160, 5120x2160, 7680x2160) | yes, once the Aspect Ratio presets exist | same |

The eye passes (3408) and the UI atlas (4048, copied into, never rendered) are the other registered heights. The aspect-ratio work does not add a new kind of risk, only more heights that hit it: every preset of 1080 lines or more.

To check in the headset: `FDM_DEBUG=1` in `app-dev/dev.env`, then in `moonlight-console.log` look for `fdm_injection: added swapchain size N` and `CmdBeginRendering: ... FDM ... WxH` lines whose WxH is the stream's size (for example 3440x1440). Any such line means the picture is drawn foveated. Then A/B small text at the picture's edges with `MLXR_FDM_LAYER=0`. If confirmed, the fix is to keep the picture's own images off a registered height (one row taller, cropped in the panel's uvRect), or not to create the video swapchain while the picture goes through its own image. That is not done in this lane.

## Frame pacing audit (moonlight-qt fe5a0ae, e4db5c2)

- fe5a0ae (lost wakeups in Pacer): moonlight-qt set `m_Stopping` and woke its vsync/render threads without the lock, and its vsync wait had no predicate. Our pacing has no vsync or render thread: `hw_video_take_refresh` runs on the XR frame loop. The one condvar wait (the latch) is a predicate loop under the lock, and every producer (a new picture, a decode error, a format change, stop) changes its state and then broadcasts while holding the lock. `stop` is stored before the lock is taken, and the waiter checks it again under the lock, so no wakeup is lost. Not affected.
- e4db5c2 (check the queue before dequeuing): `hw_video_take_refresh` only pops when `show && queued > 0` (paced) or `queued > 0` (newest), and `dropOldest` stops at an empty queue. Not affected.
- `vkWaitForFences` after `xrWaitFrame`: now timed (`xr: gpu144: waited ...`). The loop has one frame in flight. If the GPU frame runs past about the refresh minus the CPU's recording time, the loop blocks here and a refresh is lost. A non-zero average here at 144 Hz means double-buffering the frame's command buffer and fence (two in flight) would win back frames. Measure first (runs A to E give the figure); that is a bigger change, not done here.

## Headset test (144 Hz, 3440x1440)

Dev build, `app-dev/dev.env`. For each row, play 60 s with some head turns, then send the `gpu144:`, `xr: ... gpu ...`, `xr: frames:` and `xr: compositor:` lines from that minute.

| Run | Switches | Scene |
| --- | --- | --- |
| A | `MLXR_CURVE_ONEPASS=0 MLXR_GLOW_HOLE=0 MLXR_ROOM_OCCLUDE=0` (1.3.1 behaviour) | Curved, Glow Wide, no room, passthrough off |
| B | defaults | same |
| C | A's switches | Curved, Glow Soft, Home Theater room |
| D | defaults | same |
| E | defaults | Curved, Glow Wide, passthrough on |
| F | defaults + `MLXR_CURVE_REUSE=1` | still desktop, head still, then slow head turns: look for smear or wobble. This switch stays off by default and should not ship on: sending a stale projection layer again is the same mechanism as the passthrough smear (plan section 3). It is here only to measure. |
| G | defaults + `MLXR_FDM_LAYER=0` | small text at the picture's edges: sharper or the same? Compare `curve` ms with B |

Look for: any seam or dark ring where the glow meets the picture (B, D), room showing through the picture (D), controllers drawn wrong (B, D).
