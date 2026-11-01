# Building Moonlight XR for Frame

The build runs on the Steam Frame itself. The SteamOS image ships everything it needs: clang 19, lld, cmake, ninja, glslc, qmake6, pkg-config and the development headers for Vulkan, FFmpeg, OpenSSL, curl, Opus, PulseAudio, PipeWire, SDL2, libpng and Qt 6. No container and no `pacman` installs are needed. The binaries link only libraries that SteamOS ships, except FFmpeg and libxml2: the app carries its own copies of those in `app/lib`, because SteamOS updates change their versions (the Steam Frame OS Preview moved to FFmpeg 8).

## Get the source

```sh
git clone --recursive https://github.com/simplysanatori/moonlight-xr-steam-frame
cd moonlight-xr-steam-frame
```

The submodules are the [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) (release-1.1.63), [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) and [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) (v6.1.0). The patched copy of moonlight-qt is made in `build-launcher/`; the submodule itself is never changed.

## Build

```sh
env -u LD_LIBRARY_PATH ./build.sh
```

`build.sh` does this:

1. Checks the host tools and updates the submodules.
2. Builds LiteRT, the depth model runtime, the first time. This takes about 12 minutes and downloads TensorFlow and its dependencies from GitHub. The result goes to `~/Games/MoonlightXRNative/third-party-build/litert`, outside the repository. `NO_DEPTH=1` skips it; the app then has no 3D.
3. Builds FFmpeg 7.0.2 and libxml2 2.13.9 the first time (`tools/build-bundled-libs.sh`, a few minutes). It downloads the release tarballs from ffmpeg.org and gnome.org and checks their SHA-256. The result goes to `~/Games/MoonlightXRNative/third-party-build/bundled`. The binaries find these through `$ORIGIN/lib`, so they never use the system's FFmpeg or libxml2. `NO_BUNDLE=1` skips this and links the system's copies (not for releases).
4. Configures and builds the VR client (`build/moonlight-xr`), the command line tool (`build/backend/mlxr-cli`) and the tests with CMake and Ninja.
5. Builds the flat window (`build-launcher/moonlight-xr-launcher`): it copies moonlight-qt, applies `launcher/patches/*.patch` and builds it with qmake. `NO_LAUNCHER=1` skips it.
6. Runs `tools/ldd-audit.sh` on the binaries. It fails if a binary needs a library that is neither a SteamOS package nor in `lib/`.

| Variable | Default | Meaning |
| --- | --- | --- |
| `BUILD` | `build` | Build directory |
| `JOBS` | 3 | Parallel jobs |
| `NO_DEPTH` | 0 | 1: do not build LiteRT |
| `NO_LAUNCHER` | 0 | 1: do not build the flat window |
| `NO_BUNDLE` | 0 | 1: link the system's FFmpeg and libxml2 instead of building them |
| `MLXR_BUNDLE_ROOT` | `~/Games/MoonlightXRNative/third-party-build/bundled` | Where FFmpeg and libxml2 are built |
| `MLXR_LITERT_ROOT` | `~/Games/MoonlightXRNative/third-party-build/litert` | Where LiteRT is built |
| `SKIP_AUDIT` | 0 | 1: skip the library audit |

Run builds and binaries with `env -u LD_LIBRARY_PATH`. A shell started from Steam inherits Steam's runtime library path, which breaks the host tools.

## Test

```sh
env -u LD_LIBRARY_PATH ctest --test-dir build -E audio_pulse
```

Leave out `audio_pulse`: it loads a PulseAudio null sink on the running system. The video and shader tests need the Frame's decoder and GPU. The launcher tests skip themselves until `build.sh` has built the flat window. `launcher_vs_stock` also needs `tests/launcher/build-stock.sh`. The `rooms_*`, `m5_world_*` and `m6_*` tests skip themselves (exit code 77) when there is no Vulkan device.

`tools/privacy-check.sh` looks for personal data in the tracked files. It runs as the `tools_privacy_check` test.

## Install from the build

```sh
./packaging/install.sh
```

This installs from the build directory into `~/Games/MoonlightXRNative/app` and adds or updates the Steam shortcut, as the release installer does. `DEST` changes the target, `NO_SHORTCUT=1` skips the shortcut, `--seed` copies the pairing from the Moonlight flatpak, and `VR_ONLY=1` makes the shortcut start the VR client without the flat window.

## Release tarball

Releases are built by CI, not on the Frame. Every push runs `.github/workflows/build.yml` on GitHub's `ubuntu-24.04-arm` runners. Its `frame-artifact` job uploads `moonlight-xr-steam-frame-<version>-release-candidate` (the tarball, the zip and `SHA256SUMS`), and that artifact is what gets published. Ubuntu 24.04 has glibc 2.39, the same as SteamOS before build 20261020. Newer SteamOS (glibc 2.43) runs what is built against it, but not the other way round, so a build made on an updated Frame cannot be a release.

The `launcher` job builds the flat launcher (the patched moonlight-qt) against Qt 6.8.3 from aqtinstall, runs its tests, and stages it with its own Qt (`tools/stage-launcher.sh`): the Qt libraries and ICU in `lib/`, the platform (xcb, wayland, offscreen), TLS, image and input plugins in `plugins/`, the QML modules its pages import in `qml/`, and a `qt.conf` that points Qt there. The tests then run again against that staged copy with the build's Qt moved away. The `frame-artifact` job builds the VR client and the bundled FFmpeg, libxml2 and SDL2_ttf (`tools/build-bundled-libs.sh`), and then runs

```sh
OUT=dist ALLOW_MISSING_HOST=1 tools/release.sh
```

The script strips the binaries, copies only tracked assets, and stops if config, pairing keys, logs, the LiteRT build or a converted Steam Frame controller model would be packed. It refuses to pack without the bundled libraries or the staged launcher (`LAUNCHER_STAGE`, default `build-launcher/stage`). `tools/check-bundle.sh` then checks every binary, library and plugin in `app/`. Each library one needs must be in `app/lib`, or on `tools/host-libs.allow` (glibc, the C++ runtime, Mesa and the other graphics libraries, Wayland, X11, PulseAudio, D-Bus, SDL2 and a few long-stable libraries) and present under that soname in every SteamOS build listed in `docs/probe/host-sonames/`. Nothing may need a glibc symbol newer than 2.39. The script runs `tools/privacy-check.sh` on the tarball. Then it unpacks the tarball, installs it twice into a temporary folder and compares the installed files. It starts the client (to its usage error) and the launcher (offscreen, until it draws its PC list), and runs the library audit where there is pacman. The version is the `VERSION` file. If the commit carries a `v*` tag, it must match.

The `linux-arm64` job runs the tests. The tests that need a Vulkan device, the Iris decoder, PulseAudio or SteamVR either skip themselves (exit code 77) or are filtered out of the ctest run.

When a SteamOS update comes out, add its library list to `docs/probe/host-sonames/` (the sonames in its `/usr/lib`) so that the check covers it too.

## Other machines

The code builds on other aarch64 Linux systems with the same development packages (see the CI workflow). It needs a V4L2 stateful video decoder, Vulkan with dma-buf import and an OpenXR runtime to run, so in practice only the Frame runs it. The tests also need `libpng-dev`, and the flat launcher needs Qt 6.5 or newer.

## Developer notes

The design notes, build history and per-area reports are in [docs/dev](dev/README.md). They are working notes kept as history, not instructions.
