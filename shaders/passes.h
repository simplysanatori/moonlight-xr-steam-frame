// Moonlight XR Native: mlxr_passes, the Vulkan ports of Moonlight XR's GL ES
// programs (xr_shaders.c) as pipelines, and plain C calls that record each
// pass into a command buffer from a parameter struct. GPLv3.
//
// Device requirements: Vulkan 1.2 with VK_KHR_dynamic_rendering and
// VK_KHR_push_descriptor enabled; samplerYcbcrConversion enabled when the
// NV12 variants are built (info.nv12); samplerAnisotropy when
// info.atlasAnisotropy > 1.
//
// State: everything lives in the MlxrPasses object; there are no globals.
// The object is not thread safe: record from one thread at a time.
// Pipelines are built on first use of a (pass, video kind, target format)
// combination and kept; mlxr_passes_warm() builds one ahead of time.
//
// Layouts the record calls expect (the caller owns every barrier, see
// mlxr_barrier()):
//   every sampled input   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
//   every colour target   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
//   every depth target    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL (no
//                         separateDepthStencilLayouts needed)
// Each record call begins and ends its own dynamic rendering instance, so
// barriers go between calls, never inside one.
//
// Orientation: row 0 of every image is the top of the picture, the way the
// decoder writes it and the Vulkan compositor reads it. Upstream's GL images
// were bottom row first, and its passes read and wrote the same memory rows
// as these do, so no pass flips anything (see docs/dev/reports/shaders.md).
#pragma once
#include <stdint.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

// The colour sample the ambilight and the room's light come from, and the
// glow image (xr_glow.h AMBI_SAMPLE_TEX and GLOW_TEX)
#define MLXR_AMBI_TEX 32
#define MLXR_GLOW_TEX 256
// Floats a vertex in the room's buffer (upstream ROOM_VERTEX_FLOATS) and in
// the controller model's (ROOM_MESH_VERTEX_FLOATS)
#define MLXR_ROOM_VERTEX_FLOATS 10
#define MLXR_MODEL_VERTEX_FLOATS 11
// New: a vertex of the textured controller model (tools/frame-models, MXFM):
// position 3, normal 3, tangent 4 (w the bitangent's sign), uv 2
#define MLXR_MODEL_TEX_VERTEX_FLOATS 12
// The format of every intermediate a pass writes and a later pass samples:
// downscale, upsample, offset, ambi and glow edge, as upstream's GL_RGBA8
#define MLXR_PASS_FORMAT VK_FORMAT_R8G8B8A8_UNORM

typedef struct MlxrPasses MlxrPasses;

typedef enum MlxrVideoKind {
    MLXR_VIDEO_YUV420P = 0, // three R8_UNORM planes, Y full size, U and V half (software decode)
    MLXR_VIDEO_NV12 = 1,    // G8_B8R8_2PLANE_420_UNORM through the library's VkSamplerYcbcrConversion
} MlxrVideoKind;

// Where the 4:2:0 chroma samples sit. LEFT is H.264/HEVC's default
// (chroma_sample_loc_type 0: co-sited with the even luma column, midway
// between rows); CENTER is midway on both axes (what xr/shaders/yuv420p.frag
// in M1 assumes).
typedef enum MlxrChroma {
    MLXR_CHROMA_LEFT = 0,
    MLXR_CHROMA_CENTER = 1,
} MlxrChroma;

typedef struct MlxrPassesInfo {
    VkPhysicalDevice phys;
    VkDevice device;
    VkPipelineCache cache;       // may be VK_NULL_HANDLE
    int nv12;                    // also build the NV12 variants (VkSamplerYcbcrConversion)
    MlxrChroma nv12Chroma;       // chroma siting the NV12 conversion assumes
    float atlasAnisotropy;       // room atlas sampler; 0 or 1 is off
} MlxrPassesInfo;

int mlxr_passes_create(const MlxrPassesInfo* info, MlxrPasses** out);
void mlxr_passes_destroy(MlxrPasses* p);

// Once, before the first pass is recorded: clears the 1x1 white image the
// library stands in for a missing input (a mono warp's depth, a room part
// with no atlas) and moves it to SHADER_READ_ONLY_OPTIMAL.
void mlxr_passes_prepare(MlxrPasses* p, VkCommandBuffer cmd);

