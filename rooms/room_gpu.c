// Moonlight XR Native: see room_gpu.h. GPLv3.
#include "room_gpu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "xr_atlas.h"
#include "xr_roommesh.h"

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

static unsigned char* readFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* data = n > 0 ? malloc((size_t)n) : NULL;
    if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
        free(data);
        data = NULL;
    }
    fclose(f);
    if (data) {
        *size = (size_t)n;
    }
    return data;
}

static uint32_t findMemory(const MlxrGpu* gpu, uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags fallback) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(gpu->phys, &mp);
    for (int pass = 0; pass < 2; pass++) {
        VkMemoryPropertyFlags need = pass == 0 ? want : fallback;
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & need) == need) {
                return i;
            }
        }
    }
    return UINT32_MAX;
}

static uint64_t alignUp(uint64_t v, uint64_t a) {
    return (v + a - 1) / a * a;
}

// A host visible staging buffer, mapped, of `size` bytes
static int makeStaging(const MlxrGpu* gpu, uint64_t size, VkBuffer* buf, VkDeviceMemory* mem, void** map) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(gpu->device, &bi, NULL, buf) != VK_SUCCESS) {
        return -1;
    }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(gpu->device, *buf, &req);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemory(gpu, req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(gpu->device, &ai, NULL, mem) != VK_SUCCESS) {
        return -1;
    }
    vkBindBufferMemory(gpu->device, *buf, *mem, 0);
    return vkMapMemory(gpu->device, *mem, 0, VK_WHOLE_SIZE, 0, map) == VK_SUCCESS ? 0 : -1;
}

// A vertex and an index buffer in one device local allocation
static int makeMeshBuffers(const MlxrGpu* gpu, uint64_t vbytes, uint64_t ibytes, VkBuffer* vb, VkBuffer* ib,
                           VkDeviceMemory* mem, uint64_t* total) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = vbytes;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(gpu->device, &bi, NULL, vb) != VK_SUCCESS) {
        return -1;
    }
    bi.size = ibytes;
    bi.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (vkCreateBuffer(gpu->device, &bi, NULL, ib) != VK_SUCCESS) {
        return -1;
    }
    VkMemoryRequirements rv, ri;
    vkGetBufferMemoryRequirements(gpu->device, *vb, &rv);
    vkGetBufferMemoryRequirements(gpu->device, *ib, &ri);
    uint64_t ioff = alignUp(rv.size, ri.alignment);
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = ioff + ri.size;
    ai.memoryTypeIndex = findMemory(gpu, rv.memoryTypeBits & ri.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(gpu->device, &ai, NULL, mem) != VK_SUCCESS) {
        return -1;
    }
    vkBindBufferMemory(gpu->device, *vb, *mem, 0);
    vkBindBufferMemory(gpu->device, *ib, *mem, ioff);
    *total = ai.allocationSize;
    return 0;
}

static void bufferToVertexInput(VkCommandBuffer cmd, VkBuffer b, int index) {
    VkBufferMemoryBarrier bar = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar.dstAccessMask = index ? VK_ACCESS_INDEX_READ_BIT : VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar.buffer = b;
    bar.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, NULL, 1, &bar, 0,
                         NULL);
}

// Records the two buffer copies out of a staging buffer holding the vertices
// at offset 0 and the indices at ioff
static void recordMeshCopy(VkCommandBuffer cmd, VkBuffer staging, uint64_t ioff, VkBuffer vb, uint64_t vbytes, VkBuffer ib,
                           uint64_t ibytes) {
    VkBufferCopy cv = {0, 0, vbytes};
    VkBufferCopy ci = {ioff, 0, ibytes};
    vkCmdCopyBuffer(cmd, staging, vb, 1, &cv);
    vkCmdCopyBuffer(cmd, staging, ib, 1, &ci);
    bufferToVertexInput(cmd, vb, 0);
    bufferToVertexInput(cmd, ib, 1);
}

