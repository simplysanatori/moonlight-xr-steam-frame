// Moonlight XR Native - cached Vulkan import of V4L2 CAPTURE dma-bufs.
// See vk_import.h for the contract.
//
// Copyright (C) 2026 Moonlight XR Native contributors.
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 3 as published by
// the Free Software Foundation. It is distributed WITHOUT ANY WARRANTY; see
// LICENSE.txt for the full license text.

#define _GNU_SOURCE
#include "vk_import.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/videodev2.h>
#include <libdrm/drm_fourcc.h>

#define MAX_IMAGES 32
#define MAX_PENDING 64
#define YUV_FORMAT VK_FORMAT_G8_B8R8_2PLANE_420_UNORM

static const char *const k_device_exts[] = {
	VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
	VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
	VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
	VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
	VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
	VK_KHR_EXTERNAL_FENCE_FD_EXTENSION_NAME,
};

const char *const *mlxr_vkimp_device_extensions(uint32_t *count)
{
	*count = (uint32_t)(sizeof(k_device_exts) / sizeof(k_device_exts[0]));
	return k_device_exts;
}

typedef struct {
	bool used;      // slot holds a live import
	bool failed;    // import failed once: never retried for this buffer
	uint64_t inode;
	mlxr_vkimp_image img;
} cache_entry;

typedef struct {
	bool used;
	mlxr_vframe frame;
	VkSemaphore sem;
	uint64_t value;
	VkFence fence;
} pending_entry;

struct mlxr_vkimp {
	mlxr_vkimp_config cfg;
	PFN_vkGetMemoryFdPropertiesKHR get_fd_props;
	cache_entry cache[MAX_IMAGES];
	uint32_t generation;   // generation of the cached imports (0 = none)
	pending_entry pending[MAX_PENDING];
	VkSamplerYcbcrConversion conversion;
	VkSampler sampler;
	uint32_t sampler_serial;
	uint64_t conv_key;     // colour description the conversion was built for
	VkFormatFeatureFlags mod_features;
	uint32_t mod_planes;
	bool mod_checked;
	mlxr_vkimp_stats st;
};

static void ilog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void ilog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fputs("[vkimp] ", stderr);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

static bool has_ext(const VkExtensionProperties *e, uint32_t n, const char *name)
{
	for (uint32_t i = 0; i < n; i++)
		if (!strcmp(e[i].extensionName, name))
			return true;
	return false;
}

bool mlxr_vkimp_check_physical_device(VkPhysicalDevice phys)
{
	uint32_t n = 0;
	vkEnumerateDeviceExtensionProperties(phys, NULL, &n, NULL);
	VkExtensionProperties *e = calloc(n ? n : 1, sizeof(*e));
	vkEnumerateDeviceExtensionProperties(phys, NULL, &n, e);
	bool ok = true;
	uint32_t cnt;
	const char *const *req = mlxr_vkimp_device_extensions(&cnt);
	for (uint32_t i = 0; i < cnt; i++) {
		if (!has_ext(e, n, req[i])) {
			ilog("missing device extension %s", req[i]);
			ok = false;
		}
	}
	free(e);
	VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
						 .pNext = &f12 };
	VkPhysicalDeviceFeatures2 f2 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f11 };
	vkGetPhysicalDeviceFeatures2(phys, &f2);
	if (!f11.samplerYcbcrConversion) {
		ilog("samplerYcbcrConversion not supported");
		ok = false;
	}
	if (!f12.timelineSemaphore) {
		ilog("timelineSemaphore not supported");
		ok = false;
	}
	return ok;
}

static const char *modifier_name(uint64_t m)
{
	switch (m) {
	case DRM_FORMAT_MOD_LINEAR: return "LINEAR";
	case DRM_FORMAT_MOD_QCOM_COMPRESSED: return "QCOM_COMPRESSED (UBWC)";
	case DRM_FORMAT_MOD_QCOM_TILED2: return "QCOM_TILED2";
	case DRM_FORMAT_MOD_QCOM_TILED3: return "QCOM_TILED3";
	case DRM_FORMAT_MOD_INVALID: return "INVALID";
	}
	return "other";
}

