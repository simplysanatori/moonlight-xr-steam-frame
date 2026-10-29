# Gamepad certification pass on the Frame

About 20 minutes of headset time for the first pad, 5 to 8 for each further one; the Steam Controller section is the longest. Physical gamepads are so far "tested only on the desktop" (README). This pass says which pads really work on the Frame, over which connection, and what Steam Input does to them. It is plan item 2b.1; the Steam Controller questions (2b.3) are decided here too.

The tool is `mlxr-gamepad-probe`. It lists every SDL joystick and game controller the way the client's gamepad source sees it and, with `--watch`, prints hotplug events and a line per input event. The classification (type, Steam Controller list, "Frame" skip rule, Steam Input copy merge, controller numbers) is the client's own code, not a copy. It sends nothing to a PC and needs no stream.

Do not paste addresses or host names into a report. Bluetooth serials are MAC addresses: mask them before sharing (command at the end).

## Before you start

The probe is installed next to the client by `install.sh` (release tarball and source checkout alike). Check, and make a folder for the captures:

```sh
APP=~/Games/MoonlightXRNative/app
OUT=~/Games/MoonlightXRNative/gamepad-cert
mkdir -p "$OUT"
cat "$APP/VERSION"
"$APP/mlxr-gamepad-probe" --help
```

If `mlxr-gamepad-probe` is missing, the installed build predates it: install a newer release, or build with `./build.sh` and run `build/input/mlxr-gamepad-probe`.

