# Changelog

## 0.6.0-alpha.3 (October 2026)

- Fixed a black picture at high bitrates. The hardware decoder was given at most 64 pieces of each frame, and a frame is one piece per received packet. Any frame over about 86 KB was cut short, and the decoder flagged the picture as corrupt. At 126 Mbps and 90 fps every frame is about 175 KB, so no picture came out. Every piece is now submitted. A frame whose pieces do not add up to its length is dropped and counted.
- At 80 Mbps the same cut hit full frames and frames with a lot of motion. It probably caused part of the stutter seen with motion at that bitrate.
- Requests for a full frame (IDR) after a decoder error are limited to one per 100 ms. Before, every corrupt picture asked the host for a new one, about 190 requests a second, and the host answered each by encoding a frame at once.
- A frame that ends in a decoder error now counts as out of the decoder. Before, the frame loop waited 4 ms for a picture on every refresh for nothing.
- The "hw decode" part of the stats line ends with the largest frame seen: "largest AU N bytes in N parts, N refused (length mismatch)".
- New environment variable `MLXR_VIDEO_IDR_MIN_MS`: the shortest time between full-frame requests after a decoder error. 100 by default, 0 for no limit.
- Not yet confirmed in the headset. It was tested with 133 automated test suites, a unit test with 400-packet and 200-packet frames, and a headless file run with 0 decode errors.

## 0.6.0-alpha.2 (October 2026)

- bug-report.sh now collects the minimum: the app logs, your note from the headset, the version, a short system summary and a filtered copy of config.ini. Addresses, host names, user names, ids, serial numbers, Wi-Fi names, keys and certificates are masked or left out. The zip lists its files when done.
- The README says how this was made, in my own words.
- The Report a problem sheet no longer asks for an email address. It saves your note on the Frame and asks you to open an issue on GitHub with the bug-report zip.

## 0.6.0-alpha.1 (October 2026)

The first public build. It is an alpha: expect rough edges, and read the [release notes](RELEASE_NOTES_v0.6.0-alpha.1.md) for what has and has not been tested.

### Streaming

- Connects to Sunshine and Apollo with the Moonlight protocol, including pairing with a PIN.
- Decodes H.264 and HEVC on the Frame's Qualcomm video decoder and hands the frames to the GPU without a copy. A 1440p HEVC frame decodes in about 4 ms.
- Streams at the headset's refresh rate by default (Match headset). Every rate is capped at 90 Hz.
- Paces frames like Moonlight on a PC, with a choice of lowest latency, balanced or smoothest.
- Recovers from lost packets on HEVC without waiting for a full new frame.
- Stereo, 5.1 and 7.1 sound. 5.1 and 7.1 can be placed around you in stereo (virtual surround).
- Picks the link to the PC: your Wi-Fi or the Steam Frame Wireless Adapter. Automatic tries both and uses the faster one. If the chosen link does not answer, it uses the other and tells you.
- Apollo options: virtual display, resolution scale factor, custom refresh rate and clipboard sync.

### Flat window

- Moonlight 6.1.0's own PC list, pairing, app list and Resume/Quit, shown flat in the headset.
- A settings page for the headset: the network link, Match headset refresh rate, 3D, Apollo options. Options the Frame cannot use (HDR, AV1, frame rates above 90) are greyed with the reason.
- Copies the PC list and pairing from the Moonlight flatpak the first time, if it is installed.
- Hides while the stream runs and comes back when it ends.

### In the headset

- Moonlight XR's screen, bar and cog menu: distance, height, tilt, rotation, curve and size; sharpening and supersampling; picture brightness, contrast, gamma and saturation.
- Black void, passthrough, and three rooms: Home Theater, Grand Cinema and Synthwave, with glow and screen light.
- Real-time 2D to 3D with the ZipDepth or MiDaS depth model, with presets and depth and convergence sliders.
- A stats window you can move with the grip.
- A headset keyboard, compact or full tenkeyless, that you can move.
- Steam Frame controller models made from SteamVR's own models on your Frame. They move with the buttons and sticks.

### Controls

- Pointer mode: the controller ray is the mouse. Trigger to click, A or thumbstick click to right click, thumbstick to scroll. Clicks land where you aimed before the trigger pull.
- Gamepad mode: the two Frame controllers act as one Xbox controller, with rumble. Hold the left View button and the left grip to switch.
- Bluetooth keyboards, mice and gamepads connected to the Frame are forwarded to the PC.
- Start+Select+L1+R1 on a gamepad, or Ctrl+Alt+Shift+Q on a keyboard, disconnects and leaves the game running.

### Install and support

- `install.sh` adds a Steam library entry with artwork, SteamVR settings for the app (90 Hz, no motion smoothing) and a SteamVR dock icon.
- `bug-report.sh` bundles the logs with addresses, names and ids masked.
