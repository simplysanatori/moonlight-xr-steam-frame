# Moonlight XR for Frame 0.6.0-alpha.3

A fix for a black picture at high bitrates. At 126 Mbps the picture stayed black in the void and in rooms, because the hardware decoder was given only the first part of each frame. This build gives it the whole frame.

## What changed

- The hardware decode path handed the decoder at most 64 pieces of each frame. A frame arrives as one piece per network packet, so any frame over about 86 KB was cut short and the decoder flagged the picture as corrupt. At 126 Mbps and 90 fps every frame is about 175 KB, so nothing was shown. Every piece is now submitted. If the pieces do not add up to the frame's length, the frame is dropped and counted.
- At 80 Mbps the cut hit full frames and frames with a lot of motion. It probably caused part of the stutter seen with motion at that bitrate. That stutter was first put down to the network.
- After a decoder error, the app asks the host for a full frame (IDR) at most once per 100 ms. Before, it asked about 190 times a second. Set `MLXR_VIDEO_IDR_MIN_MS` to change the interval, or to 0 for no limit.
- A frame that ends in a decoder error now counts as out of the decoder, so the frame loop no longer waits 4 ms for it on every refresh.
- The "hw decode" part of the stats line ends with "largest AU N bytes in N parts, N refused (length mismatch)".

## Download

- `moonlight-xr-steam-frame-0.6.0-alpha.3-linux-arm64.tar.gz`
- `SHA256SUMS`

## Install

Same as before: unpack the tarball and run `install.sh`. It replaces the installed files and keeps your settings and pairing. See the [README](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/README.md) for the full steps and the [user guide](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/docs/USER_GUIDE.md) for the controls.

## Known problems

The fix was tested headless and with unit tests. At the time of writing it has not yet been run in a headset. If the picture is still black at 126 Mbps, report it with the bug-report zip.

Everything else is unchanged from 0.6.0-alpha.1. See its [release notes](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/docs/RELEASE_NOTES_v0.6.0-alpha.1.md).

## Reporting problems

Open an issue and attach the zip from `bug-report.sh`. Say what you did, what you expected and what happened.
