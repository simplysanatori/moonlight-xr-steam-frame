// Moonlight XR Native - mlxr-decode-test: headless V4L2 decode + Vulkan
// import harness for lane V.
//
// Copyright (C) 2026 Moonlight XR Native contributors.
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License version 3 as published by
// the Free Software Foundation. It is distributed WITHOUT ANY WARRANTY; see
// LICENSE.txt for the full license text.
//
// Reads an Annex-B .h264/.hevc file, splits it into access units, feeds them
// to the Iris decoder at a target rate (or lockstep), imports each CAPTURE
// buffer into Vulkan once, converts every frame NV12 -> RGBA in a compute pass
// and prints decode latency, import counts and errors.
//
//   mlxr-decode-test FILE [options]
//     --codec h264|hevc      default from the file extension
//     --fps N                pacing (default 60); 0 = lockstep (submit, wait for
//                            the frame, submit the next: proves 1 AU in -> 1 out)
//     --loops N              play the file N times (default 1)
//     --duration S           loop the file for S seconds (overrides --loops)
//     --dump-every N --dump-dir D   write every Nth frame as D/frame_XXXXXX.ppm
//     --check-ref F.ppm --check-frame N  compare frame N with a reference picture
//     --expect-frames N      fail unless exactly N frames came out
//     --expect-source-changes N  fail unless N SOURCE_CHANGE events were handled
//     --max-p99-ms X         fail if submit->dequeue p99 exceeds X ms
//     --capture-buffers N    CAPTURE buffers to request (default 12)
//     --progress S           print running stats every S seconds (default 10)
//     --probe-modifiers      list Turnip's DRM modifiers for NV12 and exit
//     -v                     verbose decoder/import logs
// Exit status 0 = all checks passed.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <vulkan/vulkan.h>

#include "v4l2_decoder.h"
#include "vk_import.h"

static const uint32_t k_comp_spv[] =
#include "nv12_to_rgba.comp.inc"
	;

#define SLOTS 4

// --------------------------------------------------------------- helpers

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#define VKCHECK(x)                                                                         \
	do {                                                                               \
		VkResult r_ = (x);                                                         \
		if (r_ != VK_SUCCESS) {                                                    \
			fprintf(stderr, "%s:%d %s = %d\n", __FILE__, __LINE__, #x, r_);   \
			exit(3);                                                           \
		}                                                                          \
	} while (0)

// ------------------------------------------------------ Annex-B splitting

typedef struct {
	uint32_t off, size;
} au_t;

typedef struct {
	const uint8_t *data;
	size_t size;
	au_t *aus;
	uint32_t n_aus;
} stream_t;

static size_t find_start(const uint8_t *b, size_t n, size_t pos, size_t *sc_len)
{
	for (; pos + 3 <= n; pos++) {
		if (b[pos] == 0 && b[pos + 1] == 0) {
			if (b[pos + 2] == 1) {
				*sc_len = 3;
				return pos;
			}
			if (pos + 4 <= n && b[pos + 2] == 0 && b[pos + 3] == 1) {
				*sc_len = 4;
				return pos;
			}
		}
	}
	*sc_len = 0;
	return n;
}

// Splits at picture boundaries: an AU starts at a parameter set / SEI / AUD
// after a VCL NAL, or at a VCL NAL whose first-slice flag is set after a VCL.
static int split_aus(stream_t *s, bool hevc)
{
	size_t cap = 1024, sc;
	s->aus = malloc(cap * sizeof(au_t));
	s->n_aus = 0;
	size_t pos = find_start(s->data, s->size, 0, &sc);
	size_t au_start = pos;
	bool seen_vcl = false;
	while (pos < s->size) {
		size_t hdr = pos + sc;
		size_t next_sc;
		size_t next = find_start(s->data, s->size, hdr, &next_sc);
		if (hdr >= s->size)
			break;
		uint8_t h0 = s->data[hdr];
		bool vcl, first_slice = false, starts_au = false;
		if (hevc) {
			int type = (h0 >> 1) & 0x3f;
			vcl = type < 32;
			if (vcl && hdr + 2 < s->size)
				first_slice = (s->data[hdr + 2] & 0x80) != 0;
			starts_au = (type >= 32 && type <= 38) || type == 39 || (type >= 41 && type <= 44) ||
				    (type >= 48 && type <= 55);
		} else {
			int type = h0 & 0x1f;
			vcl = type >= 1 && type <= 5;
			if (vcl && hdr + 1 < s->size)
				first_slice = (s->data[hdr + 1] & 0x80) != 0; // first_mb_in_slice == 0
			starts_au = type == 6 || type == 7 || type == 8 || type == 9 || (type >= 14 && type <= 18);
		}
		if (seen_vcl && (starts_au || (vcl && first_slice))) {
			if (s->n_aus == cap) {
				cap *= 2;
				s->aus = realloc(s->aus, cap * sizeof(au_t));
			}
			s->aus[s->n_aus++] = (au_t){ (uint32_t)au_start, (uint32_t)(pos - au_start) };
			au_start = pos;
			seen_vcl = false;
		}
		if (vcl)
			seen_vcl = true;
		pos = next;
		sc = next_sc;
	}
	if (seen_vcl && au_start < s->size) {
		if (s->n_aus == cap)
			s->aus = realloc(s->aus, (cap + 1) * sizeof(au_t));
		s->aus[s->n_aus++] = (au_t){ (uint32_t)au_start, (uint32_t)(s->size - au_start) };
	}
	return s->n_aus ? 0 : -1;
}

// ------------------------------------------------------------ PPM helpers

static int write_ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t stride)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;
	fprintf(f, "P6\n%u %u\n255\n", w, h);
	uint8_t *row = malloc(w * 3);
	for (uint32_t y = 0; y < h; y++) {
		const uint8_t *s = rgba + (size_t)y * stride;
		for (uint32_t x = 0; x < w; x++) {
			row[x * 3] = s[x * 4];
			row[x * 3 + 1] = s[x * 4 + 1];
			row[x * 3 + 2] = s[x * 4 + 2];
		}
		fwrite(row, 1, w * 3, f);
	}
	free(row);
	fclose(f);
	return 0;
}

