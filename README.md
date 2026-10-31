# Moonlight XR for Frame

A game-streaming client for the Valve Steam Frame. It plays games and the desktop from your PC on a virtual screen in SteamVR. The PC runs [Sunshine](https://github.com/LizardByte/Sunshine) or [Apollo](https://github.com/ClassicOldSong/Apollo).

It is a native Linux Arm64 program written in C, using OpenXR and Vulkan. You do not need Lepton or an Android build. The in-headset part is a port of Sean Gilleece's [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr). The flat window where you pick a PC and a game is [Moonlight](https://github.com/moonlight-stream/moonlight-qt) 6.1.0 with a few patches.

What it does:

- Decodes H.264 and HEVC on the Frame's hardware video decoder.
- Runs at the headset's refresh rate. **Match headset** (the default) uses the rate SteamVR runs the app at, 90 Hz unless you changed it; picking 72, 90, 120 or 144 Hz in Settings sets SteamVR's rate for the app before the stream starts. The stream never runs faster than the headset.
- Shows the stream on a flat or curved screen, in a black void, in passthrough or in one of three rooms.
- Turns 2D games into 3D on the headset with a depth model (ZipDepth or MiDaS), or shows the side-by-side or top-bottom 3D a PC game or film renders itself.
- Uses the Frame controllers as a mouse pointer or as an Xbox gamepad, and every button can be remapped (see [Controls](#controls)).
- Forwards Bluetooth keyboards, mice and gamepads connected to the Frame.
- Can stream over your Wi-Fi, over the Steam Frame Wireless Adapter or over a USB cable. **Automatic** measures every link that is there before each stream and picks the one with the lower latency and fewer losses.
- Use VPN addresses only away from home (on by default): if your PC also has a VPN address (Tailscale, WireGuard, ZeroTier or similar), the headset uses it only when the PC does not answer on your home network, and it learns the PC's home address by itself.

## What this adds to Moonlight XR

Sean Gilleece's Moonlight XR is an Android app for headsets like the Quest. This project is a native rewrite for the Steam Frame. Compared with it:

- It is a Linux Arm64 program using OpenXR and Vulkan in the Frame's own SteamVR, with no Android layer.
- Video is decoded on the Frame's Qualcomm decoder (H.264 and HEVC) and handed to the GPU without a copy.
- Before the stream you get Moonlight's own PC list, pairing and app list (a patched moonlight-qt), shown flat in the headset, with an Artemis-style settings page.
- Apollo options are in that settings page: virtual display, resolution scale factor, custom refresh rate and clipboard sync.
- A Network connection setting for Wi-Fi, the Steam Frame Wireless Adapter or a USB cable, with a start bitrate for each. Automatic chooses by latency and drops.
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
| Moonlight (moonlight-common-c, moonlight-qt 6.1.0) | The streaming protocol. The flat window: the PC list, adding a PC, pairing with a PIN, the app list, Resume and Quit, Wake-on-LAN, and the PCs and pairing imported from the Moonlight flatpak. The stream settings kept under Moonlight's own names (resolution, refresh rate, bitrate, codec, surround sound, Optimize game settings, Play audio on PC, software decoding, the window's language). Frame pacing and the disconnect chords. |
| Artemis (followed) | The Settings page's categories, row names and descriptions. Video frame pacing's three choices, Ultra low latency, Quick resume, the analog stick dead zone, Flip face buttons, Gamepad as mouse, stream settings profiles. How the Apollo features below are asked for. |
| Needs an Apollo host | **Use virtual display** and **Resolution scale factor**; a refresh rate with decimals such as 59.94 (**Custom...**); **Smart clipboard sync**; the Host sheet's **Paste from PC**, **Send text to PC** and server commands; starting an app by its UUID; the permission part of **Check the PC before streaming**. |
| New for the Steam Frame: platform and network | A native Linux Arm64 program on OpenXR and Vulkan, decoding H.264 and HEVC on the Frame's hardware decoder with no copy to the GPU. **Match headset** and 72 to 144 Hz, set in SteamVR before the stream. **Network connection** (Wi-Fi, Steam Frame Wireless Adapter, USB cable, **Automatic**) with a bitrate for each, switching links when video keeps breaking up, and VPN addresses only away from home. Reconnecting after a drop; disconnecting when the headset is off or idle. |
| New for the Steam Frame: picture and 3D | **3D source** (side by side or top-bottom from the PC), Full-size SBS, 3D edge, motion reset, depth detail, depth shape and eye balance. Sharper small text and coloured text, zoom and pan, screen presets, Bring here, Remember per app, Follow, Angular-size lock, Background dim, Glow edge, Void colour. |
| New for the Steam Frame: controls | Steam Frame controller models built from SteamVR's own files, the Gun and lightgun mode, controller bindings for every button with an editor, Click steadiness, Relative mouse, the head turning a first-person game's camera, opentrack head tracking, gyro from the headset, Bluetooth and USB gamepads passed through with rumble, gyro, touchpad and light bar, the gamepad card. The full keyboard, AZERTY and QWERTZ, media keys. The Host sheet with your own macros. The Accessibility tab: pointing with the head or the eyes, Dwell click, Dot size, Headset button clicks. |
| New for the Steam Frame: menus and flat window | Notices, the HUD and Lite stats, the battery and slow-connection badge, the first-run card, Headset language. The Settings page's Connection rows and headset rows, search and jump list, Check the PC before streaming, On start, the update check. The installer, the Steam shortcut and artwork, and the bug report tool. |

### How it works with Apollo

The client knows a PC is Apollo by what the PC reports about itself (its virtual display and permission fields, or its server commands), not by a version number, so a fork of Apollo that keeps those works the same way. It asks for Apollo's features with extra fields in the launch request (virtual display, scale factor, a refresh rate with decimals, the app's UUID), an HTTPS call for the clipboard, and one control-stream message for a server command.

On the PC, in Apollo's web UI:

- **Clients**: Apollo gives each paired client its own permissions. The first client paired gets all of them; one paired later gets only List Apps and View Streams. For the Frame, turn on **Launch Apps**, the input permissions (**Controller**, **Mouse**, **Keyboard**), **Clipboard Read** and **Clipboard Set** for the clipboard, and **Server Command** for server commands.
- **Virtual display**: Apollo on Windows with its virtual display driver installed. A **Display Mode Override** on the client's entry or **Always create virtual display** (Apollo 0.3.7 and later) change how it is made; see [Apollo on the PC: recipes](docs/USER_GUIDE.md#apollo-on-the-pc-recipes).
- **Clipboard**: Apollo 0.2.4 or later, text only, while the Frame is streaming.
- **Server commands**: Configuration, General, Server Commands (a name, a command, and whether it runs elevated). Up to eight show on the Host sheet; see [Clipboard and PC commands](docs/USER_GUIDE.md#clipboard-and-pc-commands-apollo).

**Check the PC before streaming** (on by default) reads this client's permissions and the virtual display's state just before each stream and says what is missing.

With Sunshine everything else works the same: streaming, pairing, apps, every headset feature. The Apollo fields are sent and ignored, so there is no virtual display and no scale factor; the Host sheet's PC page dims its cells and says "Not supported by this host"; and Check the PC before streaming says when a setting needs Apollo.

## Status

Version 1.1.0 is the current stable build. 1.0.0 was the first. It has been used on one Steam Frame (SteamOS 0.4.5, SteamVR 2.18) against one Windows PC with Sunshine and Apollo.

The 1.1.0 build went through the full headset checklist on 30 October 2026, and everything tested there worked except eye tracking. That is fixed since: the app now asks SteamVR for eye tracking whenever it is offered, and eye gaze reaches it on the Steam Frame from the Accessibility tab (checked with a gaze probe on 30 October 2026). The USB cable connection was not tried. The [changelog](docs/CHANGELOG.md) lists everything new in 1.1.0; the [1.0.0 release notes](docs/RELEASE_NOTES_v1.0.0.md) describe the first release.

## Requirements

- A Steam Frame with SteamOS and SteamVR.
- A PC with Sunshine or Apollo, on the same network as the Frame, connected to it through the Steam Frame Wireless Adapter, or connected to it with a USB data cable (the headset in developer mode; see [USB cable](docs/USER_GUIDE.md#usb-cable)).
- About 250 MB free on the Frame.
- A terminal on the Frame for the install: SSH, or Konsole from the desktop (SteamVR dashboard, Launch a program, Desktop).
- Steam running on the Frame while you install, so the installer can add the library entry.

## Install

1. Download `moonlight-xr-steam-frame-1.1.0-linux-arm64.tar.gz` and `SHA256SUMS` from the [Releases](https://github.com/simplysanatori/moonlight-xr-steam-frame/releases) page. Check the download:

   ```sh
   sha256sum -c --ignore-missing SHA256SUMS
   ```

2. Unpack it and run the installer:

   ```sh
   tar xzf moonlight-xr-steam-frame-1.1.0-linux-arm64.tar.gz
   cd MoonlightXRNative
   ./install.sh
   ```

   The installer copies the program to `~/Games/MoonlightXRNative/app`. It adds **Moonlight XR for Frame** to your Steam library as a non-Steam shortcut, with its artwork. It sets SteamVR's per-app settings for that shortcut (90 Hz, no motion smoothing; the Refresh rate in Settings can change the rate later). It also converts SteamVR's own Steam Frame controller models for the app. You can run it again at any time.

3. Start **Moonlight XR for Frame** from the Steam library.

The installer changes nothing outside the folders listed under [Files](#files), the Steam shortcut and SteamVR's settings for that shortcut. `./install.sh --help` lists its options.

From 1.1.0 on, releases also carry the same files as a `.zip` (`moonlight-xr-steam-frame-<version>-linux-arm64.zip`). The community installers [Frametanium](https://github.com/Cogential/frametanium) and FrameDrop can install the release from there; they run `install.sh` without a terminal. If Steam is not running then, the files are installed and the Steam shortcut is skipped; the installer says so (the last lines of its output, and `~/Games/MoonlightXRNative/install.log`), and running `install.sh` again with Steam up adds it.

The icon in the SteamVR dock stays grey until Steam has been restarted once after the first install.

To get a second shortcut, **Desktop in VR**, that opens your PC's Desktop in the headset straight away, run `./install.sh --desktop-shortcut` after you have streamed once from the flat window. It uses the PC you streamed from last (or the only one); `--desktop-pc "PC name"` picks another. When the stream ends, that shortcut closes.

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

## Controls

Every stream starts in pointer mode: the ray from either controller is the PC's mouse. In gamepad mode the two controllers are one Xbox controller on the PC.

| Control | Pointer mode | Gamepad mode |
| --- | --- | --- |
| Trigger | Left click, hold to drag | LT, RT |
| Grip | Grabs the move pill, a corner of the screen, the keyboard's bar or the stats window | LB, RB (**Grips in gamepad mode**: Bumpers, Nothing or Stick clicks) |
| Bumpers | Back (left) and Forward (right), the mouse's side buttons | LB, RB |
| Thumbstick | Scroll, while the ray is on the picture | Left and right stick |
| Thumbstick click | Right click | LS, RS |
| A | Right click | A |
| B | Middle click | B |
| X | 3D on or off | X |
| Y | Show or hide the headset keyboard | Y |
| Menu (right) | Open or close the cog menu | Start |
| View (left) | Nothing on its own | Back |
| D-pad up | Recentre: the screen comes round to where you face | D-pad up |
| D-pad down, right, left | Right click, middle click, nothing | D-pad |

| Shortcut | What it does |
| --- | --- |
| Hold left View and left grip for half a second | Switches between pointer and gamepad mode, both ways. **Gamepad mode shortcut** in Settings can make it a click of both thumbsticks, or both triggers and both grips squeezed, instead; only the chosen one works. In pointer mode the bar's Gamepad / Pointer button and the cog's Display tab **Mode** row switch too. |
| Hold left View and right Menu for a quarter of a second | Guide, the Xbox button (gamepad mode). |
| Hold right Menu, left View and both bumpers (Start+Select+L1+R1) | Disconnects; the game keeps running on the PC (gamepad mode, or the same buttons on a Bluetooth or USB gamepad). With the default **Grips in gamepad mode** the grips count as the bumpers. |
| Ctrl+Alt+Shift+Q on a keyboard connected to the Frame | Disconnects; the game keeps running on the PC. |
| Pinch thumb and index finger | With **Hand tracking** on (off by default): the trigger. Three quick pinches on one hand lock the hands out, so a stray pinch does not click; three more unlock them. |

The bar under the screen shows its buttons while the ray is over it. From left to right:

| Button | What it does |
| --- | --- |
| Head aim or Bring here | Head aim (with the screen head locked): your head moves the mouse. Bring here (with the screen in the world): switches the screen between the **Desk** preset and where it was before. |
| Gamepad / Pointer | Switches the mode for this session. |
| Exit | Asks **Quit app**, **Disconnect** (the game keeps running) or **Cancel**. |
| Environment | Opens the Rooms picker. |
| Move pill | Grip it to move the screen; push or pull with the thumbstick while you hold it. |
| Cog | Opens the cog menu. |
| Keyboard | Shows or hides the headset keyboard. |
| Ray | Shows or hides the ray for this session. |
| 3D | Turns 3D on or off (shown when 3D can run). |
| Host | Opens the Host sheet: Esc, Win, Alt+Enter and other keys, and your own macros. |

The cog menu's **Accessibility** tab (early) has **Pointer** (Controller, Head, Eyes), **Dwell click**, **Dot size** and **Headset button clicks**; eye tracking works on the Steam Frame. **Dwell click style** (Simple by default) adds a radial menu near the end of a dwell: look left for a double click, down to drag, right for a right click.

To change what a button does, edit `~/.config/moonlight-xr/bindings.ini` or use the flat window's **Controller bindings** page ([BINDINGS.md](docs/BINDINGS.md)); the mode switch, the disconnect chord and the grips' grab cannot be changed.

## Update

Download the new tarball, unpack it and run `./install.sh` again. Your settings, your pairing and the Steam shortcut are kept.

The flat window checks once a day whether a newer release is out and then shows an update button at the top that opens its release page. It asks GitHub only, sends nothing about you or your headset, and downloads nothing. Turn it off with **Check for updates** in Settings (General settings).

## Uninstall

1. In Steam, remove the **Moonlight XR for Frame** shortcut (Manage, Remove non-Steam game from your library), and **Desktop in VR** if you added it.
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
| Controller bindings, Host sheet macros, your own controller models | `~/.config/moonlight-xr` (`bindings.ini`, `macros.ini`, `models/`) |
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
- `ray.csv`, the ray recorder's file (header and its last 20000 rows, numbers only), if `MLXR_RAY_DEBUG=1` made one.
- `config.ini`, with only the settings you changed. Numbers, on and off, and fixed choices stay. Any key that looks like an address, host, id, name, path, key, token or certificate is dropped.
- `VERSION` and `system.txt`: the SteamOS, SteamVR and Mesa (Turnip) versions, the kernel release and the display refresh rate.

Everything in the zip is masked. IPv4 and IPv6 addresses, MAC addresses, host names, `.local` names, host names in URLs, your user name, UUIDs, long hex ids, Steam ids, e-mail addresses, Wi-Fi names (including the Frame's own access point), serial numbers, certificates and keys are replaced. Home folders show as `~`. Timestamps and timings stay.

Left out completely: `hosts.ini`, pairing keys and certificates, the flat window's settings, the host name, environment variables and any network listing.

Have a look inside before you share it. Then attach the zip to an [issue](https://github.com/simplysanatori/moonlight-xr-steam-frame/issues). Say what you did, what you expected and what happened.

You can also write a note in the headset (cog, About, Report a problem). **Save** keeps it on the Frame, and the next `bug-report.sh` run includes it. **Save and zip** runs `bug-report.sh` for you and shows where the zip is. Nothing is sent from the headset. Open an issue on GitHub with the zip.

## Build from source

See [docs/BUILDING.md](docs/BUILDING.md). The build runs on the Frame itself.

## Support

Moonlight XR is Gilleece's work. If you want to support someone, support upstream first: [Ko-fi: moonlightxr](https://ko-fi.com/moonlightxr). In the headset, the settings panel's About tab has a Ko-fi button for this port's page (the coffee link below): **Open page** shows it in Steam's browser in the dashboard.

If you would like more Steam Frame ports, you can also [buy me a coffee](https://ko-fi.com/sana748653). It pays for the Claude API credits these ports are built with, spent as carefully as I can.

## Credits

- Built by [simplysanatori](https://github.com/simplysanatori).
- [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr) by Sean Gilleece. Every in-headset module here is a port of it: screen layout, bar and menus, rooms, 3D, virtual surround. Its logo is the base of the app icon and the Steam artwork.
- [Moonlight](https://github.com/moonlight-stream) by the Moonlight Game Streaming Project (Cameron Gutman, Diego Waxemberg, Aaron Neyer, Andrew Hennessy and contributors): [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c) for the streaming protocol, and [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt) for the flat window and the host client behaviour.
- [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK) from Khronos, [LiteRT](https://github.com/tensorflow/tensorflow) for the depth models, [ZipDepth](https://github.com/fabiotosi92/ZipDepth) by Fabio Tosi and [MiDaS](https://github.com/isl-org/MiDaS) by Intel ISL, KEMAR head-related transfer functions (MIT Media Lab), and Noto Sans.
- The three rooms are by Sean Gilleece / Woodford XR (CC BY 4.0).
- The Gun controller model is "[G17 Gen 5 Handgun + Olight - NSW Police Force](https://sketchfab.com/3d-models/g17-gen-5-handgun-olight-nsw-police-force-0200c248eba14385ad2bee8d4dc4e586)" by [8sianDude](https://sketchfab.com/haoliu95) (CC BY 4.0), converted and changed for the headset.

[CREDITS.md](CREDITS.md) has the full list with licences and links.

## Licence

GNU General Public License v3. See [LICENSE.txt](LICENSE.txt).

## How this was made

I had been using Moonlight to stream my PC to the Frame and wanted something like Virtual Desktop's VR environment for it, so I built a native version of Moonlight XR for the Frame. It started as a personal project for my own headset. It ended up good enough to share.

This is vibe coded, with manual review throughout. I wrote it with Claude Code, using Anthropic's Fable 5.1 and Opus 5.5 models running on the Frame itself. I chose what to build, reviewed the code and the scripts as they were written, tested each build in the headset and reported what I saw. If that is not for you, no worries. It is more than functional, and it is a starting point for anyone who wants Moonlight in a VR environment on the Frame.

It is heavily based on Moonlight XR by Gilleece. The rooms, the 2D to 3D depth mode, the menus and the virtual surround are ports of that work. The difference is that it runs natively on SteamOS instead of through Lepton. It is GPL, so take pieces for Moonlight XR or anything else.

Next steps are manual optimisation and possibly hand tracking, depending on how far the community efforts get. Developer notes are in [docs/dev](docs/dev/README.md).