// Fills 'props' (up to max) with the modifiers listed for YUV_FORMAT.
static uint32_t query_modifiers(VkPhysicalDevice phys, VkDrmFormatModifierPropertiesEXT *props, uint32_t max)
{
	VkDrmFormatModifierPropertiesListEXT list = {
		.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT };
	VkFormatProperties2 fp = { .sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, .pNext = &list };
	vkGetPhysicalDeviceFormatProperties2(phys, YUV_FORMAT, &fp);
	if (list.drmFormatModifierCount > max)
		list.drmFormatModifierCount = max;
	list.pDrmFormatModifierProperties = props;
	vkGetPhysicalDeviceFormatProperties2(phys, YUV_FORMAT, &fp);
	return list.drmFormatModifierCount;
}

void mlxr_vkimp_print_modifiers(VkPhysicalDevice phys, FILE *out)
{
	VkDrmFormatModifierPropertiesEXT props[64];
	uint32_t n = query_modifiers(phys, props, 64);
	fprintf(out, "VK_FORMAT_G8_B8R8_2PLANE_420_UNORM: %u DRM format modifiers\n", n);
	for (uint32_t i = 0; i < n; i++) {
		VkFormatFeatureFlags f = props[i].drmFormatModifierTilingFeatures;
		fprintf(out, "  0x%016llx %-24s planes %u features 0x%08x%s%s%s%s%s%s\n",
			(unsigned long long)props[i].drmFormatModifier, modifier_name(props[i].drmFormatModifier),
			props[i].drmFormatModifierPlaneCount, f,
			(f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? " SAMPLED" : "",
			(f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) ? " YCBCR_LINEAR" : "",
			(f & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT) ? " MIDPOINT" : "",
			(f & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT) ? " COSITED" : "",
			(f & VK_FORMAT_FEATURE_DISJOINT_BIT) ? " DISJOINT" : "",
			(f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT)
				? " SEPARATE_RECON"
				: "");
	}
}

int mlxr_vkimp_create(const mlxr_vkimp_config *cfg, mlxr_vkimp **out)
{
	*out = NULL;
	if (!cfg || !cfg->device || !cfg->phys || !cfg->release)
		return -1;
	mlxr_vkimp *imp = calloc(1, sizeof(*imp));
	if (!imp)
		return -1;
	imp->cfg = *cfg;
	imp->get_fd_props = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(cfg->device,
									  "vkGetMemoryFdPropertiesKHR");
	if (!imp->get_fd_props) {
		ilog("vkGetMemoryFdPropertiesKHR unavailable (VK_KHR_external_memory_fd not enabled?)");
		free(imp);
		return -1;
	}
	*out = imp;
	return 0;
}

// ------------------------------------------------------------- conversion

static void colour_for_frame(const mlxr_vkimp *imp, const mlxr_vframe *f,
			     VkSamplerYcbcrModelConversion *model, VkSamplerYcbcrRange *range)
{
	if (imp->cfg.override_color) {
		*model = imp->cfg.model;
		*range = imp->cfg.range;
		return;
	}
	uint32_t enc = f->v4l2_ycbcr_enc;
	if (enc == V4L2_YCBCR_ENC_DEFAULT) {
		switch (f->v4l2_colorspace) {
		case V4L2_COLORSPACE_SMPTE170M:
		case V4L2_COLORSPACE_470_SYSTEM_BG:
		case V4L2_COLORSPACE_470_SYSTEM_M:
		case V4L2_COLORSPACE_JPEG: enc = V4L2_YCBCR_ENC_601; break;
		case V4L2_COLORSPACE_BT2020: enc = V4L2_YCBCR_ENC_BT2020; break;
		default: enc = V4L2_YCBCR_ENC_709; break;
		}
	}
	switch (enc) {
	case V4L2_YCBCR_ENC_601:
	case V4L2_YCBCR_ENC_XV601: *model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601; break;
	case V4L2_YCBCR_ENC_BT2020:
	case V4L2_YCBCR_ENC_BT2020_CONST_LUM: *model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020; break;
	default: *model = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709; break;
	}
	*range = (f->v4l2_quantization == V4L2_QUANTIZATION_FULL_RANGE ||
		  (f->v4l2_quantization == V4L2_QUANTIZATION_DEFAULT && f->v4l2_colorspace == V4L2_COLORSPACE_JPEG))
			 ? VK_SAMPLER_YCBCR_RANGE_ITU_FULL
			 : VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
}

static bool check_modifier(mlxr_vkimp *imp, uint64_t modifier)
{
	if (imp->mod_checked)
		return imp->mod_planes != 0;
	imp->mod_checked = true;
	VkDrmFormatModifierPropertiesEXT props[64];
	uint32_t n = query_modifiers(imp->cfg.phys, props, 64);
	for (uint32_t i = 0; i < n; i++) {
		if (props[i].drmFormatModifier == modifier) {
			imp->mod_features = props[i].drmFormatModifierTilingFeatures;
			imp->mod_planes = props[i].drmFormatModifierPlaneCount;
		}
	}
	if (!imp->mod_planes) {
		ilog("modifier 0x%llx is not listed for G8_B8R8_2PLANE_420_UNORM", (unsigned long long)modifier);
		return false;
	}
	if (!(imp->mod_features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
		ilog("modifier 0x%llx cannot be sampled", (unsigned long long)modifier);
		imp->mod_planes = 0;
		return false;
	}
	return true;
}

static int ensure_conversion(mlxr_vkimp *imp, const mlxr_vframe *f)
{
	VkSamplerYcbcrModelConversion model;
	VkSamplerYcbcrRange range;
	colour_for_frame(imp, f, &model, &range);
	uint64_t key = ((uint64_t)model << 8) | (uint64_t)range | (1ull << 32);
	if (imp->conversion && key == imp->conv_key)
		return 0;
	if (imp->conversion) {
		// A different colour description with live imports would need new
		// views; only happens across a reset in practice.
		ilog("colour description changed with live imports; keeping the old conversion");
		return 0;
	}
	VkFormatFeatureFlags ff = imp->mod_features;
	bool linear = imp->cfg.linear_filter && (ff & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT);
	VkFilter filt = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	// H.264/HEVC 4:2:0 default chroma siting: co-sited horizontally ("left"),
	// midway vertically.
	VkChromaLocation xo = (ff & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT) ? VK_CHROMA_LOCATION_COSITED_EVEN
										   : VK_CHROMA_LOCATION_MIDPOINT;
	VkChromaLocation yo = (ff & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT) ? VK_CHROMA_LOCATION_MIDPOINT
										    : VK_CHROMA_LOCATION_COSITED_EVEN;
	VkSamplerYcbcrConversionCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
		.format = YUV_FORMAT,
		.ycbcrModel = model,
		.ycbcrRange = range,
		.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
		.xChromaOffset = xo,
		.yChromaOffset = yo,
		.chromaFilter = filt,
		.forceExplicitReconstruction = VK_FALSE,
	};
	VkResult r = vkCreateSamplerYcbcrConversion(imp->cfg.device, &ci, NULL, &imp->conversion);
	if (r != VK_SUCCESS) {
		ilog("vkCreateSamplerYcbcrConversion failed: %d", r);
		return -1;
	}
	VkSamplerYcbcrConversionInfo cinfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
					       .conversion = imp->conversion };
	VkSamplerCreateInfo si = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.pNext = &cinfo,
		.magFilter = filt,
		.minFilter = filt,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.maxLod = 0.0f,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
	};
	r = vkCreateSampler(imp->cfg.device, &si, NULL, &imp->sampler);
	if (r != VK_SUCCESS) {
		ilog("vkCreateSampler failed: %d", r);
		vkDestroySamplerYcbcrConversion(imp->cfg.device, imp->conversion, NULL);
		imp->conversion = VK_NULL_HANDLE;
		return -1;
	}
	imp->conv_key = key;
	imp->sampler_serial++;
	imp->st.conversions++;
	if (imp->cfg.verbose)
		ilog("YCbCr conversion: model %s, %s range, chroma %s/%s, %s filter",
		     model == VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601 ? "BT.601"
		     : model == VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_2020 ? "BT.2020" : "BT.709",
		     range == VK_SAMPLER_YCBCR_RANGE_ITU_FULL ? "full" : "limited",
		     xo == VK_CHROMA_LOCATION_COSITED_EVEN ? "cosited" : "midpoint",
		     yo == VK_CHROMA_LOCATION_MIDPOINT ? "midpoint" : "cosited",
		     linear ? "linear" : "nearest");
	return 0;
}