// Submit what a record function put in a fresh command buffer, wait for it
static int submitSync(const MlxrGpu* gpu, VkCommandPool pool, VkCommandBuffer cmd) {
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        return -1;
    }
    VkFenceCreateInfo fci = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    if (vkCreateFence(gpu->device, &fci, NULL, &fence) != VK_SUCCESS) {
        return -1;
    }
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    int rc = -1;
    if (vkQueueSubmit(gpu->queue, 1, &si, fence) == VK_SUCCESS) {
        rc = vkWaitForFences(gpu->device, 1, &fence, VK_TRUE, 30000000000ull) == VK_SUCCESS ? 0 : -1;
    }
    vkDestroyFence(gpu->device, fence, NULL);
    return rc;
}

static int beginSync(const MlxrGpu* gpu, VkCommandPool* pool, VkCommandBuffer* cmd) {
    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pci.queueFamilyIndex = gpu->queueFamily;
    if (vkCreateCommandPool(gpu->device, &pci, NULL, pool) != VK_SUCCESS) {
        return -1;
    }
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = *pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(gpu->device, &cai, cmd) != VK_SUCCESS) {
        return -1;
    }
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    return vkBeginCommandBuffer(*cmd, &bi) == VK_SUCCESS ? 0 : -1;
}

VkFormat mlxr_room_atlas_format(uint32_t block) {
    switch (block) {
    case 4: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case 6: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
    case 8: return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
    default: return VK_FORMAT_UNDEFINED;
    }
}

// ---------------------------------------------------------------- room

static const char* roomFileName(int style) {
    switch (style) {
    case ROOM_STYLE_THEATER: return "home_theater";
    case ROOM_STYLE_GRAND_CINEMA: return "grand_cinema";
    case ROOM_STYLE_SYNTHWAVE: return "synthwave";
    default: return NULL;
    }
}

// An atlas file read, parsed and checked against the device
typedef struct {
    unsigned char* data;
    size_t size;
    AtlasInfo info;
    VkFormat format;
    int low;
    uint64_t stageOffset[ATLAS_LEVELS_MAX]; // 16+ aligned, in the staging buffer
} AtlasLoad;

static int atlasSampleable(const MlxrGpu* gpu, VkFormat format, const AtlasInfo* info) {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu->phys, format, &fp);
    VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                                VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
        return 0;
    }
    VkImageFormatProperties ip;
    if (vkGetPhysicalDeviceImageFormatProperties(gpu->phys, format, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
                                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, 0,
                                                 &ip) != VK_SUCCESS) {
        return 0;
    }
    return info->width <= ip.maxExtent.width && info->height <= ip.maxExtent.height &&
           (uint32_t)info->levels <= ip.maxMipLevels;
}

// The full size atlas of a slot, else the _lo_ one (or the other way round
// with preferLow); NULL data when neither can be used
static int pickAtlas(const MlxrGpu* gpu, const char* dir, const char* name, int slot, int preferLow, AtlasLoad* out) {
    memset(out, 0, sizeof(*out));
    for (int attempt = 0; attempt < 2; attempt++) {
        int low = preferLow ? attempt == 0 : attempt == 1;
        char path[1024];
        snprintf(path, sizeof(path), "%s/rooms/%s%s_%d.atlas", dir, name, low ? "_lo" : "", slot);
        size_t size = 0;
        unsigned char* data = readFile(path, &size);
        if (!data) {
            continue;
        }
        AtlasInfo info;
        VkFormat format = VK_FORMAT_UNDEFINED;
        if (atlasParse(data, size, &info)) {
            format = mlxr_room_atlas_format(info.blockWidth);
        }
        if (format != VK_FORMAT_UNDEFINED && info.blockWidth == info.blockHeight && atlasSampleable(gpu, format, &info)) {
            out->data = data;
            out->size = size;
            out->info = info;
            out->format = format;
            out->low = low;
            return 0;
        }
        fprintf(stderr, "room atlas %s cannot be used here\n", path);
        free(data);
    }
    return -1;
}

