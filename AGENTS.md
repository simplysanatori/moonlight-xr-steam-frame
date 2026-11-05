# AGENTS.md

This file holds the technical detail moved out of the README, for coding agents and developers.

- Building: [docs/BUILDING.md](docs/BUILDING.md). The build runs on the Frame itself.
- Developer notes (plans, status, headset test scripts, area reports): [docs/dev/README.md](docs/dev/README.md).
- What users see, every setting and control: [docs/USER_GUIDE.md](docs/USER_GUIDE.md).
- Version history: [docs/CHANGELOG.md](docs/CHANGELOG.md); the [1.0.0 release notes](docs/RELEASE_NOTES_v1.0.0.md) describe the first release.

## What this is

A game-streaming client for the Valve Steam Frame. It plays games and the desktop from your PC on a virtual screen in SteamVR. The PC runs [Sunshine](https://github.com/LizardByte/Sunshine) or [Apollo](https://github.com/ClassicOldSong/Apollo).

It is a native Linux Arm64 program written in C, using OpenXR and Vulkan. You do not need Lepton or an Android build. The in-headset part is a port of Sean Gilleece's [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr). The flat window where you pick a PC and a game is [Moonlight](https://github.com/moonlight-stream/moonlight-qt) 6.1.0 with a few patches.

## Status

Version 1.2.0 is the current stable build. 1.0.0 was the first. It has been used on one Steam Frame (SteamOS 0.4.5, SteamVR 2.18) against one Windows PC with Sunshine and Apollo.

## What this adds to Moonlight XR

Sean Gilleece's Moonlight XR is an Android app for headsets like the Quest. This project is a native rewrite for the Steam Frame. Compared with it:

- It is a Linux Arm64 program using OpenXR and Vulkan in the Frame's own SteamVR, with no Android layer.
- Video is decoded on the Frame's Qualcomm decoder (H.264 and HEVC) and handed to the GPU without a copy.
- Before the stream you get Moonlight's own PC list, pairing and app list (a patched moonlight-qt), shown flat in the headset, with an Artemis-style settings page.
- Apollo options are in that settings page: virtual display, resolution scale factor, custom refresh rate and clipboard sync.
- A Network connection setting for Wi-Fi, the Steam Frame Wireless Adapter or a USB cable, with a start bitrate for each (up to 500 Mbps). Automatic chooses by latency and drops.
- A full tenkeyless keyboard with Ctrl, Alt, Shift and Win and key repeat, next to the compact one, with a move bar, AZERTY and QWERTZ layouts and a row of media keys.
- A Host sheet on the bar for keys that are awkward to reach (Esc, Win, Alt+Enter and more) and your own macros.
- Controller bindings for every button, for every app or one app, with an editor in the flat window.
- Steam Frame controller models built at install time from SteamVR's own render models, with moving buttons and sticks.
- Frame pacing ported from Moonlight on a PC, with a low-latency default.
- A texture per eye for the curved and 3D picture and the rooms, so SteamVR's size limit does not shrink them.
- An installer that adds the Steam library entry with artwork, SteamVR's per-app settings and a dock icon.
- A bug report tool that masks addresses, names and ids, and a report sheet that saves the note on the Frame.
- It builds on the Frame itself.

The rooms, 3D mode, glow and virtual surround come from Moonlight XR and are kept.

## Where the features come from

Four projects meet here. [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr) by Sean Gilleece is the in-headset app this one ports. [Moonlight](https://github.com/moonlight-stream) is the streaming client and protocol underneath. [Apollo](https://github.com/ClassicOldSong/Apollo) is a host for the PC, a fork of Sunshine with extra features; nothing of Apollo runs on the Frame, but some features here need it on the PC. [Artemis](https://github.com/ClassicOldSong/moonlight-android) is the Android Moonlight client by Apollo's author, ClassicOldSong; this client follows how Artemis asks Apollo for those features, and its Settings page follows Artemis's.

| Source | What it gives this client |
| --- | --- |
| Moonlight XR (Sean Gilleece), ported from Java and OpenGL ES to C, Vulkan and OpenXR | The flat or curved screen and its rows (distance, height, tilt, rotate, curve, size), grabbing and moving it, the bar and the cog menu. The rooms (Home Theater, Grand Cinema, Synthwave), the black void and passthrough. The glow and the picture rows (brightness, contrast, gamma, saturation, Sharpen, Supersample). 2D to 3D with the ZipDepth and MiDaS depth models, with Depth and Convergence. Virtual surround. Pointer mode with the ray, the dot, pointer sleep and the click sound; head aim; the hand-tracking pinch click. Gamepad mode. The compact headset keyboard. The stats window. The headset's translations into 19 languages. The logo. |
| Moonlight (moonlight-common-c, moonlight-qt 6.1.0) | The streaming protocol. The flat window: the PC list, adding a PC, pairing with a PIN, the app list, Resume and Quit, Wake-on-LAN, and the PCs and pairing imported from the Moonlight flatpak. The stream settings kept under Moonlight's own names (resolution, refresh rate, bitrate, codec, surround sound, Optimize game settings, Play audio on PC, Force software decoding, the window's language). Frame pacing and the disconnect chords. |
| Artemis (followed) | The Settings page's categories, row names and descriptions. Video frame pacing's three choices, Ultra low latency, Quick resume, the analog stick dead zone, Flip face buttons, Gamepad as mouse, stream settings profiles. How the Apollo features below are asked for. |
| Needs an Apollo host | **Use virtual display** and **Resolution scale factor**; a refresh rate with decimals such as 59.94 (**Custom...**); **Smart clipboard sync**; the Host sheet's **Paste from PC**, **Send text to PC** and server commands; starting an app by its UUID; the permission part of **Check the PC before streaming**. |
| New for the Steam Frame: platform and network | A native Linux Arm64 program on OpenXR and Vulkan, decoding H.264 and HEVC on the Frame's hardware decoder with no copy to the GPU. **Match headset** and 72 to 144 Hz, set in SteamVR before the stream. **Network connection** (Wi-Fi, Steam Frame Wireless Adapter, USB cable, **Automatic**) with a bitrate for each, switching links when video keeps breaking up, and VPN only when the PC isn't on the local network. Reconnecting after a drop; disconnecting when the headset is off or idle. |
| New for the Steam Frame: picture and 3D | **3D source** (side by side or top-bottom from the PC), Full-size SBS, 3D edge, motion reset, depth detail, depth shape and eye balance. Sharper small text and coloured text, zoom and pan, screen presets, Bring here, Remember per app, Follow, Angular-size lock, Background dim, Glow edge, Void colour. |
| New for the Steam Frame: controls | Steam Frame controller models built from SteamVR's own files, the Gun and lightgun mode, controller bindings for every button with an editor, Click steadiness, Relative mouse, the head turning a first-person game's camera, opentrack head tracking, gyro from the headset, Bluetooth and USB gamepads passed through with rumble, gyro, touchpad and light bar, the gamepad card. The full keyboard, AZERTY and QWERTZ, media keys. The Host sheet with your own macros. The Accessibility tab: pointing with the head or the eyes (experimental), Dwell click, Dot size. |
| New for the Steam Frame: menus and flat window | Notices, the HUD and Lite stats, the battery and slow-connection badge, the first-run card, Headset language. The Settings page's Connection rows and headset rows, search and jump list, Check the PC before streaming, On start, the update check. The installer, the Steam shortcut and artwork, and the bug report tool. |

## How Apollo is detected and used

The client knows a PC is Apollo by what the PC reports about itself (its virtual display and permission fields, or its server commands), not by a version number, so a fork of Apollo that keeps those works the same way. It asks for Apollo's features with extra fields in the launch request (virtual display, scale factor, a refresh rate with decimals, the app's UUID), an HTTPS call for the clipboard, and one control-stream message for a server command.