static uint8_t *read_ppm(const char *path, uint32_t *w, uint32_t *h)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	unsigned mx;
	if (fscanf(f, "P6 %u %u %u", w, h, &mx) != 3 || mx != 255) {
		fclose(f);
		return NULL;
	}
	fgetc(f);
	uint8_t *p = malloc((size_t)*w * *h * 3);
	if (fread(p, 1, (size_t)*w * *h * 3, f) != (size_t)*w * *h * 3) {
		free(p);
		p = NULL;
	}
	fclose(f);
	return p;
}

// Compare a decoded RGBA picture with a software-decoded reference on a
// 16x9 grid of 6x6 blocks. Catches green / swapped-chroma / wrong-stride
// output, tolerates YCbCr->RGB rounding and chroma filter differences.
static bool check_picture(const uint8_t *rgba, uint32_t stride, uint32_t w, uint32_t h, const char *ref_path)
{
	uint32_t rw, rh;
	uint8_t *ref = read_ppm(ref_path, &rw, &rh);
	if (!ref) {
		fprintf(stderr, "check: cannot read %s\n", ref_path);
		return false;
	}
	if (rw != w || rh != h) {
		fprintf(stderr, "check: size %ux%u differs from reference %ux%u\n", w, h, rw, rh);
		free(ref);
		return false;
	}
	int bad = 0, n = 0;
	double sum = 0;
	for (int gy = 0; gy < 9; gy++) {
		for (int gx = 0; gx < 16; gx++) {
			uint32_t cx = (uint32_t)((gx + 0.5) * w / 16), cy = (uint32_t)((gy + 0.5) * h / 9);
			double a[3] = { 0 }, b[3] = { 0 };
			for (uint32_t y = cy - 3; y < cy + 3; y++)
				for (uint32_t x = cx - 3; x < cx + 3; x++)
					for (int c = 0; c < 3; c++) {
						a[c] += rgba[(size_t)y * stride + x * 4 + c];
						b[c] += ref[((size_t)y * w + x) * 3 + c];
					}
			double worst = 0;
			for (int c = 0; c < 3; c++) {
				double d = fabs(a[c] - b[c]) / 36.0;
				sum += d;
				if (d > worst)
					worst = d;
			}
			if (worst > 24)
				bad++;
			n++;
		}
	}
	double mean = sum / (n * 3);
	bool ok = bad * 10 <= n && mean <= 8.0;
	// Print a few sample points, decoded vs reference.
	printf("colour check vs %s: %d/%d grid points off by >24, mean abs diff %.2f -> %s\n", ref_path, bad,
	       n, mean, ok ? "PASS" : "FAIL");
	for (int i = 0; i < 4; i++) {
		uint32_t x = w * (2 * i + 1) / 8, y = h / 6;
		const uint8_t *p = rgba + (size_t)y * stride + x * 4;
		const uint8_t *q = ref + ((size_t)y * w + x) * 3;
		printf("  pixel (%u,%u): decoded %3u %3u %3u  reference %3u %3u %3u\n", x, y, p[0], p[1], p[2], q[0],
		       q[1], q[2]);
	}
	free(ref);
	return ok;
}

// ------------------------------------------------------------ statistics

typedef struct {
	double *v;
	size_t n, cap;
} samples_t;

static void push(samples_t *s, double x)
{
	if (s->n == s->cap) {
		s->cap = s->cap ? s->cap * 2 : 4096;
		s->v = realloc(s->v, s->cap * sizeof(double));
	}
	s->v[s->n++] = x;
}

