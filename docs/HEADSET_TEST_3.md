# Headset test 3 (after session 3, 2026-10-10)

What is installed and waiting for the owner, and what to report. About 15 minutes. Start from the Steam shortcut "Moonlight XR for Frame" as usual and stream Desktop.

Every run now also writes SteamVR's own frame timing to `~/Games/MoonlightXRNative/vrtiming.log` (a line a second: frames the compositor reused, dropped or reprojected). Nothing to do for it; just note roughly when each step happened.

## 1. Menu (the fix for the ~200 ms panel drawings)

1. In the void (no room), open the cog menu. Move the pointer up and down over the rows for 10 seconds while turning your head left and right.
2. Switch through every tab. Open the keyboard and type a few keys.
3. Pick a room and do steps 1 and 2 again inside it.

Report: does the world (room, picture) still judder while the menu is open? Does the menu itself lag behind the pointer more than before (it can now be one frame behind, which should not be visible)?

## 2. Room judder (suspected cause fixed, needs confirming)

SteamVR shrinks, every frame, any picture layer wider than 3458 pixels. At 90 Hz the room was 3520 wide, so it went through that path; the room is now capped just under it at the Standard environment resolution (High and Ultra are left as they were, for comparison). 3D on and menu closed unless said; each step about 30 seconds, turning your head slowly and stepping side to side. Note the time of each step.

1. Void (no room).
2. Home Theater, Environment Res **Standard** (the default).
3. Set Environment Res to **High**, quit and start again (the room's size is fixed when it is first made), then Home Theater.
4. Back to Standard, quit and start again, 3D **off**, Home Theater.

Report which steps judder. If 2 is smooth and 3 judders, the cause is confirmed and the 3D/curved picture (5280 wide, also shrunk; this may also be part of the poor text) gets the same treatment next.

## 3. Text quality (mip fix 6c79b4d)

With a page of small text on the Desktop:
1. 3D on, default distance.
2. Pull the screen closer: hold the move bar and push the stick south.
3. 3D off.
4. Settings, Display: Supersample Quality.

Report which of the four reads best and whether any is acceptable.

## 4. Flat UI coming back

Start a stream, leave VR with the bar's Exit, then start another stream and leave with Start+Select+L1+R1 (or Ctrl+Alt+Shift+Q on the keyboard). Report whether the flat Moonlight window came back both times.

## 5. Controller models

If the textured models are installed by then (see STATUS.md), turn controller models on (Settings, Display) and say whether the hold angle and the colour look like the real controllers.