What the user sets up on the PC is in [Setting up Apollo](docs/USER_GUIDE.md#setting-up-apollo) and [Apollo on the PC: recipes](docs/USER_GUIDE.md#apollo-on-the-pc-recipes).

## Decoding, frame pacing and textures per eye

- Video is decoded on the Frame's Qualcomm hardware decoder (H.264 and HEVC) and handed to the GPU without a copy.
- Frame pacing is ported from Moonlight on a PC, with a low-latency default.
- The curved and 3D picture and the rooms use a texture per eye, so SteamVR's size limit does not shrink them. The Frame still holds 90 Hz at 3408 x 3408 per eye, because the app only draws the screen and its surroundings.

These points are also in the lists above; the developer notes in [docs/dev/README.md](docs/dev/README.md) have the reports for each.

## Installer internals

`./install.sh` copies the program to `~/Games/MoonlightXRNative/app`. It adds **Moonlight XR for Frame** to your Steam library as a non-Steam shortcut, with its artwork. It sets SteamVR's per-app settings for that shortcut: 90 Hz, no motion smoothing, and a render resolution of 3408 x 3408 per eye, the highest SteamVR allows (the Refresh rate in Settings can change the rate later). It also converts SteamVR's own Steam Frame controller models for the app. You can run it again at any time. The folders it writes are listed under [Files](docs/USER_GUIDE.md#files).

### FrameDrop package

The release has a `moonlight-xr-steam-frame-<version>-framedrop-linux-arm64.zip` and a `moonlight-xr-steam-frame.framedrop.json` for it. FrameDrop unpacks the zip on the Frame and adds its own library entry, which runs the `run.sh` in the zip. The first time it runs, and again after an update, `run.sh` runs `install.sh` with the Steam shortcut step turned off, so you get FrameDrop's entry and no second one from the installer, and then starts Moonlight XR through its launcher.

## Credits in detail

The README keeps a short list; [CREDITS.md](CREDITS.md) has the full list with licences and links. The old README's credits, as written:

- Built by [simplysanatori](https://github.com/simplysanatori).
- [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr) by Sean Gilleece. Every in-headset module here is a port of it: screen layout, bar and menus, rooms, 3D, virtual surround. Its logo is the base of the app icon and the Steam artwork.
- [Moonlight](https://github.com/moonlight-stream) by the Moonlight Game Streaming Project (Cameron Gutman, Diego Waxemberg, Aaron Neyer, Andrew Hennessy and contributors): [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) for the streaming protocol, and [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) for the flat window and the host client behaviour.
- [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) from Khronos, [LiteRT](https://github.com/tensorflow/tensorflow) for the depth models, [ZipDepth](https://github.com/fabiotosi92/ZipDepth) by Fabio Tosi and [MiDaS](https://github.com/isl-org/MiDaS) by Intel ISL, KEMAR head-related transfer functions (MIT Media Lab), and Noto Sans.
- The three rooms are by Sean Gilleece / Woodford XR (CC BY 4.0).
- The Gun controller model is "[G17 Gen 5 Handgun + Olight - NSW Police Force](https://sketchfab.com/3d-models/g17-gen-5-handgun-olight-nsw-police-force-0200c248eba14385ad2bee8d4dc4e586)" by [8sianDude](https://sketchfab.com/haoliu95) (CC BY 4.0), converted and changed for the headset.

In the headset, the settings panel's About tab has a Ko-fi button for this port's page: **Open page** shows it in Steam's browser in the dashboard (see [About](docs/USER_GUIDE.md#about)).

## How this was made

I started this project and used AI (Claude: Claude Code with the Fable 5.1 and Opus 5.5 models, running on the Frame itself) to speed up development. I write, review and fix code by hand all the way through, keep the AI's work checked and reined in, and aim to keep the port as lightweight as possible. I test it on my own Steam Frame.

I had been using Moonlight to stream my PC to the Frame and wanted something like Virtual Desktop's VR environment for it, so I built a native version of Moonlight XR for the Frame. It started as a personal project for my own headset. It ended up good enough to share.

It is more than functional, and it is a starting point for anyone who wants Moonlight in a VR environment on the Frame.

## Project history and plans

It is heavily based on Moonlight XR by Gilleece. The rooms, the 2D to 3D depth mode, the menus and the virtual surround are ports of that work. The difference is that it runs natively on SteamOS instead of through Lepton. It is GPL, so take pieces for Moonlight XR or anything else.

Next steps are manual optimisation and possibly hand tracking, depending on how far the community efforts get. Developer notes are in [docs/dev](docs/dev/README.md).
