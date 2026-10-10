// Moonlight XR Native: the Vulkan side of the 3D rooms and the controller
// model. Loads a baked room (.room mesh + ASTC .atlas files) or the model off
// the assets dir into device local buffers and ASTC images, and hands the
// result to mlxr_pass_room / mlxr_pass_model as MlxrRoomParams /
// MlxrModelParams parts. Replaces upstream's nativeUploadRoomModel,
// nativeUploadRoomAtlas, uploadRoomGeometry and nativeUploadControllerModel.
// GPLv3.
//
// Threading: one load at a time per queue. mlxr_room_record only records into
// the command buffer and creates objects, so a loader thread can build a
// command buffer while the render thread draws the room already resident;
// the sync helpers submit on gpu->queue, which must not be used by another
// thread meanwhile.
#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "passes.h"
#include "room_logic.h"
#include "xr_roommesh.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MlxrGpu {
    VkPhysicalDevice phys;
    VkDevice device;
    uint32_t queueFamily;
    VkQueue queue; // only the *_load / *_upload sync helpers use it
} MlxrGpu;

typedef struct MlxrRoomLoadInfo {
    const char* assetsDir; // holds rooms/ and models/
    int style;             // ROOM_STYLE_*
    float scale;           // 0: roomScaleFor(0, style), the size it was built
    int preferLowAtlas;    // take the _lo_ atlases first (XR2 Gen 1); 0 on the Frame
} MlxrRoomLoadInfo;

typedef struct MlxrRoomAtlas {
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkFormat format;
    uint32_t width, height, levels;
    uint32_t blockWidth, blockHeight;
    int low;           // the _lo_ file
    uint64_t gpuBytes; // memory the image takes (vkGetImageMemoryRequirements)
    uint64_t fileBytes;
} MlxrRoomAtlas;

typedef struct MlxrRoom {
    int style;
    float scale;
    RoomParams params;
    // As upstream keeps them in XrCtx once uploadRoomGeometry has run
    float farZ, reach;
    float clear[3];       // roomClear
    float spillGain;      // roomSpillGain: the table's gain (times the light level when drawn)
    float texMix;         // roomTexMix
    float dim;            // roomDim: the table's start
    VkBuffer vertices;    // MLXR_ROOM_VERTEX_FLOATS floats a vertex
    VkBuffer indices;     // uint32
    uint32_t vertexCount, indexCount;
    uint32_t partCount;
    RoomMeshPart meshParts[ROOM_MESH_PARTS_MAX]; // the file's: atlas slot -1 for a painted part
    MlxrRoomPart parts[ROOM_MESH_PARTS_MAX];     // what mlxr_pass_room draws
    uint32_t atlasCount;
    MlxrRoomAtlas atlas[ROOM_MESH_ATLASES_MAX];
    // Stats
    uint64_t vertexBytes, indexBytes, gpuBytes; // gpuBytes: buffers + atlases, as the driver sizes them
    double loadMs;                              // file reads + parse + build + record (+ GPU wait for the sync helper)

    // private
    VkDeviceMemory bufferMemory;
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
    float* modelVerts; // the file's vertices, kept so the scale can change
    uint32_t modelVertexCount;
} MlxrRoom;

// Records the whole load into `cmd` (begun, outside a render pass, on
// gpu->queueFamily): buffers and images are created here, the copies and the
// barriers to VERTEX_INPUT / SHADER_READ_ONLY_OPTIMAL go into cmd. The staging
// memory stays alive until mlxr_room_upload_done() after cmd has finished.
// 0 on success, -1 on a missing/bad file, no ASTC, or a Vulkan failure (all
// of it freed). Atlas choice: the full size atlas unless preferLowAtlas, the
// _lo_ one if the full one cannot be sampled here or is not on disk.
int mlxr_room_record(const MlxrGpu* gpu, const MlxrRoomLoadInfo* info, VkCommandBuffer cmd, MlxrRoom** out);
void mlxr_room_upload_done(const MlxrGpu* gpu, MlxrRoom* room);
// record + submit + wait + upload_done on a private command pool
int mlxr_room_load(const MlxrGpu* gpu, const MlxrRoomLoadInfo* info, MlxrRoom** out);
void mlxr_room_unload(const MlxrGpu* gpu, MlxrRoom* room); // idle the GPU of it first

// Rebuilds the vertex buffer at another scale (the debug property) from the
// kept model vertices, recorded into cmd; update room->scale/params/farZ.
// The GPU must be done reading the buffer. Staging is again released by
// mlxr_room_upload_done.
int mlxr_room_rescale(const MlxrGpu* gpu, MlxrRoom* room, float scale, VkCommandBuffer cmd);

// Fills vertices, indices, parts, partCount of a pass_room call; the caller
// sets the rest (ambi, eyes, viewProj, eyeRect, spillGain, dim, depth)
void mlxr_room_fill(const MlxrRoom* room, MlxrRoomParams* out);

// The controller model: vertices as the file has them, painted from their
// colours. Moonlight XR Native: either the one generic controller
// (assets/models/controller.room, mirrored for the left hand) or, when both
// exist and parse, the Steam Frame's own pair that tools/frame-models makes
// from the local SteamVR install (frame_controller_{left,right}.room, in each
// hand's grip space, not mirrored): both meshes in the one pair of buffers.
typedef struct MlxrModel {
    VkBuffer vertices;
    VkBuffer indices;
    uint32_t vertexCount, indexCount; // both hands' together when perHand
    uint64_t vertexBytes, indexBytes, gpuBytes;
    double loadMs;
    int perHand;                      // 1: the Frame's pair; 0: the generic one for both
    uint32_t handFirstIndex[2];       // per hand into the index buffer
    uint32_t handIndexCount[2];
    int32_t handVertexOffset[2];      // per hand, in vertices
    char source[1100];                // what was read, for the log
    VkDeviceMemory bufferMemory;
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
} MlxrModel;

// Tries the Frame pair under assetsDir/models first; a pair with a missing or
// unreadable half is skipped (on stderr) for the generic controller.
int mlxr_model_record(const MlxrGpu* gpu, const char* assetsDir, VkCommandBuffer cmd, MlxrModel** out);
void mlxr_model_upload_done(const MlxrGpu* gpu, MlxrModel* model);
int mlxr_model_load(const MlxrGpu* gpu, const char* assetsDir, MlxrModel** out);
void mlxr_model_unload(const MlxrGpu* gpu, MlxrModel* model);
// vertices, indices, indexCount and the per-hand ranges of a pass_model call
void mlxr_model_fill(const MlxrModel* model, MlxrModelParams* out);

// The ASTC UNORM format for a block size the atlas tool writes (4, 6 or 8),
// VK_FORMAT_UNDEFINED otherwise
VkFormat mlxr_room_atlas_format(uint32_t block);

#ifdef __cplusplus
}
#endif
