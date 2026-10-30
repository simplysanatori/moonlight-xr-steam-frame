# depth/ report (lane D, branch lane/depth)

Written by the orchestrator from lane D's hand-back (the lane could not write .md files). Measured on the Frame, 2026-10-10, SteamVR running idle.

## Runtime: LiteRT 2.21.0 from source, XNNPACK, static
No official LiteRT C library exists for Linux arm64. `depth/tools/build-litert.sh` builds into `~/Games/MoonlightXRNative/third-party-build/litert/dist` (configure 5.3 min incl. clones, build 5.1 min at nice 10 / -j3; set `-DTENSORFLOW_SOURCE_DIR` or the build fetches an older TF and fails). 92 archives merge into one 20 MB `libmlxr_litert.a`; a linked binary grows ~6.6 MB stripped and needs only libc + libm (ldd audit passes). Upstream's .tflite files load unchanged. fp16 arithmetic (`MLXR_DEPTH_FP16`) roughly doubles speed. onnxruntime was not pursued (no fp16 CPU path, re-exports, ~20 MB .so). The library reports "2.19.0" (upstream header lag). Without the LiteRT build the depth targets are skipped at configure time.

## API
`depth/litert_backend.h`: model table (ZipDepth 512x288 fp16, ZipDepth 256 int8, MiDaS v2.1 small 256 fp16; default separations ZipDepth 6, MiDaS 5); `mlxr_depth_model_load(path, {threads, flags})` checks shapes and reads the thread count back off the XNNPACK delegate; `input()` / `run(&ms)` / `output()`; RGBA8 converter. Input RGB 0..1 top row first; output relative inverse depth, larger = nearer.
`depth/depth_engine.h` (port of XrRenderer's depth + stage threads and xr_depth.c's finish/upload): `mlxr_depth_start(cfg)` (loads and warms on the depth thread; `wait_ready`/`state` LOADING/READY/FAILED with a reason; workers inherit `cpuMask`); `mlxr_depth_due(e, now)` per new video frame (xr_rate's depthGateDue, refuses while a capture waits); `mlxr_depth_submit(e, rgba, stride, w, h, flip, frameId, captureNs)` from the stage thread (conversion + scene-cut detector in submit order; a newer capture replaces a waiting one, carrying its resets); depth thread normalises exactly as upstream's nativeUploadDepth (per-texel and range averages over real time, cut resets, NaN as far, global/local remap with low pass) into RGBA8 (guide colour rgb, depth a, 1 = near) plus floats; `mlxr_depth_acquire(e, haveSeq)` / `release` (three slots, never blocks the depth thread); `set_rate`, `set_params`, `reset`, `stats`. No row flips anywhere (Vulkan passes are top-row-first).

## Models (assets/depth, byte-identical to upstream)
| File | Size | sha256 |
|---|---|---|
| zipdepth_512x288_fp16.tflite | 12.3 MB | 677c7eec… |
| zipdepth_256_int8dr.tflite | 6.3 MB | f372bfab… |
| midas_v21_small_256_fp16.tflite | 33.2 MB | 79859ed7… |
Licences beside them: ZipDepth MIT (Fabio Tosi), MiDaS MIT (Intel ISL), LiteRT Apache 2.0, `third_party_LICENSES.txt` for XNNPACK, pthreadpool, cpuinfo, FXdiv, FP16, KleidiAI, abseil, ruy, gemmlowp, FlatBuffers, farmhash, fft2d, Eigen (MPL2-only build).

## Bench (`mlxr-depth-bench`, 40 runs)
| Model | Threads | Arithmetic | maps/s | p50 ms |
|---|---|---|---|---|
| ZipDepth 512x288 | 1 | fp32 / fp16 | 7.3 / 13.3 | 130 / 72 |
| ZipDepth 512x288 | 2 | fp32 / fp16 | 15.3 / 26.7–28.4 | 64 / 34–37 |
| ZipDepth 512x288 | 3 | fp32 / fp16 | 20.2 / 36.4 | 48 / 27 |
| ZipDepth 512x288 | 4 | fp32 / fp16 | 24.7 / 43.5 | 40 / 23 |
| ZipDepth int8 256 | 2 | fp32 / fp16 | 102–107 / 118–120 | 9.4 / 8.3 |
| MiDaS 256 | 2 / 4 | fp16 | 21.6 / 36.8 | – |
Target was ≥ 12 (upstream int8 did 12 on XR2 Gen 1), ideally ~20. CPUs 3–6 are Cortex-A720, 7 is the Cortex-X4; X4 alone 18.7 maps/s, one A720 11.1; unpinned single thread migrates (p95 110 ms). Memory peak (2 threads, fp16/fp32): ZipDepth 512 71/96 MB, int8 41/48 MB, MiDaS 93/133 MB. Load 15–90 ms, warm-up 80–130 ms.
Threaded engine at a 60 fps synthetic feed: ZipDepth 512 fp16 2 threads at rate 20 holds 20.3 maps/s (model 38 ms avg, 46 p95; capture to frame loop 51 ms); 3 threads 37 ms; fp32 2 threads cannot hold 20 (15.5). Sustained: 180 s at 20/s held 19.9; 120 s unlimited 3 threads 33.4 maps/s at ~67 °C. No measurable change alongside a software 1080p60 H.264 decode.

## Tests (`ctest -L depth`, 7 groups, 126 checks)
Ray-traced scenes with true depth: ordering near = brighter for all models (Spearman with 1/z: ZipDepth fp16 0.814, int8 0.840, MiDaS 0.949); bit-identical across repeats and 1 vs 4 threads; fp16 vs int8 Spearman 0.973; fp16 vs fp32 arithmetic mean 0.8/255; normalisation and engine groups (frame ids, replacement, rate held at 10.0/s, ordering, failure reason).

## Not yet seen
A live XR session with the compositor and decode competing; real `mlxr_pass_downscale` readback (input tested with synthetic RGBA8); real streamed content; whether the frame loop wants the X4 kept free. Vulkan compute is not needed (would be ~20 shaders and compete for the GPU).

## For the orchestrator (M6)
1. Run `depth/tools/build-litert.sh` once per machine (~12 min, network first time); link with `mlxr_link_depth(<target>)`; package `assets/depth/*.tflite` + licences.
2. Defaults: ZipDepth 512x288, `MLXR_DEPTH_FP16`, 2 threads, rate 20 (`DEPTH_RATE_DEFAULT`), no pinning (pin `cpuMask 0x78` to the A720s if the X4 is needed elsewhere; 3 threads saves ~10 ms latency; int8 is the low-CPU fallback).
3. Once READY, `mlxr_depth_size()` (512x288) sizes the downscale target (`MLXR_PASS_FORMAT`) and 3 rotating depth textures; on FAILED stay flat.
4. Each new video frame, if `mlxr_depth_due`: record `mlxr_pass_downscale`, `vkCmdCopyImageToBuffer` into one of 2 host-visible buffers with a timeline value; keep frame index and capture time.
5. Stage thread: `vkWaitSemaphores`, invalidate if non-coherent, `mlxr_depth_submit(mapped, w*4, w, h, 0, frameId, captureNs)`.
6. Each frame: `mlxr_depth_acquire`; on a new map memcpy 590 KB into staging, release, copy into the next texture; bind as `MlxrUpsampleParams.depth` (or the warp's depth when the upsample is off); show depth age and maps/s.
7. Governor: `depthGovernorWindow(&gov, now, rateBudgetTick verdict, 3D on && READY)`, call `mlxr_depth_set_rate` when `gov.target` changes; the Frame lacks XR_EXT_performance_settings (XR_META_performance_metrics may serve).
8. Toggles: 3D back on → `mlxr_depth_reset`; preference changes → `mlxr_depth_set_params`; model change → stop + start. Comfort/Balanced/Strong are separation steps in the 3D panel and the warp.

## Wave 5 (lane wave5-depth)
- 5.1 Edge: `[3d] edge_sharpness` 0..100 -> `depthEdgeParams` (sigmaR 0.25..0.10, sharp 0..1); 0 is upstream's upsample.
- 5.2 Disocclusion: `offset.frag` flags a pixel whose source segment lands spread over more than `MLXR_HOLE_STRETCH` (1.5) texels; the climb keeps its own size next to the nearer surface (shifted as its top is) and the rest of the gap takes the background a texel past the foot of the climb. Flags in the offset map's b (left) and a (right); `warp.frag` skips its Newton steps there. On by default (`MLXR_HOLE_FILL=0` off). `shaders_warp_fur`: thin anti-aliased dark bar, model-soft step depth, 2.9 depth texels of separation: the uncovered edge went from ~5 px of smear in one eye (1 px in the other) to 1-2 px in both. The design was worked out on a Python model of upsample + offset + warp before the shader was written; the threshold 1.5 caught the soft climbs (2 for example missed a 1.6-1.8 spread) while a gentle slope (a floor) spreads by well under 1.2.
- 5.4 `[3d] motion_reset` (percent luma, 0 off): `depthMotionReset` after the per texel blend, on the guide's luma against the last map's.
- 5.6 `[3d] depth_scale` (upstream vrDepthScale, depthLocal) and `depth_global` (depthGlobal, upstream fixed at 100); 100/100 skips the remap.
- 5.5: `[debug] depth_threads` (default 2) only. Not done, as not contained: capture at frame arrival (needs its own submit outside the frame's command buffer and a second timeline; the capture time is already the frame's own, so the age figure is unchanged by it); the governor switching to the int8 model before halving the rate (a model change is a stop, a load of 15-90 ms plus warm-up, a new map size, a new downscale target and new textures, mid-stream). No new latency was measured: the Frame was not used by this lane.