// The NV12 conversion (NULL without info.nv12). Every image view sampled
// through the NV12 path must be made with it (VkSamplerYcbcrConversionInfo
// in the view's pNext); mlxr_passes_nv12_view() does that for a plain
// G8_B8R8_2PLANE_420_UNORM image.
VkSamplerYcbcrConversion mlxr_passes_nv12_conversion(const MlxrPasses* p);
VkResult mlxr_passes_nv12_view(const MlxrPasses* p, VkImage image, VkImageView* out);
// What the NV12 conversion ended up with (the device may refuse linear
// chroma filtering or a siting): VK_FILTER_*, VK_CHROMA_LOCATION_* x and y
void mlxr_passes_nv12_caps(const MlxrPasses* p, VkFilter* chromaFilter, VkChromaLocation* x, VkChromaLocation* y);

typedef enum MlxrPassId {
    MLXR_PASS_WARP = 0,     // FRAGMENT_SRC: colour pass, grade, tint, 3D warp, (new) sharpen
    MLXR_PASS_UPSAMPLE,     // UPSAMPLE_FRAGMENT_SRC
    MLXR_PASS_OFFSET,       // OFFSET_FRAGMENT_SRC
    MLXR_PASS_DOWNSCALE,    // DOWNSCALE_FRAGMENT_SRC
    MLXR_PASS_AMBI,         // AMBI_FRAGMENT_SRC ("frame colour sample")
    MLXR_PASS_GLOW_EDGE,    // GLOW_EDGE_FRAGMENT_SRC
    MLXR_PASS_GLOW,         // GLOW_FRAGMENT_SRC
    MLXR_PASS_ROOM,         // ROOM_VERTEX_SRC + ROOM_FRAGMENT_SRC
    MLXR_PASS_MODEL,        // MODEL_VERTEX_SRC + MODEL_FRAGMENT_SRC
    MLXR_PASS_SUPERSAMPLE,  // new: app side supersampling (no FB layer settings on the Frame)
    MLXR_PASS_PANEL,        // new: texture on a flat or curved surface (no cylinder layers on the Frame)
    MLXR_PASS_MODEL_TEX,    // new: the textured controller model (the Frame's own, mlxr_pass_model with textured)
    MLXR_PASS_COUNT
} MlxrPassId;

// Builds the pipeline a record call would build on first use. depthFormat
// matters for ROOM and MODEL only.
int mlxr_passes_warm(MlxrPasses* p, MlxrPassId pass, MlxrVideoKind kind, VkFormat colorFormat, VkFormat depthFormat);

// Where a pass draws
typedef struct MlxrTarget {
    VkImageView view;
    VkFormat format;      // the view's format: picks the pipeline, and an _SRGB format turns on OUT_LINEAR
    VkRect2D rect;        // render area, viewport and scissor
    int clear;            // clear the rect to clearColor before drawing (else load)
    float clearColor[4];
} MlxrTarget;

typedef struct MlxrVideo {
    MlxrVideoKind kind;
    VkImageView planes[3];       // YUV420P: Y, U, V. NV12: planes[0], made with the NV12 conversion
    uint32_t width, height;      // the picture, in luma pixels
    uint32_t texWidth, texHeight; // the luma allocation when the decoder pads it (0: same as the picture)
    MlxrChroma chroma;           // YUV420P only (NV12's is fixed in the conversion)
} MlxrVideo;

// FRAGMENT_SRC. One eye per call: target.rect picks the half of a double
// wide image, as upstream's glViewport(eye * videoWidth, ...) did.
typedef struct MlxrWarpParams {
    MlxrVideo video;
    VkImageView depth;     // u_depth: RGBA8, depth in alpha (the raw map or the upsample); NULL: white
    VkImageView offsets;   // u_offsets: the offset pass's output; NULL: white (only read with occlusion)
    float disparity;       // u_disparity: +separation left eye, -separation right, 0 mono
    int eye;               // u_eyeIndex
    float convergence;     // u_convergence
    float dispTexels;      // u_dispTexels: separation * upsample width
    float lowResWidth;     // u_lowResWidth: the upsample's width
    float srcInset;        // u_srcInset: 0.5 / video width, or -1 for no hold
    float edgeFade;        // u_edgeFade: edge fade px / video width
    int occlusion;         // u_occlusion
    int depthCubic;        // u_depthCubic
    int showDepth;         // u_showDepth
    int barTest;           // u_barTest
    int gradeOn;           // u_gradeOn
    float grade[4];        // u_grade: PictureGrade offset, contrast, exponent, saturation
    float tint[3];         // u_tint, {1, 1, 1} normally
    float sharpen;         // new: 0 off (upstream), up to 1
} MlxrWarpParams;
void mlxr_pass_warp(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrWarpParams* w);

