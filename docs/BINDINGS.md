# Controller bindings

> **The headset side is not wired in yet.** The launcher's editor below is ready and saves the file, but the headset's client does not read `bindings.ini` yet; until it does, the controls work as [USER_GUIDE.md](USER_GUIDE.md#controllers) describes. When it does, a change takes effect after the next stream start. This section moves into the user guide then.

You can change what each Frame controller button does, for pointer mode, for gamepad mode, and for one PC app at a time, in `~/.config/moonlight-xr/bindings.ini`, from the launcher's **Controller bindings** page or by editing the file. The file is written the first time either one runs, with every default listed and commented out. The format is in [dev/bindings.md](dev/bindings.md).

## The Controller bindings page (launcher)

Settings, Input Settings, **Controller bindings**. It is part of the flat launcher (not the headset).

- **Every app / One app.** Every app edits the `[pointer]` and `[gamepad]` sections. One app lists the apps of the PC you pick (plus any app already in your file, marked with a dot, and a name you type) and edits that app's `[app "Name"]` lines, which go over the Every app ones for that app only.
- **Pointer mode / Gamepad mode**, then one row per control, left controller and right controller side by side: what it does now, and where that comes from (default, set in your file, set for this app, from Every app). Press a row to open its editor.
- **The editor.** Pick what the control does: Nothing, a Key (modifiers Ctrl, Shift, Alt, Win, then the key from the list of every key name), a Mouse button, a Wheel notch, a Pad button (gamepad mode), an App command, or for a whole stick a Stick use. The change is saved at once. You can also type the action as the file writes it. **Reset row** takes your line out so the layer below shows through; **Reset all** (press twice) takes out every line of the list you are looking at (both modes; for an app, only that app's lines).
- **Chords.** The chords in force are listed and edited like rows; type a new one (`left.view+right.menu`) to add it. Setting a default chord to Nothing switches it off.
- **Touch sensors** are listed greyed: the headset does not read them yet.
- **Reserved chords** are shown read only (the pointer/gamepad switch shortcuts and the disconnect chord).
- **Problems.** If the file has lines the parser refuses, a red strip at the top lists each as `line:column: why`. Those lines are left out (the headset logs the same) and the rest of the file works. The page never deletes them.

The page uses the same code as the headset to read and check the file (`xr/core/xr_bind.c` is built into the launcher), so a line it accepts is a line the headset accepts. **Saving does not rewrite the file.** A change edits the file's own lines: a changed binding replaces the value on its line (a comment after it stays), Reset removes the line, and a new binding goes after the last line of its section, or in a new section at the end of the file (a section may appear more than once). Your comments, the commented defaults and any line the parser refused are kept as they are; the line ends (LF or CRLF) are kept too. An edit the parser would refuse (a reserved chord, a pad button in pointer mode, a grip in a pointer-mode chord) is not written and the parser's reason is shown under the editor.

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
