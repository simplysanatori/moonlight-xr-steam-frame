# Moonlight XR for Frame 1.3.1

A quick fix for tearing and stutter when you turn your head in 1.2.0 and 1.3.0, worst at 144 Hz and high stream resolutions (#5, thanks Liberato116).

1.2.0 tried to stop the pointer's beam flashing up through the screen as it moved onto or off something. It was worst on the keyboard and never happened in gamepad mode. To stop it, the app sent SteamVR a stack of invisible stand-ins for the bar's buttons whenever they were hidden, and that stack made SteamVR miss frames. I've removed them, and the picture holds steady at 144 Hz again with a 1440p ultrawide stream (SteamVR at 144 Hz and its highest resolution).

### Known issue - regression

- The pointer's beam can flash up through the screen for one frame as it moves onto or off something, worst on the keyboard. It's back because the stand-ins are gone. I plan to fix it properly in a future release.
