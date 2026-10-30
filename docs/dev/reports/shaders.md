# Lane S (shaders): report

Vulkan ports of every GL ES program in Gilleece's Moonlight XR renderer
(`xr-renderer/xr_shaders.c`), two new passes for compositor features SteamVR on
the Frame does not offer, the `mlxr_passes` C library that builds and records
them, and a headless test suite. Everything here runs on this Frame (Turnip
Adreno 750, Mesa 26.3.0-devel) without OpenXR; nothing has been seen in the
headset yet.

| | |
| --- | --- |
| Shaders | `shaders/*.vert`, `*.frag`, includes `common.glsl`, `grade.glsl`, `video.glsl` |
| Library | `shaders/passes.h`, `shaders/passes.c` -> static lib `mlxr_passes` |
| Tests | `tests/shaders/` -> `mlxr-shader-test`, 17 ctest entries `shaders_*`, plus a `bench` group run by hand |
| Build | `shaders/CMakeLists.txt` (own glslc rule; see "Build" at the end) |

## 1. Inventory of the upstream programs

Sizes: W x H is the decoded video; MW x MH the depth map (512x288 for ZipDepth
on Quest 3, 256 square in older sessions); "quarter" is W/4 x H/4. Every
upstream intermediate is `GL_RGBA8`, `GL_LINEAR`, `GL_CLAMP_TO_EDGE`. Every
swapchain is `GL_SRGB8_ALPHA8` drawn with `GL_FRAMEBUFFER_SRGB` disabled, so the
shaders write gamma-encoded values that land in memory unchanged.

