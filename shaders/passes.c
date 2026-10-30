// Moonlight XR Native: mlxr_passes, see passes.h. GPLv3.
#include "passes.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// SPIR-V from the build (glslc -mfmt=c), one array per program and variant
static const uint32_t kVertex[] =
#include "vertex.vert.inc"
    ;
static const uint32_t kWarp[] =
#include "warp.frag.inc"
    ;
static const uint32_t kWarpNv12[] =
#include "warp.nv12.frag.inc"
    ;
static const uint32_t kUpsample[] =
#include "upsample.frag.inc"
    ;
static const uint32_t kUpsampleNv12[] =
#include "upsample.nv12.frag.inc"
    ;
static const uint32_t kDownscale[] =
#include "downscale.frag.inc"
    ;
static const uint32_t kDownscaleNv12[] =
#include "downscale.nv12.frag.inc"
    ;
static const uint32_t kAmbi[] =
#include "ambi.frag.inc"
    ;
static const uint32_t kAmbiNv12[] =
#include "ambi.nv12.frag.inc"
    ;
static const uint32_t kOffset[] =
#include "offset.frag.inc"
    ;
static const uint32_t kGlowEdge[] =
#include "glow_edge.frag.inc"
    ;
static const uint32_t kGlow[] =
#include "glow.frag.inc"
    ;
static const uint32_t kRoomVert[] =
#include "room.vert.inc"
    ;
static const uint32_t kRoomFrag[] =
#include "room.frag.inc"
    ;
static const uint32_t kModelVert[] =
#include "model.vert.inc"
    ;
static const uint32_t kModelFrag[] =
#include "model.frag.inc"
    ;
static const uint32_t kSupersample[] =
#include "supersample.frag.inc"
    ;
static const uint32_t kPanelVert[] =
#include "panel.vert.inc"
    ;
static const uint32_t kPanelFrag[] =
#include "panel.frag.inc"
    ;
// Moonlight XR Native: the textured controller model (new)
static const uint32_t kModelTexVert[] =
#include "model_tex.vert.inc"
    ;
static const uint32_t kModelTexFrag[] =
#include "model_tex.frag.inc"
    ;

// Every pipeline layout carries one push constant range this size for both
// stages; each program reads a prefix of it
#define PUSH_BYTES 128
#define PUSH_STAGES (VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
#define MAX_PIPES 64

enum { MOD_VERTEX, MOD_WARP, MOD_WARP_NV12, MOD_UPSAMPLE, MOD_UPSAMPLE_NV12, MOD_DOWNSCALE, MOD_DOWNSCALE_NV12,
       MOD_AMBI, MOD_AMBI_NV12, MOD_OFFSET, MOD_GLOW_EDGE, MOD_GLOW, MOD_ROOM_V, MOD_ROOM_F, MOD_MODEL_V,
       MOD_MODEL_F, MOD_SUPERSAMPLE, MOD_PANEL_V, MOD_PANEL_F, MOD_MODEL_TEX_V, MOD_MODEL_TEX_F, MOD_COUNT };

enum { PL_VIDEO, PL_VIDEO_NV12, PL_TEX, PL_ROOM, PL_NONE, PL_MODEL_TEX, PL_COUNT };
enum { BLEND_NONE, BLEND_CONSTANT, BLEND_PREMUL };
enum { VERTS_NONE, VERTS_ROOM, VERTS_MODEL, VERTS_MODEL_TEX };

typedef struct {
    int pass, kind;
    VkFormat color, depth;
    VkPipeline pipe;
} PipeEntry;

struct MlxrPasses {
    VkDevice device;
    VkPhysicalDevice phys;
    VkPipelineCache cache;
    PFN_vkCmdBeginRenderingKHR beginRendering;
    PFN_vkCmdEndRenderingKHR endRendering;
    PFN_vkCmdPushDescriptorSetKHR pushDescriptors;
    VkShaderModule mod[MOD_COUNT];
    VkSampler linear, atlas, nv12Sampler;
    VkSamplerYcbcrConversion nv12;
    VkFilter nv12Filter;
    VkChromaLocation nv12X, nv12Y;
    VkDescriptorSetLayout dsl[PL_COUNT];
    VkPipelineLayout layout[PL_COUNT];
    VkImage white;
    VkDeviceMemory whiteMem;
    VkImageView whiteView;
    PipeEntry pipes[MAX_PIPES];
    int pipeCount;
};

typedef struct {
    int vs, fs, fsNv12; // module indices; fsNv12 -1 when the pass reads no video
    int layout;          // PL_*; PL_VIDEO switches to PL_VIDEO_NV12 for NV12
    int blend;
    int depth;           // 1: depth test and write, LESS
    int verts;
} PassDesc;

