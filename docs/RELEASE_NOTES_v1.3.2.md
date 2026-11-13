# Moonlight XR for Frame 1.3.2

This fixes the pointer's beam flashing up through the screen for one frame as it moves onto or off something, worst on the keyboard. It was the known issue in 1.3.1.

The app now draws the pointer ahead of the buttons' hover rings, so the beam keeps its place in SteamVR's list of layers when a ring comes and goes. It no longer needs the invisible stand-ins that caused the stutter in #5.