| Upstream program (xr_shaders.c / link name) | Port | Inputs | Uniforms | Output | Driven by, when |
| --- | --- | --- | --- | --- | --- |
| `VERTEX_SRC` (shared) | `vertex.vert` | strip `VERTEX_DATA` (a_position, a_texcoord) | none | `v_plain` uv, (0,0) at NDC (-1,-1) | every full-screen pass below |
| `FRAGMENT_SRC` (`ctx->program`, "XR warp"; also the colour pass) | `warp.frag` (+ `.nv12`) | `u_texture` external OES (W x H frame); `u_depth` RGBA8, depth in a (raw map MW x MH, or the upsample, quarter); `u_offsets` RGBA8 quarter | `u_texmatrix`, `u_disparity`, `u_showDepth`, `u_barTest`, `u_tint`, `u_occlusion`, `u_eyeIndex`, `u_convergence`, `u_dispTexels`, `u_lowResWidth`, `u_frameWidth`, `u_srcInset`, `u_edgeFade`, `u_depthCubic`, `u_gradeOn`, `u_grade` | video swapchain, 2W x H in 3D (one viewport per eye) or W x H flat | `xr_gl.c renderVideoFrame`, once per new decoded frame (or a redraw on a 3D toggle); 1 draw flat, 2 in 3D; plus an unwarped draw for captures |
| `UPSAMPLE_FRAGMENT_SRC` ("upsample") | `upsample.frag` (+ `.nv12`) | `u_texture` OES; `u_depth` RGBA8 MW x MH (guide rgb, depth a; texelFetch) | `u_texmatrix`, `u_sigmaR` (0.25), `u_sharp` (`depthSharp`, 0), `u_depthSize` | `upsampleTexture` RGBA8 quarter, depth in every channel | `runUpsample`, per video frame when the depth model drives 3D and the upsample is on |
| `OFFSET_FRAGMENT_SRC` ("offset") | `offset.frag` | `u_depth` = the upsample (texelFetch) | `u_dispTexels` (separation x upsample width), `u_convergence` | `offsetTexture` RGBA8 quarter: r left eye, g right | `runOffsetSearch`, per video frame when occlusion is on and separation > 0 |
| `DOWNSCALE_FRAGMENT_SRC` ("downscale") | `downscale.frag` (+ `.nv12`) | `u_texture` OES | `u_texmatrix`, `u_texel` (1/MW, 1/MH) | `downscaleTexture` RGBA8 MW x MH, read back through a PBO for the depth model | `xr_depth.c nativeCaptureDepthInput`, when the Java depth thread asks for the next model input |
| `AMBI_FRAGMENT_SRC` ("frame colour sample") | `ambi.frag` (+ `.nv12`) | `u_texture` OES | `u_texmatrix`, `u_crop` (letterbox crop or 0 0 1 1), `u_gradeOn`, `u_grade` | `ambiTexture` RGBA8 32x32, blended `GL_CONSTANT_ALPHA` at `ambiSmooth` 0.08 after the first frame; also `ambiDetectTexture` 32x32 (no blend, crop or grade) plus a PBO readback | `runFrameColorSample` per video frame when the glow or the room is on; `runAmbiBarDetect` every 30 sampled frames; `redrawGlow` |
| `GLOW_EDGE_FRAGMENT_SRC` ("glow edge") | `glow_edge.frag` | `u_texture` = ambiTexture (texelFetch) | `u_luma` (0.5, 0.03, 0.08), `u_lit` (0.08, 0.35), `u_reach` (`glowReachTexels()` 10.67), `u_steps` (`glowInwardSteps()` 2), all set once | `glowEdgeTexture` RGBA8 32x32 | `runGlowEdge` from `runGlowRender` when `glowNorm` (default 1) |
| `GLOW_FRAGMENT_SRC` ("glow") | `glow.frag` | `u_texture` = glowEdgeTexture (or ambiTexture), bilinear clamp | `u_intensity`, `u_blur` (= glowNorm) | glow swapchain 256x256, premultiplied; quad layer (cylinder when the screen is curved) behind the screen | `runGlowRender` per video frame when the glow is on; `redrawGlow` when its level moves on a still picture |
| `ROOM_VERTEX_SRC` + `ROOM_FRAGMENT_SRC` | `room.vert`, `room.frag` | vertices 10 floats (pos, colour, spill, uv, spare), uint32 indices per part; `u_ambi` = ambiTexture (vertex stage); `u_room` ASTC 4x4/6x6/8x8 UNORM atlas with mips (or a 1x1 white) | `u_viewproj` per eye, `u_spillGain` (x room light level, 0 when unlit), `u_texMix` per part, `u_dim` | room swapchain 2 eyeW x eyeH with a D24 renderbuffer; projection layer | `renderRoom` at the end of `renderVideoFrame`: per new video frame, the compositor reprojects in between |
| `MODEL_VERTEX_SRC` + `MODEL_FRAGMENT_SRC` | `model.vert`, `model.frag` | vertices 11 floats (pos, normal, uv unused, colour) | `u_viewproj` per eye, `u_model` per hand | model swapchain 2 eyeW x eyeH, cleared to 0, own depth; projection layer | `renderControllerModels`, every display frame a model shows |
| (none) UI panels, text, stats overlay, pointer, hints, Ko-fi sheet, outline, corners | none | CPU-rasterised RGBA (xr_ui.c, xr_assets.c) | none | `glTexSubImage2D` into quad-layer swapchains | when content changes. **There is no GL program**: in Vulkan this is `vkCmdCopyBufferToImage`; see `panel` below for curved placement |
| (none) sharpening and supersampling | `warp.frag` sharpen, `supersample.frag` | | | | upstream: `XR_FB_composition_layer_settings` flags on the screen layers, done by the compositor. **Absent on the Frame** |
| (none) curved screen and curved glow | `panel.vert`, `panel.frag` | | | | upstream: `XR_KHR_composition_layer_cylinder`. **Absent on the Frame** |

The Java `GlPassthroughRenderer` (flat, non-XR) has its own program and is out
of scope.

## 2. The ports: bindings, push constants, behaviour changes

All programs are GLSL 450 for Vulkan 1.2. Uniforms are push constants (one
128-byte range, vertex+fragment, on every pipeline layout; each program reads a
prefix). Samplers are combined image samplers in set 0, written with push
descriptors (`VK_KHR_push_descriptor`). Spec constant 0 is `OUT_LINEAR`.

