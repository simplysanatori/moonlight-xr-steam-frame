# Moonlight XR for Frame

A game-streaming client for the Valve Steam Frame. It plays games and the desktop from your PC on a virtual screen in SteamVR. The PC runs [Sunshine](https://github.com/LizardByte/Sunshine) or [Apollo](https://github.com/ClassicOldSong/Apollo).

It is a native Linux Arm64 program written in C, using OpenXR and Vulkan. You do not need Lepton or an Android build. The in-headset part is a port of Sean Gilleece's [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr). The flat window where you pick a PC and a game is [Moonlight](https://github.com/moonlight-stream/moonlight-qt) 6.1.0 with a few patches.

What it does:

- Decodes H.264 and HEVC on the Frame's hardware video decoder.
- Runs at the headset's refresh rate (90 Hz, or 72 Hz when SteamVR runs at 72).
- Shows the stream on a flat or curved screen, in a black void, in passthrough or in one of three rooms.
- Turns 2D games into 3D on the headset with a depth model (ZipDepth or MiDaS).
- Uses the Frame controllers as a mouse pointer or as an Xbox gamepad.
- Forwards Bluetooth keyboards, mice and gamepads connected to the Frame.
- Can stream over your Wi-Fi or over the Steam Frame Wireless Adapter. It looks for the adapter first, and Automatic picks the path with the lower latency and fewer losses.

## What this adds to Moonlight XR

Sean Gilleece's Moonlight XR is an Android app for headsets like the Quest. This project is a native rewrite for the Steam Frame. Compared with it:

- It is a Linux Arm64 program using OpenXR and Vulkan in the Frame's own SteamVR, with no Android layer.
- Video is decoded on the Frame's Qualcomm decoder (H.264 and HEVC) and handed to the GPU without a copy.
- Before the stream you get Moonlight's own PC list, pairing and app list (a patched moonlight-qt), shown flat in the headset, with an Artemis-style settings page.
- Apollo options are in that settings page: virtual display, resolution scale factor, custom refresh rate and clipboard sync.
- A Network connection setting for Wi-Fi or the Steam Frame Wireless Adapter. It detects the adapter, and Automatic chooses by latency and drops.
- A full tenkeyless keyboard with Ctrl, Alt, Shift and Win and key repeat, next to the compact one, with a move bar.
- Steam Frame controller models built at install time from SteamVR's own render models, with moving buttons and sticks.
- Frame pacing ported from Moonlight on a PC, with a low-latency default.
- A texture per eye for the curved and 3D picture and the rooms, so SteamVR's size limit does not shrink them.
- An installer that adds the Steam library entry with artwork, SteamVR's per-app settings and a dock icon.
- A bug report tool that masks addresses, names and ids, and a report sheet that saves the note on the Frame.
- It builds on the Frame itself.

The rooms, 3D mode, glow and virtual surround come from Moonlight XR and are kept.

## Status

Version 1.0.0 is the first stable build. It has been used on one Steam Frame (SteamOS 0.4.5, SteamVR 2.18) against one Windows PC with Sunshine and Apollo.

Seen working in the headset: the stream with hardware decode at 1440p and 90 Hz, sound, the pointer, the Exit button that brings the flat window back, the in-headset bar and menus, rooms, the controller models and the Start+Select+L1+R1 disconnect. Tested only on the desktop or by automated tests: physical gamepads, the Network connection setting, the Apollo options, virtual surround and hand tracking. See the [release notes](docs/RELEASE_NOTES_v1.0.0.md) for the full list and the known problems.

## Requirements

- A Steam Frame with SteamOS and SteamVR.
- A PC with Sunshine or Apollo, on the same network as the Frame or connected to it through the Steam Frame Wireless Adapter.
- About 250 MB free on the Frame.
- A terminal on the Frame for the install: SSH, or Konsole from the desktop (SteamVR dashboard, Launch a program, Desktop).
- Steam running on the Frame while you install, so the installer can add the library entry.

## Install

1. Download `moonlight-xr-steam-frame-1.0.0-linux-arm64.tar.gz` and `SHA256SUMS` from the [Releases](https://github.com/simplysanatori/moonlight-xr-steam-frame/releases) page. Check the download:

   ```sh
   sha256sum -c --ignore-missing SHA256SUMS
   ```

2. Unpack it and run the installer:

   ```sh
   tar xzf moonlight-xr-steam-frame-1.0.0-linux-arm64.tar.gz
   cd MoonlightXRNative
   ./install.sh
   ```

   The installer copies the program to `~/Games/MoonlightXRNative/app`. It adds **Moonlight XR for Frame** to your Steam library as a non-Steam shortcut, with its artwork. It sets SteamVR's per-app settings for that shortcut (90 Hz, no motion smoothing; the Refresh rate in Settings can change the rate later). It also converts SteamVR's own Steam Frame controller models for the app. You can run it again at any time.

3. Start **Moonlight XR for Frame** from the Steam library.

The installer changes nothing outside the folders listed under [Files](#files), the Steam shortcut and SteamVR's settings for that shortcut. `./install.sh --help` lists its options.

The icon in the SteamVR dock stays grey until Steam has been restarted once after the first install.

## First run and pairing

The first screen is Moonlight's PC list, shown flat in the headset.

If you already use the Moonlight flatpak on the Frame, your PCs and their pairing are copied over on the first start. Moonlight's own files are only read.

Otherwise:

1. Your PC shows up in the list if it is on the same network. If it does not, choose the **+** button at the top (Add PC manually) and type its address.
2. Select the PC. Moonlight shows a PIN.
3. Open the Sunshine or Apollo web page on the PC (`https://localhost:47990`), go to the PIN page and type the PIN.
4. Select the PC again to see its apps, then select an app.

The stream opens in the headset. When it ends, the flat Moonlight window comes back.

The [user guide](docs/USER_GUIDE.md) explains the settings, the controls and the in-headset menus.

## Update

Download the new tarball, unpack it and run `./install.sh` again. Your settings, your pairing and the Steam shortcut are kept.

## Uninstall

1. In Steam, remove the **Moonlight XR for Frame** shortcut (Manage, Remove non-Steam game from your library).
2. Remove the SteamVR dock entry: `python3 MoonlightXRNative/dock_icon.py remove ~/Games/MoonlightXRNative/app` from the unpacked release.
3. Delete the files:

   ```sh
   rm -rf ~/Games/MoonlightXRNative ~/.config/moonlight-xr "$HOME/.config/Moonlight XR Native" "$HOME/.cache/Moonlight XR Native"
   ```

   This also deletes the pairing. Unpair the Frame in the Sunshine or Apollo web page too.

## Files

| What | Where |
| --- | --- |
| Program | `~/Games/MoonlightXRNative/app` |
| Headset settings, pairing keys, saved PCs | `~/.config/moonlight-xr` (`config.ini`, `hosts.ini`, `client.pem`, `client.key`, `servercerts/`) |
| Flat window settings and PC list | `~/.config/Moonlight XR Native/Moonlight XR Native.conf` |
| Logs | `~/Games/MoonlightXRNative/moonlight.log`, `moonlight-launcher.log`, `moonlight-console.log`, `vrtiming.log` |
| Bug reports | `~/Games/MoonlightXRNative/reports` |

The logs keep your PC's name and addresses, so you can check your own network. Do not post them as they are.

## Reporting a problem

Run this after the problem happened:

```sh
~/Games/MoonlightXRNative/app/bug-report.sh
```

It writes a zip to `~/Games/MoonlightXRNative/reports` and lists every file in it. The zip holds only what is needed to debug:

- The app logs: `moonlight.log`, `moonlight.log.1`, `moonlight-console.log`, `moonlight-launcher.log` and `vrtiming.log`, the last 20000 lines of each.
- Your note from the headset, if you wrote one.
- `config.ini`, with only the settings you changed. Numbers, on and off, and fixed choices stay. Any key that looks like an address, host, id, name, path, key, token or certificate is dropped.
- `VERSION` and `system.txt`: the SteamOS, SteamVR and Mesa (Turnip) versions, the kernel release and the display refresh rate.

Everything in the zip is masked. IPv4 and IPv6 addresses, MAC addresses, host names, `.local` names, host names in URLs, your user name, UUIDs, long hex ids, Steam ids, e-mail addresses, Wi-Fi names (including the Frame's own access point), serial numbers, certificates and keys are replaced. Home folders show as `~`. Timestamps and timings stay.

Left out completely: `hosts.ini`, pairing keys and certificates, the flat window's settings, the host name, environment variables and any network listing.

Have a look inside before you share it. Then attach the zip to an [issue](https://github.com/simplysanatori/moonlight-xr-steam-frame/issues). Say what you did, what you expected and what happened.

You can also write a note in the headset (cog, About, Report a problem). It is saved on the Frame, nothing is sent from the headset, and the next `bug-report.sh` run includes it. Open an issue on GitHub with the zip.

## Build from source

See [docs/BUILDING.md](docs/BUILDING.md). The build runs on the Frame itself.

## Support

Moonlight XR is Gilleece's work. If you want to support someone, support upstream first: [Ko-fi: moonlightxr](https://ko-fi.com/moonlightxr).

If you would like more Steam Frame ports, you can also [buy me a coffee](https://ko-fi.com/sana748653). It pays for the Claude API credits these ports are built with, spent as carefully as I can.

## Credits

- Built by [simplysanatori](https://github.com/simplysanatori).
- [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr) by Sean Gilleece. Every in-headset module here is a port of it: screen layout, bar and menus, rooms, 3D, virtual surround. Its logo is the base of the app icon and the Steam artwork.
- [Moonlight](https://github.com/moonlight-stream) by the Moonlight Game Streaming Project (Cameron Gutman, Diego Waxemberg, Aaron Neyer, Andrew Hennessy and contributors): [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) for the streaming protocol, and [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) for the flat window and the host client behaviour.
- [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) from Khronos, [LiteRT](https://github.com/tensorflow/tensorflow) for the depth models, [ZipDepth](https://github.com/fabiotosi92/ZipDepth) by Fabio Tosi and [MiDaS](https://github.com/isl-org/MiDaS) by Intel ISL, KEMAR head-related transfer functions (MIT Media Lab), and Noto Sans.
- The three rooms and the generic controller model are by Sean Gilleece / Woodford XR (CC BY 4.0).

[CREDITS.md](CREDITS.md) has the full list with licences and links.

## Licence

GNU General Public License v3. See [LICENSE.txt](LICENSE.txt).

## How this was made

I wrote this with Claude Code, using Anthropic's Fable 5.1 and Opus 5.5 models, running on the Frame itself. I chose what to build, tested each build in the headset, reported what I saw and reviewed the result myself. Developer notes are in [docs/dev](docs/dev/README.md).
