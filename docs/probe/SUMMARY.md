# Runtime probe summary (Steam Frame, 2026-10-10)

Raw outputs sit beside this file. Re-run after every SteamOS update and diff:
`./build/hello/mlxr-hello --probe --probe-out docs/probe/openxr-probe.txt` plus the
commands at the end of this file.

## Platform
| Item | Value | File |
| --- | --- | --- |
| SteamOS | 0.4.5, build 20261007.6125817, kernel 6.18.0, glibc 2.39 | system.txt |
| CPUs visible to user processes | 5 (of 8) | system.txt |
| Host toolchain | clang/lld 19.1.7, cmake 3.29, ninja, glslc (shaderc 2023.8), gcc, python3 (no numpy), all in the factory image | host-packages.txt |
| GPU | Turnip Adreno 750, Mesa 26.3.0-devel, Vulkan 1.4.362 | vulkaninfo-summary.txt |

## OpenXR (SteamVR/OpenXR 2.18.2, `/opt/steamvr/steamxr_linuxarm64.json` → `bin/linuxarm64/vrclient.so`)
- `XR_KHR_vulkan_enable2` present. **The runtime accepts Vulkan 1.0 .. 1.2 only**: create the instance at 1.2 and take newer features as extensions (`VK_KHR_dynamic_rendering`).
- Present and supported by the system: `XR_EXT_hand_tracking` (supportsHandTracking 1), `XR_EXT_eye_gaze_interaction` (supportsEyeGazeInteraction 1), `XR_EXT_user_presence` (1), `XR_FB_display_refresh_rate`, `XR_VALVE_frame_controller_interaction`, `XR_EXT_hand_interaction`, `XR_EXT_palm_pose`, `XR_EXT_local_floor`, `XR_KHR_visibility_mask`, `XR_FB_foveation`(+`_vulkan`, `_configuration`), `XR_META_foveation_eye_tracked`, `XR_FB_space_warp`, `XR_EXT_frame_synthesis`, `XR_META_performance_metrics`, `XR_VALVE_timing_utils`, `XR_KHR_composition_layer_depth` (listed; do not rely on it).
- **Absent:** `XR_FB_passthrough`, `XR_KHR_composition_layer_cylinder`, `XR_KHR_composition_layer_color_scale_bias`, `XR_FB_composition_layer_settings`, `XR_KHR_composition_layer_equirect2`, `XR_EXT_performance_settings`.
- Blend modes: OPAQUE and ALPHA_BLEND (passthrough = alpha blend with a projection layer under the quads).
- maxLayerCount 16. Recommended eye size 1728×1728 (max 8192). Swapchain formats: R8G8B8A8_SRGB, B8G8R8A8_SRGB, R8G8B8A8_UNORM, B8G8R8A8_UNORM, RGBA16F, A2B10G10R10, D32/D16/D24S8/D32S8.
- Reference spaces: VIEW, LOCAL, STAGE (LOCAL_FLOOR needs the extension enabled).
- Refresh rates: **SteamVR offers exactly one rate**, which it changes itself (90.00 in one run, 72.00 in the next two). Requests cannot raise it today.
- An implicit layer "fdm_injection" loads into every session (Valve foveation); harmless so far.

## Frame controller profile `/interaction_profiles/valve/frame_controller` (each path suggested alone)
Accepted on both hands: aim/pose, grip/pose, trigger/value|click|touch, squeeze/value|click, thumbstick, thumbstick/click|touch, system/click, bumper/click, output/haptic.
Right hand only: a/click|touch, b/click|touch, x/click|touch, y/click|touch, menu/click.
Left hand only: view/click, dpad_up|down|left|right/click.
Rejected everywhere: squeeze/force, grip/value|click, trackpad/*, bumper/value, thumbrest/touch.
So the right controller carries A/B/X/Y and Menu; the left carries the d-pad and View. Grips are `squeeze/value`.
`/interaction_profiles/ext/eye_gaze_interaction` gaze_ext/pose: accepted. `khr/simple_controller`: all accepted.

## Video decode (`/dev/video-dec0` → `/dev/video22`, iris_driver 6.18.0)
- OUTPUT (bitstream): H264, HEVC, VP90 (never use VP9: firmware crash, SteamOS #2865).
- CAPTURE: Q08C (QCOM UBWC compressed 8-bit), NV12, NV21, AB24, QC24.
- FFmpeg 7.0 (libavcodec.so.61) has `h264_v4l2m2m` and `hevc_v4l2m2m`; both decoded test clips with no kernel warnings: 1080p60 H.264 300 frames in 0.21 s, 1440p60 HEVC 180 frames in 0.20 s (throughput, not latency).

## Vulkan import path (Turnip)
`VK_EXT_external_memory_dma_buf`, `VK_EXT_image_drm_format_modifier`, `VK_EXT_queue_family_foreign`, `VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore_fd`, `VK_KHR_external_fence_fd`, `VK_KHR_sampler_ycbcr_conversion` (samplerYcbcrConversion = true), `VK_KHR_timeline_semaphore`; `VK_FORMAT_G8_B8R8_2PLANE_420_UNORM` supported. ASTC LDR and HDR supported (`VK_FORMAT_ASTC_4x4_UNORM_BLOCK` etc.).

## Audio
PulseAudio API on PipeWire 1.6.8; default sink is the headset speaker loopback.

## Libraries on the host (link targets)
libvulkan.so.1, libdrm.so.2, libpipewire-0.3.so.0, libpulse.so.0, libcrypto.so.3, libcurl.so.4, libavcodec.so.61 (also .58), libopus.so.0, libfreetype.so.6, libpng16.so.16.
The host's `/usr/lib/libopenxr_loader.a` (gfxreconstruct) is a 14 KB thin archive and unusable; the loader is built static from `third_party/OpenXR-SDK` (release-1.1.63).

## Commands for the non-OpenXR files
```
v4l2-ctl -d /dev/video-dec0 --all > v4l2-all.txt
v4l2-ctl -d /dev/video-dec0 --list-formats-out > v4l2-formats-out.txt
v4l2-ctl -d /dev/video-dec0 --list-formats > v4l2-formats-cap.txt
vulkaninfo > vulkaninfo.txt; vulkaninfo --summary > vulkaninfo-summary.txt; vulkaninfo --show-formats > vulkaninfo-formats.txt
ffmpeg -hide_banner -decoders | grep -E 'v4l2m2m|h264|hevc|opus' > ffmpeg-decoders.txt
pactl info; pw-cli info 0 > audio.txt
```
