# Moonlight XR for Frame 1.0.1

A fix for the volume-button crash. Nothing else changed since 1.0.0.

### Fixed

- Pressing a volume button on the headset during a stream crashed SteamOS and rebooted the Frame. The launcher hid its window for the stream, which left the compositor with nothing to give keyboard focus to when the volume key arrived. The window now stays open but invisible. `MLXR_LAUNCHER_FADE_WINDOW=0` restores the old behaviour.

## Download

- `moonlight-xr-steam-frame-1.0.1-linux-arm64.tar.gz`
- `SHA256SUMS`

## Install or upgrade

Unpack the tarball on the Frame and run `install.sh`. It keeps your settings and pairing. The [README](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/README.md) has the full steps.