static void destroyRoom(const MlxrGpu* gpu, MlxrRoom* r) {
    if (!r) {
        return;
    }
    VkDevice d = gpu->device;
    mlxr_room_upload_done(gpu, r);
    for (uint32_t i = 0; i < ROOM_MESH_ATLASES_MAX; i++) {
        if (r->atlas[i].view) vkDestroyImageView(d, r->atlas[i].view, NULL);
        if (r->atlas[i].image) vkDestroyImage(d, r->atlas[i].image, NULL);
        if (r->atlas[i].memory) vkFreeMemory(d, r->atlas[i].memory, NULL);
    }
    if (r->vertices) vkDestroyBuffer(d, r->vertices, NULL);
    if (r->indices) vkDestroyBuffer(d, r->indices, NULL);
    if (r->bufferMemory) vkFreeMemory(d, r->bufferMemory, NULL);
    free(r->modelVerts);
    free(r);
}

void mlxr_room_upload_done(const MlxrGpu* gpu, MlxrRoom* r) {
    if (r->staging) vkDestroyBuffer(gpu->device, r->staging, NULL);
    if (r->stagingMemory) vkFreeMemory(gpu->device, r->stagingMemory, NULL);
    r->staging = VK_NULL_HANDLE;
    r->stagingMemory = VK_NULL_HANDLE;
}

void mlxr_room_unload(const MlxrGpu* gpu, MlxrRoom* room) {
    destroyRoom(gpu, room);
}

// Builds the vertex data of a room at a scale into `verts`
static void buildRoomState(MlxrRoom* r, float scale, float* verts) {
    r->scale = scale;
    r->params = roomParams(r->style, scale);
    r->reach = roomBuildVertices(&r->params, scale, r->modelVerts, (int)r->modelVertexCount, verts);
    r->farZ = roomFarPlane(&r->params, r->reach);
    r->spillGain = r->params.spillGain;
    r->texMix = r->params.texMix;
    r->dim = r->params.dim;
}

