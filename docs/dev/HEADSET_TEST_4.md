# Headset test 4 (after the black screen fix, 2026-10-11)

The fix is in commit c3f85b8 on main. It was tested headless only. About 10 minutes for the first run, 10 more for the optional ones.

## 0. Before you start

Check what is installed:

```sh
cat ~/Games/MoonlightXRNative/app/VERSION
```

If it says 0.6.0-alpha.2, the fixed build is not installed yet. Close the app, then run:

```sh
cd ~/Games/MoonlightXRNative/src && NO_SHORTCUT=1 ./packaging/install.sh
```

Run it only with the app not running.

## 1. The run

Start **Moonlight XR for Frame** from the Steam shortcut, with the settings as they are (126 Mbps, virtual display on). Stream Desktop for about a minute, in the void and then in a room.

In the headset, look at:

- Is there a picture at all?
- Is the motion smooth? Drag a window around, or play something with movement.
- Open the stats window with the grip and read three figures: "errors", "largest AU" and "idr requests".

## 2. Read the log

After you leave VR, from a shell on the Frame. The stats line is written every 5 seconds.

The decoder part:

```sh
grep -o "hw decode.*refused (length mismatch)" ~/Games/MoonlightXRNative/moonlight.log | tail -12
```

The frame rate drawn:

```sh
grep -o "video [0-9.]* fps drawn" ~/Games/MoonlightXRNative/moonlight.log | tail -12
```

The full-frame requests (each line counts its own 5 seconds):

```sh
grep -o "IDR requests [0-9]*" ~/Games/MoonlightXRNative/moonlight.log | tail -12
```

Good looks like this:

- errors is 0 or close to it, and does not keep rising.
- largest AU is well over 88056 bytes, in more than 64 parts.
- The refused count stays at 0 or does not keep rising.
- IDR requests are in single figures over the minute.
- video fps drawn is near 90.

If errors stay high while the largest AU is over 64 parts, the cut was not the only cause. In that case also run:

```sh
grep "\[vdec\]" ~/Games/MoonlightXRNative/moonlight-console.log | sort | uniq -c | sort -rn | head
```

Look for a line about a frame exceeding the OUTPUT buffer. Do not paste addresses or host names from the logs into a report.

## 3. Optional: 80 Mbps with the UFO test

Set the bitrate to 80 Mbps in Settings and run the UFO test for a minute. This is the setting where the cut hit full frames and fast motion, so the stutter you saw on 2026-10-10 may be better. Read the log as in section 2.

## 4. Optional: 50 Mbps

Same again at 50 Mbps. Then say which of 126, 80 and 50 Mbps looks best and feels smoothest.

## What to report

- Was there a picture at 126 Mbps? Smooth or not?
- The "hw decode" part, the "idr requests" figure and "video fps drawn" from the log, for each run.
- For the optional runs: the UFO test at 80 Mbps, and which bitrate looks best.
- The time of each run, so it can be found in the log. Send the bug-report zip only if something is wrong.
