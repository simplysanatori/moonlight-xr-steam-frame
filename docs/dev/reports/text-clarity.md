# Lane text-clarity: small text on the curved and 3D picture

The owner, after a headset run: "The text clarity, I feel like could still be a little bit better." The run streamed
2560x1440 HEVC at 90 fps, 98 Mbps, 4:2:0, and the log said `curved picture: projection layer 3408x3408 per eye`: the
picture went through the app's own curved pass. Headless only; the owner's run is the real test (last section).

## The path a desktop pixel takes (curved or 3D picture)

1. Host: RGB to 4:2:0, BT.709 limited. Chroma is at half the resolution each way, left sited.
2. Decode to NV12, read through the immutable Y'CbCr sampler (linear chroma filter, cosited x, midpoint y).
3. `mlxr_pass_warp` into the curved picture's own 2560x1440 RGBA8 image, one texel a pixel, then its mip chain
   (`mlxr_record_mips`, linear blits).
4. `mlxr_pass_panel` draws that image on the cylinder into each eye's 3408x3408 projection image. **This is the one
   resample we control.**
5. SteamVR resamples the projection layer through the lens distortion onto the panel. At the lens centre the
   Frame's 3408 per eye is about one panel pixel a pixel.

At the default size and distance (3 m wide, 3 m away) the 2560 px desktop covers about 1430 of the eye image's
pixels across: **1.79 desktop pixels an eye pixel**. Small text is shrunk in step 4.

## Measurement (`ctest -R shaders_text`, `mlxr-shader-test text <dir>`)

`tests/shaders/text_scene.c` draws a 2560x1440 desktop in 4 x 4 tiles:
- Noto Sans at 12, 13, 14 and 16 px em (cap height 9 to 11 px), in grey and in ClearType-style colour fringes;
- black on white, white on dark grey, syntax colours on an editor's dark background, red and blue text on white;
- a slanted edge each way (5 degrees, 0.1 to 0.9);
- 1 px line grids at periods 2, 3, 4 and 6;
- a 1 px grid and a zone plate.

The test encodes the desktop to 4:2:0 as an encoder does (left sited, 1 2 1 across, between two rows) and runs the
real passes: the warp, the session's mip chain, and the panel pass into a 3408x3408 eye. The eye has a 100 degree
square field and sits in front of the screen as the Screen tab places it. The scores compare the eye image with the
**ideal**, which is each eye pixel's area average of the desktop under it:

| Score | What |
| --- | --- |
| MTF50 | slanted edge, cycles per desktop pixel, horizontal / vertical |
| overshoot | the edge's ring past either side, % of the step |
| lines | the line grids' fitted fundamental out / in, periods 3, 4, 6 px |
| contrast | grey text's luma spread / the ideal's |
| text | grey and ClearType text, luma RMS from the ideal, /255 |
| chroma | syntax colours and red/blue text, chroma RMS from the ideal, /255 |
| alias | the zone plate's leftover modulation past the eye image's Nyquist, RMS /255 |
| crawl | the screen moved one eye pixel; RMS difference of the text from the first image moved by that pixel, /255 |

Every score is also taken after a model of SteamVR's resample: bilinear at 1:1 with a 0.3 px phase.

The video goes through the three-plane path, which is the same shader and the same siting as the Frame's NV12
conversion. Lavapipe's NV12 conversion offers only cosited rows; it gets its own row.

## Numbers: 3 m wide at 3 m, curve 50 (the defaults with the curve on)

Ideal: MTF50 0.267 / 0.258, lines 0.36 0.58 0.80, alias 18.3, crawl 1.39.

