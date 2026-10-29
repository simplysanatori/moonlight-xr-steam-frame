# Controller bindings

> **Not active yet.** The bindings file and its defaults are in place, but the client does not read them yet; until it does, the controls work as [USER_GUIDE.md](USER_GUIDE.md#controllers) describes. This section moves into the user guide when they are wired in.

You will be able to change what each Frame controller button does, for pointer mode, for gamepad mode, and for one PC app at a time, in `~/.config/moonlight-xr/bindings.ini`. The client writes that file the first time it starts, with every default listed and commented out. The format is in [dev/bindings.md](dev/bindings.md).

## New defaults in pointer mode

The seven controls that did nothing in pointer mode get these jobs:

| Control | Does |
| --- | --- |
| Y (right) | Shows or hides the keyboard |
| X (right) | Turns 3D on or off |
| d-pad up (left) | Recentres: the screen comes round to where you face |
| Menu (right) | Opens or closes the cog panel |
| Left bumper | Mouse Back (X1) |
| Right bumper | Mouse Forward (X2) |
| d-pad left (left) | Nothing (free for your own binding) |

Everything else stays as it is: the triggers click, A and d-pad down right click, B and d-pad right middle click, a stick click right clicks, the sticks scroll, and the grips grab.

## What cannot be changed

- The pointer/gamepad switch (left View with the left grip, or the shortcut you chose).
- The disconnect chord in gamepad mode (View, Menu and both bumpers).
- The grips grabbing the screen's handles, the keyboard's bar and the stats window while the ray is on them.