// UPSAMPLE_FRAGMENT_SRC, into a MLXR_PASS_FORMAT image a quarter of the video
// on each axis, every video frame
typedef struct MlxrUpsampleParams {
    MlxrVideo video;
    VkImageView depth;     // the map: RGBA8, guide colour rgb, depth a
    uint32_t depthWidth, depthHeight; // its size (u_depthSize)
    float sigmaR;          // u_sigmaR
    float sharp;           // u_sharp
} MlxrUpsampleParams;
void mlxr_pass_upsample(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrUpsampleParams* u);

// OFFSET_FRAGMENT_SRC, both eyes into one MLXR_PASS_FORMAT image the
// upsample's size
typedef struct MlxrOffsetParams {
    VkImageView depth;     // the upsample's output
    float dispTexels;      // u_dispTexels
    float convergence;     // u_convergence
} MlxrOffsetParams;
void mlxr_pass_offset(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrOffsetParams* o);

// DOWNSCALE_FRAGMENT_SRC, into a MLXR_PASS_FORMAT image the depth map's size
// (u_texel comes from t->rect)
void mlxr_pass_downscale(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrVideo* v);

// AMBI_FRAGMENT_SRC, into the 32x32 MLXR_PASS_FORMAT colour sample. The new
// sample is mixed into what the target holds by `blend` (upstream's
// ambiSmooth via glBlendColor); 1 replaces it, which is the first frame and
// the letterbox detector. With blend < 1 the target must hold the old sample
// (t->clear 0).
typedef struct MlxrAmbiParams {
    MlxrVideo video;
    float crop[4];         // u_crop x0 y0 w h, {0, 0, 1, 1} for the whole frame
    int gradeOn;
    float grade[4];
    float blend;
} MlxrAmbiParams;
void mlxr_pass_ambi(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrAmbiParams* a);

// GLOW_EDGE_FRAGMENT_SRC, the colour sample into a 32x32 MLXR_PASS_FORMAT
// copy. Upstream's constants: luma {GLOW_LUMA_TARGET, GLOW_LUMA_FLOOR,
// GLOW_LUMA_FULL}, lit {GLOW_LIT_LO, GLOW_LIT_HI}, reach glowReachTexels(),
// steps glowInwardSteps() (xr/core/xr_glow.h).
typedef struct MlxrGlowEdgeParams {
    VkImageView sample;
    float luma[3];
    float lit[2];
    float reach;
    int steps;
} MlxrGlowEdgeParams;
void mlxr_pass_glow_edge(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrGlowEdgeParams* g);

// GLOW_FRAGMENT_SRC, into the glow swapchain image (premultiplied)
typedef struct MlxrGlowParams {
    VkImageView sample;    // the glow edge copy (blur 1) or the raw colour sample (blur 0)
    float intensity;       // u_intensity
    int blur;              // u_blur
} MlxrGlowParams;
void mlxr_pass_glow(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrGlowParams* g);

// ROOM_*_SRC: both eyes into one image (target.rect the whole of it), each
// part from its own atlas or its vertex colours, cleared to clearColor and
// depth 1 first (t->clear is ignored: the room always clears).
typedef struct MlxrRoomPart {
    uint32_t firstIndex, indexCount;
    VkImageView atlas;     // ASTC UNORM with mips; NULL: the white stand in
    float texMix;          // u_texMix: the room's mix for a textured part, 0 for a painted one
} MlxrRoomPart;
typedef struct MlxrRoomParams {
    VkImageView ambi;      // the colour sample
    VkBuffer vertices;     // MLXR_ROOM_VERTEX_FLOATS floats a vertex
    VkBuffer indices;      // uint32
    const MlxrRoomPart* parts;
    uint32_t partCount;
    int eyes;              // 1 or 2
    float viewProj[2][16]; // per eye, column major, GL clip conventions (projectionFromFov * viewFromPose)
    VkRect2D eyeRect[2];
    float spillGain;       // u_spillGain (0 when the room is unlit)
    float dim;             // u_dim
    VkImageView depth;     // depth attachment covering target.rect
    VkFormat depthFormat;
} MlxrRoomParams;
void mlxr_pass_room(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrRoomParams* r);