Have a PC stream ready for step D (a gamepad tester open on the PC, for example https://hardwaretester.com/gamepad), and the pads charged. Run the probe from an SSH session or a terminal in Desktop Mode so you can read it; the headset can be off for steps A to C.

Options in one place:

| Command | Does |
| --- | --- |
| `mlxr-gamepad-probe` | list the devices once and exit |
| `--watch` | stay running: `HOTPLUG added/removed` blocks, one line per button, axis and touchpad event with the state the host would get, sensor samples, battery changes |
| `--seconds N` | with `--watch`, stop after N seconds (default: Ctrl-C) |
| `--rumble` | pulse each motor of each pad that has them (low, high, left trigger, right trigger), 0.6 s each |
| `--axis-hz N` | axis lines per second per pad (default 20, `0` = every event) |
| `--sensor-hz N` | sensor lines per second per pad and sensor (default 4, `0` = none) |
| `--no-mappings` | skip the gamecontrollerdb files the client reads |

Reading a watch line: `  12.345 [3] btn a down   btn=A lt=0 rt=0 L=0,0 R=0,0`. The number in brackets is the SDL instance id that the list prints at the top of each device block. `btn=`, `lt`, `rt`, `L` and `R` are the packet the client would send to the host: triggers 0 to 255, sticks plus or minus 32767, stick Y up positive. A gyro line is `gyro x= y= z= rad/s`, an accelerometer line `accel ... m/s^2`.

Reading a device block (the lines to copy into the sheet): `sdl type` (SDL's name and the fork's type), `ids`, `steam` (Steam Controller list match, Steam Virtual Gamepad copy and which pad it merged with), `host` (controller number and capability bits, or SKIPPED), `sensors`, `rumble`, `other` (touchpads, LED, battery). The header lines show the SDL runtime version and any `hint` lines; `SDL_GAMECONTROLLER_IGNORE_DEVICES` there means Steam Input is hiding the real pads from this process.

## 0. Baseline, no pad (1 min)

```sh
"$APP/mlxr-gamepad-probe" | tee "$OUT/00-baseline.txt"
```

Record: SDL runtime version, the `hint` lines, and whether the Frame's own controllers appear as `SKIPPED: SteamVR's own Frame controller`. They should be skipped, never a host pad. Also record how the headset is set up: Steam running (Game Mode or Desktop Mode), a stream or not.

## Per pad, steps A to D

Do these for each pad in the list at the bottom. Use a short tag for the file names: `xbox-bt`, `ds-bt`, `pro-bt`, `8bitdo-<mode>`, `sc-puck`, `sc-bt`, `sc-wired`.

### A. Connect and list (2 min)

Pair or plug in the pad. For Bluetooth use SteamOS's Bluetooth settings; for USB-C use a data cable (the Frame's port is USB 2.0 and is also the charger, so note whether it charges while the pad is attached; a hub that passes power is unverified). Then:

```sh
"$APP/mlxr-gamepad-probe" | tee "$OUT/<tag>-list.txt"
ls -l /dev/input/by-id/ 2>/dev/null | tee "$OUT/<tag>-by-id.txt"
lsusb 2>/dev/null | tee "$OUT/<tag>-lsusb.txt"
bluetoothctl devices 2>/dev/null | tee "$OUT/<tag>-bt.txt"
```

Record: shows up at all (over this connection), time from connecting to listing, `sdl type` and `fork type`, vendor:product, `steam` line, `host` line, sensors with rates, rumble and trigger rumble, touchpads, LED, battery. A pad with no `mapping` is listed as `joystick without a game controller mapping`: copy its GUID line and name; a mapping can be added through `MLXR_GAMECONTROLLERDB`.

### B. Every input (2 min)

```sh
"$APP/mlxr-gamepad-probe" --watch --seconds 120 --rumble | tee "$OUT/<tag>-watch.txt"
```

Hold the pad during the first 10 seconds: the rumble runs first (low motor, high motor, then the triggers if it has them). Then press, one at a time and in this order: A, B, X, Y, LB, RB, Back, Start, Guide/Home/Steam button, left stick click, right stick click, d-pad up, down, left, right, any extra button (Share, Capture, Mic, paddles, back grips, touchpad click). Push each stick to every edge and let go. Squeeze each trigger slowly, then fully. Swipe the touchpad(s). Tilt and shake the pad for the sensors.

Record: a button that prints nothing or the wrong name (`btn a` should be A, positional); each stick reaching about 32767 on both sides and returning to 0,0 on release (drift or a dead zone that is too wide shows here); each trigger reaching 255 (an on/off trigger only ever shows 0 and 255); gyro and accel lines present and of sensible size (about 9.8 m/s^2 on one axis when the pad rests); touchpad lines.

Chord check: hold Start+Back+LB+RB. The watch line must read `btn=LB+RB+Start+Back`. (The probe does not act on it; the real disconnect is step D.)

Report rate (stick events per second, a measure of the connection). Start this, and circle the left stick steadily from the moment it prints `watching` until it stops:

```sh
"$APP/mlxr-gamepad-probe" --watch --seconds 12 --axis-hz 0 > "$OUT/<tag>-rate.txt"
grep -c ' axis ' "$OUT/<tag>-rate.txt"
```

Divide the count by 11 and record it. Both axes of the stick count, so the number is events, not full stick positions; compare it between pads and connections, and between Bluetooth and wired for the same pad.

### C. Hotplug (2 min)

Start the watch and leave it running:

```sh
"$APP/mlxr-gamepad-probe" --watch --seconds 180 | tee "$OUT/<tag>-hotplug.txt"
```

Turn the pad off (or unplug it), wait 5 seconds, turn it on (or plug it in). Do that three times, once with a button held while it disconnects. Record: a `HOTPLUG removed` line each time, `HOTPLUG added` within how many seconds, the same controller number each time, no stuck `btn=` after a reconnect, no duplicate pads in the summary. For a Bluetooth pad also note whether it reconnects by itself when you press its button, or has to be re-paired.

### D. In the client, with a stream (4 min)

Start the stream from the flat launcher as usual, and keep the PC's gamepad tester open. Close any probe watch first. The probe and the client can read a pad side by side (SDL does not grab it), but one watcher at a time keeps the notes clean.

1. The pad appears in the PC's tester within a few seconds of focusing the VR client. Buttons, sticks and triggers match what step B printed.
2. Rumble: use the tester's vibration button or a game. Low and high motors feel different; trigger rumble (Xbox, DualSense) on the triggers only. Note anything missing.
3. Turn the pad off and on during the stream: it vanishes from the tester and comes back on the same number.
4. Disconnect chord: hold Start+Back+LB+RB. The VR client leaves the stream and the PC's session stays (the flat launcher offers Resume). The pad must not leave a button stuck on the PC.
5. Input latency: with the tester's graph or a game, rate the feel against the Frame's own controller in gamepad mode on the same stream (better, same, worse, and by how much). Note the stream's resolution and rate.
6. Log: `grep "gamepad" ~/Games/MoonlightXRNative/moonlight.log | tail -20 > "$OUT/<tag>-client-log.txt"`. The `gamepad #N arrived` line carries the type and capability bits the PC was told. It should agree with the `host` line the probe printed.

## Two pads at once (3 min)

Connect two different pads (for example Xbox and DualSense) and the Frame controllers in gamepad mode (cog toggle).

```sh
"$APP/mlxr-gamepad-probe" --watch --seconds 120 | tee "$OUT/two-pads.txt"
```

Record the summary block: the pads get `#0` and `#1`, the Frame pad takes the next free number once gamepad mode is on. In the stream the PC's tester shows each pad separately; rumble from the PC goes to the pad whose number it names; pressing buttons on pad 1 does not light pad 0. Disconnect one and check the other keeps its number and keeps working. Then connect two pads of the same model: they must be two pads (no serial means the client cannot tell them apart and counts both, which is right; two entries with the same serial are one pad seen twice and the second is skipped).

## Steam Input on and off (5 min, any pad; required for the Steam Controller)

Steam Input decides what the client sees. Shortcut setting in Steam (SteamOS Game Mode or Desktop Mode): the "Moonlight XR for Frame" shortcut, Properties, Controller, then Steam Input set to Enabled, then Disabled (the wording varies by Steam version; "Use default" follows the global setting).

To run the probe under the same setting as the client, add the probe as a non-Steam game and give it the same setting:

- Target: `/usr/bin/env`
- Launch options: `bash -c '"$HOME/Games/MoonlightXRNative/app/mlxr-gamepad-probe" --watch --seconds 150 --rumble > "$HOME/Games/MoonlightXRNative/gamepad-cert/<tag>-steaminput-<on|off>.txt" 2>&1'`

(This is how it was meant to be run; whether Steam passes the quoting through unchanged on the Frame is unverified. If it does not, run it from a terminal started by the shortcut, or compare with the SSH run below.) Do the run twice per pad, Steam Input enabled and disabled, with the pad doing something (buttons, sticks, a shake) while it runs.

From an SSH shell the probe sees what a desktop process sees, which is the third case (Steam running, no per-game override). Do that run too:

```sh
"$APP/mlxr-gamepad-probe" --watch --seconds 60 | tee "$OUT/<tag>-ssh.txt"
```

For each of the three, record:

| Look at | Meaning |
| --- | --- |
| `hint SDL_GAMECONTROLLER_IGNORE_DEVICES=...` in the header | Steam Input is hiding the real devices from this process |
| a device `Steam Virtual Gamepad` (28de:11ff) | Steam Input's copy; the `steam` line says which real pad it merged with, or `no real pad joined to it` when only the copy exists |
| the real pad next to its copy: `its Steam Input copy is [n]` | Steam Input is on but leaves the real device visible; the client treats both as one pad |
| the copy's `sensors` line | the copy is expected to carry no gyro or accelerometer; write down if it does |
| `summary` block | exactly one `pad #n` per physical pad, never two |

With more than one real pad and Steam Input on, the client joins each copy to the lowest-numbered pad that has none; the probe prints a warning line. Check in the stream that the right pad moves when you press buttons on each (this is a known weak point).

## Steam Controller (all three connections, 8 min)

Test the controller via the puck on USB-C, over Bluetooth, and wired (USB-C data cable), each with Steam Input enabled and disabled (previous section). The 2026 controller's ids are 28de:1302 (USB), 28de:1303 (Bluetooth LE), 28de:1304 and 28de:1305 (the two pucks); the client already treats them as `Steam` type (list taken from moonlight-qt 6.2 and SDL).

Run for each of the six combinations:

```sh
"$APP/mlxr-gamepad-probe" --watch --seconds 90 --rumble | tee "$OUT/sc-<puck|bt|wired>-<on|off>.txt"
```

Record: is the device listed, with which vendor:product (`steam` line names the product), SDL type and `fork type` (must read Steam), the `steam` line's copy information, `sensors` (the gyro question), touchpads (the 2026 controller has two), and the input lines for both trackpads, grips and the Steam button. Also the SDL runtime version from the header: an SDL without a driver for the 2026 controller cannot read its gyro directly, whatever the Steam Input setting, and then only R2 is left.

The 2026 controller's own gyro needs the two routes from plan item 2b.3:

- R1, Steam Input disabled for the shortcut: SDL's own driver reads the pad. Gyro works if and only if the probe prints `sensors     gyro yes` for the real Steam Controller device in the "off" run (the "on" run showing only the Virtual Gamepad copy means Steam Input is on). Also try the same run with the driver forced on: `SDL_JOYSTICK_HIDAPI_STEAM=1 "$APP/mlxr-gamepad-probe" --watch --seconds 60` and record whether anything changes.
- R2, Steam Input enabled, with Gyro to Mouse or Gyro to Joystick in the Steam Input layout for the shortcut: the client forwards the result as mouse or stick input with no code change. In the stream, record whether tilting moves the PC's cursor or right stick, and which activation you used. The PC game does not see native gyro in R2.

## Pads to cover

| Pad | Connection | Tag |
| --- | --- | --- |
| Xbox controller | Bluetooth | `xbox-bt` |
| DualSense | Bluetooth | `ds-bt` |
| Switch Pro controller | Bluetooth | `pro-bt` |
| 8BitDo | Bluetooth, and its wired or 2.4 GHz mode if it has one; note the mode switch position (X, D, S, Switch) | `8bitdo-<mode>` |
| Steam Controller | puck on USB-C, Bluetooth, wired | `sc-puck`, `sc-bt`, `sc-wired` |

Each needs A to D. The Steam Input on/off section and the two-pad section need only a pair of them; the Steam Controller section is all three connections.

## Sheet to fill in

One row per pad and connection:

| Tag | Listed | sdl type -> fork type | vid:pid | Buttons ok | Sticks/triggers ok | Report rate | Rumble | Trigger rumble | Gyro/accel | Hotplug ok | Chord ok | Latency vs Frame pad | Steam Input on | Steam Input off |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| | | | | | | | | | | | | | | |

Pass for a pad: listed with the right type, every button and axis correct, rumble felt where the probe says it has it, reconnect on the same number, the chord leaves the stream, and exactly one host pad per physical pad with Steam Input on and off. Anything else goes in the notes column with the probe line that shows it.

## Handing the results back

Mask Bluetooth addresses, then pack the folder:

```sh
sed -i -E 's/([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}/xx:xx:xx:xx:xx:xx/g' "$OUT"/*.txt
tar czf ~/gamepad-cert.tgz -C "$OUT" .
```

Send the archive and the filled sheet. What each result decides:

| Result | Decides |
| --- | --- |
| `sensors gyro yes` on DualSense / Switch Pro | motion forwarding for physical pads (2b.2) has real hardware to test on the Frame |
| Steam Controller: gyro in R1 or only in R2 | the setup the hint and the user guide recommend (2b.3) |
| copy merging wrong with two pads | the pairing rule in `gpAdd` needs more than "lowest pad without a copy" |
| a type that reads Unknown for a known pad | its product id goes on the type list |
| report rates or latency | whether a Bluetooth pad is usable for aiming games on the Frame |