int mlxr_room_record(const MlxrGpu* gpu, const MlxrRoomLoadInfo* info, VkCommandBuffer cmd, MlxrRoom** out) {
    double started = nowMs();
    *out = NULL;
    const char* name = roomFileName(info->style);
    if (!name || !info->assetsDir) {
        return -1;
    }
    MlxrRoom* r = calloc(1, sizeof(*r));
    AtlasLoad al[ROOM_MESH_ATLASES_MAX];
    memset(al, 0, sizeof(al));
    unsigned char* file = NULL;
    float* verts = NULL;
    void* map = NULL;
    if (!r) {
        return -1;
    }
    r->style = info->style;
    int rc = -1;

    char path[1024];
    snprintf(path, sizeof(path), "%s/rooms/%s.room", info->assetsDir, name);
    size_t fileSize = 0;
    file = readFile(path, &fileSize);
    RoomMeshInfo mi;
    if (!file || !roomMeshParse(file, fileSize, &mi)) {
        fprintf(stderr, "room mesh %s is not a mesh the renderer reads\n", path);
        goto done;
    }
    r->vertexCount = mi.vertexCount;
    r->indexCount = mi.indexCount;
    r->partCount = mi.partCount;
    r->atlasCount = mi.atlasCount;
    memcpy(r->meshParts, mi.parts, mi.partCount * sizeof(RoomMeshPart));

    // The model's vertices stay in its own space; the anchor and the scale go
    // on as the buffer is built
    r->modelVertexCount = mi.vertexCount;
    r->modelVerts = malloc((size_t)mi.vertexCount * ROOM_MODEL_FLOATS * sizeof(float));
    verts = malloc((size_t)mi.vertexCount * ROOM_VERTEX_FLOATS * sizeof(float));
    if (!r->modelVerts || !verts) {
        goto done;
    }
    memcpy(r->modelVerts, file + mi.vertexOffset, (size_t)mi.vertexCount * ROOM_MODEL_FLOATS * sizeof(float));
    buildRoomState(r, info->scale > 0.0f ? info->scale : roomScaleFor(0.0f, info->style), verts);
    r->clear[0] = ROOM_CLEAR_R;
    r->clear[1] = ROOM_CLEAR_G;
    r->clear[2] = ROOM_CLEAR_B;

    // Atlases: choose, read, parse
    for (uint32_t i = 0; i < mi.atlasCount; i++) {
        if (pickAtlas(gpu, info->assetsDir, name, (int)i, info->preferLowAtlas, &al[i]) != 0) {
            fprintf(stderr, "room %s: no usable atlas in slot %u (ASTC sampled support missing?)\n", name, i);
            goto done;
        }
    }

    // Staging layout: vertices, indices, then every atlas level
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu->phys, &props);
    uint64_t copyAlign = props.limits.optimalBufferCopyOffsetAlignment;
    if (copyAlign < 16) copyAlign = 16;
    r->vertexBytes = (uint64_t)mi.vertexCount * ROOM_VERTEX_FLOATS * sizeof(float);
    r->indexBytes = (uint64_t)mi.indexCount * sizeof(uint32_t);
    uint64_t ioff = alignUp(r->vertexBytes, 16);
    uint64_t total = alignUp(ioff + r->indexBytes, copyAlign);
    for (uint32_t i = 0; i < mi.atlasCount; i++) {
        for (int l = 0; l < al[i].info.levels; l++) {
            al[i].stageOffset[l] = total;
            total = alignUp(total + al[i].info.lengths[l], copyAlign);
        }
    }
    if (makeStaging(gpu, total, &r->staging, &r->stagingMemory, &map) != 0) {
        goto done;
    }
    memcpy(map, verts, r->vertexBytes);
    memcpy((char*)map + ioff, file + mi.indexOffset, r->indexBytes);
    for (uint32_t i = 0; i < mi.atlasCount; i++) {
        for (int l = 0; l < al[i].info.levels; l++) {
            memcpy((char*)map + al[i].stageOffset[l], al[i].data + al[i].info.offsets[l], al[i].info.lengths[l]);
        }
    }
    vkUnmapMemory(gpu->device, r->stagingMemory);

    // Device local buffers
    if (makeMeshBuffers(gpu, r->vertexBytes, r->indexBytes, &r->vertices, &r->indices, &r->bufferMemory, &r->gpuBytes) !=
        0) {
        goto done;
    }
    recordMeshCopy(cmd, r->staging, ioff, r->vertices, r->vertexBytes, r->indices, r->indexBytes);

    // Atlases: every mip level, as the file has them
    for (uint32_t i = 0; i < mi.atlasCount; i++) {
        MlxrRoomAtlas* a = &r->atlas[i];
        const AtlasInfo* ai = &al[i].info;
        a->format = al[i].format;
        a->width = ai->width;
        a->height = ai->height;
        a->levels = (uint32_t)ai->levels;
        a->blockWidth = ai->blockWidth;
        a->blockHeight = ai->blockHeight;
        a->low = al[i].low;
        a->fileBytes = al[i].size;
        VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = a->format;
        ci.extent = (VkExtent3D){a->width, a->height, 1};
        ci.mipLevels = a->levels;
        ci.arrayLayers = 1;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (vkCreateImage(gpu->device, &ci, NULL, &a->image) != VK_SUCCESS) {
            goto done;
        }
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(gpu->device, a->image, &req);
        VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemory(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
        if (mai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(gpu->device, &mai, NULL, &a->memory) != VK_SUCCESS) {
            goto done;
        }
        vkBindImageMemory(gpu->device, a->image, a->memory, 0);
        a->gpuBytes = req.size;
        r->gpuBytes += req.size;
        VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = a->image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = a->format;
        vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, a->levels, 0, 1};
        if (vkCreateImageView(gpu->device, &vi, NULL, &a->view) != VK_SUCCESS) {
            goto done;
        }

        VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = a->image;
        b.subresourceRange = vi.subresourceRange;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                             &b);
        VkBufferImageCopy regions[ATLAS_LEVELS_MAX];
        for (uint32_t l = 0; l < a->levels; l++) {
            regions[l] = (VkBufferImageCopy){.bufferOffset = al[i].stageOffset[l],
                                             .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, l, 0, 1},
                                             .imageExtent = {atlasLevelSize(a->width, (int)l),
                                                             atlasLevelSize(a->height, (int)l), 1}};
        }
        vkCmdCopyBufferToImage(cmd, r->staging, a->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, a->levels, regions);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL,
                             1, &b);
    }

    // The draw parts, as upstream's drawRoomEyes walks them: a textured part
    // from its atlas at the room's mix, a painted one from its colours
    for (uint32_t i = 0; i < r->partCount; i++) {
        const RoomMeshPart* p = &r->meshParts[i];
        int textured = p->atlas >= 0;
        r->parts[i].firstIndex = p->firstIndex;
        r->parts[i].indexCount = p->indexCount;
        r->parts[i].atlas = textured ? r->atlas[p->atlas].view : VK_NULL_HANDLE;
        r->parts[i].texMix = textured ? r->texMix : 0.0f;
    }
    r->loadMs = nowMs() - started;
    *out = r;
    r = NULL;
    rc = 0;
