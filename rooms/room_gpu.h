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
// from the local SteamVR install, in each hand's grip space, not mirrored:
// both meshes in the one pair of buffers. The pair is textured
// (frame_controller_{left,right}.model, MXFM: full meshes, per component
// ranges and motions, colour + AO and normal + roughness images, mipped here
// on the GPU) or, from an older converter, painted (frame_controller_{left,
// right}.room).
#define MLXR_MODEL_COMPONENTS_MAX 32
#define MLXR_MODEL_MOTION_FLOATS 32
#define MLXR_MODEL_NAME_BYTES 32
// The motion floats (tools/frame-models MOTION_*), all in grip space
#define MLXR_MOTION_ORIGIN 0   // 3: rotate pivot, joystick centre
#define MLXR_MOTION_AXIS 3     // 3: translate direction, rotate axis
#define MLXR_MOTION_RANGE 6    // 2: offset (m) or angle (deg) at input 0 and 1
#define MLXR_MOTION_PRESSED 8  // 1: angle a click forces, NaN for none
#define MLXR_MOTION_FRAME 9    // 9: joystick frame, columns x y z
#define MLXR_MOTION_ROT_X 18   // 2: joystick degrees at x = -1, +1
#define MLXR_MOTION_ROT_Y 20   // 2: joystick degrees at y = -1, +1
#define MLXR_MOTION_PRESS 22   // 3: joystick click offset
enum { MLXR_MOTION_NONE = 0, MLXR_MOTION_TRANSLATE = 1, MLXR_MOTION_ROTATE = 2, MLXR_MOTION_JOYSTICK = 3,
       MLXR_MOTION_DPAD = 4 /* MXFM 2 */ };

// Native: the controls a component can follow, as the client reads them off the
// actions each frame (0 to 1; the stick -1 to 1). A control the hand does not
// have, or the runtime does not give the app (the system button), stays 0.
enum {
    MLXR_IN_TRIGGER, MLXR_IN_GRIP, MLXR_IN_STICK_X, MLXR_IN_STICK_Y, MLXR_IN_STICK_CLICK,
    MLXR_IN_A, MLXR_IN_B, MLXR_IN_X, MLXR_IN_Y, MLXR_IN_MENU, MLXR_IN_VIEW, MLXR_IN_SYSTEM, MLXR_IN_BUMPER,
    MLXR_IN_DPAD_UP, MLXR_IN_DPAD_DOWN, MLXR_IN_DPAD_LEFT, MLXR_IN_DPAD_RIGHT,
    MLXR_IN_COUNT
};
typedef struct MlxrModelInput {
    float v[MLXR_IN_COUNT];
} MlxrModelInput;

typedef struct MlxrModelComponent {
    char name[MLXR_MODEL_NAME_BYTES];  // SteamVR's ("body", "trigger", "button_a", ...)
    char path[MLXR_MODEL_NAME_BYTES];  // the input that moves it ("/input/trigger"), "" for none
    uint32_t firstIndex, indexCount;   // into the model's index buffer (absolute)
    uint32_t motionType;               // MLXR_MOTION_*
    int input;                         // MLXR_IN_* that drives it (the stick's x, the d-pad's up), -1 none
    float motion[MLXR_MODEL_MOTION_FLOATS];
} MlxrModelComponent;

// Where everything sits in a .model file (MXFM version 1)
typedef struct MlxrModelFileInfo {
    uint32_t vertexCount, indexCount, componentCount;
    uint32_t version;                  // 1, or 2 (the d-pad's motion)
    uint32_t texWidth, texHeight;
    uint64_t componentOffset, vertexOffset, indexOffset, colourOffset, surfaceOffset, size;
} MlxrModelFileInfo;
// 1 when the first headBytes (at least 32) of a file fileBytes long start a
// model this reads, and the sizes add up to exactly fileBytes
int mlxr_model_file_parse(const unsigned char* head, size_t headBytes, uint64_t fileBytes, MlxrModelFileInfo* out);

typedef struct MlxrModel {
    VkBuffer vertices;
    VkBuffer indices;
    uint32_t vertexCount, indexCount; // both hands' together when perHand
    uint64_t vertexBytes, indexBytes, gpuBytes;
    double loadMs;
    int perHand;                      // 1: the Frame's pair; 0: the generic one for both
    // Native: which CONTROLLER_MODEL_* this is (what loaded, which is not the kind asked for when
    // that fell back), where it is drawn (MODEL_SPACE_*) and whether the left hand is the right's
    // mesh mirrored by its matrix (the generic controller, a single custom file) rather than its
    // own
    int kind;
    int space;
    int mirrorLeft;
    uint32_t handFirstIndex[2];       // per hand into the index buffer
    uint32_t handIndexCount[2];
    int32_t handVertexOffset[2];      // per hand, in vertices
    char source[1100];                // what was read, for the log
    // Native: the textured pair (perHand too)
    int textured;
    uint32_t texWidth, texHeight, texLevels;
    MlxrRoomAtlas texture[2][2];      // per hand: colour (R8G8B8A8_SRGB), surface (R8G8B8A8_UNORM)
    uint32_t componentCount[2];
    MlxrModelComponent components[2][MLXR_MODEL_COMPONENTS_MAX];
    VkDeviceMemory bufferMemory;
    VkBuffer staging;
    VkDeviceMemory stagingMemory;
} MlxrModel;

// Tries the Frame pair under assetsDir/models first (textured, then painted);
// a pair with a missing or unreadable half is skipped (on stderr) for the
// generic controller. The same as mlxr_model_record_kind for the Frame kind.
int mlxr_model_record(const MlxrGpu* gpu, const char* assetsDir, VkCommandBuffer cmd, MlxrModel** out);
// Native: the model of a CONTROLLER_MODEL_* kind (not off), chosen by the kind rather than by
// which files happen to be there: controllerModelCandidates (xr_controller.h) lists what to read
// and in what order, the first that loads is the model, and each one skipped is said on stderr.
// userDir is where the user's own models are (NULL: none). *out->kind says what loaded.
int mlxr_model_record_kind(const MlxrGpu* gpu, const char* assetsDir, const char* userDir, int kind,
                           VkCommandBuffer cmd, MlxrModel** out);
void mlxr_model_upload_done(const MlxrGpu* gpu, MlxrModel* model);
int mlxr_model_load(const MlxrGpu* gpu, const char* assetsDir, MlxrModel** out);
int mlxr_model_load_kind(const MlxrGpu* gpu, const char* assetsDir, const char* userDir, int kind, MlxrModel** out);
void mlxr_model_unload(const MlxrGpu* gpu, MlxrModel* model);
// vertices, indices, indexCount, the per-hand ranges and (textured) images of
// a pass_model call
void mlxr_model_fill(const MlxrModel* model, MlxrModelParams* out);
// Native: each component of a hand's model moved as SteamVR moves it for these
// control values (trigger and grip turn about their pivot, the stick tilts about
// its centre and dips when clicked, buttons slide, the d-pad tilts toward a
// direction): the parts for MlxrModelParams.handParts, in the file's order (parts at rest next to
// each other are one draw, so a hand at rest is a single range),
// up to cap. 0 for a model with no components (the generic one), which is drawn whole.
uint32_t mlxr_model_pose(const MlxrModel* model, int hand, const MlxrModelInput* in, MlxrModelPart* parts,
                         uint32_t cap);

// The ASTC UNORM format for a block size the atlas tool writes (4, 6 or 8),
// VK_FORMAT_UNDEFINED otherwise
VkFormat mlxr_room_atlas_format(uint32_t block);

#ifdef __cplusplus
}
#endif
