# Changelog

## 1.0.0 (October 2026)

The first stable build. It includes everything from the three alphas and the changes below.

### Black picture at high bitrates

- Fixed a black picture at high bitrates. The hardware decoder was given at most 64 pieces of each frame, and a frame is one piece per received packet. Any frame over about 86 KB was cut short, and the decoder flagged the picture as corrupt. At 126 Mbps and 90 fps every frame is about 175 KB, so no picture came out. Every piece is now submitted. A frame whose pieces do not add up to its length is dropped and counted.
- At 80 Mbps the same cut hit full frames and frames with a lot of motion. It probably caused part of the stutter seen with motion at that bitrate.
- Requests for a full frame (IDR) after a decoder error are limited to one per 100 ms. Before, every corrupt picture asked the host for a new one, about 190 requests a second, and the host answered each by encoding a frame at once.
- A frame that ends in a decoder error now counts as out of the decoder. Before, the frame loop waited 4 ms for a picture on every refresh for nothing.
- The "hw decode" part of the stats line ends with the largest frame seen: "largest AU N bytes in N parts, N refused (length mismatch)".
- New environment variable `MLXR_VIDEO_IDR_MIN_MS`: the shortest time between full-frame requests after a decoder error. 100 by default, 0 for no limit.
- Tested with 133 automated test suites, a unit test with 400-packet and 200-packet frames, and a headless file run with 0 decode errors. In the headset I got a picture and sound with the fixed build.

### Streaming

- The flat window's default bitrate is now twice Moonlight's figure for the same resolution and frame rate, in steps of 0.5 Mbps and capped at 150 Mbps. At 1440p and 90 fps that is 98 Mbps (Moonlight: 49). A value you saved stays until you change the resolution or the frame rate. The Steam Frame Wireless Adapter lost packets above 40 to 60 Mbps in earlier tests, so lower the slider if you stream over it and see losses.

### Picture

- Every projection layer now has one swapchain per eye. SteamVR shrinks any scene texture wider or taller than 3458 pixels, and the curved or 3D picture and the rooms were drawn side by side in one double-wide texture, so each eye got about 1729 pixels whatever the resolution setting. Each eye now has its own texture of up to 3458 pixels. The flat picture (curve off, 3D off) was not affected.
- New environment variable `MLXR_EYE_SCALE` (0.5 to 1.6, default 1) scales the size of the picture layer's eye texture. The log says the size and whether it is inside SteamVR's limit.
- The room's resolution tiers follow the new rule per eye: at a 2160 recommendation Standard is 1760 (was 1728), High 2160 and Ultra 2800. High and Ultra were shrunk by SteamVR before.
- The app can now use up to 11 of SteamVR's 16 swapchains (it was 8). The stats line shows the count.

### Pointer

- Double click is easier. After a click, the cursor waits where it was released while your aim stays within 24 pixels of that point and less than 500 ms have passed since the press. A second press in that time goes down at the first press's point. Move further, or wait longer, and the cursor follows you at once. New variables `MLXR_DOUBLE_CLICK_PX` (24) and `MLXR_DOUBLE_CLICK_MS` (500); 0 for either turns the wait off.
- Pulling the held picture closer with the stick works at the same speed as pushing it away. The stick's whole tilt now counts while it leans south or north (within 60 degrees), so a slanted thumb is enough. Letting go of a held window logs how far each stick reached.

### Network

- The Steam Frame Wireless Adapter is only probed or chosen when a device is associated with the Frame's access point. With none, Automatic uses Wi-Fi and the log says why. If you force the adapter and none is there, the app uses Wi-Fi at once and shows a notice. `mlxr-cli adapter` prints the result.
- Automatic now picks the path with the lower latency score: median round trip, jitter, the slow end (90th percentile) and failed probes, plus the losses of your last stream on that path. It sends 15 probes per path instead of 7. The last stream's figures are kept in `netpath-history.ini` in the config folder, with no addresses or names. The "weak signal" rule is gone. The log explains each choice.

### Flat window

- After a stream the flat window did not always come back in the headset. The launcher now logs what the window does after the stream (shown, exposed, active, first frame drawn). In Game Mode it shows the window again if it was not exposed after one second. The Exit button now brings the flat window back (checked in the headset).
- In Game Mode the flat window is 1.5 times larger. `MLXR_LAUNCHER_SCALE` (1.0 to 2.5) changes it.
- The Help button opens this repository's README, not Moonlight's setup guide.

### Project

- The GitHub repository and the release tarball are now named moonlight-xr-steam-frame (before: moonlight-xr-native). The old repository address redirects. The Android route repository is now moonlight-xr-lepton.

### Pending

- 120 and 144 Hz in the headset's refresh list, and the launcher setting SteamVR's per-app rate (lane refresh, not merged yet).

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
