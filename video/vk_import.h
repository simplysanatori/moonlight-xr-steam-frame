// Moonlight XR Native - cached Vulkan import of V4L2 CAPTURE dma-bufs.
//
// Copyright (C) 2026 Moonlight XR Native contributors.
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 3 as published by
// the Free Software Foundation. It is distributed WITHOUT ANY WARRANTY; see
// LICENSE.txt for the full license text.
//
// Each decoder CAPTURE buffer is imported ONCE (VkImage + VkDeviceMemory +
// VkImageView) and cached, keyed by (generation, buffer index, dma-buf inode).
// Imports are destroyed only by mlxr_vkimp_reset() / mlxr_vkimp_destroy(),
// never per frame: on the Frame's 6.18 kernel, per-frame import/free churn
// can hit a NULL dereference in msm_gem_import() that freezes the headset
// (SteamOS #2882).
//
// Per frame, the renderer:
//   1. img = mlxr_vkimp_get(imp, &frame)          (imports on first sight)
//   2. mlxr_vkimp_cmd_acquire(imp, cmd, img)      before sampling img->view
//      sample with the immutable sampler (mlxr_vkimp_sampler) in a descriptor
//      set layout created for it
//   3. mlxr_vkimp_cmd_release(imp, cmd, img)      after the last sample
//   4. submit, signalling a timeline semaphore value (or a fence)
//   5. mlxr_vkimp_frame_done_timeline(imp, &frame, sem, value) when the
//      renderer stops using this frame (i.e. after its LAST submission)
//   6. mlxr_vkimp_poll(imp) once per loop iteration: gives every frame whose
//      GPU work finished back to the decoder (QBUF) through the release
//      callback. Nothing is requeued before the GPU is done.
//
// Thread-safety: one thread (the render thread) uses an importer.
#ifndef MLXR_VK_IMPORT_H
#define MLXR_VK_IMPORT_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <vulkan/vulkan.h>

#include "v4l2_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

// Device extensions the importer needs, for VkDeviceCreateInfo (or for the
// OpenXR xrCreateVulkanDeviceKHR create info). All are present on Turnip.
// Also enable these features: VkPhysicalDeviceVulkan11Features.
// samplerYcbcrConversion and VkPhysicalDeviceVulkan12Features.timelineSemaphore.
const char *const *mlxr_vkimp_device_extensions(uint32_t *count);

// Checks the extensions and features above on a physical device. Returns
// false and prints what is missing.
bool mlxr_vkimp_check_physical_device(VkPhysicalDevice phys);

typedef void (*mlxr_vkimp_release_fn)(void *user, const mlxr_vframe *frame);

typedef struct {
	VkPhysicalDevice phys;
	VkDevice device;
	uint32_t queue_family;  // the queue that samples the images
	// Called from mlxr_vkimp_poll() when a frame's GPU work finished, normally
	// a wrapper around mlxr_vdec_release().
	mlxr_vkimp_release_fn release;
	void *release_user;
	// Filter for the YCbCr sampler (falls back to NEAREST when the format
	// lacks linear YCbCr filtering for the modifier).
	bool linear_filter;
	// Colour override. With override_color false the model/range come from the
	// decoder's V4L2 colour description, defaulting to BT.709 limited range.
	bool override_color;
	VkSamplerYcbcrModelConversion model;
	VkSamplerYcbcrRange range;
	bool verbose;
} mlxr_vkimp_config;

typedef struct {
	VkImage image;
	VkImageView view;         // sampled with mlxr_vkimp_sampler()
	VkDeviceMemory memory;
	uint32_t index, generation;
	uint32_t width, height;             // visible
	uint32_t coded_width, coded_height; // image extent
	float uv_scale[2];        // visible / coded: multiply UVs by this
	bool first_use;           // no acquire recorded yet
} mlxr_vkimp_image;

typedef struct {
	uint64_t imports;         // dma-buf imports (must equal decoder CAPTURE buffers)
	uint64_t import_failures;
	uint64_t frames_pending;  // frames waiting for the GPU right now
	uint64_t frames_released; // frames given back to the decoder
	uint64_t conversions;     // VkSamplerYcbcrConversion objects created
} mlxr_vkimp_stats;

typedef struct mlxr_vkimp mlxr_vkimp;

int mlxr_vkimp_create(const mlxr_vkimp_config *cfg, mlxr_vkimp **out);

// Imported image for a frame (cached). NULL on failure (logged; the import
// is not retried for that buffer, the frame should be released unused).
const mlxr_vkimp_image *mlxr_vkimp_get(mlxr_vkimp *imp, const mlxr_vframe *frame);

// Import every buffer of a CAPTURE generation up front (from
// mlxr_vdec_get_buffers), so no import happens in the middle of the stream.
// Buffers already imported are skipped. Returns the number of failures.
int mlxr_vkimp_import_all(mlxr_vkimp *imp, const mlxr_vframe *buffers, uint32_t count);
// Generation of the current imports (0 = none).
uint32_t mlxr_vkimp_generation(mlxr_vkimp *imp);

// The immutable sampler (with its YCbCr conversion) every image view is
// compatible with. Created with the first imported frame from its colour
// description; 'serial' changes whenever it is re-created (then rebuild the
// descriptor set layout and pipeline that embed it).
VkSampler mlxr_vkimp_sampler(mlxr_vkimp *imp, uint32_t *serial);
VkSamplerYcbcrConversion mlxr_vkimp_conversion(mlxr_vkimp *imp);

// Queue-family-foreign ownership barriers around the sampling.
void mlxr_vkimp_cmd_acquire(mlxr_vkimp *imp, VkCommandBuffer cmd, const mlxr_vkimp_image *img,
			    VkPipelineStageFlags dst_stage);
void mlxr_vkimp_cmd_release(mlxr_vkimp *imp, VkCommandBuffer cmd, const mlxr_vkimp_image *img,
			    VkPipelineStageFlags src_stage);

// Hand a frame back once 'sem' reaches 'value' (timeline semaphore) or once
// 'fence' signals (the importer only reads the fence status; do not reset or
// destroy it before this frame is released).
int mlxr_vkimp_frame_done_timeline(mlxr_vkimp *imp, const mlxr_vframe *frame, VkSemaphore sem,
				   uint64_t value);
int mlxr_vkimp_frame_done_fence(mlxr_vkimp *imp, const mlxr_vframe *frame, VkFence fence);

// Release every frame whose GPU work completed. Returns how many.
int mlxr_vkimp_poll(mlxr_vkimp *imp);
// Wait for (up to timeout_ns) and release every pending frame.
int mlxr_vkimp_drain(mlxr_vkimp *imp, uint64_t timeout_ns);

// Destroy all imports (after vkDeviceWaitIdle or equivalent and drain). Used
// on a decoder format change or full teardown only.
void mlxr_vkimp_reset(mlxr_vkimp *imp);
void mlxr_vkimp_get_stats(mlxr_vkimp *imp, mlxr_vkimp_stats *stats);
void mlxr_vkimp_destroy(mlxr_vkimp *imp);

// Prints the DRM format modifiers Turnip lists for
// VK_FORMAT_G8_B8R8_2PLANE_420_UNORM (diagnostics).
void mlxr_vkimp_print_modifiers(VkPhysicalDevice phys, FILE *out);

#ifdef __cplusplus
}
#endif
#endif
