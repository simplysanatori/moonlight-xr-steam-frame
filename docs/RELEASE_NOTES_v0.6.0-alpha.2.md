# Moonlight XR for Frame 0.6.0-alpha.2

A small follow-up to the first alpha. The app changes in one place, the Report a problem sheet. The rest is in what the bug report collects and in the README.

## What changed

- `bug-report.sh` now collects only what is needed to debug: the app logs, your note from the headset, the version, a short system summary (SteamOS, SteamVR, Mesa and kernel versions, display refresh rate) and a filtered copy of `config.ini` with numbers, switches and fixed choices only. Addresses, host names, user names, ids, serial numbers, Wi-Fi names, keys and certificates are masked or left out. It prints the file list when done. Look inside before you share it.
- The README says how this was made, in my own words.
- The Report a problem sheet no longer asks for an email address. It saves your note on the Frame and asks you to open an issue on GitHub with the bug-report zip.

## Download

- `moonlight-xr-native-0.6.0-alpha.2-linux-arm64.tar.gz`
- `SHA256SUMS`

## Install

Same as before: unpack the tarball and run `install.sh`. It replaces the installed files and keeps your settings and pairing. See the [README](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/README.md) for the full steps and the [user guide](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/docs/USER_GUIDE.md) for the controls.

## Known problems

Unchanged from 0.6.0-alpha.1. See its [release notes](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/docs/RELEASE_NOTES_v0.6.0-alpha.1.md).

## Reporting problems

Open an issue and attach the zip from `bug-report.sh`. Say what you did, what you expected and what happened.
