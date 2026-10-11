# Moonlight XR for Frame

A game-streaming client for the Valve Steam Frame. It connects to a Sunshine or Apollo host (or GeForce Experience), shows the stream on a virtual screen in SteamVR, and sends back pointer, keyboard and controller input. It is a native Linux Arm64 OpenXR program written in C: video is decoded on the Frame's Qualcomm Iris hardware decoder through V4L2 and drawn with Vulkan (Turnip). It is not an Android app and needs no Lepton.

Built by [simplysanatori](https://github.com/simplysanatori). The XR side (screen layout, rooms, depth, virtual surround, interface) is a port of Sean Gilleece's [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr), which is a fork of Moonlight for Android. See [CREDITS.md](CREDITS.md).

The project and its files are still called Moonlight XR Native in places (the repository, the `~/Games/MoonlightXRNative` and `~/.config/moonlight-xr` directories, the tarball name), so existing installs, settings and pairing keep working.

## Status

Unfinished. Video, hardware decode, audio and mouse input have been tested on a Frame against a Sunshine host. The in-headset interface, gamepad input, rooms and depth are being added. [STATUS.md](STATUS.md) lists what has been confirmed on hardware and what has only been reasoned from code or tested without a headset.

## Requirements

- Steam Frame running SteamOS, with SteamVR (the OpenXR runtime)
- A PC running Sunshine or Apollo (or GeForce Experience), reachable from the Frame
- Steam running on the Frame, if you want the library shortcut

## Install from a release

1. Download `moonlight-xr-native-<version>-linux-arm64.tar.gz` and `SHA256SUMS` from the Releases page, and check them: `sha256sum -c --ignore-missing SHA256SUMS`.
2. Unpack and run the installer (desktop mode, with Steam running):
   ```
   tar xzf moonlight-xr-native-*-linux-arm64.tar.gz
   cd MoonlightXRNative
   ./install.sh --seed
   ```
3. Start "Moonlight XR for Frame" from the Steam library.

`install.sh` copies the program into `~/Games/MoonlightXRNative/app` (set `DEST` to change it), adds or updates the Steam shortcut "Moonlight XR for Frame" (an older shortcut named "Moonlight XR Native" is renamed in place, so its Steam settings stay), puts the shortcut's artwork from `assets/branding` into Steam's grid folder, and can be run again at any time. It does not touch anything else. `NO_SHORTCUT=1` skips the shortcut. If Steam is not running, the files are still installed and the shortcut is added the next time you run it.

## First run: pairing

The host has to know the Frame before it will stream. Either:

- **Import from Moonlight Qt.** If Moonlight Qt (the flatpak) on the Frame is already paired with your host, `./install.sh --seed` copies its client identity and host list. The host sees the same client, so no PIN is needed. Run `~/Games/MoonlightXRNative/app/seed-from-moonlight-qt.sh` by hand to do it later (`--force` replaces an existing identity). Moonlight Qt's files are only read.
- **Pair with the command line tool.** `~/Games/MoonlightXRNative/app/mlxr-cli pair <host address>` prints a PIN; enter it in the Sunshine or Apollo web interface (`https://<host>:47990/pin`). Then `mlxr-cli apps <host>` should list the host's apps.

Identity, host list and settings are in `~/.config/moonlight-xr/`.

With no arguments the program connects to the first host in `hosts.ini` and starts the "Desktop" app at 2560x1440, 90 Hz, 80 Mbps. Options can be put in the Steam shortcut's launch options, for example `--host my-pc --app Steam --mode 1920x1080x72 --wake`; `moonlight-xr --help` lists them.

Logs: `~/Games/MoonlightXRNative/moonlight.log` (the previous run is kept as `moonlight.previous.log`). `tools/bug-report.sh` collects them with some system information into a zip, leaving out the client key and certificates.

## Controls (current)

- Point with the controller; the trigger clicks
- Right controller A: right click. Left controller d-pad down: right click
- Thumbstick: scroll
- A Bluetooth keyboard is forwarded to the host while the app is focused and streaming

The settings bar (screen size and distance, cursor dot, gamepad toggle, rooms, depth) arrives with the interface milestone; until then the defaults apply.

## Build from source

On the Frame's SteamOS image, which ships clang, cmake, ninja, glslc and the development headers:

```
git clone --recursive https://github.com/simplysanatori/moonlight-xr-native
cd moonlight-xr-native
./build.sh
./packaging/install.sh --seed
```

`build.sh` also builds LiteRT (the depth model runtime) the first time. That takes about 12 minutes and downloads sources from GitHub; set `NO_DEPTH=1` to skip it. It then builds everything and runs `tools/ldd-audit.sh`, which fails if a binary needs a library that is not a SteamOS package. `JOBS=2 ./build.sh` limits parallelism. Tests: `env -u LD_LIBRARY_PATH ctest --test-dir build -E audio_pulse` (some need the Frame's hardware decoder and GPU).

`tools/release.sh` builds the release tarball and checks it by installing it into a temporary directory.

On other aarch64 Linux systems with the same development packages (see `.github/workflows/build.yml`) the program builds, but it needs a V4L2 stateful decoder, Vulkan with dma-buf import, and an OpenXR runtime, so in practice only the Frame runs it.

## Credits

- Built by [simplysanatori](https://github.com/simplysanatori).
- [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr) by Sean Gilleece is the origin of every XR module here, and of the logo the icon and Steam art are made from. The [Steam Frame fork](https://github.com/simplysanatori/moonlight-android-xr-steam-frame) of it was the reference for how it behaves on the Frame.
- [Moonlight](https://github.com/moonlight-stream) by the Moonlight Game Streaming Project (Cameron Gutman, Diego Waxemberg, Aaron Neyer, Andrew Hennessy and contributors): [moonlight-android](https://github.com/moonlight-stream/moonlight-android), which Moonlight XR is built on; [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c), the streaming protocol; and [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) (GPLv3), the host client behaviour and the flat launcher.
- [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) from Khronos (Apache 2.0): the OpenXR loader.
- [LiteRT](https://github.com/tensorflow/tensorflow) (Apache 2.0) runs the depth models: [ZipDepth](https://github.com/fabiotosi92/ZipDepth) by Fabio Tosi (MIT) and [MiDaS](https://github.com/isl-org/MiDaS) by Intel ISL (MIT).
- KEMAR head-related transfer functions by Bill Gardner and Keith Martin, MIT Media Lab, 1994, for virtual surround.
- The three rooms and the controller model are by Sean Gilleece / Woodford XR, CC BY 4.0.
- Noto Sans, SIL Open Font License 1.1.

The full list with licences and links is in [CREDITS.md](CREDITS.md).

## Licence

GNU General Public License v3, see [LICENSE.txt](LICENSE.txt).

## How this was made

Claude (Anthropic) models wrote the code through Claude Code, at the owner's direction. The owner tested it on a Steam Frame. Anything marked as tested in `STATUS.md` was seen on hardware; the rest was not.