static int cmp_d(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static double pct(double *sorted, size_t n, double p)
{
	if (!n)
		return 0;
	size_t i = (size_t)(p / 100.0 * (double)(n - 1) + 0.5);
	return sorted[i < n ? i : n - 1];
}

static void print_lat(const char *label, samples_t *s, size_t from, double *p99_out)
{
	size_t n = s->n - from;
	if (!n) {
		printf("%s: no samples\n", label);
		return;
	}
	double *t = malloc(n * sizeof(double));
	memcpy(t, s->v + from, n * sizeof(double));
	qsort(t, n, sizeof(double), cmp_d);
	double sum = 0;
	for (size_t i = 0; i < n; i++)
		sum += t[i];
	printf("%s: n %zu  mean %.2f  p50 %.2f  p95 %.2f  p99 %.2f  max %.2f ms\n", label, n, sum / n,
	       pct(t, n, 50), pct(t, n, 95), pct(t, n, 99), t[n - 1]);
	if (p99_out)
		*p99_out = pct(t, n, 99);
	free(t);
}

// ------------------------------------------------------------- Vulkan bits

typedef struct {
	VkInstance inst;
	VkPhysicalDevice phys;
	VkDevice dev;
	uint32_t qf;
	VkQueue queue;
	VkCommandPool pool;
	VkSemaphore timeline;
	uint64_t timeline_value;
	VkPhysicalDeviceMemoryProperties memprops;
	// compute pipeline (rebuilt when the immutable sampler changes)
	uint32_t sampler_serial;
	VkDescriptorSetLayout dsl;
	VkPipelineLayout pl;
	VkPipeline pipe;
	VkDescriptorPool dpool;
	VkShaderModule shader;
} vk_t;

typedef struct {
	VkCommandBuffer cmd;
	VkDescriptorSet set;
	VkImage out;
	VkDeviceMemory out_mem;
	VkImageView out_view;
	uint32_t out_w, out_h;
	VkBuffer rb;
	VkDeviceMemory rb_mem;
	void *rb_map;
	uint64_t value;      // timeline value of the last submission
	bool readback;       // this submission copies to rb
	int64_t pts;
	uint32_t w, h;
} slot_t;

static uint32_t find_mem(vk_t *vk, uint32_t bits, VkMemoryPropertyFlags want)
{
	for (uint32_t i = 0; i < vk->memprops.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (vk->memprops.memoryTypes[i].propertyFlags & want) == want)
			return i;
	fprintf(stderr, "no memory type for bits 0x%x flags 0x%x\n", bits, want);
	exit(3);
}

static void vk_init(vk_t *vk, bool probe_only)
{
	VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
				  .pApplicationName = "mlxr-decode-test",
				  .apiVersion = VK_API_VERSION_1_2 };
	VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
	VKCHECK(vkCreateInstance(&ici, NULL, &vk->inst));
	uint32_t n = 0;
	vkEnumeratePhysicalDevices(vk->inst, &n, NULL);
	VkPhysicalDevice devs[8];
	if (n > 8)
		n = 8;
	vkEnumeratePhysicalDevices(vk->inst, &n, devs);
	vk->phys = VK_NULL_HANDLE;
	for (uint32_t i = 0; i < n; i++) {
		VkPhysicalDeviceProperties p;
		vkGetPhysicalDeviceProperties(devs[i], &p);
		if (p.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU) {
			vk->phys = devs[i];
			printf("Vulkan device: %s (API %u.%u.%u)\n", p.deviceName, VK_VERSION_MAJOR(p.apiVersion),
			       VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion));
			break;
		}
	}
	if (!vk->phys) {
		fprintf(stderr, "no GPU\n");
		exit(3);
	}
	if (probe_only)
		return;
	if (!mlxr_vkimp_check_physical_device(vk->phys))
		exit(3);
	vkGetPhysicalDeviceMemoryProperties(vk->phys, &vk->memprops);
	uint32_t nq = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nq, NULL);
	VkQueueFamilyProperties qp[16];
	if (nq > 16)
		nq = 16;
	vkGetPhysicalDeviceQueueFamilyProperties(vk->phys, &nq, qp);
	vk->qf = UINT32_MAX;
	for (uint32_t i = 0; i < nq; i++)
		if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
			vk->qf = i;
			break;
		}
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
					.queueFamilyIndex = vk->qf,
					.queueCount = 1,
					.pQueuePriorities = &prio };
	uint32_t next;
	const char *const *exts = mlxr_vkimp_device_extensions(&next);
	VkPhysicalDeviceVulkan12Features f12 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
						 .timelineSemaphore = VK_TRUE };
	VkPhysicalDeviceVulkan11Features f11 = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
						 .pNext = &f12,
						 .samplerYcbcrConversion = VK_TRUE };
	VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
				   .pNext = &f11,
				   .queueCreateInfoCount = 1,
				   .pQueueCreateInfos = &qci,
				   .enabledExtensionCount = next,
				   .ppEnabledExtensionNames = exts };
	VKCHECK(vkCreateDevice(vk->phys, &dci, NULL, &vk->dev));
	vkGetDeviceQueue(vk->dev, vk->qf, 0, &vk->queue);
	VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
					.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
					.queueFamilyIndex = vk->qf };
	VKCHECK(vkCreateCommandPool(vk->dev, &cpi, NULL, &vk->pool));
	VkSemaphoreTypeCreateInfo sti = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
					  .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE };
	VkSemaphoreCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &sti };
	VKCHECK(vkCreateSemaphore(vk->dev, &sci, NULL, &vk->timeline));
	VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
					 .codeSize = sizeof(k_comp_spv),
					 .pCode = k_comp_spv };
	VKCHECK(vkCreateShaderModule(vk->dev, &smi, NULL, &vk->shader));
}

static void destroy_pipeline(vk_t *vk)
{
	if (vk->pipe)
		vkDestroyPipeline(vk->dev, vk->pipe, NULL);
	if (vk->pl)
		vkDestroyPipelineLayout(vk->dev, vk->pl, NULL);
	if (vk->dsl)
		vkDestroyDescriptorSetLayout(vk->dev, vk->dsl, NULL);
	if (vk->dpool)
		vkDestroyDescriptorPool(vk->dev, vk->dpool, NULL);
	vk->pipe = VK_NULL_HANDLE;
	vk->pl = VK_NULL_HANDLE;
	vk->dsl = VK_NULL_HANDLE;
	vk->dpool = VK_NULL_HANDLE;
	vk->sampler_serial = 0;
}

