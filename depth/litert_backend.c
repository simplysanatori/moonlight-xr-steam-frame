// LiteRT C API + XNNPACK delegate for the depth models. See litert_backend.h.
// GPLv3.
#include "litert_backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"

static const MlxrDepthModelSpec SPECS[MLXR_DEPTH_MODEL_COUNT] = {
    { MLXR_DEPTH_ZIPDEPTH_512x288_FP16, "zipdepth", "ZipDepth 512x288 fp16",
      "zipdepth_512x288_fp16.tflite", 512, 288, 6 },
    { MLXR_DEPTH_ZIPDEPTH_256_INT8DR, "zipdepth-int8", "ZipDepth 256 int8dr",
      "zipdepth_256_int8dr.tflite", 256, 256, 6 },
    { MLXR_DEPTH_MIDAS_256_FP16, "midas", "MiDaS v2.1 small 256 fp16",
      "midas_v21_small_256_fp16.tflite", 256, 256, 5 },
};

const MlxrDepthModelSpec* mlxr_depth_model_spec(MlxrDepthModelId id) {
    return (unsigned)id < MLXR_DEPTH_MODEL_COUNT ? &SPECS[id] : NULL;
}

const MlxrDepthModelSpec* mlxr_depth_model_find(const char* keyOrFile) {
    if (keyOrFile == NULL) {
        return NULL;
    }
    const char* base = strrchr(keyOrFile, '/');
    base = base != NULL ? base + 1 : keyOrFile;
    for (int i = 0; i < MLXR_DEPTH_MODEL_COUNT; i++) {
        if (strcmp(base, SPECS[i].key) == 0 || strcmp(base, SPECS[i].file) == 0) {
            return &SPECS[i];
        }
    }
    return NULL;
}

struct MlxrDepthModel {
    void* bytes;
    TfLiteModel* model;
    TfLiteInterpreterOptions* options;
    TfLiteDelegate* xnnpack;
    TfLiteInterpreter* interp;
    TfLiteTensor* input;
    const TfLiteTensor* output;
    int width, height;
    int threads;
    unsigned flags;
    float loadMs;
};

static double nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void fail(char* err, size_t errLen, const char* fmt, ...) {
    if (err == NULL || errLen == 0) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errLen, fmt, ap);
    va_end(ap);
}

// LiteRT's error reporter: the last message is kept for the caller's err
static char lastError[256];
static void reporter(void* user, const char* fmt, va_list args) {
    (void)user;
    vsnprintf(lastError, sizeof lastError, fmt, args);
}

// Upstream's outputFits: axes of 1 ignored, the two left height then width
static int outputFits(const TfLiteTensor* t, int w, int h) {
    int kept[2], count = 0;
    for (int i = 0; i < TfLiteTensorNumDims(t); i++) {
        int d = TfLiteTensorDim(t, i);
        if (d == 1) {
            continue;
        }
        if (count == 2) {
            return 0;
        }
        kept[count++] = d;
    }
    return count == 2 && kept[0] == h && kept[1] == w;
}

static void *readFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    void* buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
            // 64 byte aligned: LiteRT maps tensors straight out of it
            buf = aligned_alloc(64, ((size_t)n + 63) & ~(size_t)63);
            if (buf != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
                free(buf);
                buf = NULL;
            }
            *size = (size_t)n;
        }
    }
    fclose(f);
    return buf;
}