| | MTF50 | lines 3/4/6 | contrast | text | chroma | alias | crawl | after SteamVR: MTF50, lines, text, chroma |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| before (trilinear, bilinear chroma) | 0.221 0.199 | 0.22 0.45 0.71 | 0.97 | 11.9 | 5.8 | 16.4 | 1.25 | 0.164 0.151, 0.10 0.20 0.49, 20.4, 6.8 |
| before, Sharpen quality (warp CAS) | 0.248 0.219 | 0.22 0.45 0.71 | 0.98 | 11.8 | 5.2 | 21.9 | | 0.175 0.159, 0.10 0.20 0.49, 20.0, 6.5 |
| + footprint filter | 0.281 0.273 | 0.41 0.62 0.82 | 1.01 | 3.0 | 4.9 | 21.6 | 1.59 | 0.185 0.176, 0.19 0.27 0.57, 17.0, 6.3 |
| + chroma from luma (**new default**) | 0.281 0.273 | 0.41 0.62 0.82 | 1.01 | 3.0 | **2.8** | 21.6 | 1.58 | 0.185 0.176, 0.19 0.27 0.57, 17.0, 5.1 |
| new, Sharpen normal | 0.393 0.380 | 0.55 0.77 1.00 | 1.06 | 14.1 | 2.6 | 31.9 | 2.29 | 0.206 0.195, 0.26 0.34 0.69, 15.9, 4.6 |
| new, Sharpen quality | 0.496 0.476 | 0.63 0.86 1.12 | 1.08 | 20.8 | 3.3 | 39.8 | | 0.217 0.203, 0.29 0.38 0.77, 17.4, 4.5 |
| new default, 4:4:4 frame (bound) | 0.281 0.273 | | | 3.0 | 0.8 | | | chroma 4.1 |