static void build_pipeline(vk_t *vk, VkSampler sampler, uint32_t serial, slot_t *slots)
{
	destroy_pipeline(vk);
	VkDescriptorSetLayoutBinding b[2] = {
		{ .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1,
		  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .pImmutableSamplers = &sampler },
		{ .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1,
		  .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
	};
	VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
						.bindingCount = 2,
						.pBindings = b };
	VKCHECK(vkCreateDescriptorSetLayout(vk->dev, &dli, NULL, &vk->dsl));
	VkPushConstantRange pcr = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 16 };
	VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
					   .setLayoutCount = 1,
					   .pSetLayouts = &vk->dsl,
					   .pushConstantRangeCount = 1,
					   .pPushConstantRanges = &pcr };
	VKCHECK(vkCreatePipelineLayout(vk->dev, &pli, NULL, &vk->pl));
	VkComputePipelineCreateInfo cpi = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			   .stage = VK_SHADER_STAGE_COMPUTE_BIT,
			   .module = vk->shader,
			   .pName = "main" },
		.layout = vk->pl,
	};
	VKCHECK(vkCreateComputePipelines(vk->dev, VK_NULL_HANDLE, 1, &cpi, NULL, &vk->pipe));
	// YCbCr samplers may take several descriptors each: be generous.
	VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SLOTS * 4 },
				       { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SLOTS } };
	VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
					   .maxSets = SLOTS,
					   .poolSizeCount = 2,
					   .pPoolSizes = ps };
	VKCHECK(vkCreateDescriptorPool(vk->dev, &dpi, NULL, &vk->dpool));
	for (int i = 0; i < SLOTS; i++) {
		VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
						    .descriptorPool = vk->dpool,
						    .descriptorSetCount = 1,
						    .pSetLayouts = &vk->dsl };
		VKCHECK(vkAllocateDescriptorSets(vk->dev, &dai, &slots[i].set));
	}
	vk->sampler_serial = serial;
}

static void slot_free_images(vk_t *vk, slot_t *s)
{
	if (s->out_view)
		vkDestroyImageView(vk->dev, s->out_view, NULL);
	if (s->out)
		vkDestroyImage(vk->dev, s->out, NULL);
	if (s->out_mem)
		vkFreeMemory(vk->dev, s->out_mem, NULL);
	if (s->rb)
		vkDestroyBuffer(vk->dev, s->rb, NULL);
	if (s->rb_mem)
		vkFreeMemory(vk->dev, s->rb_mem, NULL);
	s->out_view = VK_NULL_HANDLE;
	s->out = VK_NULL_HANDLE;
	s->out_mem = VK_NULL_HANDLE;
	s->rb = VK_NULL_HANDLE;
	s->rb_mem = VK_NULL_HANDLE;
	s->rb_map = NULL;
	s->out_w = s->out_h = 0;
}

static void slot_ensure_images(vk_t *vk, slot_t *s, uint32_t w, uint32_t h)
{
	if (s->out && s->out_w == w && s->out_h == h)
		return;
	slot_free_images(vk, s);
	VkImageCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
				  .imageType = VK_IMAGE_TYPE_2D,
				  .format = VK_FORMAT_R8G8B8A8_UNORM,
				  .extent = { w, h, 1 },
				  .mipLevels = 1,
				  .arrayLayers = 1,
				  .samples = VK_SAMPLE_COUNT_1_BIT,
				  .tiling = VK_IMAGE_TILING_OPTIMAL,
				  .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT };
	VKCHECK(vkCreateImage(vk->dev, &ici, NULL, &s->out));
	VkMemoryRequirements req;
	vkGetImageMemoryRequirements(vk->dev, s->out, &req);
	VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				     .allocationSize = req.size,
				     .memoryTypeIndex = find_mem(vk, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
	VKCHECK(vkAllocateMemory(vk->dev, &mai, NULL, &s->out_mem));
	VKCHECK(vkBindImageMemory(vk->dev, s->out, s->out_mem, 0));
	VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
				      .image = s->out,
				      .viewType = VK_IMAGE_VIEW_TYPE_2D,
				      .format = VK_FORMAT_R8G8B8A8_UNORM,
				      .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
	VKCHECK(vkCreateImageView(vk->dev, &vci, NULL, &s->out_view));
	VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
				   .size = (VkDeviceSize)w * h * 4,
				   .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT };
	VKCHECK(vkCreateBuffer(vk->dev, &bci, NULL, &s->rb));
	vkGetBufferMemoryRequirements(vk->dev, s->rb, &req);
	mai.allocationSize = req.size;
	mai.memoryTypeIndex = find_mem(vk, req.memoryTypeBits,
				       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	VKCHECK(vkAllocateMemory(vk->dev, &mai, NULL, &s->rb_mem));
	VKCHECK(vkBindBufferMemory(vk->dev, s->rb, s->rb_mem, 0));
	VKCHECK(vkMapMemory(vk->dev, s->rb_mem, 0, VK_WHOLE_SIZE, 0, &s->rb_map));
	s->out_w = w;
	s->out_h = h;
}

static void wait_value(vk_t *vk, uint64_t v)
{
	if (!v)
		return;
	VkSemaphoreWaitInfo wi = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
				   .semaphoreCount = 1,
				   .pSemaphores = &vk->timeline,
				   .pValues = &v };
	VKCHECK(vkWaitSemaphores(vk->dev, &wi, 5000000000ull));
}

// ------------------------------------------------------------ the test

