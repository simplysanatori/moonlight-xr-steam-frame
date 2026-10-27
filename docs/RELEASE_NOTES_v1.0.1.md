# Moonlight XR for Frame 1.0.1

A fix for a crash in Game Mode. Nothing else changed since 1.0.0.

### Fixed

- Pressing a Frame volume button during a stream in Game Mode crashed gamescope, and with it Steam and SteamVR. The launcher hid its flat window for the stream, which left gamescope with no window to give keyboard focus to. The launcher now keeps the flat window mapped at opacity 0 instead of hiding it. `MLXR_LAUNCHER_FADE_WINDOW=0` restores the old behaviour.

## Download

- `moonlight-xr-steam-frame-1.0.1-linux-arm64.tar.gz`
- `SHA256SUMS`

## Install or upgrade

Unpack the tarball on the Frame and run `install.sh`. It keeps your settings and pairing on an update. The [README](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/README.md) has the full steps.