VkSampler mlxr_vkimp_sampler(mlxr_vkimp *imp, uint32_t *serial)
{
	if (serial)
		*serial = imp->sampler_serial;
	return imp->sampler;
}

VkSamplerYcbcrConversion mlxr_vkimp_conversion(mlxr_vkimp *imp)
{
	return imp->conversion;
}

// ----------------------------------------------------------------- import

static void destroy_entry(mlxr_vkimp *imp, cache_entry *e)
{
	VkDevice dev = imp->cfg.device;
	if (e->img.view)
		vkDestroyImageView(dev, e->img.view, NULL);
	if (e->img.image)
		vkDestroyImage(dev, e->img.image, NULL);
	if (e->img.memory)
		vkFreeMemory(dev, e->img.memory, NULL);
	memset(e, 0, sizeof(*e));
}

static int import_frame(mlxr_vkimp *imp, cache_entry *e, const mlxr_vframe *f)
{
	VkDevice dev = imp->cfg.device;
	if (f->drm_format != DRM_FORMAT_NV12 || f->num_planes != 2) {
		ilog("unsupported frame format 0x%08x with %u planes", f->drm_format, f->num_planes);
		return -1;
	}
	if (f->fd[0] != f->fd[1] || f->inode[0] != f->inode[1]) {
		// Two dma-bufs would need a disjoint image; Iris gives one.
		ilog("NV12 planes in separate dma-bufs are not supported");
		return -1;
	}
	if (!check_modifier(imp, f->drm_modifier))
		return -1;
	if (imp->mod_planes != 2) {
		ilog("modifier 0x%llx has %u memory planes, expected 2", (unsigned long long)f->drm_modifier,
		     imp->mod_planes);
		return -1;
	}
	if (ensure_conversion(imp, f))
		return -1;

	VkSubresourceLayout planes[2] = {
		{ .offset = f->offset[0], .rowPitch = f->pitch[0] },
		{ .offset = f->offset[1], .rowPitch = f->pitch[1] },
	};
	VkImageDrmFormatModifierExplicitCreateInfoEXT mod = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
		.drmFormatModifier = f->drm_modifier,
		.drmFormatModifierPlaneCount = 2,
		.pPlaneLayouts = planes,
	};
	VkExternalMemoryImageCreateInfo ext = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
						.pNext = &mod,
						.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
	VkImageCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.pNext = &ext,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = YUV_FORMAT,
		// The coded (allocated) size, so the plane layout matches; the
		// visible part is selected with uv_scale.
		.extent = { f->coded_width, f->coded_height, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
		.usage = VK_IMAGE_USAGE_SAMPLED_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};
	VkResult r = vkCreateImage(dev, &ici, NULL, &e->img.image);
	if (r != VK_SUCCESS) {
		ilog("vkCreateImage (dma-buf NV12 %ux%u) failed: %d", f->coded_width, f->coded_height, r);
		return -1;
	}
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(dev, e->img.image, &req);
	// Validate everything we can before the import: a failing import is the
	// kernel path that oopses on this kernel (SteamOS #2882).
	if (f->object_size && req.size > f->object_size) {
		ilog("image needs %llu bytes but the dma-buf has %llu", (unsigned long long)req.size,
		     (unsigned long long)f->object_size);
		return -1;
	}
	VkMemoryFdPropertiesKHR fdp = { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
	r = imp->get_fd_props(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, f->fd[0], &fdp);
	if (r != VK_SUCCESS) {
		ilog("vkGetMemoryFdPropertiesKHR failed: %d", r);
		return -1;
	}
	uint32_t bits = req.memoryTypeBits & fdp.memoryTypeBits;
	if (!bits) {
		ilog("no memory type for the dma-buf (image 0x%x, fd 0x%x)", req.memoryTypeBits, fdp.memoryTypeBits);
		return -1;
	}
	uint32_t type = (uint32_t)__builtin_ctz(bits);

	int fd = fcntl(f->fd[0], F_DUPFD_CLOEXEC, 0); // vkAllocateMemory consumes it
	if (fd < 0) {
		ilog("dup dma-buf fd failed: %s (errno %d)", strerror(errno), errno);
		return -1;
	}
	VkImportMemoryFdInfoKHR imf = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
					.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
					.fd = fd };
	VkMemoryDedicatedAllocateInfo ded = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
					      .pNext = &imf,
					      .image = e->img.image };
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				     .pNext = &ded,
				     .allocationSize = req.size,
				     .memoryTypeIndex = type };
	r = vkAllocateMemory(dev, &mai, NULL, &e->img.memory);
	if (r != VK_SUCCESS) {
		ilog("vkAllocateMemory (dma-buf import) failed: %d", r);
		close(fd); // not consumed on failure
		e->img.memory = VK_NULL_HANDLE;
		return -1;
	}
	r = vkBindImageMemory(dev, e->img.image, e->img.memory, 0);
	if (r != VK_SUCCESS) {
		ilog("vkBindImageMemory failed: %d", r);
		return -1;
	}
	VkSamplerYcbcrConversionInfo cinfo = { .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
					       .conversion = imp->conversion };
	VkImageViewCreateInfo vci = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.pNext = &cinfo,
		.image = e->img.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = YUV_FORMAT,
		.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
	};
	r = vkCreateImageView(dev, &vci, NULL, &e->img.view);
	if (r != VK_SUCCESS) {
		ilog("vkCreateImageView failed: %d", r);
		return -1;
	}
	e->img.index = f->index;
	e->img.generation = f->generation;
	e->img.width = f->width;
	e->img.height = f->height;
	e->img.coded_width = f->coded_width;
	e->img.coded_height = f->coded_height;
	e->img.uv_scale[0] = (float)f->width / (float)f->coded_width;
	e->img.uv_scale[1] = (float)f->height / (float)f->coded_height;
	e->img.first_use = true;
	e->inode = f->inode[0];
	e->used = true;
	imp->st.imports++;
	if (imp->cfg.verbose)
		ilog("imported buffer %u gen %u (inode %llu) %ux%u coded %ux%u, %llu bytes", f->index,
		     f->generation, (unsigned long long)f->inode[0], f->width, f->height, f->coded_width,
		     f->coded_height, (unsigned long long)req.size);
	return 0;
}

