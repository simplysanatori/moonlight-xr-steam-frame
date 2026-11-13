# Moonlight XR for Frame

Play games and the desktop from your PC on a virtual screen on the Valve Steam Frame. A native port of Sean Gilleece's [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr), with [Moonlight](https://github.com/moonlight-stream/moonlight-qt) 6.1.0 for picking your PC.

## Features

- Up to 144 Hz
- Flat or curved screen, three rooms, or passthrough
- 2D games in 3D, or the PC's own 3D
- Controllers as a mouse or an Xbox gamepad
- Bluetooth keyboards, mice and gamepads
- Point and click with your head or eyes
- Wi-Fi, Steam Frame Wireless Adapter or USB cable

## How it works

- Your PC runs the game and sends it to the headset as video.
- You pick the PC and a game in a flat Moonlight window.
- It shows on a big screen in the headset. Your presses go back to the PC.

## Requirements

- A Steam Frame with SteamOS and SteamVR, and about 250 MB free.
- A PC with [Sunshine](https://github.com/LizardByte/Sunshine) or [Apollo](https://github.com/ClassicOldSong/Apollo) (some [extras need Apollo](docs/USER_GUIDE.md#setting-up-apollo)).
- The PC on your network, the Steam Frame Wireless Adapter, or a [USB cable](docs/USER_GUIDE.md#usb-cable).
- A terminal on the Frame: SSH, or Konsole (SteamVR dashboard, Launch a program, Desktop).
- Steam running while you install.

## Install

1. Download `moonlight-xr-steam-frame-1.3.2-linux-arm64.tar.gz` and `SHA256SUMS` from [Releases](https://github.com/simplysanatori/moonlight-xr-steam-frame/releases), then check them:

   ```sh
   sha256sum -c --ignore-missing SHA256SUMS
   ```

2. Unpack it and run the installer:

   ```sh
   tar xzf moonlight-xr-steam-frame-1.3.2-linux-arm64.tar.gz
   cd MoonlightXRNative
   ./install.sh
   ```

3. Start **Moonlight XR for Frame** from the Steam library.

No terminal? Use [Frametanium](https://github.com/Cogential/frametanium), or [Install with FrameDrop](https://framedropvr.com/install?manifest=https%3A%2F%2Fgithub.com%2Fsimplysanatori%2Fmoonlight-xr-steam-frame%2Freleases%2Flatest%2Fdownload%2Fmoonlight-xr-steam-frame.framedrop.json). More options: [user guide](docs/USER_GUIDE.md#installing).

## First run

1. Select your PC in the list. Missing? Choose **+** (Add PC manually) and type its address.
2. Moonlight shows a PIN. On the PC, open `https://localhost:47990`, go to the PIN page and type it.
3. Select the PC again, then an app.

PCs paired in the Moonlight flatpak are already there.

## Controls

The controller ray is the PC's mouse.

| Control | Pointer mode |
| --- | --- |
| Trigger | Left click |
| Grip | Move or resize the screen |
| A | Right click |
| Y | Keyboard |
| Menu (right) | Cog menu |
| D-pad up | Recentre |
| Hold left View + left grip | Gamepad mode and back |
| Hold Menu, View and both bumpers | Disconnect (gamepad mode) |

Point below the screen to show the bar. Remap buttons in [**Controller bindings**](docs/BINDINGS.md). All controls: [user guide](docs/USER_GUIDE.md#controllers).

## Update

Unpack the new release and run its installer:

```sh
./install.sh
```

## Uninstall

1. In Steam, remove **Moonlight XR for Frame** and any **Desktop in VR** (Manage, Remove non-Steam game from your library).
2. Remove the dock entry, from the unpacked release:

   ```sh
   python3 MoonlightXRNative/dock_icon.py remove ~/Games/MoonlightXRNative/app
   ```

3. Delete the files, then unpair the Frame on the Sunshine or Apollo web page.

   ```sh
   rm -rf ~/Games/MoonlightXRNative ~/.config/moonlight-xr "$HOME/.config/Moonlight XR Native" "$HOME/.cache/Moonlight XR Native"
   ```

## Troubleshooting

- **PC not found**: start Sunshine or Apollo on it.
- **Blurry text**: rerun `./install.sh` ([details](docs/USER_GUIDE.md#for-the-sharpest-picture)).
- **Stutter**: lower the bitrate in **Settings**.
- **No Steam entry**: start Steam, rerun `./install.sh`.
- **Grey dock icon**: restart Steam once.

More fixes: [user guide](docs/USER_GUIDE.md#troubleshooting).

## Reporting a problem

After a problem, run:

```sh
~/Games/MoonlightXRNative/app/bug-report.sh
```

The zip goes to `~/Games/MoonlightXRNative/reports`, with personal details masked ([what's in it](docs/USER_GUIDE.md#reporting-a-problem)). Check it, then attach it to an [issue](https://github.com/simplysanatori/moonlight-xr-steam-frame/issues).

## More

- [User guide](docs/USER_GUIDE.md)
- [AGENTS.md](AGENTS.md), for developers and AI agents
- [Building from source](docs/BUILDING.md)
- [Changelog](docs/CHANGELOG.md)

## Support

Moonlight XR is Gilleece's work, so support upstream first: [Ko-fi: moonlightxr](https://ko-fi.com/moonlightxr).

For more Steam Frame ports, [buy me a coffee](https://ko-fi.com/sana748653).

## Credits

- [simplysanatori](https://github.com/simplysanatori): this port.
- [Moonlight XR](https://github.com/Gilleece/moonlight-android-xr) by Sean Gilleece: in-headset app, logo, rooms.
- [Moonlight](https://github.com/moonlight-stream): [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c), [moonlight-qt](https://github.com/moonlight-stream/moonlight-qt).
- [OpenXR-SDK](https://github.com/KhronosGroup/OpenXR-SDK), [LiteRT](https://github.com/tensorflow/tensorflow), [ZipDepth](https://github.com/fabiotosi92/ZipDepth), [MiDaS](https://github.com/isl-org/MiDaS): VR and 3D.
- [Gun model](https://sketchfab.com/3d-models/g17-gen-5-handgun-olight-nsw-police-force-0200c248eba14385ad2bee8d4dc4e586) by [8sianDude](https://sketchfab.com/haoliu95), CC BY 4.0.

Full list with licences: [CREDITS.md](CREDITS.md).

## Licence

GNU General Public License v3. See [LICENSE.txt](LICENSE.txt).

## How this was made

I started this project and used AI (Claude: Claude Code with the Fable 5.1 and Opus 5.5 models) to speed up development. I write, review and fix code by hand all the way through, keep the AI's work checked and reined in, and aim to keep the port as lightweight as possible. I test it on my own Steam Frame.
