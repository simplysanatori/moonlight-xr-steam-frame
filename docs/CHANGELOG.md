# Changelog

## 1.0.0 (October 2026)

The first release of Moonlight XR for Frame, a native game-streaming client for the Valve Steam Frame. It streams from a PC running Sunshine or Apollo into SteamVR.

### Streaming

- Hardware H.264 and HEVC decoding on the Frame's video decoder, up to 2560x1440.
- Refresh rates of 72, 90, 120 and 144 Hz. "Match headset" follows the rate SteamVR runs at, and picking a rate sets SteamVR's per-app rate before the stream starts.
- Frame pacing with a small queue, so an uneven network does not turn into repeated frames.
- Reference frame invalidation, so a lost frame is repaired without waiting for a full new one.
- Surround sound (5.1 and 7.1) rendered for headphones, and an audio buffer that grows only when the network needs it.

### Network

- Streams over Wi-Fi or over the Steam Frame Wireless Adapter. The "Network connection" setting offers Automatic, Wi-Fi and Steam Frame Wireless Adapter.
- Automatic probes both links before each stream and picks the better one, from round-trip time, jitter, failed probes and the losses seen on that link last time.

### The picture

- A flat or curved screen in a black void, in passthrough, or in one of three rooms: Home Theater, Grand Cinema and Synthwave.
- 3D from a 2D stream with a depth model (ZipDepth or MiDaS), with strength and convergence controls.
- Supersampling and sharpening options, glow around the picture, and a movable screen: grab the bar under it and push or pull it with the thumbstick.
- One eye texture per eye for every layer, sized within SteamVR's limits.

### Controls

- Pointer mode: the controller ray is the mouse, the trigger clicks, a thumbstick click right-clicks, and double clicks land on the first click's spot.
- Gamepad mode: the Frame controllers act as an Xbox gamepad. Hold a grip and press Menu or View to switch modes.
- An in-headset keyboard with a compact and a full layout, Hacker's-Keyboard-style modifiers and key repeat. It has its own move bar, so it can be placed anywhere or laid flat.
- Bluetooth keyboards, mice and gamepads connected to the Frame reach the PC, with rumble for gamepads.
- Disconnect without closing the game on the PC: Start+Select+L1+R1 on a gamepad, or Ctrl+Alt+Shift+Q on a keyboard. Exit on the bar ends the game.
- Steam Frame controller models drawn from Valve's own files, matte, with the trigger, grips, sticks and buttons moving as you press them.

### The flat window

- Moonlight's own window for the PC list, pairing and app list, with a Settings page in the style of Artemis: connection, video, 3D, audio, gamepads and Apollo options.
- Apollo features: a virtual display made for the headset, a resolution scale factor, a custom refresh rate and clipboard sync in both directions.
- A Steam shortcut with library art and a dock icon, created by the installer.

### In the headset

- A stats window you can grab and move, with the stream, decoder, network and frame timing.
- A Report a problem sheet that saves your note on the Frame.
- bug-report.sh gathers the logs, your note, the version and a filtered copy of the settings into one zip, with addresses, host names, ids, serial numbers, Wi-Fi names, keys and certificates masked or left out.