const mlxr_vkimp_image *mlxr_vkimp_get(mlxr_vkimp *imp, const mlxr_vframe *f)
{
	if (f->index >= MAX_IMAGES)
		return NULL;
	if (imp->generation && f->generation != imp->generation) {
		// The decoder reallocated CAPTURE without a reset here. Never
		// re-import over live imports.
		ilog("frame from CAPTURE generation %u while imports belong to %u: call mlxr_vkimp_reset first",
		     f->generation, imp->generation);
		return NULL;
	}
	cache_entry *e = &imp->cache[f->index];
	if (e->used) {
		if (e->inode != f->inode[0]) {
			ilog("buffer %u changed dma-buf (inode %llu -> %llu) without a reset", f->index,
			     (unsigned long long)e->inode, (unsigned long long)f->inode[0]);
			return NULL;
		}
		return &e->img;
	}
	if (e->failed)
		return NULL;
	if (import_frame(imp, e, f)) {
		destroy_entry(imp, e);
		e->failed = true;
		imp->st.import_failures++;
		return NULL;
	}
	imp->generation = f->generation;
	return &e->img;
}

// ---------------------------------------------------------------- barriers

void mlxr_vkimp_cmd_acquire(mlxr_vkimp *imp, VkCommandBuffer cmd, const mlxr_vkimp_image *img,
			    VkPipelineStageFlags dst_stage)
{
	cache_entry *e = img->index < MAX_IMAGES ? &imp->cache[img->index] : NULL;
	bool first = e ? e->img.first_use : true;
	VkImageMemoryBarrier b = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		// The memory layout is fixed by the modifier, so UNDEFINED on the
		// first acquire keeps the decoded pixels; later acquires match the
		// GENERAL layout of our release.
		.oldLayout = first ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
		.dstQueueFamilyIndex = imp->cfg.queue_family,
		.image = img->image,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, dst_stage, 0, 0, NULL, 0, NULL, 1, &b);
	if (e)
		e->img.first_use = false;
}