typedef struct {
	const char *path;
	mlxr_codec codec;
	double fps;
	int loops;
	double duration;
	int dump_every;
	const char *dump_dir;
	const char *check_ref;
	long check_frame;
	long expect_frames;
	long expect_source_changes;
	double max_p99;
	uint32_t capture_buffers;
	double progress;
	bool verbose;
	bool probe;
} opts_t;

typedef struct {
	opts_t *o;
	stream_t *s;
	mlxr_vdec *dec;
	atomic_bool done;
	atomic_bool stop;
	atomic_ullong submitted;
	atomic_ullong submit_errors;
} feeder_t;

static void *feeder_main(void *arg)
{
	feeder_t *f = arg;
	opts_t *o = f->o;
	uint64_t period = (uint64_t)(1e9 / o->fps);
	uint64_t t0 = now_ns(), end = o->duration > 0 ? t0 + (uint64_t)(o->duration * 1e9) : UINT64_MAX;
	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);
	int64_t pts = 0;
	for (int loop = 0; !atomic_load(&f->stop); loop++) {
		if (o->duration <= 0 && loop >= o->loops)
			break;
		for (uint32_t i = 0; i < f->s->n_aus && !atomic_load(&f->stop); i++) {
			if (now_ns() >= end)
				goto out;
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
			const au_t *a = &f->s->aus[i];
			int r = mlxr_vdec_submit(f->dec, f->s->data + a->off, a->size, pts++);
			if (r < 0) {
				// A real session would call LiRequestIdrFrame() here.
				fprintf(stderr, "submit AU %u: %s\n", i, mlxr_vdec_status_str(r));
				atomic_fetch_add(&f->submit_errors, 1);
			} else {
				atomic_fetch_add(&f->submitted, 1);
			}
			next.tv_nsec += (long)period;
			while (next.tv_nsec >= 1000000000L) {
				next.tv_nsec -= 1000000000L;
				next.tv_sec++;
			}
		}
	}
out:
	atomic_store(&f->done, true);
	return NULL;
}

typedef struct {
	mlxr_vdec *dec;
	uint64_t released;
} release_ctx;

static void release_cb(void *user, const mlxr_vframe *frame)
{
	release_ctx *rc = user;
	mlxr_vdec_release(rc->dec, frame);
	rc->released++;
}

static void usage(void)
{
	fprintf(stderr, "usage: mlxr-decode-test FILE.h264|FILE.hevc [--codec h264|hevc] [--fps N] [--loops N]\n"
			"  [--duration S] [--dump-every N --dump-dir D] [--check-ref F.ppm --check-frame N]\n"
			"  [--expect-frames N] [--expect-source-changes N] [--max-p99-ms X]\n"
			"  [--capture-buffers N] [--progress S] [--probe-modifiers] [-v]\n");
	exit(2);
}