done:
    for (int i = 0; i < ROOM_MESH_ATLASES_MAX; i++) {
        free(al[i].data);
    }
    free(file);
    free(verts);
    if (r) {
        destroyRoom(gpu, r);
    }
    return rc;
}

int mlxr_room_load(const MlxrGpu* gpu, const MlxrRoomLoadInfo* info, MlxrRoom** out) {
    double started = nowMs();
    *out = NULL;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    int rc = -1;
    MlxrRoom* r = NULL;
    if (beginSync(gpu, &pool, &cmd) != 0) {
        goto done;
    }
    if (mlxr_room_record(gpu, info, cmd, &r) != 0) {
        goto done;
    }
    if (submitSync(gpu, pool, cmd) != 0) {
        destroyRoom(gpu, r);
        goto done;
    }
    mlxr_room_upload_done(gpu, r);
    r->loadMs = nowMs() - started;
    *out = r;
    rc = 0;
done:
    if (pool) {
        vkDestroyCommandPool(gpu->device, pool, NULL);
    }
    return rc;
}

int mlxr_room_rescale(const MlxrGpu* gpu, MlxrRoom* r, float scale, VkCommandBuffer cmd) {
    if (r->staging || scale <= 0.0f) {
        return -1;
    }
    float* verts = malloc((size_t)r->vertexCount * ROOM_VERTEX_FLOATS * sizeof(float));
    if (!verts) {
        return -1;
    }
    void* map = NULL;
    buildRoomState(r, roomScaleFor(scale, r->style), verts);
    int rc = -1;
    if (makeStaging(gpu, r->vertexBytes, &r->staging, &r->stagingMemory, &map) == 0) {
        memcpy(map, verts, r->vertexBytes);
        vkUnmapMemory(gpu->device, r->stagingMemory);
        VkBufferCopy cv = {0, 0, r->vertexBytes};
        vkCmdCopyBuffer(cmd, r->staging, r->vertices, 1, &cv);
        bufferToVertexInput(cmd, r->vertices, 0);
        rc = 0;
    }
    free(verts);
    return rc;
}

void mlxr_room_fill(const MlxrRoom* r, MlxrRoomParams* out) {
    out->vertices = r->vertices;
    out->indices = r->indices;
    out->parts = r->parts;
    out->partCount = r->partCount;
}

// ---------------------------------------------------------------- model

static void destroyModel(const MlxrGpu* gpu, MlxrModel* m) {
    if (!m) {
        return;
    }
    mlxr_model_upload_done(gpu, m);
    if (m->vertices) vkDestroyBuffer(gpu->device, m->vertices, NULL);
    if (m->indices) vkDestroyBuffer(gpu->device, m->indices, NULL);
    if (m->bufferMemory) vkFreeMemory(gpu->device, m->bufferMemory, NULL);
    free(m);
}

