# Moonlight XR for Frame 1.3.0

I made the Glow edge borderless, encrypted the clipboard sync, and fixed a picture that froze after a decoder error.

### Changed

- **Glow edge** (Picture tab) now makes the screen borderless, after Lobs1994's fork of Moonlight XR. The picture's edge fades out, and the glow behind it carries the edge colours outward at the picture's brightness. There's no seam between the picture and the glow. It used to paint a blurred band over the border, which smeared the edge and went dark at a low glow level.
  - **Soft** fades the outer 4 % and keeps the glow close.
  - **Wide** fades the outer 8 % and spreads the glow across most of the view.

### Security

- Smart clipboard sync no longer leaves clipboard text readable on the Frame. It used to pass it in two plain text files, and the PC's clipboard stayed in one after the stream. Both files are now encrypted with a key made fresh for each stream. The key is never written to disk and is wiped when the stream ends. Each file is deleted as soon as it's read. The old plain text files are deleted the next time either app starts.

### Fixed

- If the video decoder failed, the picture froze for the rest of the stream while sound and input carried on. The stream now reconnects with a fresh decoder. A decoder that keeps failing no longer keeps a CPU core busy.
- Leaving or reconnecting a stream could, rarely, crash the app.
- A bug report could keep a saved PC's name in some lines, and an owner's name in a Bluetooth device's name ("Sam's Keyboard"). Both are masked now.
- `install.sh` failed when its folder's path had a space in it.
