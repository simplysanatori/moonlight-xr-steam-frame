# Moonlight XR for Frame 1.0.0

This is the first stable build, after the three alphas. The black picture at high bitrates is fixed. Sound, the picture, the Exit button and the stream itself have been checked in the headset. The pointer and the network choice were rebuilt and tested on the desktop and with automated tests. The items I have not yet seen in the headset are listed under Known problems.

## What changed since 0.6.0-alpha.2

### The black picture

At 126 Mbps the picture stayed black in the void and in rooms, because the hardware decoder was given only the first part of each frame.

- The hardware decode path handed the decoder at most 64 pieces of each frame. A frame arrives as one piece per network packet, so any frame over about 86 KB was cut short and the decoder flagged the picture as corrupt. At 126 Mbps and 90 fps every frame is about 175 KB, so nothing was shown. Every piece is now submitted. If the pieces do not add up to the frame's length, the frame is dropped and counted.
- At 80 Mbps the cut hit full frames and frames with a lot of motion. It probably caused part of the stutter seen with motion at that bitrate. That stutter was first put down to the network.
- After a decoder error, the app asks the host for a full frame (IDR) at most once per 100 ms. Before, it asked about 190 times a second. Set `MLXR_VIDEO_IDR_MIN_MS` to change the interval, or to 0 for no limit.
- A frame that ends in a decoder error now counts as out of the decoder, so the frame loop no longer waits 4 ms for it on every refresh.
- The "hw decode" part of the stats line ends with "largest AU N bytes in N parts, N refused (length mismatch)".

### Streaming and picture

- The flat window's default bitrate is twice Moonlight's figure for the same mode: 98 Mbps at 1440p and 90 fps, capped at 150 Mbps. A value you saved stays until you change the resolution or the frame rate. The Steam Frame Wireless Adapter lost packets above 40 to 60 Mbps in earlier tests, so lower the slider if you stream over it and see losses.
- The curved and 3D picture, the rooms and the controller models now have one swapchain per eye. SteamVR shrinks any scene texture over 3458 pixels, and the old double-wide texture gave each eye about 1729 pixels whatever the resolution setting. Each eye now gets its own texture of up to 3458 pixels. `MLXR_EYE_SCALE` (0.5 to 1.6) scales the size. The app uses up to 11 of SteamVR's 16 swapchains.

### Pointer

- Double click: after a click the cursor waits where it was released while your aim stays within 24 pixels and less than 500 ms have passed since the press. A second press then lands on the same point. `MLXR_DOUBLE_CLICK_PX` and `MLXR_DOUBLE_CLICK_MS` change the two limits, and 0 turns the wait off.
- Pulling the held picture closer with the stick now works at the same speed as pushing it away. A slanted thumb counts.

### Network

- The Steam Frame Wireless Adapter is only probed or chosen when a device is associated with the Frame's access point. `mlxr-cli adapter` shows what the app sees.
- Automatic picks the path with the better latency score (median round trip, jitter, the slow end and failed probes), plus the losses of your last stream on that path. The last stream's figures are kept in `netpath-history.ini` in the config folder, without addresses or names. The log gives the reason for each choice.

### Flat window

- After a stream, the flat window comes back. The Exit button brings it back in the headset. In Game Mode the launcher also shows it again if it was not exposed after a second, and logs what the window does.
- In Game Mode the flat window is 1.5 times larger (`MLXR_LAUNCHER_SCALE`).
- The Help button opens this repository's README.

### Project

- The repository and the tarball are named moonlight-xr-steam-frame. The Android route is moonlight-xr-lepton.

### Refresh rate

- 120 and 144 Hz are in the headset's refresh list. Picking 72, 90, 120 or 144 Hz in Settings sets SteamVR's per-app rate for this app before the stream starts, and the stream follows the rate the headset runs at, 144 Hz at most. Match headset leaves SteamVR's setting alone. The default bitrate scales with the rate: 113 Mbps at 1440p and 120 Hz, 124 Mbps at 144 Hz.

## Download

- `moonlight-xr-steam-frame-1.0.0-linux-arm64.tar.gz`
- `SHA256SUMS`

## Install

Same as before: unpack the tarball and run `install.sh`. It replaces the installed files and keeps your settings and pairing. See the [README](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/README.md) for the full steps and the [user guide](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/docs/USER_GUIDE.md) for the controls.

## Known problems

Confirmed in the headset: the picture and sound with the decoder fix, the better picture with a texture per eye, and the Exit button bringing the flat window back. I preferred 80 Mbps to 126 Mbps for how it felt.

Not yet seen in the headset:

- Double click. It was tested with unit tests only. If the cursor feels held after a click, set `MLXR_DOUBLE_CLICK_PX=12`. Two small buttons less than 24 pixels apart, clicked within half a second, can count as a double click.
- Text sharpness with the curve on. The picture is better, but I have not yet compared the text against the flat picture.
- The 1.5 times larger flat window in Game Mode. It was checked in offscreen renders, not in gamescope.
- Pulling the picture closer with the stick. The cause was read from timings in the log, and the fix is tested with unit tests.
- Choosing the network by score. The history file only exists after a first stream on each path.
- 120 and 144 Hz.

Everything else is unchanged from 0.6.0-alpha.1. See its [release notes](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/docs/RELEASE_NOTES_v0.6.0-alpha.1.md).

## Reporting problems

Open an issue and attach the zip from `bug-report.sh`. Say what you did, what you expected and what happened.
