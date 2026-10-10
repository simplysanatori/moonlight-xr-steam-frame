// The depth model on LiteRT (TensorFlow Lite) with the XNNPACK delegate on
// the CPU: ZipDepth or MiDaS v2.1 small, the models Moonlight XR ships
// (MidasDepthSource.java). Every model takes plain RGB in 0..1, NHWC, rows top
// first (the graph normalises it itself), and gives relative inverse depth on
// an arbitrary scale, larger is nearer; depth_engine.h turns that into the map
// the warp samples. Not thread safe: one thread owns a model. Its XNNPACK
// worker threads are made on the thread that loads it and inherit that
// thread's CPU affinity. GPLv3.

#ifndef MLXR_LITERT_BACKEND_H
#define MLXR_LITERT_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The shipped exports, all upstream's own conversions (tools/convert_*.py)
typedef enum MlxrDepthModelId {
    // ZipDepth (base head) at 16:9, fp16 weights. Upstream's Gen 2 GPU route.
    MLXR_DEPTH_ZIPDEPTH_512x288_FP16 = 0,
    // ZipDepth (npu head) at 256 square, int8 dynamic range weights.
    // Upstream's Gen 1 CPU route, 12 maps/s on an XR2 Gen 1.
    MLXR_DEPTH_ZIPDEPTH_256_INT8DR = 1,
    // MiDaS v2.1 small at 256 square, fp16 weights
    MLXR_DEPTH_MIDAS_256_FP16 = 2,
    MLXR_DEPTH_MODEL_COUNT
} MlxrDepthModelId;

typedef struct MlxrDepthModelSpec {
    MlxrDepthModelId id;
    const char* key;        // "zipdepth", "zipdepth-int8", "midas"
    const char* name;       // for logs: "ZipDepth 512x288 fp16"
    const char* file;       // file name under assets/depth/
    int width, height;      // the input and output size the export takes
    // Upstream's 3D pair for the model, in its preference units (tenths of a
    // percent of frame width, and percent): ZipDepth 6/.., MiDaS 5/..
    int defaultSeparation;
} MlxrDepthModelSpec;

const MlxrDepthModelSpec* mlxr_depth_model_spec(MlxrDepthModelId id);
// By key or file name; NULL when nothing matches
const MlxrDepthModelSpec* mlxr_depth_model_find(const char* keyOrFile);

// Flags for MlxrDepthModelOptions.flags
// Runs the float graph in fp16 arithmetic (XNNPACK FORCE_FP16). The A720 and
// X4 cores have fp16 vector arithmetic; see depth/REPORT.md for its cost in
// agreement with fp32.
#define MLXR_DEPTH_FP16 0x1

typedef struct MlxrDepthModelOptions {
    int threads;            // XNNPACK threads including the caller; <= 0 means 2
    unsigned flags;         // MLXR_DEPTH_*
} MlxrDepthModelOptions;

typedef struct MlxrDepthModel MlxrDepthModel;

// Reads the file (copied into memory, so the path may go away), builds the
// interpreter and allocates its tensors. Checks the input is 1xHxWx3 float
// and the output H x W floats with any axes of 1, as upstream's inputFits and
// outputFits do. NULL on failure with the reason in err.
MlxrDepthModel* mlxr_depth_model_load(const char* path, const MlxrDepthModelOptions* opt,
                                      char* err, size_t errLen);
void mlxr_depth_model_free(MlxrDepthModel* m);

int mlxr_depth_model_width(const MlxrDepthModel* m);
int mlxr_depth_model_height(const MlxrDepthModel* m);
// The threads and flags it really runs with (the delegate's own count is read
// back, as upstream's depth_xnnpack.c does)
int mlxr_depth_model_threads(const MlxrDepthModel* m);
unsigned mlxr_depth_model_flags(const MlxrDepthModel* m);
// Time to read, build and allocate, in ms
float mlxr_depth_model_load_ms(const MlxrDepthModel* m);

// The input tensor, width x height x 3 floats, rows top first; written before
// each run
float* mlxr_depth_model_input(MlxrDepthModel* m);
// Runs the model; 0 on success. Wall time of the run in *ms when not NULL.
int mlxr_depth_model_run(MlxrDepthModel* m, float* ms);
// The output, width x height floats, rows top first, larger is nearer.
// Valid until the next run.
const float* mlxr_depth_model_output(const MlxrDepthModel* m);

// RGBA8 rows (stride in bytes; the downscale pass's MLXR_PASS_FORMAT) to the
// model's float RGB 0..1, alpha dropped. flip reads the rows bottom first, for
// a GL style readback; the Vulkan downscale is top first and needs none.
void mlxr_depth_rgba8_to_input(const uint8_t* rgba, size_t stride, int w, int h, int flip,
                               float* dst);

#ifdef __cplusplus
}
#endif

#endif