void mlxr_model_upload_done(const MlxrGpu* gpu, MlxrModel* m) {
    if (m->staging) vkDestroyBuffer(gpu->device, m->staging, NULL);
    if (m->stagingMemory) vkFreeMemory(gpu->device, m->stagingMemory, NULL);
    m->staging = VK_NULL_HANDLE;
    m->stagingMemory = VK_NULL_HANDLE;
}

void mlxr_model_unload(const MlxrGpu* gpu, MlxrModel* m) {
    destroyModel(gpu, m);
}

int mlxr_model_record(const MlxrGpu* gpu, const char* assetsDir, VkCommandBuffer cmd, MlxrModel** out) {
    double started = nowMs();
    *out = NULL;
    char path[1024];
    snprintf(path, sizeof(path), "%s/models/controller.room", assetsDir);
    size_t size = 0;
    unsigned char* file = readFile(path, &size);
    RoomMeshInfo mi;
    MlxrModel* m = NULL;
    int rc = -1;
    if (!file || !roomMeshParse(file, size, &mi)) {
        fprintf(stderr, "controller model %s is not a mesh the renderer reads\n", path);
        goto done;
    }
    // Drawn in one go from its colours, so a part asking for an atlas would
    // come out wrong rather than missing (nativeUploadControllerModel)
    if (mi.atlasCount != 0 || roomMeshPaintedParts(&mi) != (int)mi.partCount) {
        fprintf(stderr, "controller model asks for %u atlases, it is drawn from its colours alone\n", mi.atlasCount);
        goto done;
    }
    m = calloc(1, sizeof(*m));
    if (!m) {
        goto done;
    }
    m->vertexCount = mi.vertexCount;
    m->indexCount = mi.indexCount;
    m->vertexBytes = (uint64_t)mi.vertexCount * ROOM_MESH_VERTEX_FLOATS * sizeof(float);
    m->indexBytes = (uint64_t)mi.indexCount * sizeof(uint32_t);
    uint64_t ioff = alignUp(m->vertexBytes, 16);
    void* map = NULL;
    if (makeStaging(gpu, ioff + m->indexBytes, &m->staging, &m->stagingMemory, &map) != 0) {
        goto done;
    }
    memcpy(map, file + mi.vertexOffset, m->vertexBytes);
    memcpy((char*)map + ioff, file + mi.indexOffset, m->indexBytes);
    vkUnmapMemory(gpu->device, m->stagingMemory);
    if (makeMeshBuffers(gpu, m->vertexBytes, m->indexBytes, &m->vertices, &m->indices, &m->bufferMemory, &m->gpuBytes) != 0) {
        goto done;
    }
    recordMeshCopy(cmd, m->staging, ioff, m->vertices, m->vertexBytes, m->indices, m->indexBytes);
    m->loadMs = nowMs() - started;
    *out = m;
    m = NULL;
    rc = 0;
done:
    free(file);
    destroyModel(gpu, m);
    return rc;
}

int mlxr_model_load(const MlxrGpu* gpu, const char* assetsDir, MlxrModel** out) {
    double started = nowMs();
    *out = NULL;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    MlxrModel* m = NULL;
    int rc = -1;
    if (beginSync(gpu, &pool, &cmd) != 0 || mlxr_model_record(gpu, assetsDir, cmd, &m) != 0) {
        goto done;
    }
    if (submitSync(gpu, pool, cmd) != 0) {
        destroyModel(gpu, m);
        goto done;
    }
    mlxr_model_upload_done(gpu, m);
    m->loadMs = nowMs() - started;
    *out = m;
    rc = 0;
done:
    if (pool) {
        vkDestroyCommandPool(gpu->device, pool, NULL);
    }
    return rc;
}

void mlxr_model_fill(const MlxrModel* m, MlxrModelParams* out) {
    out->vertices = m->vertices;
    out->indices = m->indices;
    out->indexCount = m->indexCount;
}