| Pass | Set 0 bindings | Push constants (bytes) | Target |
| --- | --- | --- | --- |
| warp | 0..2 video, 3 `u_depth`, 4 `u_offsets` | 128: vidRect, vidChroma, grade, tint + gradeOn, (disparity, eye, convergence, dispTexels), (lowResWidth, frameWidth, srcInset, edgeFade), (occlusion, depthCubic, showDepth, barTest), (sharpen) | swapchain view or RGBA8 |
| upsample | 0..2 video, 3 depth map | 48: vidRect, vidChroma, (sigmaR, sharp, depthSize) | `MLXR_PASS_FORMAT` |
| offset | 3 upsample | 16: (dispTexels, convergence) | `MLXR_PASS_FORMAT` |
| downscale | 0..2 video | 48: vidRect, vidChroma, texel | `MLXR_PASS_FORMAT` |
| ambi | 0..2 video | 80: vidRect, vidChroma, crop, grade, gradeOn; dynamic blend constant | `MLXR_PASS_FORMAT` 32x32 |
| glow_edge | 0 colour sample | 48: luma, (lit, reach), int steps | `MLXR_PASS_FORMAT` 32x32 |
| glow | 0 sample | 16: (intensity, blur) | glow swapchain view |
| room | 0 ambi (vertex stage), 1 atlas | 80: viewproj, (spillGain, texMix, dim) | swapchain view + depth |
| model | none | 128: viewproj, model | swapchain view + depth |
| supersample | 0 source | 32: srcRect, (destination size, taps) | swapchain view |
| panel | 0 texture | 112: mvp, (width, height, radius, segments), uvRect, colour | swapchain view |

Video bindings: three-plane variant 0 = Y, 1 = U, 2 = V (R8_UNORM, linear
clamp); NV12 variant 0 = the NV12 view through the immutable Y'CbCr sampler,
no 1 or 2. `MLXR_PASS_FORMAT` is `VK_FORMAT_R8G8B8A8_UNORM`, upstream's
`GL_RGBA8`.

### Maths kept identical
Every expression in the upstream sources is carried over unchanged: the warp's
spline folds, the Newton steps and their clamps, the bilateral weights and the
snap, the offset search, the 4x4 boxes, the glow edge's ring and roll-off, the
glow's two spline kernels and its fall-off, the room's five-tap wash and the
model's light. The tests compare each one against a CPU copy, or against the
xr/core module that owns the same sums (section 4).

### Behaviour changes (each one deliberate)
1. **External OES texture -> two video sources.** `samplerExternalOES` plus the
   SurfaceTexture `u_texmatrix` become `video.glsl`: either three R8 planes
   converted in the shader with the same BT.709 limited-range sums as M1's
   `xr/shaders/yuv420p.frag`, or one `G8_B8R8_2PLANE_420_UNORM` image sampled
   through an immutable sampler with a `VkSamplerYcbcrConversion` (BT.709,
   ITU narrow, identity swizzle, linear chroma filter), which hands the shader
   RGB. Chosen at compile time (`-DVIDEO_NV12=1`, two SPIR-V variants of
   warp, upsample, downscale and ambi), because the two need different
   descriptor set layouts. `u_texmatrix` becomes `vidRect`: picture size over
   allocated size, for a decoder that pads (1080 -> 1088 rows, say). There is
   no flip.
2. **Chroma siting is explicit.** H.264/HEVC's default is left (co-sited with
   even luma columns, midway between rows). The NV12 conversion asks for
   `COSITED_EVEN` x and `MIDPOINT` y (the Frame supports both). The
   three-plane path matches it by moving the chroma read a quarter chroma
   texel right (`MlxrVideo.chroma = MLXR_CHROMA_LEFT`). `MLXR_CHROMA_CENTER`
   reproduces M1's shader exactly. Upstream never chose: MediaCodec's OES
   sampler did.
3. **sRGB.** Upstream writes gamma values into `SRGB8_ALPHA8` with sRGB writes
   off. Vulkan cannot switch encoding off on an `_SRGB` view, so a pipeline
   whose target format is `_SRGB` gets `OUT_LINEAR = true`. Every swapchain
   writer (warp, glow, room, model, supersample, panel) then decodes its
   result (`srgbToLinear(clamp(c))`) before the write, and the hardware
   re-encodes it to the bytes upstream stored. Premultiplied output (the glow)
   is decoded as it stands, which is what keeps the stored bytes equal.
   Measured: at most 1 byte from a `_UNORM` view of the same image. With
   `XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT` and a `_UNORM` view the conversion
   is skipped entirely. All intermediates are `_UNORM`, so nothing else
   changes.