Overshoot is 0.0 % on every new row (the warp's CAS had 0.7 %). Alias after SteamVR's model: 8.8 before, 8.8 new
default, 13.0 Sharpen normal.

Other placements, new default against before (after SteamVR's model in brackets):

| | MTF50 | text | chroma | crawl (ideal) |
| --- | --- | --- | --- | --- |
| 4 m at 2.5 m (1.12 px an eye px) | 0.361 0.363 against 0.361 0.325 (0.279 0.258 against 0.279 0.241) | 7.3 against 5.4 (9.5 against 10.1) | 3.1 against 5.7 | 3.54 against 3.52 (2.85) |
| 2 m at 4 m (3.58 px an eye px) | 0.147 0.143 against 0.109 0.105 (0.095 0.093 against 0.083 0.082) | 5.6 against 13.4 (16.6 against 18.9) | 2.6 against 4.9 | 0.37 against 0.37 (0.32) |

## Where the sharpness went, largest first

1. **The panel pass's trilinear read** (fixed). Shrinking 1.79 times, the hardware takes LOD 0.84, so 84 % of each
   eye pixel comes from the half size level. Each of that level's texels is already a 2 x 2 box of desktop pixels,
   read bilinear again. The picture came out well below the ideal: MTF50 0.221 / 0.199 against 0.267 / 0.258, and
   3 px lines at 0.22 against 0.36.
2. **SteamVR's resample of the layer** (modelled, not ours). In the model it takes MTF50 from 0.28 to 0.19. Sharpen
   can pre-compensate it (below), but whether the model is right is for the headset to say.
3. **Chroma from 4:2:0** (improved). On coloured text the chroma error is 4.9 with bilinear chroma, against 0.8
   if the frame were 4:4:4.
4. **The Sharpen row** (changed). The warp's CAS never touches black text on white. Its weight is
   `sqrt(min(min, 1 - max) / max)`, which is 0 wherever a neighbourhood reaches black or white. So Sharpen quality
   left the 1 px line grids exactly as they were (0.22 0.45 0.71).

Looked at and left alone:
- **One resample or two?** One. The warp writes the picture 1:1 at texel centres (exact) and the panel pass is the
  only resample before SteamVR's.
- **Mip generation** (linear 2:1 blits): correct, and now used only past 2:1.
- **The glow**: drawn under the picture, or over its outer edge band with Glow edge on. Never over text.
- **sRGB**: the picture's own image is `_UNORM` holding gamma-encoded bytes, and every filter averages encoded
  values. A linear-light average would make black text on white thinner and white on black bolder. It was not
  changed, because there was no measurement here that favours it and it would shift every picture's look.
- **The supersample default**: the flat quad path's Supersample normal is already the same 2 x 2 footprint box,
  and the curved path never used it.
- **Compositor hints**: SteamVR on the Frame has no `XR_FB_composition_layer_settings`, so no sharpening or
  supersampling flag. A projection layer has no flag that changes its filtering.

## The changes (each its own commit, numbers in the messages)

1. **Footprint filter** (`panel.frag`; `[display] picture_filter`, default true; `MLXR_PICTURE_FILTER=0|1`
   overrides it).
   - Each eye pixel averages 4 reads at a quarter of its footprint each way (the supersample pass's box).
   - Below 2:1 the reads come from the top level; past 2:1, from the level where a read covers half the footprint.
   - From 1:1 to 1.5:1 the reads close in on the centre, so near 1:1 it is no softer than before.
   - Magnified, it is the old single bilinear read.
   - The glow and the panels keep their single trilinear read.
2. **Chroma from luma** (`warp.frag guidedChroma`; `[display] chroma_from_luma`, default true;
   `MLXR_CHROMA_FROM_LUMA=0|1`).
   - Over the 4 chroma sites round the pixel, chroma is fitted as a line of the luma read at the same sites, then
     the line is evaluated at the pixel's full resolution luma.
   - The fit is mixed in by its r², and the result is held to a valid colour.
   - A stroke of one colour on another is exactly such a line, so a thin red stroke gets its whole colour back.
   - It runs in the warp, so it helps every path, the flat quad included.
3. **Sharpen at the eye's pixels** for the curved and 3D picture (`panel.frag sharpened`).
   - Normal 0.5, quality 1.0: an unsharp mask against the next coarser level, clamped to the range of the pixel's
     own 4 reads. Magnified, it uses the 4 neighbouring texels and clamps to their range.
   - No overshoot at any placement.
   - The warp's CAS stays for the flat quad picture only.
   - The default stays off (the owner's choice). It buys after-SteamVR sharpness (MTF50 +11 %, 3 px lines 0.19 to
     0.26) for more crawl (1.58 to 2.29) and zone plate alias (8.8 to 13.0). The trade is for the owner's eye.

## Cost

Texture reads per pixel:

| Pass | Before | Now |
| --- | --- | --- |
| warp | 1 NV12 read (5 with CAS) | 5 with chroma from luma; the curved path's warp no longer runs CAS |
| panel, picture shrunk | 1 trilinear read | 4 bilinear reads (trilinear past 2:1), 5 with Sharpen |
| panel, picture magnified | 1 bilinear read | 1 bilinear read, 5 with Sharpen |

On the Frame the shaders report's bench gives about 0.024 ms per extra read per million pixels (warp + sharpen,
1080p: 0.19 to 0.39 ms for 4 reads). From that:
- **Chroma from luma**: about +0.36 ms of GPU per video frame at 2560x1440. The warp runs once per video frame:
  3 % of a 90 Hz frame's 11.1 ms, 5 % at 144 Hz.
- **Footprint filter**: the default placement covers 2 x 1.15 million eye pixels. About +0.1 ms per display frame
  (4 bilinear reads against one trilinear, which Adreno does at half rate). The worst case, just past 1:1 with the
  picture over 2560x1440 in each eye, is about +0.35 ms. A magnified picture costs what it did.
- **Sharpen**: about +0.06 ms per display frame at the default placement.

Lavapipe's timings (report only, in the test output) rise in the same order.

## The owner's check

See the lane's final report, "Headset must confirm". In short:
- With `MLXR_PICTURE_FILTER=0` and `MLXR_CHROMA_FROM_LUMA=0` in the shortcut's launch options, compare small text
  and coloured text against the new default.
- Then try Sharpen normal.

## Open risks

- **SteamVR's resample** is a model. If SteamVR already sharpens or supersamples projection layers on the Frame,
  Sharpen normal may look over-done; the default leaves it off.
- **Encoder chroma**: the chroma from luma fit assumes the host's chroma is a local average. An encoder that
  sharpens chroma, or another siting, makes the fit weaker, but never worse than the bilinear it falls back to by r².
- **Field and resolution**: the eye's field is taken as 100 degrees square. The Frame's real field moves the
  ratios a little, not the order of the results.