static const PassDesc kPasses[MLXR_PASS_COUNT] = {
    [MLXR_PASS_WARP] = {MOD_VERTEX, MOD_WARP, MOD_WARP_NV12, PL_VIDEO, BLEND_NONE, 0, VERTS_NONE},
    [MLXR_PASS_UPSAMPLE] = {MOD_VERTEX, MOD_UPSAMPLE, MOD_UPSAMPLE_NV12, PL_VIDEO, BLEND_NONE, 0, VERTS_NONE},
    [MLXR_PASS_OFFSET] = {MOD_VERTEX, MOD_OFFSET, -1, PL_VIDEO, BLEND_NONE, 0, VERTS_NONE},
    [MLXR_PASS_DOWNSCALE] = {MOD_VERTEX, MOD_DOWNSCALE, MOD_DOWNSCALE_NV12, PL_VIDEO, BLEND_NONE, 0, VERTS_NONE},
    [MLXR_PASS_AMBI] = {MOD_VERTEX, MOD_AMBI, MOD_AMBI_NV12, PL_VIDEO, BLEND_CONSTANT, 0, VERTS_NONE},
    [MLXR_PASS_GLOW_EDGE] = {MOD_VERTEX, MOD_GLOW_EDGE, -1, PL_TEX, BLEND_NONE, 0, VERTS_NONE},
    [MLXR_PASS_GLOW] = {MOD_VERTEX, MOD_GLOW, -1, PL_TEX, BLEND_NONE, 0, VERTS_NONE},
    [MLXR_PASS_ROOM] = {MOD_ROOM_V, MOD_ROOM_F, -1, PL_ROOM, BLEND_NONE, 1, VERTS_ROOM},
    [MLXR_PASS_MODEL] = {MOD_MODEL_V, MOD_MODEL_F, -1, PL_NONE, BLEND_NONE, 1, VERTS_MODEL},
    [MLXR_PASS_SUPERSAMPLE] = {MOD_VERTEX, MOD_SUPERSAMPLE, -1, PL_TEX, BLEND_NONE, 0, VERTS_NONE},
    [MLXR_PASS_PANEL] = {MOD_PANEL_V, MOD_PANEL_F, -1, PL_TEX, BLEND_PREMUL, 0, VERTS_NONE},
    [MLXR_PASS_MODEL_TEX] = {MOD_MODEL_TEX_V, MOD_MODEL_TEX_F, -1, PL_MODEL_TEX, BLEND_NONE, 1, VERTS_MODEL_TEX},
};

static int isSrgb(VkFormat f) {
    switch (f) {
    case VK_FORMAT_R8_SRGB:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
        return 1;
    default:
        return 0;
    }
}

static uint32_t memoryType(VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
}

static VkShaderModule makeModule(VkDevice dev, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(dev, &ci, NULL, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

static VkDescriptorSetLayout makeSetLayout(VkDevice dev, const VkDescriptorSetLayoutBinding* b, uint32_t n) {
    VkDescriptorSetLayoutCreateInfo ci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = n;
    ci.pBindings = b;
    VkDescriptorSetLayout l = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(dev, &ci, NULL, &l) != VK_SUCCESS) return VK_NULL_HANDLE;
    return l;
}

static VkPipelineLayout makePipelineLayout(VkDevice dev, VkDescriptorSetLayout dsl) {
    VkPushConstantRange pr = {PUSH_STAGES, 0, PUSH_BYTES};
    VkPipelineLayoutCreateInfo ci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = dsl ? 1 : 0;
    ci.pSetLayouts = dsl ? &dsl : NULL;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &pr;
    VkPipelineLayout l = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(dev, &ci, NULL, &l) != VK_SUCCESS) return VK_NULL_HANDLE;
    return l;
}

static VkDescriptorSetLayoutBinding binding(uint32_t i, VkShaderStageFlags stages, const VkSampler* immutable) {
    VkDescriptorSetLayoutBinding b = {i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, immutable};
    return b;
}