4. **Orientation.** GL's NDC y = -1 and Vulkan's are both memory row 0 of the
   target, and textures are read at the same `v_plain`. So every full-screen
   pass reads and writes the same memory rows in the same order as upstream,
   and nothing is flipped. What row 0 *means* differs: upstream's is the
   picture's bottom (glReadPixels order, after the SurfaceTexture flip); here
   it is the top (decoder order, and what a Vulkan compositor shows first).
   Consequences:
   - `offset.frag` and `glow_edge.frag` index with `gl_FragCoord` (now
     top-left origin) and still hit the same texels.
   - The glow ring runs from the top-left corner.
   - The ambilight crop is still applied in the space it was measured in, but
     its "top" and "bottom" log labels swap.
   - Captures written from these images are top row first.
     `tools/xr/warp_lab.py` expects source, left, right, depthtex and
     upsampled bottom row first, so a native capture needs a flip or a flag
     there.
5. **Clip space for the 3D passes.** `room.vert`, `model.vert` and
   `panel.vert` take upstream's matrices (xr_math `projectionFromFov` x
   `viewFromPose`, GL clip space) and convert at the end: `y = -y`,
   `z = (z + w) / 2`. The C maths stays upstream's, and the image comes out
   top row first. Depth: LESS, cleared to 1, as GL's default. No culling
   (upstream never enables it).
6. **Precision.** `mediump` in the room and model fragment shaders is dropped
   (full precision); the outputs are 8 bit either way.
7. **Attribute-less full-screen triangle** instead of the four-vertex strip.
8. **Blending.** Ambi's `glBlendColor` + `GL_CONSTANT_ALPHA` is a pipeline with
   `VK_BLEND_FACTOR_CONSTANT_ALPHA` and a dynamic blend constant; 1.0 is a
   plain overwrite (the first frame, the letterbox detector).
9. **New: sharpen** (warp `extra.x`; 0 is upstream). A contrast-adaptive
   sharpen (the AMD CAS shape: four neighbouring video texels, and a weight
   that shrinks near black and white) on the video taps, before the grade.
   Upstream got this from the compositor (`NORMAL`/`QUALITY_SHARPENING_BIT_FB`).
   Suggested values: 0.4 for "normal", 0.8 for "quality".
10. **New: supersample.** A box over each destination pixel's footprint, made
    of taps x taps bilinear reads (2 for "normal", 4 for "quality"). It is
    held half a texel inside the source rectangle, so the eyes of a
    double-wide image never bleed into each other. It averages encoded
    values, as a compositor sampling an `_SRGB` texture without decoding
    would; averaging in linear light would need an `_SRGB` view on the source.
11. **New: panel.** A premultiplied texture on a flat or cylindrical surface
    (a procedural grid of `segments` columns), blended ONE /
    ONE_MINUS_SRC_ALPHA into a projection-layer image. The cylinder bends
    toward the viewer around an axis `radius` behind the surface, as upstream
    poses its cylinder layers. It is for the curved screen, the curved glow
    and curved UI panels.
12. **Later (lane text-clarity, `text-clarity.md`).** The panel pass has a picture filter for the curved and 3D
    picture (a 2 x 2 box over each eye pixel's footprint, and Sharpen at the eye's pixels). The warp has chroma
    from luma (`guidedChroma`, switch bits 16, 32 and 64). The mip chain is `mlxr_record_mips`. Group `text` of
    the shader tests measures them.
13. **Upstream's UI has no shader.** Panels, text and the stats overlay are CPU
    pixels uploaded with `glTexSubImage2D`. In Vulkan that is a buffer-to-image
    copy into the quad layer's swapchain image, which the orchestrator or
    lane U records. Nothing for it lives in this lane.

## 3. The API (`shaders/passes.h`)

```c
MlxrPassesInfo pi = {vk.phys, vk.device, VK_NULL_HANDLE /*cache*/, 1 /*nv12*/, MLXR_CHROMA_LEFT,
                     0.0f /*atlas anisotropy*/};
MlxrPasses* passes;
mlxr_passes_create(&pi, &passes);          // modules, layouts, samplers, NV12 conversion
mlxr_passes_prepare(passes, cmd);          // once, in the first command buffer (the 1x1 white)
mlxr_passes_warm(passes, MLXR_PASS_WARP, MLXR_VIDEO_YUV420P, VK_FORMAT_R8G8B8A8_SRGB, 0); // optional
...
mlxr_passes_destroy(passes);
```

