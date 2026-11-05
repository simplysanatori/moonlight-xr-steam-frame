# Moonlight XR for Frame 1.2.0

In 1.2.0 I fixed the white streak that flashed across the view while pointing at the keyboard, made the trigger grab the screen's move pill and corners, fixed stats that read 0 during a stream, and tidied the text in the headset and the flat window.

### Fixed

- A large white streak, like a reflected ray, flashed through the middle of the view while the ray moved over the keyboard. The ring round the key under the ray came and went at every gap between keys, and that shifted the beam in SteamVR's list of layers. Everything that appears with the ray now keeps its place in that list, so the beam never moves. As a safeguard, a beam that would reach your eyes is cut short in front of them.
- The stats window's "decoded, dropped, decode" row read 0 for the whole stream. It now shows the hardware decoder's numbers.
- The audio row read "0ch 0 ms buffered" when the PC sent no sound. It now says "Audio: No stream from PC".
- The HUD and Lite stats wrote 0.0 for the host's encode time and the decode time when nothing measured them. Those now read n/a.

### Changed

- The trigger now grabs the move pill under the screen and the resize corners, the same as the grip, as windows do in the Steam Frame's dashboard. Pressing on a handle sends no click to the PC.
- The corners' grab area reaches less than 2 cm into the picture, so the desktop's own corners, such as the Start button, still click.
- The text in the headset and in the flat window is shorter and plainer, and uses Moonlight's terms.

## Download

- `moonlight-xr-steam-frame-1.2.0-linux-arm64.tar.gz`
- `SHA256SUMS`

## Install or upgrade

Unpack the tarball on the Frame and run `install.sh`. It keeps your settings and pairing. The [README](https://github.com/simplysanatori/moonlight-xr-steam-frame/blob/main/README.md) has the full steps.