int main(int argc, char **argv)
{
	opts_t o = { .fps = 60, .loops = 1, .check_frame = -1, .expect_frames = -1, .expect_source_changes = -1,
		     .capture_buffers = 12, .progress = 10, .codec = (mlxr_codec)-1 };
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
#define ARG() (i + 1 < argc ? argv[++i] : (usage(), ""))
		if (!strcmp(a, "--codec")) {
			const char *c = ARG();
			o.codec = !strcmp(c, "h264") ? MLXR_CODEC_H264 : MLXR_CODEC_HEVC;
		} else if (!strcmp(a, "--fps")) o.fps = atof(ARG());
		else if (!strcmp(a, "--loops")) o.loops = atoi(ARG());
		else if (!strcmp(a, "--duration")) o.duration = atof(ARG());
		else if (!strcmp(a, "--dump-every")) o.dump_every = atoi(ARG());
		else if (!strcmp(a, "--dump-dir")) o.dump_dir = ARG();
		else if (!strcmp(a, "--check-ref")) o.check_ref = ARG();
		else if (!strcmp(a, "--check-frame")) o.check_frame = atol(ARG());
		else if (!strcmp(a, "--expect-frames")) o.expect_frames = atol(ARG());
		else if (!strcmp(a, "--expect-source-changes")) o.expect_source_changes = atol(ARG());
		else if (!strcmp(a, "--max-p99-ms")) o.max_p99 = atof(ARG());
		else if (!strcmp(a, "--capture-buffers")) o.capture_buffers = (uint32_t)atoi(ARG());
		else if (!strcmp(a, "--progress")) o.progress = atof(ARG());
		else if (!strcmp(a, "--probe-modifiers")) o.probe = true;
		else if (!strcmp(a, "-v")) o.verbose = true;
		else if (a[0] == '-') usage();
		else o.path = a;
	}
	vk_t vk = { 0 };
	if (o.probe) {
		vk_init(&vk, true);
		mlxr_vkimp_print_modifiers(vk.phys, stdout);
		vkDestroyInstance(vk.inst, NULL);
		return 0;
	}
	if (!o.path)
		usage();
	if ((int)o.codec < 0) {
		const char *ext = strrchr(o.path, '.');
		o.codec = ext && (!strcmp(ext, ".h264") || !strcmp(ext, ".264")) ? MLXR_CODEC_H264 : MLXR_CODEC_HEVC;
	}

	// Map the stream and split it into access units.
	int sfd = open(o.path, O_RDONLY | O_CLOEXEC);
	struct stat sb;
	if (sfd < 0 || fstat(sfd, &sb) < 0) {
		fprintf(stderr, "cannot open %s: %s\n", o.path, strerror(errno));
		return 2;
	}
	stream_t s = { .size = (size_t)sb.st_size };
	s.data = mmap(NULL, s.size, PROT_READ, MAP_PRIVATE, sfd, 0);
	close(sfd);
	if (s.data == MAP_FAILED || split_aus(&s, o.codec == MLXR_CODEC_HEVC)) {
		fprintf(stderr, "%s: no access units\n", o.path);
		return 2;
	}
	uint32_t max_au = 0;
	for (uint32_t i = 0; i < s.n_aus; i++)
		if (s.aus[i].size > max_au)
			max_au = s.aus[i].size;
	printf("%s: %s, %u access units, %.1f MB, largest AU %u bytes; %s\n", o.path,
	       o.codec == MLXR_CODEC_H264 ? "H.264" : "HEVC", s.n_aus, s.size / 1e6, max_au,
	       o.fps > 0 ? "paced" : "lockstep");

	vk_init(&vk, false);

	mlxr_vdec_config dc;
	mlxr_vdec_config_defaults(&dc);
	dc.codec = o.codec;
	dc.capture_buffers = o.capture_buffers;
	dc.verbose = o.verbose;
	mlxr_vdec *dec;
	int r = mlxr_vdec_open(&dc, &dec);
	if (r) {
		fprintf(stderr, "mlxr_vdec_open: %s\n", mlxr_vdec_status_str(r));
		return 3;
	}
	release_ctx rc = { .dec = dec };
	mlxr_vkimp_config ic = { .phys = vk.phys,
				 .device = vk.dev,
				 .queue_family = vk.qf,
				 .release = release_cb,
				 .release_user = &rc,
				 .linear_filter = true,
				 .verbose = o.verbose };
	mlxr_vkimp *imp;
	if (mlxr_vkimp_create(&ic, &imp)) {
		fprintf(stderr, "mlxr_vkimp_create failed\n");
		return 3;
	}

	slot_t slots[SLOTS] = { 0 };
	for (int i = 0; i < SLOTS; i++) {
		VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
						    .commandPool = vk.pool,
						    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
						    .commandBufferCount = 1 };
		VKCHECK(vkAllocateCommandBuffers(vk.dev, &cai, &slots[i].cmd));
	}

	feeder_t fd = { .o = &o, .s = &s, .dec = dec };
	pthread_t feeder;
	bool lockstep = o.fps <= 0;
	if (!lockstep)
		pthread_create(&feeder, NULL, feeder_main, &fd);

	samples_t lat = { 0 };
	uint64_t frames = 0, decode_errors = 0, format_changes = 0, import_null = 0, lockstep_stalls = 0;
	uint64_t t_start = now_ns(), t_last_progress = t_start, t_last_frame = t_start;
	size_t progress_from = 0;
	uint32_t slot_i = 0;
	bool have_held = false, check_done = false, check_ok = true;
	mlxr_vframe held;           // the frame "on screen" (kept until replaced)
	uint64_t held_value = 0;
	uint32_t lock_i = 0;
	int64_t lock_pts = 0;
	uint64_t total_target = lockstep ? (uint64_t)s.n_aus * (uint64_t)(o.loops > 0 ? o.loops : 1) : 0;

	for (;;) {
		if (lockstep) {
			if (fd.submitted >= total_target && frames + decode_errors >= fd.submitted)
				break;
			if (frames + decode_errors >= fd.submitted && fd.submitted < total_target) {
				const au_t *a = &s.aus[lock_i];
				int sr = mlxr_vdec_submit(dec, s.data + a->off, a->size, lock_pts++);
				if (sr < 0) {
					fprintf(stderr, "submit: %s\n", mlxr_vdec_status_str(sr));
					fd.submit_errors++;
				} else {
					fd.submitted++;
				}
				lock_i = (lock_i + 1) % s.n_aus;
			}
		} else if (atomic_load(&fd.done)) {
			mlxr_vdec_stats ds;
			mlxr_vdec_get_stats(dec, &ds);
			if (frames + decode_errors >= atomic_load(&fd.submitted))
				break;
			if (now_ns() - t_last_frame > 2000000000ull) {
				fprintf(stderr, "no frame for 2 s after the last submit; %llu of %llu out\n",
					(unsigned long long)frames, (unsigned long long)atomic_load(&fd.submitted));
				break;
			}
		}

		mlxr_vframe f;
		r = mlxr_vdec_receive(dec, &f, lockstep ? 200 : 20);
		mlxr_vkimp_poll(imp);
		if (r == MLXR_VDEC_AGAIN) {
			if (lockstep && frames + decode_errors < fd.submitted) {
				lockstep_stalls++;
				fprintf(stderr, "lockstep: AU %llu gave no frame within 200 ms\n",
					(unsigned long long)fd.submitted - 1);
				if (lockstep_stalls > 3)
					break;
			}
			if (now_ns() - t_last_frame > 5000000000ull) {
				fprintf(stderr, "decoder stalled for 5 s\n");
				break;
			}
			continue;
		}
		if (r == MLXR_VDEC_FORMAT_CHANGE) {
			// Full stop of the import side: GPU idle, frames back, imports gone.
			format_changes++;
			vkQueueWaitIdle(vk.queue);
			if (have_held) {
				mlxr_vkimp_frame_done_timeline(imp, &held, vk.timeline, held_value);
				have_held = false;
			}
			mlxr_vkimp_drain(imp, 1000000000ull);
			mlxr_vkimp_reset(imp);
			destroy_pipeline(&vk);
			r = mlxr_vdec_apply_format_change(dec);
			if (r) {
				fprintf(stderr, "apply_format_change: %s\n", mlxr_vdec_status_str(r));
				break;
			}
			continue;
		}
		if (r < 0) {
			decode_errors++;
			fprintf(stderr, "receive: %s (a session would request an IDR)\n", mlxr_vdec_status_str(r));
			if (r == MLXR_VDEC_E_FATAL || r == MLXR_VDEC_E_IO)
				break;
			continue;
		}
		frames++;
		t_last_frame = f.t_dequeue_ns;
		if (f.t_submit_ns)
			push(&lat, (double)(f.t_dequeue_ns - f.t_submit_ns) / 1e6);

		if (f.generation != mlxr_vkimp_generation(imp)) {
			// New CAPTURE set: import every buffer once, now.
			mlxr_vframe bufs[32];
			uint32_t nb = 0;
			mlxr_vdec_get_buffers(dec, bufs, 32, &nb);
			if (mlxr_vkimp_import_all(imp, bufs, nb))
				fprintf(stderr, "import_all: failures\n");
		}
		const mlxr_vkimp_image *img = mlxr_vkimp_get(imp, &f);
		if (!img) {
			import_null++;
			mlxr_vdec_release(dec, &f);
			continue;
		}
		uint32_t serial;
		VkSampler smp = mlxr_vkimp_sampler(imp, &serial);
		slot_t *sl = &slots[slot_i];
		slot_i = (slot_i + 1) % SLOTS;
		wait_value(&vk, sl->value); // slot free again
		if (serial != vk.sampler_serial) {
			vkQueueWaitIdle(vk.queue);
			build_pipeline(&vk, smp, serial, slots);
		}
		slot_ensure_images(&vk, sl, f.width, f.height);
		long fidx = f.pts >= 0 ? (long)(f.pts % s.n_aus) : -1;
		bool want_check = o.check_ref && !check_done && fidx == o.check_frame;
		bool want_dump = o.dump_every > 0 && o.dump_dir && (frames - 1) % (uint64_t)o.dump_every == 0;
		sl->readback = want_check || want_dump;
		sl->pts = f.pts;
		sl->w = f.width;
		sl->h = f.height;

		VkDescriptorImageInfo src = { .imageView = img->view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		VkDescriptorImageInfo dst = { .imageView = sl->out_view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL };
		VkWriteDescriptorSet w[2] = {
			{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sl->set, .dstBinding = 0,
			  .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			  .pImageInfo = &src },
			{ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sl->set, .dstBinding = 1,
			  .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &dst },
		};
		vkUpdateDescriptorSets(vk.dev, 2, w, 0, NULL);

		VkCommandBuffer cmd = sl->cmd;
		VKCHECK(vkResetCommandBuffer(cmd, 0));
		VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
						.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };
		VKCHECK(vkBeginCommandBuffer(cmd, &bi));
		mlxr_vkimp_cmd_acquire(imp, cmd, img, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		VkImageMemoryBarrier ob = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
					    .srcAccessMask = 0,
					    .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
					    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
					    .newLayout = VK_IMAGE_LAYOUT_GENERAL,
					    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
					    .image = sl->out,
					    .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
				     NULL, 0, NULL, 1, &ob);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pipe);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, vk.pl, 0, 1, &sl->set, 0, NULL);
		struct {
			float uv[2];
			int32_t size[2];
		} pc = { { img->uv_scale[0], img->uv_scale[1] }, { (int32_t)f.width, (int32_t)f.height } };
		vkCmdPushConstants(cmd, vk.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
		vkCmdDispatch(cmd, (f.width + 15) / 16, (f.height + 15) / 16, 1);
		mlxr_vkimp_cmd_release(imp, cmd, img, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
		if (sl->readback) {
			ob.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			ob.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			ob.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			ob.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
					     0, NULL, 0, NULL, 1, &ob);
			VkBufferImageCopy cp = { .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
						 .imageExtent = { f.width, f.height, 1 } };
			vkCmdCopyImageToBuffer(cmd, sl->out, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sl->rb, 1, &cp);
		}
		VKCHECK(vkEndCommandBuffer(cmd));
		uint64_t value = ++vk.timeline_value;
		VkTimelineSemaphoreSubmitInfo tsi = { .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
						      .signalSemaphoreValueCount = 1,
						      .pSignalSemaphoreValues = &value };
		VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
				    .pNext = &tsi,
				    .commandBufferCount = 1,
				    .pCommandBuffers = &cmd,
				    .signalSemaphoreCount = 1,
				    .pSignalSemaphores = &vk.timeline };
		VKCHECK(vkQueueSubmit(vk.queue, 1, &si, VK_NULL_HANDLE));
		sl->value = value;

		// The frame loop keeps the newest frame on screen; the previous one
		// goes back to the decoder once the GPU is done with it.
		if (have_held)
			mlxr_vkimp_frame_done_timeline(imp, &held, vk.timeline, held_value);
		held = f;
		held_value = value;
		have_held = true;

		if (sl->readback) {
			wait_value(&vk, value);
			if (want_check) {
				check_done = true;
				check_ok = check_picture(sl->rb_map, f.width * 4, f.width, f.height, o.check_ref);
			}
			if (want_dump) {
				char path[512];
				snprintf(path, sizeof(path), "%s/frame_%06llu.ppm", o.dump_dir,
					 (unsigned long long)(frames - 1));
				write_ppm(path, sl->rb_map, f.width, f.height, f.width * 4);
			}
		}

		uint64_t tn = now_ns();
		if (o.progress > 0 && tn - t_last_progress >= (uint64_t)(o.progress * 1e9)) {
			char label[96];
			mlxr_vkimp_stats is;
			mlxr_vkimp_get_stats(imp, &is);
			snprintf(label, sizeof(label), "[%6.0f s] frames %llu imports %llu window latency",
				 (tn - t_start) / 1e9, (unsigned long long)frames, (unsigned long long)is.imports);
			print_lat(label, &lat, progress_from, NULL);
			fflush(stdout);
			progress_from = lat.n;
			t_last_progress = tn;
		}
	}
	atomic_store(&fd.stop, true);
	if (!lockstep)
		pthread_join(feeder, NULL);
	double secs = (now_ns() - t_start) / 1e9;

	vkQueueWaitIdle(vk.queue);
	if (have_held)
		mlxr_vkimp_frame_done_timeline(imp, &held, vk.timeline, held_value);
	mlxr_vkimp_drain(imp, 1000000000ull);

	mlxr_vdec_stats ds;
	mlxr_vdec_get_stats(dec, &ds);
	mlxr_vkimp_stats is;
	mlxr_vkimp_get_stats(imp, &is);

	printf("---- results (%.1f s)\n", secs);
	printf("frames out %llu of %llu AUs submitted (%llu submit errors), decode errors %llu\n",
	       (unsigned long long)frames, (unsigned long long)atomic_load(&fd.submitted),
	       (unsigned long long)atomic_load(&fd.submit_errors), (unsigned long long)decode_errors);
	double p99 = 0;
	if (lat.n)
		printf("first frame latency (includes SOURCE_CHANGE + CAPTURE setup): %.2f ms\n", lat.v[0]);
	print_lat("decode latency submit->dequeue, all frames", &lat, 0, NULL);
	// Steady state: skip the start-up backlog (AUs queued before CAPTURE existed).
	print_lat("decode latency submit->dequeue, steady (frame 30+)", &lat, lat.n > 30 ? 30 : lat.n, &p99);
	printf("AUs in flight max %u%s\n", ds.in_flight_max,
	       lockstep ? (lockstep_stalls ? " (lockstep STALLED)" : " (lockstep: 1 AU in -> 1 frame out)") : "");
	printf("CAPTURE allocations %llu, CAPTURE buffers %llu, EXPBUF %llu, Vulkan imports %llu, import failures %llu\n",
	       (unsigned long long)ds.capture_allocs, (unsigned long long)ds.capture_buffers,
	       (unsigned long long)ds.expbufs, (unsigned long long)is.imports, (unsigned long long)is.import_failures);
	printf("frames released to decoder %llu (callback %llu), requeue errors %llu, unimported frames %llu\n",
	       (unsigned long long)is.frames_released, (unsigned long long)rc.released,
	       (unsigned long long)ds.requeue_errors, (unsigned long long)import_null);
	printf("source-change events %llu, format changes applied %llu, YCbCr conversions %llu\n",
	       (unsigned long long)ds.source_changes, (unsigned long long)format_changes,
	       (unsigned long long)is.conversions);

	int fail = 0;