Each `mlxr_pass_*` call records one pass from a plain struct:
- a `MlxrTarget`: the view, its format, the rect to draw, and whether to clear;
- the pass's parameters: image views, and the upstream uniforms by name.

Each call begins and ends its own dynamic rendering instance. The caller owns
every barrier (`mlxr_barrier()` derives the masks from the two layouts) and
keeps:
- inputs in `SHADER_READ_ONLY_OPTIMAL`;
- colour targets in `COLOR_ATTACHMENT_OPTIMAL`;
- depth targets in `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`.

Pipelines are built on first use of a (pass, video kind, target format, depth
format) combination and cached in the object. There is no global state, and
the object is not thread safe. A NULL depth, offset or atlas view binds the
library's 1x1 white image. The room and model passes need their depth view.

NV12:
- `mlxr_passes_nv12_view()` makes the view of a `G8_B8R8_2PLANE_420_UNORM`
  image with the library's conversion. Every NV12 view must use that
  conversion.
- `mlxr_passes_nv12_conversion()` returns the conversion, for lane V to create
  its own views (for a dma-buf import, say).
- `mlxr_passes_nv12_caps()` reports which filter and siting the conversion
  got.

What a frame looks like in xr_session.c, in upstream's order:

```c
// per new decoded frame, video planes already in SHADER_READ_ONLY_OPTIMAL
MlxrVideo v = {MLXR_VIDEO_YUV420P, {y, u, v}, w, h, 0, 0, MLXR_CHROMA_LEFT}; // or NV12
if (upsampling) { barrier(up -> COLOR); mlxr_pass_upsample(...); barrier(up -> SHADER_READ); }
if (occluding)  { barrier(off -> COLOR); mlxr_pass_offset(...);  barrier(off -> SHADER_READ); }
if (glow || room) { barrier(ambi -> COLOR); mlxr_pass_ambi(..., .blend = seeded ? 0.08f : 1); barrier(ambi -> SHADER_READ); }
if (glow) { glow_edge into a 32x32 image; mlxr_pass_glow into the acquired glow swapchain image; }
for (eye = 0; eye < eyes; eye++)          // the acquired video swapchain image, 2W x H in 3D
    mlxr_pass_warp(passes, cmd, &(MlxrTarget){view, fmt, {{eye * w, 0}, {w, h}}}, &warpParams);
if (room) mlxr_pass_room(...);             // after the warp, as upstream
// every display frame: mlxr_pass_model(...) when a controller shows
```

M1's `yuv420p.frag` pipeline is `mlxr_pass_warp` with no depth, disparity 0,
grade off, tint 1 and `MLXR_CHROMA_CENTER`; the output is the same.

## 4. Tests (`ctest -R shaders`: all 17 pass; `mlxr-shader-test bench <dir>` separately)

