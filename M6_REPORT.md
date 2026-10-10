# M6 report: realtime 2D to 3D (lane/m6)

Moonlight XR's depth path is now wired end to end in the native client. The pieces come from the Steam Frame fork of Gilleece's Moonlight XR (GPLv3): upstream's `xr_depth.c`, the 3D half of `xr_gl.c renderVideoFrame`, and the depth and stage threads in `XrRenderer.java`. They run on lane D's depth engine (LiteRT 2.21 + XNNPACK, ZipDepth) and lane S's passes.

The chain: hardware decode → downscale → readback → stage thread → model → map rotation → upsample → offset search → one warp per eye into a double-wide picture. The flat and curved layers show each eye its own half.

Built on main 493426d, then main bf76866 merged in (the flat launcher). Commits on lane/m6: c261e29, b211c6f, 726743f, plus this report.

Nothing here has been seen in the headset. SteamVR keeps a session started over SSH SYNCHRONIZED, so only one frame renders. The pipeline was proven headless with a test binary that drives the same code (`tests/m6`).

## Ported / rewritten (upstream logic kept; changes marked `native` in the code)

| File | What |
| --- | --- |
| `xr/xr_depth.c` + `.h` (new, ~1000 lines) | Rewrite of upstream's `xr_depth.c` and the depth half of `renderVideoFrame`. GL's second and third contexts, its pixel buffers and its fences are replaced by the following. **Capture:** `DEPTH_PAIRS` (2) host-visible readback buffers (cached memory, invalidated if non-coherent). Each is keyed by the frame's timeline value. The frame loop records `mlxr_pass_downscale` + `vkCmdCopyImageToBuffer` + a buffer barrier to HOST_READ. **Stage thread:** waits for that value with `vkWaitSemaphores` (100 ms slices, a warning past upstream's 500 ms), then calls `mlxr_depth_submit`. Model-input conversion and the scene-cut check happen inside that call, then the buffer is freed. **Map rotation:** `DEPTH_TEX_COUNT` (3) depth textures, each with its own staging buffer. The newest map is copied into the next slot in a fixed rotation. Nothing is allocated per frame. A slot whose last use the GPU has not passed is skipped (`uploadBlocked`, 0 in every run). **Kept as upstream:** when to capture (`mlxr_depth_due` = xr_rate's `depthGateDue`, by time not frame count); skip, never wait, when no pair is free; the capture at once when the 3D comes back on, plus `mlxr_depth_reset`; flat until a fresh map or `STEREO_WAIT_NS`; the upsample on every video frame; the offset search only with occlusion and separation > 0; disparity ±separation per eye; convergence, held source (`srcInset`), edge fade (`EDGE_FADE_PX` 8), the cubic read (off), the seam inset, the test patterns (flat/ramp/blob/eye test/shift test via `fillSyntheticDepth`), and the depth-age measure. **Native:** the engine sits behind a table (`XrDepthEngineOps`) so tests can use a fake engine. The real table is in `xr_depth_engine.c`, library `mlxr_xrdepth`, built only with LiteRT, so the M4/M5 tests do not link LiteRT. |
| `xr/xr_session.c` | **Picture swapchain:** double wide whenever a stereo mode is set, as upstream's `chainWidth`. **`recordWarp`:** calls `xrDepthPicture` (capture, upsample, offset), then `xrDepthRecordWarp` (one `mlxr_pass_warp` per eye) into the swapchain or the curve's own image, then sets `drawnEyes`. **Curve:** the curved picture's own image is as wide as the chain, and `mlxr_pass_panel`'s `uvRect` gives each eye its half (eye swap and the seam inset included). **Supersample:** off while the 3D is on. **Redraw on a still picture:** after a 3D switch (`warpRedraw`) and when the 3D tab's depth or convergence moves. **Per frame:** `xrDepthFrame` (lifecycle, the governor's rate, map upload, end of waiting) and `xrDepthSubmitted`. **Logging:** the 3D clause in the 5 s stats line, and depth lines on the Stats overlay. **Exit:** `xrDepthDestroy`. **Saving:** the 3D switch is saved to config.ini. |
| `xr/xr_prefs.c` | Upstream's nativeInit depth defaults: sigmaR 0.25, upsample on, occlusion on, src inset on, edge fade 8, cubic off, depth sharp 0. Its debug properties become environment variables. `xrApplySettings` calls `xrDepthConfigure` and `separationPref`. |
| `xr/xr_world.c/.h` | New GPU sections: downscale, upsample, offset. The picture's GPU time (warp + glow + depth passes) now feeds `rateBudgetGpu`, and the room's feeds `rateBudgetRoom`, which the governor judges. Natively nothing fed them before. **Fix (M5 bug):** `vkGetQueryPoolResults` returns `VK_NOT_READY` whenever any section was not drawn that frame, and every time was thrown away. So the stats line only had GPU times on frames that ran every section. |
| `xr/xr_layers.c`, `xr_panels.c`, `xr_renderer.h` | **Layers:** the supersampled quad is used only when one eye is drawn (both eyes' quads were already ported). **Panels:** the 3D tab's reason line (off in settings / model not installed / model failed); About says 0.6 (M6). **Context:** upstream's depth fields are back, plus `depth`, `gpuTimeline`, `depthModel`, `depthFailed`, `depthReason`, `separationPref`. |
| `ui/settings.c/.h`, `ui/ui_strings.h` | **`[3d] stereo`** (native: the switch as last left, default off). **`depth_source = zipdepth-int8`** added, appended after the test patterns so existing values keep their index. **Three strings:** no model, model failed, loading. |

## Pipeline and threads

```
frame loop (90/72 Hz)           stage thread                depth thread (engine, 2 XNNPACK threads)
 new video frame
  due? -> downscale 512x288
          copy -> readback[i] -- timeline value -->  vkWaitSemaphores
                                                     mlxr_depth_submit (to float RGB,
                                                     scene cut) ------------------>  ZipDepth fp16, normalise
  each display frame: mlxr_depth_acquire newest map <------------------------------  (tau 30 ms, range 150 ms)
   memcpy 590 KB -> staging[k], copy -> depthTex[k], k = next of 3
  upsample (W/4 x H/4, guided by this frame's colour) -> offset search -> warp L (+sep), warp R (-sep)
```

## Numbers

Measured with `mlxr-m6-pipeline` on the Frame, headless. Synthetic 1080p30 clip (`tests/m6/gen_scene.sh`: far brick wall, perspective checker floor, red ball rolling in front), hardware decode, a 90 Hz paced loop, 10–20 s, separation 0.6 % / 0.5 %, convergence 50. Another agent's moonlight-qt build was idle at the time.

| Model | maps/s (target 20) | model ms avg / p95 | submit → map | capture → display (age) | process CPU |
| --- | --- | --- | --- | --- | --- |
| ZipDepth 512x288 fp16 (default) | 20.2 | 36.4–37.4 / 39–42 | 41–42 ms | **77.5–78 ms** (2.3 video frames) | 1.47–1.49 cores |
| ZipDepth 256 int8 | 19.8 | 13.9 / 16.6 | 19 ms | 45 ms (1.4 frames) | 0.56 cores |
| MiDaS v2.1 small 256 fp16 | 19.8 | 45.1 / 48.6 | 53 ms | 82 ms (2.5 frames) | 1.78 cores |

Other figures from the same runs:
- Stage thread: about 3.2 ms timeline wait per capture (the frame's own GPU work) and 0.2–0.3 ms submit.
- Captures: none skipped for lack of a buffer, none replaced. Load 35–64 ms, warm-up 39–133 ms; READY 90–250 ms after the 3D is turned on.

GPU time per pass, 1920x1080, from timestamps, averaged over 300 frames. Clocks are not pinned and the GPU was otherwise idle, so these are upper bounds against the shader lane's 0.65 ms warmed bench.

| Pass | ms |
| --- | --- |
| warp, both eyes, occlusion | 1.90 |
| downscale + readback copy (on a capture) | 0.34–0.37 |
| upsample to 480x270 | 0.26 |
| offset search | 0.20 |

Frame loop work per frame, including the wait for the previous frame's GPU as the session does: p50 0.5–0.7 ms, p99 3.6–4.1 ms, no late 90 Hz ticks.

**Correctness**, ZipDepth, final frame:
- In the map, the ball reads 242–245 of 255 (near) and the wall 3 (far). The floor shades from far at the horizon to near at the bottom.
- The ball sits 6.4 px further right in the left eye than in the right: crossed disparity, so in front of the screen.
- The wall shifts −11 px: uncrossed, behind the screen.
- int8 and MiDaS give the same signs.

I looked at `anaglyph.png` and `depth.png`, and both are as described. The binary also writes `left.png`, `right.png`, `upsample.png` and `guide.png`.

**CPU placement (task 4)**, 20 s each:

| Depth threads | Frame loop | Model ms | Frame loop p99 | maps/s |
| --- | --- | --- | --- | --- |
| any (default) | any | 37.4 | 4.08 ms | 20.2 |
| 3–6 (A720) | any | 43.0 | 3.89 ms | 20.1 |
| 3–6 | pinned to 7 (X4) | 43.4 | 3.86 ms | 20.1 |
| 5–6 | 7 | 44.1 | 3.78 ms | 19.9 |

Pinning costs the model about 6 ms (it loses the X4) and does not measurably change the frame loop (within noise). **Choice: no pinning by default.** `MLXR_DEPTH_CPUS` ("3-6" or "0x78") pins the model and the stage thread if a headset run shows contention.

## 3D tab, bar and settings (state)

| Item | State |
| --- | --- |
| Bar 3D button | Shown with a stereo mode: a model whose file is in `assets/depth` (and the depth library built in), or a test pattern. Hidden with `depth_source = off`, when the model is missing, or after FAILED. On/off, with a toast. |
| 3D tab: Preset (Comfort / Balanced / Strong) | Live. Writes the separation (ZipDepth 3/6/9, MiDaS from its own default), saves it, and turns the 3D on (upstream). |
| 3D tab: Depth track | Live and saved (`[3d] separation`, tenths of a percent). Amber past the model's default. A still picture redraws while dragging. |
| 3D tab: Convergence track | Live and saved (`[3d] convergence`). Also sent to the engine's parameters. |
| 3D tab: Switch, Reset | Live. The switch is saved (`[3d] stereo`). Reset goes back to the model's own 0.6 % / 0.5 % and convergence 50. |
| Greyed reasons | "3D is off in settings" / "3D needs the depth model, which is not installed" / "3D is off: the depth model did not load". |
| config.ini `[vr] depth_source` | zipdepth (default), midas, zipdepth-int8, off, flat, ramp, blob, eyetest, shifttest. Read at start; a model change needs a restart. |
| `[debug] depth_rate` (the governor's cap), `depth_debug` (shows the map), `eye_swap` | As upstream. |
| Engine lifecycle | Starts the first time the 3D is on (at session start if the switch was saved on), and never otherwise. Reset when the 3D comes back on. On FAILED (or a thread that never starts): flat, toast, button and tab put away, `depthGaveUp` set. Stopped and joined at exit. |
| Governor | xr_rate's `depthGovernorWindow` on the frame budget, which now gets GPU time. Each change of `gov.target` goes to `mlxr_depth_set_rate`. The depth rate halves (20 → 10 → 5, holding 10 s each time) before the display steps down, and the display never goes under 72 Hz. Measured headless only: the Frame lacks XR_EXT_performance_settings, and SteamVR offers one rate. |
| Stats | **5 s log line:** `3d on, ZipDepth 512x288 fp16 20.2 maps/s (target 20), model 36.4 ms (p95 39.1), stage 3.4 ms, capture to display 78 ms avg (2.3 frames, worst 100), skipped 0 (no buffer 0, replaced 0), 2 eyes drawn`, plus `gpu ... downscale/upsample/offset`. **Stats overlay:** `Depth 20.2/s (target 20), ZipDepth …` / `Depth inference 36 ms, age 78 ms, skipped 0`, or `3D: off, depth model idle`. |
| Knobs (upstream's debug properties as environment variables) | `MLXR_SEPARATION` (tenths of a percent), `MLXR_DEPTH_CUBIC`, `MLXR_OCCLUSION`, `MLXR_UPSAMPLE`, `MLXR_EDGE_FADE`, `MLXR_DEPTH_CPUS`, `MLXR_DEPTH_THREADS`, `MLXR_DEPTH_FP32`. |

## Budget (3D worst case)

**Swapchains:** no new ones. The picture's chain is double wide (2W x H, about 88 MB at 1440p instead of 44 MB) whenever a stereo mode is set, even with the 3D off, as upstream's. The curved picture's own image is 2W x H. The count stays at M5's ≤ 8 of 16 (headless runs: 3 with a room, 3 when curved).

**Layers:** the picture takes two quads in 3D (one per eye, `eyeVisibility`), and the bar gains the 3D button. `m6_budget` measures the busiest frames:
- void, head locked, full bar + 3D, glow, models, stats, ray, dot, toast: **16 of 16, nothing dropped**
- the same, not head locked: 16
- passthrough: 15

If a runtime offered fewer layers, M5's drop order applies: the toast first, then the models.

**GPU memory added:**
- 3 textures + 3 staging at 512x288 (3.5 MB)
- 2 readbacks (1.2 MB)
- the downscale image (0.6 MB)
- upsample + offset at a quarter of the video (1 MB at 1080p)

## Proof

- **ctest:** 108 of 108 pass (`-E audio_pulse`; the two launcher tests skip without build-launcher). That is main's 102 plus the 6 new tests below.
- **`m6_pipeline`** (fake engine, frames kept 2 deep on the GPU): 163 captures, every capture's pixels match its frame id, frame ids in order. No readback buffer reused while queued, no texture slot rewritten while in flight. The texture holds the newest map. A rate of 10 is held at 10.0 captures/s.
- **`m6_governor`:** xr_rate's cut/hold/display sequence, the 72 Hz floor, climbing back under the ceiling, and the target reaching the engine (also after a new preference).
- **`m6_settings`:** config.ini round trip of every 3D key, the models' separations, presets, both tracks, reset, off, a test pattern, a missing model, no engine.
- **`m6_states`:**
  - never on, never started; LOADING then READY, flat meanwhile
  - off stops the captures
  - back on resets the engine, captures at once, and waits for a fresh map (a stale pre-off map does not count)
  - FAILED and a NULL start leave it flat, with a toast, the switch put away, and refusing to turn on again
- **`m6_budget`:** the layer counts above.
- **`m6_pipeline_real`:** the real model on `test-1080p60.mp4` for 4 s.
- **Headless `--file` runs with the 3D on**, 6–10 s each: ZipDepth, int8, MiDaS, shift test + curve, ZipDepth + curve in the void, ZipDepth + home theater, 3D off + supersample, and software decode. All exited with rc 0. The only W line is the known "display rate 72 Hz refused". Every run shows the model's start, ready (91–250 ms) and stop. The kernel log over all runs has no iris/msm/smmu/hangcheck lines, only the camera ISP's usual CSID lines.
- **`tools/ldd-audit.sh build-m6/moonlight-xr`:** ok (LiteRT static).

## Deviations from upstream

1. **The 3D switch is remembered** in config.ini (`[3d] stereo`). It was off by default at merge; the owner chose on (2026-10-10), as upstream starts every session, so a fresh install now starts in 3D and loads the model at session start.
2. **The model starts on first use** when the saved switch is off; with it on (the default) it starts at session start, as upstream.
3. **On FAILED the 3D is put away**: the stereo mode goes to off, the button is hidden, the tab is greyed with a reason, and there is a toast. Upstream kept the flat synthetic map and the splash waiting.
4. **Maps go up in the frame loop's command buffer** (a copy per new map) instead of from the stage thread's own GL context.
5. **"Fresh map" after switching back on** is judged by the map's submit time, so a capture queued before the 3D went off does not end the wait.
6. **Not ported:**
   - upstream's debug captures (`writeCapture` / warp_lab) and the shift test's readback log line
   - the depth-scale preference (depthLocal: no native setting, fixed at 1.0)
   - the splash's wait for the first map (natively no splash)

## For the next session / the orchestrator

- **Owner headset check (script below).** Owner's answers 2026-10-10: 3D on by default; keep Moonlight XR's defaults (ZipDepth fp16 at its own 0.6 %, convergence 50; int8 stays an option, as upstream uses it only on Gen 1 headsets). Still open: the 78 ms depth age on moving content.
- **app/main.c needs no lines.** The session hands in the engine itself (`MLXR_HAVE_DEPTH` on `mlxr_xr`). It still logs "moonlight-xr (M4) starting" if you want a version bump there.
- **Build:** without `third-party-build/litert` the 3D stays off with "not installed", and `mlxr-m6-pipeline` and `m6_pipeline_real` are not built. `build.sh` already builds LiteRT. Installed assets already include `assets/depth` (install.sh mirrors `assets/`).
- **`tests/m6/gen_scene.sh [dir]`** remakes the analysis clip. Then run `build/xr/tests_m6/mlxr-m6-pipeline --clip DIR/scene.mp4 --seconds 10 --out OUT --analyse [--int8|--midas] [--frame-cpus 7]`, with `MLXR_DEPTH_CPUS=...` for placement runs.
- **Memory of the double-wide chain with the 3D off:** if it matters, the chain could be made double only while the 3D is live. That costs a swapchain re-create at each switch, which upstream avoided.

## Headset test script (owner)

1. Launch from Steam and start Desktop or a video. The bar now has a **3D** button (struck through: off).
2. Play a video with depth (a film, a game with a floor and characters). Press **3D**. Expect:
   - a "3D on" toast, and the picture flat for a moment while the model loads (well under a second)
   - then depth: near things in front of the screen, the background behind it
   - the Stats overlay (Display → Stats On) shows `Depth ~20/s (target 20)`, `Depth inference ~37 ms, age ~80 ms, skipped 0`
3. Cog → **3D** tab:
   - Comfort / Balanced / Strong change the strength at once.
   - Drag Depth (amber past the default means harder on the eyes) and Convergence (what sits on the screen plane) on a paused picture: it changes live.
   - Reset goes back to 0.6 % / 50.
   - Switch off/on: back on, the picture is flat for a few frames, then 3D.
4. Move your head side to side: each eye should see its own picture, with no doubled edges or swimming of the frame. Look at the left and right edges of the picture: the shift fades out there, with no smeared columns.
5. Screen tab → Curve up: still 3D, and still one picture per eye. Environment → Home Theater: 3D on the theater wall.
6. Turn 3D off and quit. Start again: the 3D comes back off. Turn it on and quit, then start again: 3D on at start.
7. Optional, via config.ini `[vr] depth_source` (then restart):
   - `zipdepth-int8`: lighter (about 0.6 CPU cores, 45 ms age). Compare the look with zipdepth.
   - `midas`: the older model.
   - `shifttest`: a white bar on black, shifted in each eye: left eye right of centre, right eye left.
   - `eyetest`: left eye red, right eye blue. A swapped pair means `[debug] eye_swap`.
8. Send `~/Games/MoonlightXRNative/moonlight.log`. The 5 s lines carry the `3d on, ... maps/s, model ... ms, capture to display ... ms, skipped ...` clause and `gpu warp/downscale/upsample/offset`.