MlxrDepthModel* mlxr_depth_model_load(const char* path, const MlxrDepthModelOptions* opt,
                                      char* err, size_t errLen) {
    double start = nowMs();
    int threads = opt != NULL && opt->threads > 0 ? opt->threads : 2;
    unsigned flags = opt != NULL ? opt->flags : 0;
    MlxrDepthModel* m = calloc(1, sizeof *m);
    if (m == NULL) {
        fail(err, errLen, "out of memory");
        return NULL;
    }
    size_t size = 0;
    m->bytes = readFile(path, &size);
    if (m->bytes == NULL) {
        fail(err, errLen, "cannot read %s", path);
        goto bad;
    }
    m->model = TfLiteModelCreate(m->bytes, size);
    if (m->model == NULL) {
        fail(err, errLen, "%s is not a LiteRT model", path);
        goto bad;
    }

    TfLiteXNNPackDelegateOptions xo = TfLiteXNNPackDelegateOptionsDefault();
    xo.num_threads = threads;
    if (flags & MLXR_DEPTH_FP16) {
        xo.flags |= TFLITE_XNNPACK_DELEGATE_FLAG_FORCE_FP16;
    }
    m->xnnpack = TfLiteXNNPackDelegateCreate(&xo);
    if (m->xnnpack == NULL) {
        fail(err, errLen, "no XNNPACK delegate on %d threads", threads);
        goto bad;
    }
    // The delegate keeps its own copy of the options and makes its pool from
    // it: one thread runs without a pool (upstream's depth_xnnpack.c check)
    const TfLiteXNNPackDelegateOptions* held = TfLiteXNNPackDelegateGetOptions(m->xnnpack);
    int pooled = TfLiteXNNPackDelegateGetThreadPool(m->xnnpack) != NULL;
    if (held == NULL || held->num_threads != threads || pooled != (threads > 1)) {
        fail(err, errLen, "XNNPACK delegate asked for %d threads came back with %d and %s pool",
             threads, held != NULL ? held->num_threads : -1, pooled ? "a" : "no");
        goto bad;
    }

    m->options = TfLiteInterpreterOptionsCreate();
    // For any op the delegate leaves behind
    TfLiteInterpreterOptionsSetNumThreads(m->options, threads);
    TfLiteInterpreterOptionsAddDelegate(m->options, m->xnnpack);
    TfLiteInterpreterOptionsSetErrorReporter(m->options, reporter, NULL);
    lastError[0] = 0;
    m->interp = TfLiteInterpreterCreate(m->model, m->options);
    if (m->interp == NULL) {
        fail(err, errLen, "interpreter not built: %s", lastError);
        goto bad;
    }
    if (TfLiteInterpreterAllocateTensors(m->interp) != kTfLiteOk) {
        fail(err, errLen, "tensors not allocated: %s", lastError);
        goto bad;
    }
    if (TfLiteInterpreterGetInputTensorCount(m->interp) != 1
            || TfLiteInterpreterGetOutputTensorCount(m->interp) < 1) {
        fail(err, errLen, "expected one input and an output");
        goto bad;
    }
    m->input = TfLiteInterpreterGetInputTensor(m->interp, 0);
    m->output = TfLiteInterpreterGetOutputTensor(m->interp, 0);
    // Upstream's inputFits: one RGB image, rows then columns
    if (TfLiteTensorType(m->input) != kTfLiteFloat32 || TfLiteTensorNumDims(m->input) != 4
            || TfLiteTensorDim(m->input, 0) != 1 || TfLiteTensorDim(m->input, 3) != 3) {
        fail(err, errLen, "input is not a 1xHxWx3 float image");
        goto bad;
    }
    m->height = TfLiteTensorDim(m->input, 1);
    m->width = TfLiteTensorDim(m->input, 2);
    if (TfLiteTensorType(m->output) != kTfLiteFloat32
            || !outputFits(m->output, m->width, m->height)) {
        fail(err, errLen, "output is not %dx%d floats", m->width, m->height);
        goto bad;
    }
    m->threads = threads;
    m->flags = flags;
    m->loadMs = (float)(nowMs() - start);
    return m;
bad:
    mlxr_depth_model_free(m);
    return NULL;
}

void mlxr_depth_model_free(MlxrDepthModel* m) {
    if (m == NULL) {
        return;
    }
    // The interpreter goes before the delegate it was built with
    if (m->interp != NULL) {
        TfLiteInterpreterDelete(m->interp);
    }
    if (m->options != NULL) {
        TfLiteInterpreterOptionsDelete(m->options);
    }
    if (m->xnnpack != NULL) {
        TfLiteXNNPackDelegateDelete(m->xnnpack);
    }
    if (m->model != NULL) {
        TfLiteModelDelete(m->model);
    }
    free(m->bytes);
    free(m);
}

int mlxr_depth_model_width(const MlxrDepthModel* m) { return m->width; }
int mlxr_depth_model_height(const MlxrDepthModel* m) { return m->height; }
int mlxr_depth_model_threads(const MlxrDepthModel* m) { return m->threads; }
unsigned mlxr_depth_model_flags(const MlxrDepthModel* m) { return m->flags; }
float mlxr_depth_model_load_ms(const MlxrDepthModel* m) { return m->loadMs; }

float* mlxr_depth_model_input(MlxrDepthModel* m) {
    return (float*)TfLiteTensorData(m->input);
}

int mlxr_depth_model_run(MlxrDepthModel* m, float* ms) {
    double start = nowMs();
    TfLiteStatus s = TfLiteInterpreterInvoke(m->interp);
    if (ms != NULL) {
        *ms = (float)(nowMs() - start);
    }
    return s == kTfLiteOk ? 0 : -1;
}

const float* mlxr_depth_model_output(const MlxrDepthModel* m) {
    return (const float*)TfLiteTensorData(m->output);
}

void mlxr_depth_rgba8_to_input(const uint8_t* rgba, size_t stride, int w, int h, int flip,
                               float* dst) {
    const float k = 1.0f / 255.0f;
    for (int y = 0; y < h; y++) {
        const uint8_t* src = rgba + (size_t)(flip ? h - 1 - y : y) * stride;
        float* d = dst + (size_t)y * w * 3;
        for (int x = 0; x < w; x++) {
            d[x * 3 + 0] = src[x * 4 + 0] * k;
            d[x * 3 + 1] = src[x * 4 + 1] * k;
            d[x * 3 + 2] = src[x * 4 + 2] * k;
        }
    }
}