The tests create a headless Vulkan 1.2 instance and device (dynamic rendering,
push descriptors, sampler Y'CbCr conversion), run each pass on a synthetic
fixture, read the result back and check it numerically. PPMs land in
`build-shaders/shaders/tests/out/`. I looked at these and all are as expected:
the stereo pattern pair, the step-edge warp and the plain gather's ghost, the
NV12 bars, the glow, the room and the curved panel.

| Group | What is checked | Result on the Frame |
| --- | --- | --- |
| device | every pass's pipeline builds for SRGB, UNORM and BGRA targets and both video kinds; NV12 caps | 20/20; Turnip offers linear chroma filtering, cosited and midpoint |
| grade | ungraded colour pass vs CPU BT.709 limited; neutral grade vs off; 10 grades vs `pictureGradeApply`; tint | max diff 0, 0, at most 1/255 (one case 1, the rest 0); tint at most 1 |
| nv12 | NV12 through the conversion vs three planes from the same bytes, warp and ambi | 1/255 (smooth and bars); ambi 1/255 |
| srgb | `_SRGB` view with OUT_LINEAR vs a `_UNORM` view of the same image, warp and premultiplied glow | at most 1 byte |
| warp_identity | separation 0: plain gather, occlusion path, occlusion + cubic depth, both eyes | 0/255 from mono, all three |
| warp_step | a near step over a far background through the full chain (map -> upsample -> offset -> warp), vs a slow CPU inversion of the same depth; the disocclusion; the plain gather's ghost | left-eye edge within 0.03 px of the CPU inversion, right eye within 1.0 px (a fold, see below); left disocclusion has 0 object pixels; the plain gather shows a 6 px ghost on the same fixture |
| sharpen | flat picture untouched; edge steepened (38/217 -> 16/239); nothing else touched | pass |
| upsample | four fixtures vs a CPU copy of the JBU (snap included); a flat map stays flat | at most 1/255 |
| offset | separations 0, 0.02 and 0.05 vs a CPU copy; zero separation encodes 0 | 0/255 |
| downscale | 256x128 -> 64x32 vs the CPU 4x4 box | 1/255 |
| ambi | full and cropped, graded, vs CPU; constant-alpha smoothing at 0.25 | at most 1/255 |
| glow_edge | vs the CPU sums built from xr_glow.c (`glowNormalise`, `glowLit`, the ring functions) | 0/255 |
| glow | spline and blurred spline vs CPU copies; the four-fetch fold vs a true 16-tap B-spline (CPU only); clear rim | colour 1/255, alpha 0; fold < 1e-4 |
| room | atlas x dim + wash, painted x dim when unlit, depth order, the top of the view on the top rows, crossed parallax between the eyes | exact (0/255) |
| model | lit and unlit sides (`0.45 + 0.55 max(n.l, 0)`); alpha 1 on the model, 0 elsewhere | at most 2 (summed over channels) |
| supersample | 2:1 checkerboard -> grey with 2 and 4 taps; no bleed across the eye seam; a 3:1 ramp | exact grey away from the clamped border; 0 leaks |
| panel | the texture's top lands at the top; premultiplied blend; a curved panel's width is sin(angle) of the flat one's | within 1 px |

About the step-edge warp. In the left eye the object edge does **not** move by
the full `sep x (d - conv) x W` (8.2 px here), only by about 1 px. The warp
reads depth bilinearly from the quarter-resolution upsample, so the colour edge
sits in the middle of a 4 px depth ramp. That is upstream's behaviour, and the
CPU inversion of the same depth agrees to 0.03 px. The right eye is a fold
(the object covers the background), and there the GPU edge is 1 px left of the
CPU inversion: the low-resolution search blends two surfaces' offsets, and the
nearer surface reaches a pixel too far. This is the fold case
`tools/xr/warp_lab.py` documents.

### GPU time (bench: headless, no compositor running, median of 21 runs, clocks warmed)

| Pass | 1920x1080 | 3840x2160 |
| --- | --- | --- |
| warp mono, graded (three planes / NV12) | 0.20 / 0.19 ms | 0.80 / 0.75 ms |
| warp mono + sharpen | 0.44 / 0.39 ms | 1.76 / 1.56 ms |
| warp stereo, occlusion, both eyes | 0.66 / 0.65 ms | 2.67 / 2.58 ms |
| the same + cubic depth | 0.90 / 0.88 ms | 3.59 / 3.48 ms |
| upsample to quarter | 0.08 / 0.07 ms | 0.27 / 0.24 ms |
| offset search (separation 0.05) | 0.15 ms | 1.05 ms |
| downscale to 512x288 | 0.11 ms | 0.46 / 0.31 ms |
| ambi + glow edge + glow | 0.09 / 0.07 ms | 0.12 / 0.11 ms |
| supersample one eye to 2/3 size, 4 taps | 0.39 ms | 1.54 ms |

So a 4K stereo frame with everything on costs about 4.5 ms of GPU per video
frame, before the compositor's own load; 1080p costs about 1 ms. The offset
search's cost grows with separation (its reach is
`ceil(dispTexels x max(conv, 1 - conv)) + 2` texels either side, for each eye).

## 5. Confirmed on this device vs reasoned

