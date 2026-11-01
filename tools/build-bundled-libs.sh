#!/bin/bash
# Builds the shared libraries the release carries in app/lib, outside the repo. GPLv3 (this script).
#   tools/build-bundled-libs.sh            -> $ROOT/dist/{lib,include,lib/pkgconfig}
#   ROOT = MLXR_BUNDLE_ROOT (default ~/Games/MoonlightXRNative/third-party-build/bundled), JOBS=3
#
# Why: SteamOS updates move these libraries to new sonames, and a binary linked against the old one
# then does not start at all ("error while loading shared libraries: libavcodec.so.61"). The Steam
# Frame OS Preview moved FFmpeg 7 to FFmpeg 8 (libavcodec.so.62); libxml2 2.14 moved libxml2.so.2 to
# libxml2.so.16 (the 2.14.0 release notes) before that, so any SteamOS new enough for FFmpeg 8 is new
# enough for it too. So the app ships its own copies and finds them through its RUNPATH
# ($ORIGIN/lib):
#   FFmpeg 7.0.2  libavcodec.so.61 libavformat.so.61 libavutil.so.59 libswscale.so.8 (LGPL 2.1+)
#   libxml2 2.13.9 libxml2.so.2 (MIT; the last series with that soname)
#   SDL2_ttf 2.24.0 libSDL2_ttf-2.0.so.0 (zlib), for the flat launcher (moonlight-qt): SteamOS 20261020
#                 dropped it (only SDL3_ttf is left). Its vendored FreeType 2.13.2 (FTL) is built in
#                 statically, and nothing of it is exported (--exclude-libs), so it never meets the
#                 system's libfreetype that Qt's fontconfig loads. No HarfBuzz (SDL2_ttf's default).
# FFmpeg and libxml2 are built with no optional dependencies, so they need nothing but glibc (libc,
# libm); SDL2_ttf needs the system's SDL2 (libSDL2-2.0.so.0, on every SteamOS; sdl2-compat on newer ones).
#
# FFmpeg carries only what the app calls: the live stream is decoded by the client's own V4L2 decoder
# (video/v4l2_decoder.c, docs/dev/reports/video.md), never through FFmpeg. FFmpeg serves the software
# decoder fallback and test-file playback (stream/sw_decoder.c: h264/hevc decode + swscale;
# stream/file_source.c: mov/matroska/raw demux + the mp4toannexb filters). The flat launcher
# (moonlight-qt) links libavcodec, libavutil and libswscale too, but in hand-off mode it decodes
# nothing (launcher/patches/0005). LGPL build: no --enable-gpl, no --enable-nonfree.
#
# CMake finds dist/ through MLXR_BUNDLED_LIBS_DIR (build.sh passes it when dist/ is there);
# tools/release.sh copies the sonames into app/lib. The CI cache key is this file's hash, so a change
# here rebuilds them. Re-running with nothing changed does nothing (dist/.stamp).
set -euo pipefail
ROOT="${MLXR_BUNDLE_ROOT:-$HOME/Games/MoonlightXRNative/third-party-build/bundled}"
JOBS="${JOBS:-3}"
D="$ROOT/dist"

FFMPEG_VERSION=7.0.2
# sha256 of ffmpeg-7.0.2.tar.xz (signature checked against the FFmpeg release signing key
# FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8)
FFMPEG_SHA256=8646515b638a3ad303e23af6a3587734447cb8fc0a0c064ecdb8e95c4fd8b389
FFMPEG_CONFIGURE=(
    --enable-shared --disable-static --disable-programs --disable-doc
    --disable-debug --disable-autodetect --enable-pthreads --disable-network
    --disable-avdevice --disable-avfilter --disable-swresample
    --disable-everything
    --enable-decoder=h264,hevc --enable-parser=h264,hevc
    --enable-demuxer=mov,matroska,h264,hevc
    --enable-bsf=h264_mp4toannexb,hevc_mp4toannexb
    --enable-protocol=file
)
LIBXML2_VERSION=2.13.9
# sha256 of libxml2-2.13.9.tar.xz (as in download.gnome.org's libxml2-2.13.9.sha256sum)
LIBXML2_SHA256=a2c9ae7b770da34860050c309f903221c67830c86e4a7e760692b803df95143a
LIBXML2_CONFIGURE=(
    --enable-shared --disable-static
    --without-python --without-zlib --without-lzma --without-icu
    --without-readline --without-history --without-http --without-ftp --without-debug
)
SDL2_TTF_VERSION=2.24.0
# sha256 of SDL2_ttf-2.24.0.tar.gz (github.com/libsdl-org/SDL_ttf release-2.24.0; holds external/freetype)
SDL2_TTF_SHA256=0b2bf1e7b6568adbdbc9bb924643f79d9dedafe061fa1ed687d1d9ac4e453bfd
SDL2_TTF_CMAKE=(
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DSDL2TTF_SAMPLES=OFF -DSDL2TTF_INSTALL=ON
    -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5   # its FreeType says 2.8.12; CMake 4 refuses that
    "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--exclude-libs,ALL"
)
# the sonames dist/lib must end up with (and nothing else that is a shared library)
SONAMES="libavcodec.so.61 libavformat.so.61 libavutil.so.59 libswscale.so.8 libxml2.so.2 libSDL2_ttf-2.0.so.0"

STAMP="$(cat "$0" | sha256sum | cut -c1-16)"
if [ "$(cat "$D/.stamp" 2>/dev/null)" = "$STAMP" ]; then
    echo "bundled libraries are up to date in $D"
    exit 0
