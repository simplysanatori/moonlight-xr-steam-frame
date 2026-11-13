#!/bin/bash
# Runs the layer gate on a real session's ray recorder CSV (MLXR_RAY_DEBUG=1 writes
# ~/Games/MoonlightXRNative/reports/ray-<date>-<time>.csv). On the desktop build:
#   tools/gate-from-ray-csv.sh reports/ray-2026-11-13-1200.csv   (set KB=1 to keep the keyboard up)
# Needs the cross build from tools/desktop-dev-build.sh (or any build dir) with test_m4_layergate.
set -euo pipefail
CSV="$(readlink -f "${1:?ray csv}")"
BUILD="${BUILD:-$HOME/frame-build/mlxr/build}"
BIN="$BUILD/tests/m4/test_m4_layergate"
export MLXR_GATE_CSV="$CSV" MLXR_SOFT_GPU=1
[ -n "${KB:-}" ] && export MLXR_GATE_KB=1
if [ "$(uname -m)" = aarch64 ]; then exec "$BIN"; fi
QEMU_LD_PREFIX="${QEMU_LD_PREFIX:-$HOME/frame-build/mlxr/sysroot}" exec qemu-aarch64 "$BIN"
