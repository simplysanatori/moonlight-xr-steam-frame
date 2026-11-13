# Moonlight XR for Frame 1.3.3

I added an Aspect ratio setting and a heat guard, made 3D much lighter on the CPU and rooms lighter on the GPU, and fixed the refresh rate and the screen and panels around rooms.

### Added

- **Aspect ratio** (Settings page): 4:3, 16:9, 16:10, 21:9 and 32:9. Video resolution lists real monitor sizes for each, such as 3440x1440 for 21:9.
- With Full-size SBS on, a 32:9 stream is shown as a 3D pair, with a notice. For a 2D 32:9 picture, set 3D source to Depth.
- **Heat guard:** the stats now show the headset's CPU and GPU temperatures. If it gets hot, a notice tells you what to turn off. With 3D on, it eases off the depth model, then turns 3D off before the headset overheats.
- **Depth model** on the headset's 3D tab: ZipDepth Light or ZipDepth Full.

### Changed

- **Size** (Screen tab) is now the width of a 16:9 picture, so a wider picture keeps the same height, up to 8 m wide. You can go back to 1.3.2 and forward again and the screen stays as it was.
- 2D to 3D now uses **ZipDepth Light** by default, which uses about 60% less CPU. **ZipDepth Full** has finer depth edges and is still in the Depth model setting, after a warning.
- Rooms use less GPU. With the room's glow off they comfortably hit 120 fps, and they're now much closer to 144.
- In a room, the Display tab's **Glow** is now the same switch as the Room tab's Glow, so the two always match.

### Fixed

- 3D could overheat the headset within minutes and make it shut down (#6, thanks Liberato116). ZipDepth Light uses much less CPU, and the heat guard warns you and eases off 3D as the headset heats up.
- Leaving a room after resizing the screen or moving the stats window or keyboard there didn't keep them right. Your screen size now persists, and the stats window and keyboard go back to the screen.
- When you picked a new refresh rate, a stream could stay at the old frame rate (for example 90 fps on a 144 Hz headset), which judders. The stream now starts at the headset's real refresh rate, and if the two stop matching, a notice tells you to start the stream again.
- Long notices in the headset are no longer cut off.

### Known issues

- Confirmed: with the glow on, bright text and icons can ghost when you turn your head quickly, especially at 144 Hz.
- 2D to 3D halves your frame rate (your refresh rate stays the same). For example, at 144 Hz, 144 fps drops to 72 fps.
- Confirmed (#5, thanks Liberato116): at 144 Hz with passthrough and a 3440x1440 stream (or, less often, other high-resolution streams), passthrough can flash black occasionally. A lower stream resolution, such as 2560x1080, helps.
  - This comes from GPU load at high resolutions.

### Reporting a problem

In the headset, open the cog, then About, Report a problem, and press **Save and zip**. Attach the zip to a GitHub issue. It hides your PC's name and addresses, and makes debugging much faster.