fi
for t in curl sha256sum tar make cc pkg-config cmake; do
    command -v "$t" >/dev/null || { echo "missing host tool: $t"; exit 1; }
done
fetch() {   # fetch <url> <sha256> <file>
    [ -f "$3" ] && echo "$2  $3" | sha256sum -c --status - && return 0
    curl -sSfL -o "$3.part" "$1"
    echo "$2  $3.part" | sha256sum -c --quiet - || { echo "checksum mismatch: $1"; rm -f "$3.part"; exit 1; }
    mv "$3.part" "$3"
}
mkdir -p "$ROOT/src"
rm -rf "$D" "$ROOT/build"; mkdir -p "$ROOT/build"

echo "== FFmpeg $FFMPEG_VERSION"
fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" "$FFMPEG_SHA256" "$ROOT/src/ffmpeg-$FFMPEG_VERSION.tar.xz"
tar -xf "$ROOT/src/ffmpeg-$FFMPEG_VERSION.tar.xz" -C "$ROOT/build"
( cd "$ROOT/build/ffmpeg-$FFMPEG_VERSION" && ./configure --prefix="$D" "${FFMPEG_CONFIGURE[@]}" \
    && nice -n 10 make -j"$JOBS" && make install ) > "$ROOT/build/ffmpeg.log" 2>&1 || { tail -n 40 "$ROOT/build/ffmpeg.log"; exit 1; }

echo "== libxml2 $LIBXML2_VERSION"
fetch "https://download.gnome.org/sources/libxml2/${LIBXML2_VERSION%.*}/libxml2-$LIBXML2_VERSION.tar.xz" \
    "$LIBXML2_SHA256" "$ROOT/src/libxml2-$LIBXML2_VERSION.tar.xz"
tar -xf "$ROOT/src/libxml2-$LIBXML2_VERSION.tar.xz" -C "$ROOT/build"
( cd "$ROOT/build/libxml2-$LIBXML2_VERSION" && ./configure --prefix="$D" "${LIBXML2_CONFIGURE[@]}" \
    && nice -n 10 make -j"$JOBS" && make install ) > "$ROOT/build/libxml2.log" 2>&1 || { tail -n 40 "$ROOT/build/libxml2.log"; exit 1; }

echo "== SDL2_ttf $SDL2_TTF_VERSION"
fetch "https://github.com/libsdl-org/SDL_ttf/releases/download/release-$SDL2_TTF_VERSION/SDL2_ttf-$SDL2_TTF_VERSION.tar.gz" \
    "$SDL2_TTF_SHA256" "$ROOT/src/SDL2_ttf-$SDL2_TTF_VERSION.tar.gz"
tar -xzf "$ROOT/src/SDL2_ttf-$SDL2_TTF_VERSION.tar.gz" -C "$ROOT/build"
( cmake -S "$ROOT/build/SDL2_ttf-$SDL2_TTF_VERSION" -B "$ROOT/build/sdl2_ttf" -DCMAKE_INSTALL_PREFIX="$D" \
    -DCMAKE_INSTALL_LIBDIR=lib "${SDL2_TTF_CMAKE[@]}" && nice -n 10 cmake --build "$ROOT/build/sdl2_ttf" -j"$JOBS" \
    && cmake --install "$ROOT/build/sdl2_ttf" ) > "$ROOT/build/sdl2_ttf.log" 2>&1 || { tail -n 40 "$ROOT/build/sdl2_ttf.log"; exit 1; }
rm -rf "$D/share" "$D/bin" "$D/lib/cmake" "$D/lib"/*.la "$D/lib"/xml2Conf.sh

# exactly the expected sonames, each needing only glibc
have="$(cd "$D/lib" && ls lib*.so.* | grep -E '^lib.+\.so\.[0-9]+$' | sort | tr '\n' ' ')"
want="$(printf '%s\n' $SONAMES | sort | tr '\n' ' ')"
[ "$have" = "$want" ] || { echo "dist/lib holds the sonames: $have; expected: $want"; exit 1; }
if command -v objdump >/dev/null; then
    for s in $SONAMES; do
        bad="$(objdump -p "$D/lib/$s" | awk '$1 == "NEEDED" { print $2 }' \
            | grep -v -E '^(libc\.so\.6|libm\.so\.6|ld-linux.*|libav[a-z]+\.so\.[0-9]+|libsw[a-z]+\.so\.[0-9]+|libSDL2-2\.0\.so\.0)$' || true)"
        [ -z "$bad" ] || { echo "$s needs more than glibc, FFmpeg and SDL2: $bad"; exit 1; }
    done
    # SDL2_ttf exports its own API only: the FreeType inside it must not stand in for the system's
    leak="$(objdump -T "$D/lib/libSDL2_ttf-2.0.so.0" | awk '$2 == "g" && $4 != "*UND*" { print $NF }' | grep -v -E '^(TTF_|_init$|_fini$)' | head -n 5 || true)"
    [ -z "$leak" ] || { echo "libSDL2_ttf-2.0.so.0 exports more than TTF_*: $leak"; exit 1; }
fi
echo "$STAMP" > "$D/.stamp"
echo "bundled libraries in $D/lib:"
for s in $SONAMES; do echo "  $s -> $(readlink -f "$D/lib/$s" | sed 's|.*/||') ($(du -k "$(readlink -f "$D/lib/$s")" | cut -f1) KB)"; done
