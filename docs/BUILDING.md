# Building Moonlight XR for Frame

The build runs on the Steam Frame itself. The SteamOS image ships everything it needs: clang 19, lld, cmake, ninja, glslc, qmake6, pkg-config and the development headers for Vulkan, FFmpeg, OpenSSL, curl, Opus, PulseAudio, PipeWire, SDL2 and Qt 6. No container and no `pacman` installs are needed. The binaries link only libraries that SteamOS ships.

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
3. Configures and builds the VR client (`build/moonlight-xr`), the command line tool (`build/backend/mlxr-cli`) and the tests with CMake and Ninja.
4. Builds the flat window (`build-launcher/moonlight-xr-launcher`): it copies moonlight-qt, applies `launcher/patches/*.patch` and builds it with qmake. `NO_LAUNCHER=1` skips it.
5. Runs `tools/ldd-audit.sh` on the binaries. It fails if a binary needs a library that is not a SteamOS package.

| Variable | Default | Meaning |
| --- | --- | --- |
| `BUILD` | `build` | Build directory |
| `JOBS` | 3 | Parallel jobs |
| `NO_DEPTH` | 0 | 1: do not build LiteRT |
| `NO_LAUNCHER` | 0 | 1: do not build the flat window |
| `MLXR_LITERT_ROOT` | `~/Games/MoonlightXRNative/third-party-build/litert` | Where LiteRT is built |
| `SKIP_AUDIT` | 0 | 1: skip the library audit |

Run builds and binaries with `env -u LD_LIBRARY_PATH`. A shell started from Steam inherits Steam's runtime library path, which breaks the host tools.

## Test

```sh
env -u LD_LIBRARY_PATH ctest --test-dir build -E audio_pulse
```

Leave out `audio_pulse`: it loads a PulseAudio null sink on the running system. The video and shader tests need the Frame's decoder and GPU. The launcher tests skip themselves until `build.sh` has built the flat window. `launcher_vs_stock` also needs `tests/launcher/build-stock.sh`.

`tools/privacy-check.sh` looks for personal data in the tracked files. It runs as the `tools_privacy_check` test.

## Install from the build

```sh
./packaging/install.sh
```

This installs from the build directory into `~/Games/MoonlightXRNative/app` and adds or updates the Steam shortcut, as the release installer does. `DEST` changes the target, `NO_SHORTCUT=1` skips the shortcut, `--seed` copies the pairing from the Moonlight flatpak, and `VR_ONLY=1` makes the shortcut start the VR client without the flat window.

## Release tarball

```sh
BUILD=build tools/release.sh
```

It packs `moonlight-xr-steam-frame-<version>-linux-arm64.tar.gz` and updates `SHA256SUMS` in `~/Games/MoonlightXRNative/releases` (`OUT` changes it). The version is the `VERSION` file. If the commit carries a `v*` tag, it must match.

The script strips the binaries, copies only tracked assets, and stops if config, pairing keys, logs, the LiteRT build or a converted Steam Frame controller model would be packed. It runs `tools/privacy-check.sh` on the tarball. Then it unpacks the tarball, installs it twice into a temporary folder, compares the installed files and runs the library audit on them.

Build releases on the Frame. GitHub Actions builds every push on `ubuntu-24.04-arm` (`.github/workflows/build.yml`), but that is a build check only. Ubuntu 24.04 has FFmpeg 6.1 and SteamOS has 7.0, so the CI binaries do not run on the Frame.

## Other machines

The code builds on other aarch64 Linux systems with the same development packages (see the CI workflow). It needs a V4L2 stateful video decoder, Vulkan with dma-buf import and an OpenXR runtime to run, so in practice only the Frame runs it.

## Developer notes

Design notes, the build history and per-area reports are in [docs/dev](dev/README.md).
