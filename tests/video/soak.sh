#!/bin/sh
# Moonlight XR Native - decode soak: loop the 1440p60 HEVC ~80 Mbps stream at
# 90 fps pacing for N minutes while capturing the kernel log.
# Copyright (C) 2026 Moonlight XR Native contributors. GPLv3, see LICENSE.txt.
#
# soak.sh <build-dir> <out-dir> [minutes=30]
# Pass: the harness passes and the kernel log has no iris / msm / vidc /
# smmu / gem warnings or errors during the run.
set -u
build=$1 out=$2 min=${3:-30}
mkdir -p "$out"
samples=$build/tests/video/samples
sh "$(dirname "$0")/gen_samples.sh" "$samples" soak || exit 2
journalctl -k -f -n 0 -o short-monotonic > "$out/kernel.log" 2>&1 &
jpid=$!
sleep 1
env -u LD_LIBRARY_PATH nice -n 10 "$build/tests/video/mlxr-decode-test" "$samples/t1440_hevc_80m.hevc" \
	--fps 90 --duration $((min * 60)) --progress 60 --trace-slow 20 > "$out/soak.txt" 2>&1
rc=$?
sleep 2
kill $jpid
pattern='iris|msm|vidc|venus|smmu|iommu|gem|drm|adreno|kgsl|oops|bug:|warn|call trace'
grep -iE "$pattern" "$out/kernel.log" > "$out/kernel-video.log"
nk=$(wc -l < "$out/kernel-video.log")
tail -n 14 "$out/soak.txt"
echo "kernel log lines: $(wc -l < "$out/kernel.log") total, $nk matching /$pattern/"
[ "$rc" -eq 0 ] && [ "$nk" -eq 0 ] && echo "SOAK: PASS" && exit 0
echo "SOAK: FAIL (harness rc $rc, kernel matches $nk)"
exit 1
