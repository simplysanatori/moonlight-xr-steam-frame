# Headset test 5 (2026-10-11)

For the build installed on the Frame now. About 20 minutes, plus 10 for the optional runs. Do not paste addresses, host names or Wi-Fi names from the logs into a report.

Check the installed version first:

```sh
cat ~/Games/MoonlightXRNative/app/VERSION
```

Then stream Desktop at 2560x1440 and go through the items below. Read the logs after you leave VR. The log is `~/Games/MoonlightXRNative/moonlight.log`; the launcher's is `~/Games/MoonlightXRNative/moonlight-launcher.log`.

## 1. Double click

Pointer mode, the Frame controller as the mouse.

- Double click desktop icons and files in Explorer, at a normal pace and a slow one (just under half a second).
- Drag across a few words of text in a browser or editor, then release.
- Click once, then move away at once.

Log line at the start of the run:

```sh
grep -o "pointer clicks: .*" ~/Games/MoonlightXRNative/moonlight.log | tail -1
```

Good looks like: `MLXR_DOUBLE_CLICK_PX=24 MLXR_DOUBLE_CLICK_MS=500` in that line, icons open on the first try, a text drag selects from the press point, and the cursor follows you straight away after a single click. If it feels held after clicks, tell me. `MLXR_DOUBLE_CLICK_PX=12` is the fix to try.

## 2. Text sharpness with the curve on, and with 3D on

Open a page with small text. Compare three cases at the same distance: curve off and 3D off (the flat picture), curve on, then 3D on. Try a room as well.

```sh
grep -o "curved picture: projection layer.*" ~/Games/MoonlightXRNative/moonlight.log | tail -2
grep -o "the layer's two views on two swapchains.*" ~/Games/MoonlightXRNative/moonlight.log | tail -1
grep -o "swapchains alive [0-9]* of 16" ~/Games/MoonlightXRNative/moonlight.log | tail -1
```

Good looks like: a size per eye with "a swapchain each" and "within SteamVR's 3458", and at most 11 of 16 swapchains. Curved text should be about as sharp as the flat picture, a little softer at most. Tell me how it compares. If the frame rate drops with a room in 3D, start with `MLXR_EYE_SCALE=0.65` and say so.

## 3. Pull the picture closer with the left stick

Hold the picture by its move bar with the left grip. Pull the stick back the way your thumb does it. Then do the same with the right controller and with the keyboard's bar.

```sh
grep -o "move bar let go: .*" ~/Games/MoonlightXRNative/moonlight.log | tail -6
```

Good looks like: the picture comes in as fast as it goes out when you push. In the log, "sticks pushed it" is negative for a pull, and the "stick y seen" figure for the pulling stick goes well below zero. If a pull feels slow, send those lines. A left value that never gets below about -0.3 means the stick stops short.

## 4. The flat window's size and the Help button

Leave the stream, or start the app and stay on the PC list.

- Is the flat window 1.5 times larger than before? Is anything cut off at the edges? Open Settings and scroll to the end.
- Press the Help button. It should open this repository's README in a browser.
- Open Settings and look at the bitrate. At 1440p and 90 fps it should read 98 Mbps (only if you changed the resolution or frame rate since the update; a saved value stays).

Good looks like: all text readable, the header buttons in view, the last Settings row reachable. If the window is the old size, `MLXR_LAUNCHER_SCALE=1.5` in the launch options is worth a try. Tell me what you see.

## 5. Automatic network choice

Set Network connection to Automatic and start a stream. Then:

```sh
grep -o "network path: .*" ~/Games/MoonlightXRNative/moonlight.log | tail -12
cat ~/.config/moonlight-xr/netpath-history.ini
```

Good looks like:

- A line for the adapter that says whether it was found: either "present (...)" or "no adapter found (...)". With no station on the access point it should not be probed.
- One line per path with the median round trip, the p90, the jitter, the failed probes and a score.
- A line that starts "chose" and gives the reason, such as "score N ms against ... (lower is better)". Lower score wins, and scores within 3 ms count as a tie.
- After the stream, `netpath-history.ini` has a `[home]` or `[adapter]` entry with frames, lost, jitter, rtt and time. No addresses or names should be in it.
- On the next stream, a "history:" line.

Say which path it chose, and whether that was the one you would have picked. Do not paste the lines if they show addresses.

## 6. The Exit button once more

Start a stream, leave with the bar's Exit, and check that the flat window comes back. Then do it a second time, and once with the Start+Select+L1+R1 chord.

```sh
grep -E "flat window|exited with|Asked Steam" ~/Games/MoonlightXRNative/moonlight-launcher.log | tail -20
```

Good looks like: "flat window exposed after N ms and drawn" after "The VR client exited with 0", with no "not exposed, showing it again" and no "still not exposed". If the window does not come back, send the bug-report zip and the time of the exit.

## 7. Optional: 98 Mbps and 80 Mbps

Run the UFO test for a minute at 98 Mbps (the new default) and then at 80 Mbps.

```sh
grep -o "hw decode.*refused (length mismatch)" ~/Games/MoonlightXRNative/moonlight.log | tail -12
grep -o "video [0-9.]* fps drawn" ~/Games/MoonlightXRNative/moonlight.log | tail -12
grep -o "IDR requests [0-9]*" ~/Games/MoonlightXRNative/moonlight.log | tail -12
```

Good looks like: errors at 0 or close to it and not rising, the refused count at 0, IDR requests in single figures over the minute and video fps drawn near 90. Say which of the two looks and feels better. If 98 Mbps loses frames over the Steam Frame Wireless Adapter, 80 Mbps or lower is the answer there.

## What to report

- A line for each item 1 to 6: fine, or what was wrong.
- For items 2, 3 and 5, the log lines asked for, if they carry no addresses or names.
- For the optional runs, the three figures and which bitrate you prefer.
- The time of each run, so it can be found in the log. Send the bug-report zip only if something is wrong.
