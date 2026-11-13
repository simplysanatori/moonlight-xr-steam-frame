# Moonlight XR for Frame 1.3.2

A quick fix for the pointer's beam flashing up through the screen, the known issue in 1.3.1.

### Fixed

- The pointer's beam no longer flashes up through the screen for one frame as it moves onto or off something, worst on the keyboard. SteamVR keeps track of each thing the app draws by its place in a list. When a hover ring under the pointer disappeared, the beam moved up a place and SteamVR could draw it wrong for a frame. The app now draws the pointer ahead of the hover rings, so the beam is always in place.
- My earlier quick fix in 1.2.0 added invisible stand-ins, which increased the overhead and caused the stutter seen in #5. This fix doesn't need them, so the picture holds steady when you move your head, even at higher refresh rates like 144 Hz.