void mlxr_vkimp_cmd_release(mlxr_vkimp *imp, VkCommandBuffer cmd, const mlxr_vkimp_image *img,
			    VkPipelineStageFlags src_stage)
{
	VkImageMemoryBarrier b = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = 0, // reads only: nothing to make available
		.dstAccessMask = 0,
		.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcQueueFamilyIndex = imp->cfg.queue_family,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
		.image = img->image,
		.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
	};
	vkCmdPipelineBarrier(cmd, src_stage, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

// -------------------------------------------------------- frame retirement

static int add_pending(mlxr_vkimp *imp, const mlxr_vframe *f, VkSemaphore sem, uint64_t value, VkFence fence)
{
	for (int i = 0; i < MAX_PENDING; i++) {
		pending_entry *p = &imp->pending[i];
		if (!p->used) {
			*p = (pending_entry){ .used = true, .frame = *f, .sem = sem, .value = value, .fence = fence };
			imp->st.frames_pending++;
			return 0;
		}
	}
	ilog("too many frames pending GPU completion");
	return -1;
}

int mlxr_vkimp_frame_done_timeline(mlxr_vkimp *imp, const mlxr_vframe *f, VkSemaphore sem, uint64_t value)
{
	return add_pending(imp, f, sem, value, VK_NULL_HANDLE);
}

int mlxr_vkimp_frame_done_fence(mlxr_vkimp *imp, const mlxr_vframe *f, VkFence fence)
{
	return add_pending(imp, f, VK_NULL_HANDLE, 0, fence);
}

static bool pending_done(mlxr_vkimp *imp, const pending_entry *p)
{
	if (p->fence)
		return vkGetFenceStatus(imp->cfg.device, p->fence) == VK_SUCCESS;
	if (!p->sem)
		return true;
	uint64_t v = 0;
	if (vkGetSemaphoreCounterValue(imp->cfg.device, p->sem, &v) != VK_SUCCESS)
		return false;
	return v >= p->value;
}

int mlxr_vkimp_poll(mlxr_vkimp *imp)
{
	int n = 0;
	for (int i = 0; i < MAX_PENDING; i++) {
		pending_entry *p = &imp->pending[i];
		if (p->used && pending_done(imp, p)) {
			p->used = false;
			imp->st.frames_pending--;
			imp->st.frames_released++;
			imp->cfg.release(imp->cfg.release_user, &p->frame);
			n++;
		}
	}
	return n;
}

int mlxr_vkimp_drain(mlxr_vkimp *imp, uint64_t timeout_ns)
{
	int n = 0;
	for (int i = 0; i < MAX_PENDING; i++) {
		pending_entry *p = &imp->pending[i];
		if (!p->used)
			continue;
		if (p->fence) {
			vkWaitForFences(imp->cfg.device, 1, &p->fence, VK_TRUE, timeout_ns);
		} else if (p->sem) {
			VkSemaphoreWaitInfo wi = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
						   .semaphoreCount = 1,
						   .pSemaphores = &p->sem,
						   .pValues = &p->value };
			vkWaitSemaphores(imp->cfg.device, &wi, timeout_ns);
		}
		if (pending_done(imp, p)) {
			p->used = false;
			imp->st.frames_pending--;
			imp->st.frames_released++;
			imp->cfg.release(imp->cfg.release_user, &p->frame);
			n++;
		}
	}
	return n;
}

void mlxr_vkimp_reset(mlxr_vkimp *imp)
{
	for (int i = 0; i < MAX_PENDING; i++)
		if (imp->pending[i].used)
			ilog("reset with frame %u still pending GPU completion", imp->pending[i].frame.index);
	for (int i = 0; i < MAX_IMAGES; i++)
		destroy_entry(imp, &imp->cache[i]);
	if (imp->sampler)
		vkDestroySampler(imp->cfg.device, imp->sampler, NULL);
	if (imp->conversion)
		vkDestroySamplerYcbcrConversion(imp->cfg.device, imp->conversion, NULL);
	imp->sampler = VK_NULL_HANDLE;
	imp->conversion = VK_NULL_HANDLE;
	imp->conv_key = 0;
	imp->generation = 0;
}

void mlxr_vkimp_get_stats(mlxr_vkimp *imp, mlxr_vkimp_stats *s)
{
	*s = imp->st;
}

void mlxr_vkimp_destroy(mlxr_vkimp *imp)
{
	if (!imp)
		return;
	mlxr_vkimp_reset(imp);
	free(imp);
}