// MODEL_*_SRC: both eyes, each shown hand at its grip, over a clear to
// nothing (premultiplied as it stands), with its own depth.
//
// Moonlight XR Native: textured 1 draws the Steam Frame's own controllers as
// SteamVR ships them (MLXR_PASS_MODEL_TEX, MLXR_MODEL_TEX_VERTEX_FLOATS a
// vertex): each hand samples its colour image (an _SRGB format, the ambient
// occlusion in alpha) and its surface image (UNORM: tangent space normal,
// OpenGL green, roughness in alpha), both with mips, lit by a key light and a
// sky/ground fill fixed in the room, with GGX highlights and a reflection of
// that sky, so the near black plastic still shows its shape. A hand's parts
// (handPartCount > 0) are drawn one by one, each moved by its own transform in
// grip space (the moving buttons); none: the hand's whole range at once.
typedef struct MlxrModelPart {
    uint32_t firstIndex, indexCount; // into the index buffer (absolute)
    float transform[16];             // column major, grip space; rigid
} MlxrModelPart;
typedef struct MlxrModelParams {
    VkBuffer vertices;     // MLXR_MODEL_VERTEX_FLOATS floats a vertex
    VkBuffer indices;
    uint32_t indexCount;
    // Moonlight XR Native: perHand 0 (zeroed params): one mesh drawn for both
    // hands, indexCount of it. perHand 1: each hand its own range of the
    // buffers (first index, index count, vertex offset in vertices).
    int perHand;
    uint32_t handFirstIndex[2];
    uint32_t handIndexCount[2];
    int32_t handVertexOffset[2];
    int eyes;
    float viewProj[2][16];
    VkRect2D eyeRect[2];
    int shown[2];          // per hand
    float model[2][16];    // per hand, controllerModelMatrix()
    VkImageView depth;
    VkFormat depthFormat;
    // Moonlight XR Native: the textured path (perHand must be 1)
    int textured;
    VkImageView handColour[2];  // R8G8B8A8_SRGB: base colour, AO in alpha
    VkImageView handSurface[2]; // R8G8B8A8_UNORM: normal xyz, roughness in alpha
    uint32_t handPartCount[2];
    const MlxrModelPart* handParts[2];
    int debugView;              // 0 lit; 1 normals; 2 lit without the normal map; 3 base colour * AO
    float gain;                 // the base colour times this in linear light (0: 1)
    // Native: draw over what the target holds rather than clearing it to
    // nothing (the models in another layer's image: one projection layer a
    // frame, xr_world.c xrWorldPlanLayers). The depth is cleared either way.
    int over;
} MlxrModelParams;
void mlxr_pass_model(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrModelParams* m);

// New: an image drawn smaller than it is, filtered over each destination
// pixel's footprint. taps 2 normal, 4 quality.
typedef struct MlxrSupersampleParams {
    VkImageView src;
    float srcRect[4];      // region of src in uv
    int taps;
} MlxrSupersampleParams;
void mlxr_pass_supersample(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrSupersampleParams* s);

// New: a premultiplied texture on a flat (radius 0) or cylindrical surface,
// blended over what the target holds, per eye
typedef struct MlxrPanelParams {
    VkImageView src;
    int eyes;
    float mvp[2][16];      // per eye viewProj * model (model: the surface's pose), GL conventions
    VkRect2D eyeRect[2];
    float uvRect[2][4];    // per eye region of src (a stereo picture's halves)
    float width, height, radius;
    int segments;          // across; 1 is enough for a flat one
    float color[4];        // premultiplied multiplier, {1, 1, 1, 1} normally
} MlxrPanelParams;
void mlxr_pass_panel(MlxrPasses* p, VkCommandBuffer cmd, const MlxrTarget* t, const MlxrPanelParams* pp);

// An image barrier whose stages and access masks follow from the two
// layouts (UNDEFINED, TRANSFER_DST/SRC, COLOR_ATTACHMENT,
// DEPTH_STENCIL_ATTACHMENT, SHADER_READ_ONLY; anything else as all commands)
void mlxr_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, VkImageLayout from,
                  VkImageLayout to);

#ifdef __cplusplus
}
#endif
