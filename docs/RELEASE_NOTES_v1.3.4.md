# Moonlight XR for Frame 1.3.4

I rearranged the headset menu, added a new glow and a Foveated rendering setting, and made passthrough lighter on the GPU.

### Added

- **Foveated rendering:** a switch on the Picture tab and the Settings page. It's off by default.
- **Turning, tilting and resizing what you hold:** while you hold the screen, the keyboard or the stats, use the other hand's thumbstick to turn and tilt it. The holding hand's thumbstick pushes or pulls it, and left or right resizes it.
- **Glow spread** (Room tab and Settings page): how far the glow reaches.
- **Mouse mode** and **Scroll speed** are now in the headset, on the Controls tab.
- The Full and Compact stats windows show the GPU's clock speed, and say when the headset slows itself down for heat.

### Changed

- **The headset menu** has seven tabs, the same everywhere: Screen, Room, Picture, 3D, Controls, Accessibility and About. The glow and lighting are on Room, and the pointer and controller settings are on Controls. Settings that do nothing in a room are greyed.
- **Size** (Screen tab) now resizes the screen in rooms too, except in Grand Cinema.
- **Stats** is a single setting now: Off, Full, Compact or Minimal.
- **Glow** is now a backlight, like an LED strip behind a TV (#7, thanks LiamShinkso). It replaces Glow edge, and it works in the void, rooms and passthrough. It's off by default in every room.
- **Passthrough:** controllers and glow are off in passthrough. Turning either on asks first, because drawing them over the cameras can cause stutter or glitches.
- With the **Gun**, the trigger now grabs the bar below the screen and the keyboard's bar.

### Fixed

- In Gamepad mode with Look to point on, looking at the bar made its icons show, but you couldn't press them.
- On a curved screen, the stats, settings, keyboard and other panels now stay in front of the curve.
- In a room, the stats window jumped to a huge size when you pushed or pulled it, and couldn't be put back.
- The keyboard's click sound skipped presses when you typed fast.

### Known issues

- With the glow on, bright text and icons can ghost when you turn your head quickly, especially at 144 Hz.
- 2D to 3D lowers your frame rate in rooms if frame timings can't be kept for your refresh rate. For example, at 144 Hz, 144 fps drops to about 72 fps.
- Media keys on the headset's keyboard may not work on some Linux distros.
- At 144 Hz with passthrough and a 3440x1440 stream, passthrough can flash black occasionally (#5). A lower stream resolution, such as 2560x1080, helps.

### Reporting a problem

In the headset, open the cog, then About, Report a Problem, and press **Save and zip**. Attach the zip to a GitHub issue. It hides your PC's name and addresses, and makes debugging much faster.
