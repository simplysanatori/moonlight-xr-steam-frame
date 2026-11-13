# Moonlight XR for Frame 1.3.1

I fixed the tearing and stutter when you turn your head, which made 1.2.0 and 1.3.0 hard to play at 144 Hz.

### Fixed

- Turning your head made the whole view tear and stutter, worst at 144 Hz, unless the pointer was on the menu bar or you were holding the move handle (#5, thanks Liberato116). Since 1.2.0 the app sent SteamVR a stack of invisible stand-ins for the bar's buttons whenever they were hidden, and that stack made SteamVR miss frames. They're gone, and the picture holds steady at 144 Hz again.

### Known issue

- The pointer's beam can flash up through the screen for one frame as it moves onto or off something, like a keyboard key or a bar button. The stand-ins removed above were there to stop this. I plan to fix it properly in a future release.