Confirmed (headless Vulkan on the Frame's Turnip, in this session):
- every SPIR-V module and pipeline builds;
- push descriptors, dynamic rendering, and push constants up to 128 bytes work;
- the NV12 conversion works with a linear chroma filter and cosited x;
- NV12 and three-plane agree to 1/255;
- `_SRGB` views with OUT_LINEAR store the `_UNORM` bytes to within 1;
- every numeric result in section 4, and the GPU times above.

Reasoned, not yet seen:
- How SteamVR's compositor shows these bytes. Expected to be identical to
  upstream's, since the stored bytes are.
- Orientation in the headset. Row 0 = top is what a Vulkan compositor shows
  first, and the room test puts world-up on the top rows.
- NV12 from a dma-buf import. Only optimal-tiling `VkImage`s were tested; a
  DRM-modifier import may report different Y'CbCr features.
- Performance under SteamVR's compositor.
- Validity under the validation layers. None are installed here, so the API
  use was checked against the spec by reading, not by a layer.

## 6. Requests to the orchestrator (outside lane S's directories)

1. In `xr_vk_create`, enable `VK_KHR_push_descriptor` by passing it in
   `extraDeviceExts`. The Frame has it (maxPushDescriptors 32). Without it,
   `mlxr_passes_create` fails.
2. Enable `textureCompressionASTC_LDR` before the room uploads its ASTC
   atlases. Enable `samplerAnisotropy` too if `atlasAnisotropy > 1` is wanted
   (upstream caps it at 4). `samplerYcbcrConversion` is already on.
3. Link `mlxr_passes` into `mlxr_xr`, and replace `videoPipeline`/`kYuvFrag`
   with `mlxr_pass_warp` (identical output with `MLXR_CHROMA_CENTER`).
4. Swapchains: either keep `_SRGB` views (OUT_LINEAR, tested), or create the
   swapchains with `XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT` and make `_UNORM`
   views (exact bytes, and no per-pixel decode). The library works with
   either; it keys on the view format.

## 7. Open questions

- **Chroma siting of the stream.** Left (the H.264/HEVC default) is assumed
  for NV12 and recommended for the three planes; M1 uses centre. Worth
  checking what Sunshine/GFE signal in `chroma_sample_loc_type`, and
  following it.
- **Per eye vs per video frame.** Upstream runs downscale (on the model's
  schedule), upsample, offset, ambi, glow edge, glow, warp and the room once
  per *video* frame. Only the controller models run per *display* frame. The
  native app should keep that split: the video and glow quad layers hold
  still between video frames. The room is projection-layer content drawn
  with the pose of the frame it was drawn for, and the compositor reprojects
  it.
- **Swapchain count.** Upstream uses:
  - one double-wide video chain (2W x H in 3D);
  - a 256x256 glow chain;
  - a double-wide room chain with its own depth;
  - a double-wide model chain with its own depth;
  - the art chains.

  The Frame changes three things:
  - Without cylinder layers, a curved screen or glow has to be drawn by
    `panel` into a projection layer. That is either the room's image (one
    more draw per eye, and the room then has to be redrawn every display
    frame) or a dedicated projection chain.
  - Without `XR_FB_composition_layer_settings`, sharpening becomes an
    in-pass cost (see the bench).
  - Supersampling becomes one more pass, from an intermediate eye image. The
    warp then writes that intermediate rather than the swapchain.

  Which of these the orchestrator wants decides how many chains there are.
- **Bar detection readback.** Upstream reads the 32x32 detector image through
  a PBO a frame later. The Vulkan equivalent is a copy into a host-visible
  buffer, read after that frame's fence. It is not in this library.
- **GPU timing.** Upstream times the warp and the room with GL timer queries.
  The native app can bracket the `mlxr_pass_*` calls with
  `vkCmdWriteTimestamp`, which is exactly what the bench does.
- **Captures.** If native captures for `tools/xr/warp_lab.py` are wanted,
  they are top row first throughout (see change 4).
- **A guide that matches nothing.** In the upsample, when the guide colour
  matches none of the neighbourhood, every range weight falls under the 1e-6
  floor on the denominator, and the depth collapses toward 0 (far). This is
  upstream's maths, reproduced exactly. It matters only if lane D ever
  uploads a map whose guide rgb is not the frame the depth came from.

## Build

`shaders/CMakeLists.txt` uses its own `mlxr_shader()` rule rather than
`glsl_to_c`, for two reasons:
- The programs share three include files that `glsl_to_c` would not list as
  dependencies.
- The four video passes are compiled twice, the second time with
  `-DVIDEO_NV12=1` (output `<name>.nv12.frag.inc`).

The flags are otherwise `glsl_to_c`'s (`--target-env=vulkan1.2 -O -mfmt=c`).
The library and the test build with `-Werror`. The library links only Vulkan
and libm; the test adds `mlxr_xrcore`.
