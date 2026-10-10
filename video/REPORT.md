# video/ report (lane V, branch lane/video)

Written by the orchestrator from lane V's hand-back (the lane could not write .md files). All numbers measured on the Frame with SteamVR running, 2026-10-10.

## Decisions
**Direct V4L2, not FFmpeg.** The host FFmpeg 7.0 `h264_v4l2m2m`/`hevc_v4l2m2m` expose no pixel-format list, no hw configs and no `drm_prime` option (probe in `evidence/ffmpeg_v4l2m2m_probe.{c,txt}`); upstream's wrapper hands out pointers into mmap'd NV12 (a CPU copy). The decoder follows dev-decoder.rst's stateful sequence: S_FMT OUTPUT, REQBUFS+mmap OUTPUT, SUBSCRIBE_EVENT(SOURCE_CHANGE) before STREAMON, queue bitstream, wait for the event, G_FMT + G_SELECTION(COMPOSE) on CAPTURE, REQBUFS, one EXPBUF per buffer kept for the whole CAPTURE set, QBUF all, STREAMON. No FFmpeg dependency.

Device facts: CAPTURE stays at the 384x256 placeholder until the event (#2869), which arrives ~1 ms after the first AU. 1080p: NV12 1920x1088, bytesperline 1920, sizeimage 3133440, one memory plane, CbCr at bpl*1088, COMPOSE 1920x1080. Iris parses the VUI (BT.709 limited). 12 CAPTURE buffers accepted.

**NV12 linear by default; Q08C (UBWC) opt-in.** Turnip lists LINEAR (0x0) and QCOM_COMPRESSED (0x0500000000000001) for `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM`, both sampled with linear YCbCr filtering, cosited and midpoint chroma (`evidence/turnip_nv12_modifiers.txt`). Q08C decodes correctly with the Iris layout [Y meta][Y][UV meta][UV], 4 KiB aligned (Y meta pitch ALIGN(ceil(w/32),64), rows ALIGN(ceil(h/8),16); Y stride ALIGN(w,128), rows ALIGN(h,32); UV meta pitch ALIGN(ceil(w/2/16),64), rows ALIGN(ceil(h/2/8),16); UV rows ALIGN(h/2,32); Vulkan plane 1 offset = Y meta + Y; sizeimage 3153920 at 1080p). Gain ~0.1 ms decode latency at 1440p; no soak, so it stays behind the `ubwc` option.

## Measurements
| Run | Decode latency submit→dequeue |
| --- | --- |
| 1080p HEVC | p50 1.78–1.86 ms, p99 ~2.2 ms |
| 1080p H.264 | p50 ~2.1–2.2 ms |
| 1440p HEVC ~80 Mbps, 90 fps | p50 5.8 ms |
| 30 min soak, 1440p60 HEVC 78.6 Mbps, 90 fps pacing, nice 10 | 162001/162001 frames; mean 5.80, p50 5.82, p95 6.35, p99 6.56, max 17.67 ms (first frame), 8.81 ms after |

Soak: one CAPTURE allocation of 12 buffers, 12 EXPBUFs, 12 Vulkan imports; 0 import failures, 0 requeue errors, 0 decode errors; 719 kernel log lines captured, 0 matching iris/msm/vidc/smmu/iommu/gem/drm/oops/warn (`evidence/soak_30min_1440p_hevc_80m_90fps.txt`).
One AU in gives one frame out (lockstep never stalls). Import count equals CAPTURE buffer count (ctest fails otherwise). Resolution change 1080p→1440p→1080p: 3 SOURCE_CHANGE events, 2 reallocations, 36 imports for 36 buffers, pictures correct, up to ~90 ms spike per change. `mlxr_vdec_reset` mid-stream then replay from the IDR: all frames. A dropped AU gives 2 ERROR-flagged frames → `E_DECODE` (IDR request), then decoding continues. Colour vs software decode: mean difference 0.57–0.59; a hue-rotated negative control fails as intended.

## API
Decoder `mlxr_vdec_*`: `open(cfg)` (codec, size hint, capture buffers 12, output buffers 6, ubwc); `submit(au,size,pts)` / `submit_parts(parts[],sizes[],n,pts)` (stream thread; copies into an mmap'd OUTPUT buffer, waits ≤5 ms for one); `receive(&frame,timeout_ms)` → OK / AGAIN / FORMAT_CHANGE / negative error (decode thread); `release(frame)`; `get_buffers()`; `reset()` (next AU must be an IDR); `apply_format_change()`; `get_stats()`; `close()`. Every negative status except misuse means request an IDR (`mlxr_vdec_needs_idr`). Failing ioctls log `VIDIOC_X failed: <error> (errno N)`.
Frame: buffer index, CAPTURE generation, visible and coded size, per-plane fd (owned by the decoder) + inode/offset/pitch, DRM format + modifier, V4L2 colour fields, pts, submit/dequeue times, corrupt flag.
Importer `mlxr_vkimp_*`: `device_extensions()` (VK_KHR_external_memory_fd, VK_EXT_external_memory_dma_buf, VK_EXT_image_drm_format_modifier, VK_EXT_queue_family_foreign, VK_KHR_external_semaphore_fd, VK_KHR_external_fence_fd; features samplerYcbcrConversion + timelineSemaphore); `check_physical_device()`; `create(phys, device, queue family, release cb)`; `import_all(buffers)`; `get(frame)` (cached image + view; extent = coded size, `uv_scale` = visible/coded); `sampler()` (immutable YCbCr sampler + serial); `descriptor_count()`; `cmd_acquire()` (FOREIGN → queue family; UNDEFINED first, then GENERAL → SHADER_READ_ONLY); `cmd_release()`; `frame_done_timeline(frame, sem, value)` / `frame_done_fence()`; `poll()` (requeues frames whose GPU work finished); `drain()`, `reset()`, `destroy()`, `print_modifiers()`.
`get()` checks vkGetPhysicalDeviceImageFormatProperties2 (modifier, dma-buf importable, max extent), size and memory type before importing, because a failing import is the kernel path that oopses (#2882). The fd is dup'd before import and closed only if the import fails.

## Confirmed vs reasoned
Confirmed: everything measured above. Not proven: validation layers (none installed); behaviour inside the OpenXR frame loop; real Moonlight streams (loss, RFI, Sunshine colour tags); 10-bit; whether the kernel has the #2882 fix (avoided by design); two decoder sessions at once; Q08C under soak. Early short runs showed occasional 85–128 ms spikes with up to 12 AUs in flight; absent in the 30-minute soak (other device load).

## For the orchestrator
- FORMAT_CHANGE: the decode thread stops and hands over to the render thread, which waits for GPU idle, calls frame_done on the frame it holds, `vkimp_drain` + `vkimp_reset`, rebuilds pipelines/descriptor layouts (the immutable sampler changes), then `apply_format_change` while nothing else touches the decoder. Same-size SOURCE_CHANGE is handled inside the decoder.
- Frame holding: keep the newest frame until a newer one has been rendered; call frame_done with the timeline value of the last XR-frame submission that sampled it (a 60 fps frame is sampled several times at 72–90 Hz). 12 buffers leave headroom.
- Decode units: `submit_parts` with moonlight-common-c's buffer list; return DR_NEED_IDR on any needs_idr status from submit or receive.
- Open: advertise RFI? Override colour with the negotiated space/range? Q08C default after a soak? Rerun `tests/video/soak.sh` and `--probe-modifiers` after kernel updates.
