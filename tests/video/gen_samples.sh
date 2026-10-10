#!/bin/sh
# Moonlight XR Native - make the Annex-B test streams for the video tests.
# Copyright (C) 2026 Moonlight XR Native contributors. GPLv3, see LICENSE.txt.
#
# gen_samples.sh <outdir> [soak]
# testsrc2 encoded with the host's libx264/libx265, no B-frames, one IDR at the
# start of each segment, tagged BT.709 limited range. Never VP9 (SteamOS #2865).
# 'soak' also makes the 10 s 1440p60 HEVC ~80 Mbps stream (takes ~1 min).
set -e
out=$1
[ -n "$out" ] || { echo "usage: $0 outdir [soak]" >&2; exit 2; }
mkdir -p "$out"
cd "$out"
FF="env -u LD_LIBRARY_PATH nice -n 10 ffmpeg -hide_banner -loglevel error -y"
TAG="-pix_fmt yuv420p -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv"
X264="-c:v libx264 -preset veryfast -bf 0 -g 1200 -f h264"
X265="-c:v libx265 -preset ultrafast -x265-params bframes=0:keyint=1200:log-level=error -f hevc"
src() { echo "testsrc2=size=$1:rate=60:duration=$2"; }

[ -s t1080_h264.h264 ] || $FF -f lavfi -i "$(src 1920x1080 2)" $TAG -b:v 20M $X264 t1080_h264.h264
[ -s t1080_hevc.hevc ] || $FF -f lavfi -i "$(src 1920x1080 2)" $TAG -b:v 20M $X265 t1080_hevc.hevc
if [ ! -s reschange_hevc.hevc ]; then
	$FF -f lavfi -i "$(src 1920x1080 1)" $TAG -b:v 20M $X265 seg_a.hevc
	$FF -f lavfi -i "$(src 2560x1440 1)" $TAG -b:v 30M $X265 seg_b.hevc
	cat seg_a.hevc seg_b.hevc seg_a.hevc > reschange_hevc.hevc
	rm -f seg_a.hevc seg_b.hevc
fi
# Software-decoded reference pictures (frame 30) for the colour checks.
for f in t1080_h264.h264 t1080_hevc.hevc; do
	ref=ref_${f%.*}_f30.ppm
	[ -s "$ref" ] || $FF -i "$f" -vf "select=eq(n\,30)" -frames:v 1 "$ref"
done
if [ "$2" = soak ] && [ ! -s t1440_hevc_80m.hevc ]; then
	# Temporal noise forces the bitrate up to the 80 Mbps target.
	$FF -f lavfi -i "$(src 2560x1440 10)" -vf noise=alls=12:allf=t $TAG -b:v 80M \
		-c:v libx265 -preset ultrafast \
		-x265-params bframes=0:keyint=1200:vbv-maxrate=80000:vbv-bufsize=1400:log-level=error \
		-f hevc t1440_hevc_80m.hevc
fi
exit 0