#define FAIL(...) do { printf("FAIL: " __VA_ARGS__); printf("\n"); fail = 1; } while (0)
	if (is.imports != ds.capture_buffers)
		FAIL("import count %llu != %llu CAPTURE buffers (churn or missing imports)", (unsigned long long)is.imports,
		     (unsigned long long)ds.capture_buffers);
	if (is.import_failures || import_null)
		FAIL("import failures");
	if (ds.requeue_errors)
		FAIL("requeue errors");
	if (o.expect_frames >= 0 && frames != (uint64_t)o.expect_frames)
		FAIL("expected %ld frames, got %llu", o.expect_frames, (unsigned long long)frames);
	if (o.expect_source_changes >= 0 && ds.source_changes != (uint64_t)o.expect_source_changes)
		FAIL("expected %ld source changes, got %llu", o.expect_source_changes,
		     (unsigned long long)ds.source_changes);
	if (o.check_ref && (!check_done || !check_ok))
		FAIL("colour check %s", check_done ? "failed" : "frame never decoded");
	if (o.max_p99 > 0 && p99 > o.max_p99)
		FAIL("p99 latency %.2f ms > %.2f ms", p99, o.max_p99);
	if (lockstep && lockstep_stalls)
		FAIL("lockstep stalls %llu", (unsigned long long)lockstep_stalls);
	if (decode_errors || atomic_load(&fd.submit_errors))
		FAIL("decode/submit errors");
	printf("%s\n", fail ? "RESULT: FAIL" : "RESULT: PASS");

	// Teardown order: GPU idle, imports, then the decoder (which frees CAPTURE).
	vkDeviceWaitIdle(vk.dev);
	mlxr_vkimp_destroy(imp);
	mlxr_vdec_close(dec);
	for (int i = 0; i < SLOTS; i++)
		slot_free_images(&vk, &slots[i]);
	destroy_pipeline(&vk);
	vkDestroyShaderModule(vk.dev, vk.shader, NULL);
	vkDestroySemaphore(vk.dev, vk.timeline, NULL);
	vkDestroyCommandPool(vk.dev, vk.pool, NULL);
	vkDestroyDevice(vk.dev, NULL);
	vkDestroyInstance(vk.inst, NULL);
	free(lat.v);
	free(s.aus);
	munmap((void *)s.data, s.size);
	return fail;
}
