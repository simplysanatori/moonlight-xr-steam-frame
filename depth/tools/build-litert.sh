#!/bin/bash
# Builds LiteRT (TensorFlow Lite) 2.21.0's C API with the XNNPACK delegate as
# one static archive for the depth lane, outside the repo:
#   $ROOT/src/tensorflow-2.21.0      the source (GitHub tag v2.21.0)
#   $ROOT/build                      the CMake build (tensorflow/lite/c)
#   $ROOT/dist/lib/libmlxr_litert.a  every archive the C API pulls in, merged
#   $ROOT/dist/include/...           the nine headers the C API needs
# depth/CMakeLists.txt finds dist/ through MLXR_LITERT_DIR (default this
# ROOT). ~7 min configure+build at -j3 on the Frame, first time (the configure
# step clones XNNPACK, abseil, ruy etc. from GitHub). GPLv3 (this script);
# LiteRT and its dependencies are Apache 2.0 / BSD / MIT.
set -euo pipefail
ROOT="${MLXR_LITERT_ROOT:-$HOME/Games/MoonlightXRNative/third-party-build/litert}"
TF_TAG=v2.21.0
SRC="$ROOT/src/tensorflow-${TF_TAG#v}"
B="$ROOT/build"
JOBS="${JOBS:-3}"
mkdir -p "$ROOT/src"
if [ ! -d "$SRC" ]; then
    curl -sL -o "$ROOT/tf-${TF_TAG#v}.tar.gz" \
        "https://github.com/tensorflow/tensorflow/archive/refs/tags/$TF_TAG.tar.gz"
    tar -xzf "$ROOT/tf-${TF_TAG#v}.tar.gz" -C "$ROOT/src"
fi
mkdir -p "$B"
if [ ! -f "$B/build.ninja" ]; then
    # TENSORFLOW_SOURCE_DIR matters: without it the CMake fetches a different
    # (older) TensorFlow for its headers and the build fails on macros.h
    nice -n 10 cmake -G Ninja -S "$SRC/tensorflow/lite/c" -B "$B" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DTENSORFLOW_SOURCE_DIR="$SRC" -DTFLITE_C_BUILD_SHARED_LIBS=OFF \
        -DTFLITE_ENABLE_XNNPACK=ON -DTFLITE_ENABLE_GPU=OFF -DTFLITE_ENABLE_NNAPI=OFF \
        -DTFLITE_ENABLE_EXTERNAL_DELEGATE=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DABSL_PROPAGATE_CXX_STD=ON
fi
nice -n 10 ninja -C "$B" -j"$JOBS" tensorflowlite_c

# Which archives a C API program really pulls from: link a probe with every
# archive in a group and let the linker say
D="$ROOT/dist"
rm -rf "$D" && mkdir -p "$D/lib" "$D/include" "$ROOT/probe"
cat > "$ROOT/probe/probe.c" <<'C'
#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/delegates/xnnpack/xnnpack_delegate.h"
int main(void) {
    TfLiteXNNPackDelegateOptions o = TfLiteXNNPackDelegateOptionsDefault();
    TfLiteDelegate* d = TfLiteXNNPackDelegateCreate(&o);
    TfLiteInterpreterOptions* opt = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsAddDelegate(opt, d);
    TfLiteModel* m = TfLiteModelCreateFromFile("/nonexistent");
    TfLiteInterpreter* in = TfLiteInterpreterCreate(m, opt);
    TfLiteInterpreterInvoke(in);
    TfLiteXNNPackDelegateGetThreadPool(d);
    TfLiteXNNPackDelegateGetOptions(d);
    TfLiteXNNPackDelegateDelete(d);
    return TfLiteVersion()[0];
}
C
clang -O2 -c "$ROOT/probe/probe.c" -I"$SRC" -o "$ROOT/probe/probe.o" -H 2>&1 \
    | grep -o "$SRC/[^ ]*\.h" | sort -u > "$ROOT/probe/headers.txt"
ALL=$(find "$B" -name "*.a" | sort)
clang++ -o "$ROOT/probe/probe" "$ROOT/probe/probe.o" -Wl,--start-group $ALL -Wl,--end-group \
    -static-libstdc++ -static-libgcc -lpthread -ldl -lm -Wl,--trace 2>&1 \
    | grep "^$B" | sed -E 's/\(.*//' | sort -u > "$ROOT/probe/used.txt"
{
    echo "CREATE $D/lib/libmlxr_litert.a"
    while read -r a; do echo "ADDLIB $a"; done < "$ROOT/probe/used.txt"
    echo "SAVE"; echo "END"
} | ar -M
ranlib "$D/lib/libmlxr_litert.a"
while read -r h; do
    rel="${h#$SRC/}"
    mkdir -p "$D/include/$(dirname "$rel")"
    cp "$h" "$D/include/$rel"
done < "$ROOT/probe/headers.txt"
cp "$SRC/LICENSE" "$D/LICENSE.tensorflow"
echo "$TF_TAG $(wc -l < "$ROOT/probe/used.txt") archives merged, $(du -h "$D/lib/libmlxr_litert.a" | cut -f1)" \
    | tee "$D/VERSION"