static int makeNv12(MlxrPasses* p, const MlxrPassesInfo* info) {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(p->phys, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, &fp);
    VkFormatFeatureFlags f = fp.optimalTilingFeatures;
    if (!(f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
        !(f & (VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT | VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT)))
        return -1;
    int cosited = (f & VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT) != 0;
    int midpoint = (f & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT) != 0;
    // The siting asked for when the format allows it, else the one it has
    p->nv12X = (info->nv12Chroma == MLXR_CHROMA_LEFT && cosited) || !midpoint ? VK_CHROMA_LOCATION_COSITED_EVEN
                                                                             : VK_CHROMA_LOCATION_MIDPOINT;
    p->nv12Y = midpoint ? VK_CHROMA_LOCATION_MIDPOINT : VK_CHROMA_LOCATION_COSITED_EVEN;
    p->nv12Filter =
        (f & VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    VkSamplerYcbcrConversionCreateInfo ci = {VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
    ci.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    ci.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;
    ci.ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW;
    ci.components = (VkComponentMapping){VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
    ci.xChromaOffset = p->nv12X;
    ci.yChromaOffset = p->nv12Y;
    ci.chromaFilter = p->nv12Filter;
    ci.forceExplicitReconstruction = VK_FALSE;
    if (vkCreateSamplerYcbcrConversion(p->device, &ci, NULL, &p->nv12) != VK_SUCCESS) return -1;

    VkSamplerYcbcrConversionInfo conv = {VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    conv.conversion = p->nv12;
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.pNext = &conv;
    // Without separate reconstruction filtering the sampler's filters must
    // equal the conversion's chroma filter
    si.magFilter = si.minFilter = p->nv12Filter;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    if (vkCreateSampler(p->device, &si, NULL, &p->nv12Sampler) != VK_SUCCESS) return -1;
    return 0;
}

int mlxr_passes_create(const MlxrPassesInfo* info, MlxrPasses** out) {
    *out = NULL;
    MlxrPasses* p = calloc(1, sizeof(*p));
    if (!p) return -1;
    p->device = info->device;
    p->phys = info->phys;
    p->cache = info->cache;
    VkDevice dev = p->device;
    p->beginRendering = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(dev, "vkCmdBeginRenderingKHR");
    p->endRendering = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(dev, "vkCmdEndRenderingKHR");
    p->pushDescriptors = (PFN_vkCmdPushDescriptorSetKHR)vkGetDeviceProcAddr(dev, "vkCmdPushDescriptorSetKHR");
    if (!p->beginRendering || !p->endRendering || !p->pushDescriptors) goto fail;

    static const struct {
        const uint32_t* code;
        size_t bytes;
    } mods[MOD_COUNT] = {
#define M(a) {a, sizeof(a)}
        [MOD_VERTEX] = M(kVertex),
        [MOD_WARP] = M(kWarp),
        [MOD_WARP_NV12] = M(kWarpNv12),
        [MOD_UPSAMPLE] = M(kUpsample),
        [MOD_UPSAMPLE_NV12] = M(kUpsampleNv12),
        [MOD_DOWNSCALE] = M(kDownscale),
        [MOD_DOWNSCALE_NV12] = M(kDownscaleNv12),
        [MOD_AMBI] = M(kAmbi),
        [MOD_AMBI_NV12] = M(kAmbiNv12),
        [MOD_OFFSET] = M(kOffset),
        [MOD_GLOW_EDGE] = M(kGlowEdge),
        [MOD_GLOW] = M(kGlow),
        [MOD_ROOM_V] = M(kRoomVert),
        [MOD_ROOM_F] = M(kRoomFrag),
        [MOD_MODEL_V] = M(kModelVert),
        [MOD_MODEL_F] = M(kModelFrag),
        [MOD_SUPERSAMPLE] = M(kSupersample),
        [MOD_PANEL_V] = M(kPanelVert),
        [MOD_PANEL_F] = M(kPanelFrag),
        [MOD_MODEL_TEX_V] = M(kModelTexVert),
        [MOD_MODEL_TEX_F] = M(kModelTexFrag),
#undef M
    };
    for (int i = 0; i < MOD_COUNT; i++) {
        if (!info->nv12 && (i == MOD_WARP_NV12 || i == MOD_UPSAMPLE_NV12 || i == MOD_DOWNSCALE_NV12 ||
                            i == MOD_AMBI_NV12))
            continue;
        p->mod[i] = makeModule(dev, mods[i].code, mods[i].bytes);
        if (!p->mod[i]) goto fail;
    }

    // Upstream sets GL_LINEAR and GL_CLAMP_TO_EDGE on every texture it samples
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.0f;
    if (vkCreateSampler(dev, &si, NULL, &p->linear) != VK_SUCCESS) goto fail;
    // The room's atlases: GL_LINEAR_MIPMAP_LINEAR over their whole chain, and
    // anisotropy when the device has it (upstream caps it at
    // ROOM_ANISOTROPY_MAX and passes ctx->roomAnisotropy)
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.maxLod = VK_LOD_CLAMP_NONE;
    if (info->atlasAnisotropy > 1.0f) {
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy = info->atlasAnisotropy;
    }
    if (vkCreateSampler(dev, &si, NULL, &p->atlas) != VK_SUCCESS) goto fail;

    if (info->nv12 && makeNv12(p, info)) goto fail;

    const VkShaderStageFlags FS = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutBinding b[5];
    // Video passes: 0..2 the planes, 3 depth, 4 offsets
    for (uint32_t i = 0; i < 5; i++) b[i] = binding(i, FS, NULL);
    p->dsl[PL_VIDEO] = makeSetLayout(dev, b, 5);
    if (info->nv12) {
        VkDescriptorSetLayoutBinding n[3] = {binding(0, FS, &p->nv12Sampler), binding(3, FS, NULL),
                                             binding(4, FS, NULL)};
        p->dsl[PL_VIDEO_NV12] = makeSetLayout(dev, n, 3);
    }
    b[0] = binding(0, FS, NULL);
    p->dsl[PL_TEX] = makeSetLayout(dev, b, 1);
    VkDescriptorSetLayoutBinding r[2] = {binding(0, VK_SHADER_STAGE_VERTEX_BIT, NULL), binding(1, FS, NULL)};
    p->dsl[PL_ROOM] = makeSetLayout(dev, r, 2);
    // Native: the textured model's colour and surface images
    VkDescriptorSetLayoutBinding mt[2] = {binding(0, FS, NULL), binding(1, FS, NULL)};
    p->dsl[PL_MODEL_TEX] = makeSetLayout(dev, mt, 2);
    for (int i = 0; i < PL_COUNT; i++) {
        if (i == PL_VIDEO_NV12 && !info->nv12) continue;
        if (i != PL_NONE && !p->dsl[i]) goto fail;
        p->layout[i] = makePipelineLayout(dev, i == PL_NONE ? VK_NULL_HANDLE : p->dsl[i]);
        if (!p->layout[i]) goto fail;
    }

    // The 1x1 white stand in (upstream's roomWhiteTexture)
    VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = (VkExtent3D){1, 1, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (vkCreateImage(dev, &ici, NULL, &p->white) != VK_SUCCESS) goto fail;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev, p->white, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memoryType(p->phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(dev, &ai, NULL, &p->whiteMem) != VK_SUCCESS) goto fail;
    vkBindImageMemory(dev, p->white, p->whiteMem, 0);
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = p->white;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(dev, &vi, NULL, &p->whiteView) != VK_SUCCESS) goto fail;

    *out = p;
    return 0;
fail:
    mlxr_passes_destroy(p);
    return -1;
}

void mlxr_passes_destroy(MlxrPasses* p) {
    if (!p) return;
    VkDevice dev = p->device;
    for (int i = 0; i < p->pipeCount; i++) vkDestroyPipeline(dev, p->pipes[i].pipe, NULL);
    if (p->whiteView) vkDestroyImageView(dev, p->whiteView, NULL);
    if (p->white) vkDestroyImage(dev, p->white, NULL);
    if (p->whiteMem) vkFreeMemory(dev, p->whiteMem, NULL);
    for (int i = 0; i < PL_COUNT; i++) {
        if (p->layout[i]) vkDestroyPipelineLayout(dev, p->layout[i], NULL);
        if (p->dsl[i]) vkDestroyDescriptorSetLayout(dev, p->dsl[i], NULL);
    }
    if (p->nv12Sampler) vkDestroySampler(dev, p->nv12Sampler, NULL);
    if (p->nv12) vkDestroySamplerYcbcrConversion(dev, p->nv12, NULL);
    if (p->atlas) vkDestroySampler(dev, p->atlas, NULL);
    if (p->linear) vkDestroySampler(dev, p->linear, NULL);
    for (int i = 0; i < MOD_COUNT; i++)
        if (p->mod[i]) vkDestroyShaderModule(dev, p->mod[i], NULL);
    free(p);
}

void mlxr_passes_prepare(MlxrPasses* p, VkCommandBuffer cmd) {
    mlxr_barrier(cmd, p->white, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkClearColorValue white = {.float32 = {1.0f, 1.0f, 1.0f, 1.0f}};
    VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, p->white, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &white, 1, &range);
    mlxr_barrier(cmd, p->white, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

VkSamplerYcbcrConversion mlxr_passes_nv12_conversion(const MlxrPasses* p) {
    return p->nv12;
}

VkResult mlxr_passes_nv12_view(const MlxrPasses* p, VkImage image, VkImageView* out) {
    if (!p->nv12) return VK_ERROR_FEATURE_NOT_PRESENT;
    VkSamplerYcbcrConversionInfo conv = {VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    conv.conversion = p->nv12;
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.pNext = &conv;
    vi.image = image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return vkCreateImageView(p->device, &vi, NULL, out);
}

void mlxr_passes_nv12_caps(const MlxrPasses* p, VkFilter* chromaFilter, VkChromaLocation* x, VkChromaLocation* y) {
    if (chromaFilter) *chromaFilter = p->nv12Filter;
    if (x) *x = p->nv12X;
    if (y) *y = p->nv12Y;
}

static VkPipeline buildPipeline(MlxrPasses* p, int pass, int kind, VkFormat color, VkFormat depth) {
    const PassDesc* d = &kPasses[pass];
    int nv12 = kind == MLXR_VIDEO_NV12 && d->fsNv12 >= 0;
    VkShaderModule vs = p->mod[d->vs];
    VkShaderModule fs = p->mod[nv12 ? d->fsNv12 : d->fs];
    int layout = d->layout == PL_VIDEO && nv12 ? PL_VIDEO_NV12 : d->layout;
    if (!vs || !fs || !p->layout[layout]) return VK_NULL_HANDLE;

    VkBool32 outLinear = isSrgb(color) ? VK_TRUE : VK_FALSE;
    VkSpecializationMapEntry me = {0, 0, sizeof(VkBool32)};
    VkSpecializationInfo spec = {1, &me, sizeof(outLinear), &outLinear};
    VkPipelineShaderStageCreateInfo st[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", NULL},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main",
         &spec},
    };

    VkVertexInputBindingDescription vb = {0, 0, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[4];
    VkPipelineVertexInputStateCreateInfo vin = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (d->verts == VERTS_ROOM) {
        // position, colour, spill, uv (upstream's glVertexAttribPointer offsets)
        vb.stride = MLXR_ROOM_VERTEX_FLOATS * sizeof(float);
        va[0] = (VkVertexInputAttributeDescription){0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        va[1] = (VkVertexInputAttributeDescription){1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float)};
        va[2] = (VkVertexInputAttributeDescription){2, 0, VK_FORMAT_R32_SFLOAT, 6 * sizeof(float)};
        va[3] = (VkVertexInputAttributeDescription){3, 0, VK_FORMAT_R32G32_SFLOAT, 7 * sizeof(float)};
        vin.vertexAttributeDescriptionCount = 4;
    } else if (d->verts == VERTS_MODEL) {
        // position, normal, colour (the uv at 6 is skipped)
        vb.stride = MLXR_MODEL_VERTEX_FLOATS * sizeof(float);
        va[0] = (VkVertexInputAttributeDescription){0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        va[1] = (VkVertexInputAttributeDescription){1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float)};
        va[2] = (VkVertexInputAttributeDescription){2, 0, VK_FORMAT_R32G32B32_SFLOAT, 8 * sizeof(float)};
        vin.vertexAttributeDescriptionCount = 3;
    } else if (d->verts == VERTS_MODEL_TEX) {
        // Native: position, normal, tangent (w the bitangent's sign), uv
        vb.stride = MLXR_MODEL_TEX_VERTEX_FLOATS * sizeof(float);
        va[0] = (VkVertexInputAttributeDescription){0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        va[1] = (VkVertexInputAttributeDescription){1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float)};
        va[2] = (VkVertexInputAttributeDescription){2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 6 * sizeof(float)};
        va[3] = (VkVertexInputAttributeDescription){3, 0, VK_FORMAT_R32G32_SFLOAT, 10 * sizeof(float)};
        vin.vertexAttributeDescriptionCount = 4;
    }
    if (d->verts != VERTS_NONE) {
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &vb;
        vin.pVertexAttributeDescriptions = va;
    }

    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE; // upstream never enables GL_CULL_FACE
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    if (d->depth) {
        // GL's default depth function, cleared to 1
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
    }
    VkPipelineColorBlendAttachmentState cba = {0};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    if (d->blend == BLEND_CONSTANT) {
        // glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA)
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA;
        cba.dstColorBlendFactor = cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    } else if (d->blend == BLEND_PREMUL) {
        cba.blendEnable = VK_TRUE;
        cba.srcColorBlendFactor = cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstColorBlendFactor = cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp = cba.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[3] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo dy = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = d->blend == BLEND_CONSTANT ? 3 : 2;
    dy.pDynamicStates = dyn;
    VkPipelineRenderingCreateInfoKHR rci = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &color;
    rci.depthAttachmentFormat = d->depth ? depth : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo gp = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.pNext = &rci;
    gp.stageCount = 2;
    gp.pStages = st;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dy;
    gp.layout = p->layout[layout];
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(p->device, p->cache, 1, &gp, NULL, &pipe) != VK_SUCCESS) return VK_NULL_HANDLE;
    return pipe;
}

static VkPipeline pipeline(MlxrPasses* p, int pass, int kind, VkFormat color, VkFormat depth) {
    if (kPasses[pass].fsNv12 < 0) kind = MLXR_VIDEO_YUV420P;
    if (!kPasses[pass].depth) depth = VK_FORMAT_UNDEFINED;
    for (int i = 0; i < p->pipeCount; i++) {
        PipeEntry* e = &p->pipes[i];
        if (e->pass == pass && e->kind == kind && e->color == color && e->depth == depth) return e->pipe;
    }
    if (p->pipeCount >= MAX_PIPES) return VK_NULL_HANDLE;
    VkPipeline pipe = buildPipeline(p, pass, kind, color, depth);
    if (!pipe) return VK_NULL_HANDLE;
    p->pipes[p->pipeCount++] = (PipeEntry){pass, kind, color, depth, pipe};
    return pipe;
}

int mlxr_passes_warm(MlxrPasses* p, MlxrPassId pass, MlxrVideoKind kind, VkFormat colorFormat, VkFormat depthFormat) {
    if ((int)pass < 0 || pass >= MLXR_PASS_COUNT) return -1;
    return pipeline(p, pass, kind, colorFormat, depthFormat) ? 0 : -1;
}

// --- recording ------------------------------------------------------------

static void setRect(VkCommandBuffer cmd, VkRect2D r) {
    VkViewport vp = {(float)r.offset.x, (float)r.offset.y, (float)r.extent.width, (float)r.extent.height, 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &r);
}

// overwrites: the pass writes every pixel of the rect, so nothing needs
// loading when it is not cleared
static void beginRender(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, int overwrites,
                        VkImageView depth, int clearColour) {
    VkRenderingAttachmentInfoKHR ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    ca.imageView = t->view;
    ca.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ca.loadOp = clearColour ? VK_ATTACHMENT_LOAD_OP_CLEAR
                            : (overwrites ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD);
    ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    memcpy(ca.clearValue.color.float32, t->clearColor, sizeof(t->clearColor));
    VkRenderingAttachmentInfoKHR da = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
    da.imageView = depth;
    da.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    da.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    da.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    da.clearValue.depthStencil.depth = 1.0f;
    VkRenderingInfoKHR ri = {VK_STRUCTURE_TYPE_RENDERING_INFO_KHR};
    ri.renderArea = t->rect;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &ca;
    if (depth) ri.pDepthAttachment = &da;
    p->beginRendering(cmd, &ri);
    setRect(cmd, t->rect);
}

static VkWriteDescriptorSet imageWrite(uint32_t binding, const VkDescriptorImageInfo* ii) {
    VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = ii;
    return w;
}

static VkDescriptorImageInfo imageInfo(VkSampler s, VkImageView v) {
    VkDescriptorImageInfo ii = {s, v, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    return ii;
}

// The video bindings 0..2 plus the optional depth (3) and offsets (4)
static void pushVideoSet(MlxrPasses* p, VkCommandBuffer cmd, VkPipelineLayout layout, const MlxrVideo* v,
                         VkImageView depth, VkImageView offsets, int withDepth) {
    VkDescriptorImageInfo ii[5];
    VkWriteDescriptorSet w[5];
    uint32_t n = 0;
    if (v) {
        if (v->kind == MLXR_VIDEO_NV12) {
            ii[n] = imageInfo(VK_NULL_HANDLE, v->planes[0]);
            w[n] = imageWrite(0, &ii[n]);
            n++;
        } else {
            for (uint32_t i = 0; i < 3; i++) {
                ii[n] = imageInfo(p->linear, v->planes[i]);
                w[n] = imageWrite(i, &ii[n]);
                n++;
            }
        }
    }
    if (withDepth) {
        ii[n] = imageInfo(p->linear, depth ? depth : p->whiteView);
        w[n] = imageWrite(3, &ii[n]);
        n++;
    }
    if (withDepth > 1) {
        ii[n] = imageInfo(p->linear, offsets ? offsets : p->whiteView);
        w[n] = imageWrite(4, &ii[n]);
        n++;
    }
    p->pushDescriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, n, w);
}

static void pushTex(MlxrPasses* p, VkCommandBuffer cmd, VkPipelineLayout layout, VkImageView view) {
    VkDescriptorImageInfo ii = imageInfo(p->linear, view ? view : p->whiteView);
    VkWriteDescriptorSet w = imageWrite(0, &ii);
    p->pushDescriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &w);
}

static VkPipelineLayout videoLayout(MlxrPasses* p, const MlxrVideo* v) {
    return p->layout[v && v->kind == MLXR_VIDEO_NV12 ? PL_VIDEO_NV12 : PL_VIDEO];
}

// The header every video pass's push block starts with: vidRect, vidChroma
static void videoHeader(const MlxrVideo* v, float out[8]) {
    float tw = (float)(v->texWidth ? v->texWidth : v->width);
    float th = (float)(v->texHeight ? v->texHeight : v->height);
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = (float)v->width / tw;
    out[3] = (float)v->height / th;
    // Three planes: a left sited chroma sample sits a quarter of a chroma
    // texel left of where a centred one would, so the read moves right by
    // that much (see docs/dev/reports/shaders.md). Vertically both sitings are midway.
    float chromaW = (float)(((uint32_t)tw + 1u) / 2u);
    out[4] = v->kind == MLXR_VIDEO_YUV420P && v->chroma == MLXR_CHROMA_LEFT ? 0.25f / chromaW : 0.0f;
    out[5] = 0.0f;
    out[6] = 1.0f / tw;
    out[7] = 1.0f / th;
}

void mlxr_pass_warp(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrWarpParams* w) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_WARP, w->video.kind, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = videoLayout(p, &w->video);
    float pc[32];
    videoHeader(&w->video, pc);
    memcpy(&pc[8], w->grade, sizeof(w->grade));
    pc[12] = w->tint[0];
    pc[13] = w->tint[1];
    pc[14] = w->tint[2];
    pc[15] = w->gradeOn ? 1.0f : 0.0f;
    pc[16] = w->disparity;
    pc[17] = (float)w->eye;
    pc[18] = w->convergence;
    pc[19] = w->dispTexels;
    pc[20] = w->lowResWidth;
    pc[21] = (float)w->video.width;
    pc[22] = w->srcInset;
    pc[23] = w->edgeFade;
    pc[24] = w->occlusion ? 1.0f : 0.0f;
    pc[25] = w->depthCubic ? 1.0f : 0.0f;
    pc[26] = w->showDepth ? 1.0f : 0.0f;
    pc[27] = w->barTest ? 1.0f : 0.0f;
    pc[28] = w->sharpen;
    pc[29] = (float)w->split;
    pc[30] = (float)w->half;
    pc[31] = w->shift;
    beginRender(p, cmd, t, 1, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    pushVideoSet(p, cmd, layout, &w->video, w->depth, w->offsets, 2);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_upsample(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrUpsampleParams* u) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_UPSAMPLE, u->video.kind, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = videoLayout(p, &u->video);
    float pc[12];
    videoHeader(&u->video, pc);
    pc[8] = u->sigmaR;
    pc[9] = u->sharp;
    pc[10] = (float)u->depthWidth;
    pc[11] = (float)u->depthHeight;
    beginRender(p, cmd, t, 1, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    pushVideoSet(p, cmd, layout, &u->video, u->depth, VK_NULL_HANDLE, 1);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_offset(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrOffsetParams* o) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_OFFSET, MLXR_VIDEO_YUV420P, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = p->layout[PL_VIDEO];
    float pc[4] = {o->dispTexels, o->convergence, 0.0f, 0.0f};
    beginRender(p, cmd, t, 1, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    pushVideoSet(p, cmd, layout, NULL, o->depth, VK_NULL_HANDLE, 1);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_downscale(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrVideo* v) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_DOWNSCALE, v->kind, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = videoLayout(p, v);
    float pc[12];
    videoHeader(v, pc);
    pc[8] = 1.0f / (float)t->rect.extent.width;
    pc[9] = 1.0f / (float)t->rect.extent.height;
    pc[10] = pc[11] = 0.0f;
    beginRender(p, cmd, t, 1, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    pushVideoSet(p, cmd, layout, v, VK_NULL_HANDLE, VK_NULL_HANDLE, 0);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_ambi(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrAmbiParams* a) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_AMBI, a->video.kind, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = videoLayout(p, &a->video);
    float pc[20];
    videoHeader(&a->video, pc);
    memcpy(&pc[8], a->crop, sizeof(a->crop));
    memcpy(&pc[12], a->grade, sizeof(a->grade));
    pc[16] = a->gradeOn ? 1.0f : 0.0f;
    pc[17] = pc[18] = pc[19] = 0.0f;
    // A blend of 1 replaces what is there, so nothing needs loading
    int replaces = a->blend >= 1.0f;
    beginRender(p, cmd, t, replaces, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    float bc[4] = {a->blend, a->blend, a->blend, a->blend};
    vkCmdSetBlendConstants(cmd, bc);
    pushVideoSet(p, cmd, layout, &a->video, VK_NULL_HANDLE, VK_NULL_HANDLE, 0);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_glow_edge(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrGlowEdgeParams* g) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_GLOW_EDGE, 0, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = p->layout[PL_TEX];
    struct {
        float luma[4];
        float litReach[4];
        int32_t steps[4];
    } pc = {{g->luma[0], g->luma[1], g->luma[2], 0.0f}, {g->lit[0], g->lit[1], g->reach, 0.0f}, {g->steps, 0, 0, 0}};
    beginRender(p, cmd, t, 1, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    pushTex(p, cmd, layout, g->sample);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), &pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_glow(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrGlowParams* g) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_GLOW, 0, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = p->layout[PL_TEX];
    float pc[4] = {g->intensity, g->blur ? 1.0f : 0.0f, 0.0f, 0.0f};
    beginRender(p, cmd, t, 1, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    pushTex(p, cmd, layout, g->sample);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_room(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrRoomParams* r) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_ROOM, 0, t->format, r->depthFormat);
    if (!pipe) return;
    VkPipelineLayout layout = p->layout[PL_ROOM];
    beginRender(p, cmd, t, 0, r->depth, 1);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    if (r->partCount > 0) {
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &r->vertices, &off);
        vkCmdBindIndexBuffer(cmd, r->indices, 0, VK_INDEX_TYPE_UINT32);
    }
    float pc[20];
    pc[16] = r->spillGain;
    pc[18] = r->dim;
    pc[19] = 0.0f;
    int eyes = r->eyes < 1 ? 1 : (r->eyes > 2 ? 2 : r->eyes);
    for (int e = 0; e < eyes && r->partCount > 0; e++) {
        setRect(cmd, r->eyeRect[e]);
        memcpy(pc, r->viewProj[e], 16 * sizeof(float));
        for (uint32_t i = 0; i < r->partCount; i++) {
            const MlxrRoomPart* part = &r->parts[i];
            VkDescriptorImageInfo ii[2] = {imageInfo(p->linear, r->ambi ? r->ambi : p->whiteView),
                                           imageInfo(p->atlas, part->atlas ? part->atlas : p->whiteView)};
            VkWriteDescriptorSet w[2] = {imageWrite(0, &ii[0]), imageWrite(1, &ii[1])};
            p->pushDescriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, w);
            pc[17] = part->texMix;
            vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
            vkCmdDrawIndexed(cmd, part->indexCount, 1, part->firstIndex, 0, 0);
        }
    }
    p->endRendering(cmd);
}

// Native helpers for the textured model: a x b (column major 4x4), the
// inverse of a rigid (or any invertible affine) transform's 3x3 and where a
// view-projection's camera sits (its rows x, y and w all vanish there)
static void mul4(const float* a, const float* b, float* out) {
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}

static int inverse3(const float* m, float inv[9]) {
    // m: the upper 3x3 of a column major 4x4; inv: column major 3x3
    float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (fabsf(det) < 1e-12f) return -1;
    float s = 1.0f / det;
    // rows of the inverse: (A, -(b i - c h), b f - c e), (B, a i - c g, -(a f - c d)), (C, -(a h - b g), a e - b d)
    float r[9] = {A, -(b * i - c * h), b * f - c * e, B, a * i - c * g, -(a * f - c * d), C, -(a * h - b * g), a * e - b * d};
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) inv[col * 3 + row] = r[row * 3 + col] * s;
    return 0;
}

static void apply3(const float inv[9], const float v[3], float out[4]) {
    for (int r = 0; r < 3; r++) out[r] = inv[r] * v[0] + inv[3 + r] * v[1] + inv[6 + r] * v[2];
    out[3] = 0.0f;
}

static void cameraOf(const float* vp, float out[3]) {
    // The camera C (homogeneous) satisfies row_x . C = row_y . C = row_w . C = 0:
    // the generalised cross product of those three rows
    float r[3][4];
    const int rows[3] = {0, 1, 3};
    for (int i = 0; i < 3; i++)
        for (int k = 0; k < 4; k++) r[i][k] = vp[k * 4 + rows[i]];
    float c[4];
    for (int k = 0; k < 4; k++) {
        int m[3], n = 0;
        for (int j = 0; j < 4; j++)
            if (j != k) m[n++] = j;
        float det = r[0][m[0]] * (r[1][m[1]] * r[2][m[2]] - r[1][m[2]] * r[2][m[1]]) -
                    r[0][m[1]] * (r[1][m[0]] * r[2][m[2]] - r[1][m[2]] * r[2][m[0]]) +
                    r[0][m[2]] * (r[1][m[0]] * r[2][m[1]] - r[1][m[1]] * r[2][m[0]]);
        c[k] = (k & 1) ? -det : det;
    }
    float w = fabsf(c[3]) > 1e-20f ? c[3] : 1e-20f;
    for (int k = 0; k < 3; k++) out[k] = c[k] / w;
}

// The key light, from above and behind the viewer's usual side, fixed in the
// room (the painted model's light, model.vert)
static const float kModelKey[3] = {-0.303f, 0.808f, 0.505f};

static void drawTexturedModel(MlxrPasses* p, VkCommandBuffer cmd, const MlxrModelParams* m, int eyes) {
    VkPipelineLayout layout = p->layout[PL_MODEL_TEX];
    float pc[32];
    for (int e = 0; e < eyes; e++) {
        setRect(cmd, m->eyeRect[e]);
        float cam[3];
        cameraOf(m->viewProj[e], cam);
        for (int h = 0; h < 2; h++) {
            if (!m->shown[h]) continue;
            VkDescriptorImageInfo ii[2] = {imageInfo(p->atlas, m->handColour[h] ? m->handColour[h] : p->whiteView),
                                           imageInfo(p->atlas, m->handSurface[h] ? m->handSurface[h] : p->whiteView)};
            VkWriteDescriptorSet w[2] = {imageWrite(0, &ii[0]), imageWrite(1, &ii[1])};
            p->pushDescriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 2, w);
            MlxrModelPart whole = {m->handFirstIndex[h], m->handIndexCount[h], {0}};
            for (int i = 0; i < 16; i++) whole.transform[i] = i % 5 == 0 ? 1.0f : 0.0f;
            uint32_t parts = m->handPartCount[h] && m->handParts[h] ? m->handPartCount[h] : 1;
            for (uint32_t i = 0; i < parts; i++) {
                const MlxrModelPart* part = m->handPartCount[h] && m->handParts[h] ? &m->handParts[h][i] : &whole;
                float model[16], inv[9];
                mul4(m->model[h], part->transform, model);
                if (inverse3(model, inv) != 0) continue;
                mul4(m->viewProj[e], model, pc);
                // The camera, the key and the room's up in the part's own space
                float rel[3] = {cam[0] - model[12], cam[1] - model[13], cam[2] - model[14]};
                const float up[3] = {0.0f, 1.0f, 0.0f};
                apply3(inv, rel, &pc[16]);
                apply3(inv, kModelKey, &pc[20]);
                apply3(inv, up, &pc[24]);
                pc[28] = (float)m->debugView;
                pc[29] = m->gain;
                pc[30] = pc[31] = 0.0f;
                vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
                vkCmdDrawIndexed(cmd, part->indexCount, 1, part->firstIndex, m->handVertexOffset[h], 0);
            }
        }
    }
}

void mlxr_pass_model(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrModelParams* m) {
    // Native: the Frame's textured pair has its own pipeline
    int textured = m->textured && m->perHand;
    VkPipeline pipe = pipeline(p, textured ? MLXR_PASS_MODEL_TEX : MLXR_PASS_MODEL, 0, t->format, m->depthFormat);
    if (!pipe) return;
    VkPipelineLayout layout = p->layout[PL_NONE];
    // Nothing but the models: the layer shows through everywhere else
    MlxrTarget clear = *t;
    memset(clear.clearColor, 0, sizeof(clear.clearColor));
    beginRender(p, cmd, &clear, 0, m->depth, !m->over);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &m->vertices, &off);
    vkCmdBindIndexBuffer(cmd, m->indices, 0, VK_INDEX_TYPE_UINT32);
    int eyes = m->eyes < 1 ? 1 : (m->eyes > 2 ? 2 : m->eyes);
    if (textured) {
        drawTexturedModel(p, cmd, m, eyes);
        p->endRendering(cmd);
        return;
    }
    float pc[32];
    for (int e = 0; e < eyes; e++) {
        setRect(cmd, m->eyeRect[e]);
        memcpy(pc, m->viewProj[e], 16 * sizeof(float));
        for (int h = 0; h < 2; h++) {
            if (!m->shown[h]) continue;
            memcpy(&pc[16], m->model[h], 16 * sizeof(float));
            vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
            if (m->perHand) {
                vkCmdDrawIndexed(cmd, m->handIndexCount[h], 1, m->handFirstIndex[h], m->handVertexOffset[h], 0);
            }
            else {
                vkCmdDrawIndexed(cmd, m->indexCount, 1, 0, 0, 0);
            }
        }
    }
    p->endRendering(cmd);
}

void mlxr_pass_supersample(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrSupersampleParams* s) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_SUPERSAMPLE, 0, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = p->layout[PL_TEX];
    float pc[8];
    memcpy(pc, s->srcRect, sizeof(s->srcRect));
    pc[4] = (float)t->rect.extent.width;
    pc[5] = (float)t->rect.extent.height;
    pc[6] = (float)(s->taps < 1 ? 1 : s->taps);
    pc[7] = 0.0f;
    beginRender(p, cmd, t, 1, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    pushTex(p, cmd, layout, s->src);
    vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    p->endRendering(cmd);
}

void mlxr_pass_panel(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrPanelParams* pp) {
    VkPipeline pipe = pipeline(p, MLXR_PASS_PANEL, 0, t->format, VK_FORMAT_UNDEFINED);
    if (!pipe) return;
    VkPipelineLayout layout = p->layout[PL_TEX];
    int segments = pp->segments < 1 ? 1 : pp->segments;
    beginRender(p, cmd, t, 0, VK_NULL_HANDLE, t->clear);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    // Native: trilinear with anisotropy (the atlas's sampler), for a source
    // with mips (the curved or 3D picture's own image) shrunk into an eye
    VkDescriptorImageInfo ii = imageInfo(p->atlas, pp->src ? pp->src : p->whiteView);
    VkWriteDescriptorSet wr = imageWrite(0, &ii);
    p->pushDescriptors(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &wr);
    int eyes = pp->eyes < 1 ? 1 : (pp->eyes > 2 ? 2 : pp->eyes);
    float pc[28];
    pc[16] = pp->width;
    pc[17] = pp->height;
    pc[18] = pp->radius;
    pc[19] = (float)segments;
    memcpy(&pc[24], pp->color, sizeof(pp->color));
    for (int e = 0; e < eyes; e++) {
        setRect(cmd, pp->eyeRect[e]);
        memcpy(pc, pp->mvp[e], 16 * sizeof(float));
        memcpy(&pc[20], pp->uvRect[e], 4 * sizeof(float));
        vkCmdPushConstants(cmd, layout, PUSH_STAGES, 0, sizeof(pc), pc);
        vkCmdDraw(cmd, (uint32_t)segments * 6u, 1, 0, 0);
    }
    p->endRendering(cmd);
}

static void layoutSync(VkImageLayout l, VkPipelineStageFlags* stage, VkAccessFlags* access) {
    switch (l) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        *stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        *access = 0;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        *access = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        *stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        *access = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        *stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        *access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        *stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        *access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        *stage = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        *access = VK_ACCESS_SHADER_READ_BIT;
        break;
    default:
        *stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        *access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        break;
    }
}

void mlxr_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                  VkImageLayout to) {
    VkPipelineStageFlags srcStage, dstStage;
    VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    layoutSync(from, &srcStage, &b.srcAccessMask);
    layoutSync(to, &dstStage, &b.dstAccessMask);
    if (to == VK_IMAGE_LAYOUT_UNDEFINED) dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = (VkImageSubresourceRange){aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &b);
}
