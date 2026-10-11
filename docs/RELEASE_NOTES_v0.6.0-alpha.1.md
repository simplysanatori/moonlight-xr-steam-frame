# Moonlight XR for Frame 0.6.0-alpha.1

The first public alpha of Moonlight XR for Frame: a native game-streaming client for the Valve Steam Frame. It streams from a PC running Sunshine or Apollo into a virtual screen in SteamVR. The in-headset part is a port of Sean Gilleece's Moonlight XR; the flat window is a patched Moonlight 6.1.0.

This is an alpha. It has been used on one Steam Frame (SteamOS 0.4.5, SteamVR 2.18) with one Windows PC. Some parts have only been tested without a headset. Please report what you find.

## Download

- `moonlight-xr-native-0.6.0-alpha.1-linux-arm64.tar.gz`
- `SHA256SUMS`

## Install

On the Frame, in a terminal (SSH, or Konsole from the desktop), with Steam running:

```sh
sha256sum -c --ignore-missing SHA256SUMS
tar xzf moonlight-xr-native-0.6.0-alpha.1-linux-arm64.tar.gz
cd MoonlightXRNative
./install.sh
```

Then start **Moonlight XR for Frame** from the Steam library, select your PC and pair it with the PIN. The [README](../README.md) has the details, and the [user guide](USER_GUIDE.md) covers the settings and controls.

If you already use the Moonlight flatpak on the Frame, your PCs and pairing are copied over on the first start.

## Seen working in the headset

- Streaming from Sunshine with hardware decode: 1440p, 90 fps, HEVC at 80 Mbps, about 4 ms per frame to decode, no decoder errors.
- 90 Hz in SteamVR for this app (set by the installer).
- Sound.
- The pointer: clicking, dragging, right click.
- The in-headset bar and cog menu.
- Rooms, with no judder when you move your head.
- The Steam Frame controller models.
- Start+Select+L1+R1 to disconnect.
- Starting a stream from the flat window.

## Tested only without a headset

These pass the automated tests on the Frame but nobody has checked them in the headset yet:

- The Network connection setting (Automatic, Wi-Fi, Steam Frame Wireless Adapter) and the fallback to the other link.
- The Apollo options: virtual display, scale factor, custom refresh rate, clipboard sync.
- Bluetooth gamepads, and the Bluetooth mouse.
- Ctrl+Alt+Shift+Q on a Bluetooth keyboard.
- Moving the stats window and the headset keyboard.
- The frame pacing and packet-loss recovery changes made after the last latency test.
- Virtual surround (5.1 and 7.1 placed around you).
- Hand tracking and eye-gaze pointing. Both are off by default.

## Known problems

- **Stutter on the Steam Frame Wireless Adapter link at high bitrates.** At 80 Mbps over the adapter, bursts of lost packets caused stutter and frame drops. Use 40 to 60 Mbps on that link.
- **Latency.** From the decoder to the display took 40 to 52 ms in testing, about 30 ms of which is SteamVR. Pacing changes since then are untested.
- **Crackling sound** was heard on a lossy link. Changes to the audio buffer since then are untested.
- **Text is soft.** Small text on a 1440p desktop is hard to read at the default size. Move the screen closer, or try Supersample Quality.
- **The flat window may not come back** after leaving a stream the second time. Start the app again from the library if it does not.
- **Grey SteamVR dock icon** until Steam has been restarted once after the first install.
- **3D** adds about 80 ms between the picture and its depth, so fast motion shows soft edges.
- A fresh install without the Moonlight flatpak starts at 1440p, 90 Hz, 80 Mbps. Lower the bitrate if you use the adapter.

## Reporting a problem

Run `~/Games/MoonlightXRNative/app/bug-report.sh` after the problem. It writes a zip to `~/Games/MoonlightXRNative/reports` with the app logs, your headset note, the settings you changed and a short version summary. IP and MAC addresses, host and network names, your user name and ids are masked. `hosts.ini`, pairing keys and certificates are never included. Look inside, then attach the zip to an issue on GitHub, with what you did and what happened.

## Credits

Moonlight XR by Sean Gilleece; Moonlight by the Moonlight Game Streaming Project; built by simplysanatori. Licence: GPLv3.
