# Moonlight XR for Frame: user guide

This guide covers version 1.0.1. For install, update and uninstall, see the [README](../README.md).

## Contents

- [How it fits together](#how-it-fits-together)
- [The flat window](#the-flat-window)
- [Settings page](#settings-page)
- [In the headset: the bar](#in-the-headset-the-bar)
- [The cog menu](#the-cog-menu)
- [Controllers](#controllers)
- [Moving and sizing the screen](#moving-and-sizing-the-screen)
- [Rooms and passthrough](#rooms-and-passthrough)
- [3D](#3d)
- [Keyboards, mice and gamepads](#keyboards-mice-and-gamepads)
- [Host: keys for the PC](#host-keys-for-the-pc)
- [PC VR](#pc-vr)
- [Ending a stream](#ending-a-stream)
- [Recommended PC settings](#recommended-pc-settings)
- [Troubleshooting](#troubleshooting)
- [Advanced: config.ini](#advanced-configini)
- [Advanced: environment variables](#advanced-environment-variables)

## How it fits together

There are two programs.

- **The flat window** is Moonlight 6.1.0 with patches. SteamVR shows it as a flat screen in the headset. You pick a PC and an app here, and change the stream settings.
- **The VR client** (`moonlight-xr`) starts when you select an app. It decodes the stream and draws it in the headset. It has its own bar and cog menu for the screen, rooms, 3D and the pointer.

When the stream ends, the VR client closes and the flat window comes back.

## The flat window

It works like Moonlight on a PC.

| Screen | What you do there |
| --- | --- |
| PC list | Your PCs. A PC on the same network appears by itself. The **+** button (Add PC manually) adds one by address. Select a PC to pair with it or to open its apps. |
| Pairing | Moonlight shows a PIN. Type it on the Sunshine or Apollo PIN page (`https://localhost:47990` on the PC). The dialog closes when pairing is done. |
| App list | The apps your PC offers. Select one to start it. A running app has **Resume Game** and **Quit Game** in its menu. |
| Settings | The cog at the top. See [Settings page](#settings-page). |

While a stream starts, the window shows a tip: "Tip: Press Start+Select+L1+R1 to disconnect your session". With no gamepad connected it names Ctrl+Alt+Shift+Q instead.

In the headset, the Moonlight XR logo with "Connecting to *PC name*..." under it hangs in front of you from the moment a stream is the picture's source until the first frame arrives, then fades out over about a third of a second. It shows again, as "Reconnecting...", while the headset reconnects after a dropped link (see When the connection drops). It has no setting and is not shown once a picture is there.

In the headset (Game Mode) the window is drawn 1.5 times larger than on a desktop, so the text is easier to read. Set `MLXR_LAUNCHER_SCALE` to change it, for example `MLXR_LAUNCHER_SCALE=1.25`. It takes 1.0 (no scaling) to 2.5. On a desktop the window is not scaled.

The **Help** button (the question mark at the top) opens this project's readme: <https://github.com/simplysanatori/moonlight-xr-steam-frame#readme>.

If you used the Moonlight flatpak on the Frame before, its PC list and pairing are copied the first time the flat window starts. The flatpak's own files are not changed.

## Settings page

The page has the categories below, in this order. A greyed row shows "Not available:" and the reason. Settings are saved when you leave the page.

### Connection

| Setting | Options | Notes |
| --- | --- | --- |
| Network connection | **Automatic**, **Wi-Fi (*network name*, *band*)**, **Steam Frame Wireless Adapter (6 GHz)**, **USB cable** | Automatic (the default) tries every link that is there when a stream starts (Wi-Fi, the adapter and a USB cable, see [USB cable](#usb-cable)) and uses the one with the lower latency and the fewer losses. It sends about 15 short probes down each link (about a second in all) and gives each link a score in milliseconds, lower is better: the median round trip, plus the jitter, plus how far the slowest probes run above the median, plus 50 ms for every probe that failed, plus a penalty from the last stream on that link. That penalty applies when the last stream lost more than 0.5 percent of its frames on the network: 20 ms for each percent, at most 150 ms. A link with no history gets no penalty. The lowest score wins. When two scores are within 3 ms of each other, the stronger signal and faster link rate break the tie, and Wi-Fi wins a tie after that. Signal and link rate no longer decide on their own. A link that does not answer is never used. The client keeps the last stream's numbers for each link in `netpath-history.ini` next to `config.ini` (frames received, frames lost, jitter, round trip and the time; no addresses or names). It writes them when a stream ends, after at least 500 frames. Delete the file to forget them. History older than 14 days is ignored. Wi-Fi streams over your home network. The adapter option streams over the Steam Frame Wireless Adapter plugged into your PC. The USB cable option streams over a USB data cable between the headset and your PC. Before it tries the adapter, the client checks that the adapter is there: the headset's own adapter network (`wlanap`) must be up and your PC must be connected to it. With no adapter found, Automatic uses Wi-Fi without trying the adapter, and if you chose the adapter the client uses Wi-Fi and shows a notice in the headset ("Steam Frame Wireless Adapter not reachable (no adapter found: ...). Using Wi-Fi."). If the chosen link does not answer, the client uses the other one and says so in the headset. |

### USB cable

A USB data cable between the headset's USB-C port and your PC carries the stream with no radio in between. It avoids Wi-Fi interference and the packet loss the adapter showed at high bitrates. It has not been measured in the headset yet; the notes below say what is expected.

What you need:

- The headset in developer mode. The headset only creates its USB network in developer mode.
- A USB data cable, not a charge-only one, from the headset to a USB port on the PC.
- On the PC, Windows 11 sets up the network adapter by itself. The first time, set that network to **Private** (Settings, Network and Internet, the new Ethernet adapter, Network profile type) so the firewall lets the headset in, and check that Apollo or Sunshine is allowed through the firewall on it. The headset gives the PC an address on a small private network of its own; there is no internet on it and Windows will say so.

How to use it:

- Choose **USB cable** in Settings, Connection, Network connection, or leave **Automatic**. Automatic probes the cable like the other links and uses it when its score is the lowest, which it normally is. The row says whether a cable is detected; it looks again every 10 seconds, so you can plug the cable in after starting the app.
- The same choice in `config.ini` is `network_path = usb` in the `[stream]` section. On the command line it is `--network usb`, and `MLXR_NETWORK=usb` overrides both.
- If you choose USB cable and no cable is there, the stream uses the other link and the headset shows a notice, for example "USB cable not reachable (no USB cable found: usb0 down (no cable, or the PC end is not up)). Using Wi-Fi." If the cable is in and the PC does not answer on it, the notice names the address that failed.
- Pulling the cable out during a stream is a lost connection: the headset tries to reconnect twice (see Ending a stream, When the connection drops) before the flat window comes back. `moonlight.log` has `USB cable link lost`.
- Power: over the cable the headset draws from the PC's USB port, which gives far less than the headset uses while streaming, so the battery may still drain slowly. Whether a hub with USB Power Delivery pass-through fixes that has not been tested.

If the cable is never used, see Troubleshooting. The developer notes (`docs/dev/reports/usb-cable.md`) have the details.

### Video Settings

| Setting | Options | Default |
| --- | --- | --- |
| Video resolution | 360p, 480p, 720p, 1080p, 1440p, 4K, or a custom size | 1440p (2560x1440) |
| Refresh rate | **Match headset**, 144 Hz, 120 Hz, 90 Hz, 72 Hz, 60 Hz, 30 Hz, Custom | Match headset. It uses the rate SteamVR runs the headset's display at for this app: 90 Hz unless you changed it in SteamVR's settings. Pick 72, 90, 120 or 144 Hz here to choose the headset's rate: when the stream starts, the launcher sets that rate in SteamVR's per-app video settings for this app, and the stream runs at it. 120 and 144 Hz need the stream and your PC to keep up: the PC has to render and encode that many frames a second, and the network has to carry the higher bitrate. The stream never goes above the rate the headset runs at, 144 Hz at most. If SteamVR still runs the app at its old rate, the log says so and the stream uses that rate; start the app again. Custom takes values such as 59.94 and needs Apollo. |
| Video bitrate | 0.5 to 150 Mbps | Twice Moonlight's own default for the size and rate you pick: 98 Mbps at 1440p and 90 Hz, 113 Mbps at 1440p and 120 Hz, 124 Mbps at 1440p and 144 Hz, 49 Mbps at 1080p and 90 Hz, 80 Mbps at 1440p and 60 Hz. With Match headset it is the 90 Hz figure. It is set again to this value whenever you change the resolution or the refresh rate, and it never goes above 150. A value you saved earlier is kept until then. See [Recommended PC settings](#recommended-pc-settings). |
| Video frame pacing | Prefer lowest latency, Balanced, Prefer smoothest video | Balanced |
| Ultra Low Latency (experimental) | on, off | off. Runs the network and decoder threads at realtime priority. |
| Use Virtual Display | on, off | off. Needs Apollo. Apollo makes a display that matches the stream. |
| Resolution Scale Factor | 20 to 200 % | 100 %. Needs Apollo. |
| Custom Resolution | width and height | |
| Custom bitrate | 0.5 to 500 Mbps | |
| Render Mode | Default 2D, AI SBS 3D | AI SBS 3D. This is the 3D switch; the headset can also change it. See [3D](#3d). |
| 3D Effect Strength | 1 to 100 | The depth model's own value. Greyed in Default 2D. |
| 3D Convergence | 0 to 100 | 50. Greyed in Default 2D. |
| 3D Edge Sharpness | 0, 25, 50, 75, 100 (press to step) | 0. See [3D](#3d). Greyed in Default 2D. |
| 3D Motion Reset | Off, 5, 10, 20, 30 % (press to step) | Off. See [3D](#3d). Greyed in Default 2D. |
| 3D Depth Detail | 50 to 200 % (press to step) | 100 %. See [3D](#3d). Greyed in Default 2D. |
| 3D Depth Shape | 50 to 200 % (press to step) | 100 %. See [3D](#3d). Greyed in Default 2D. |
| Enable HDR (Experimental) | greyed | The Frame's video path is 8-bit SDR. |

### Audio, gamepad, input, host, general, UI

| Setting | Options | Default |
| --- | --- | --- |
| Surround sound configuration | Stereo, 5.1 Surround Sound, 7.1 Surround Sound | Stereo |
| Adjust analog stick deadzone | -20 to 20 % | 0 %. Above 0 it is also the dead zone of the Frame controllers in gamepad mode, which use 7 % at 0. |
| Flip face buttons | on, off | off. Swaps A/B and X/Y. |
| Headset keyboard layout | QWERTY (US), AZERTY (French), QWERTZ (German) | QWERTY. The PC must be on the same Windows layout, see [AZERTY and QWERTZ](#azerty-and-qwertz). |
| Enable Rumble | on, off | on |
| Enable forward/back mouse buttons | on, off | on |
| Optimize game settings | on, off | on |
| Play audio on PC | on, off | off |
| Smart Clipboard Sync (Experimental) | on, off | off. Needs Apollo. |
| Quick Resume | on, off | off. Selecting a running app resumes it at once. |
| Enable Full Screen | on, off | Takes effect at the next start. |

### PC VR

Four rows for the Host sheet's PC VR button: the check, the PC, the game and the game's Steam app id. They are described under [PC VR](#pc-vr).

### Performance Monitor and Advanced Settings

| Setting | Options | Default |
| --- | --- | --- |
| Show performance stats while streaming | on, off | off. Same as Stats in the headset's Display tab. In `config.ini`, `stats = hud` or `stats = lite` turns them on in that style (see Stats styles below). |
| Change codec settings | Automatic (Recommended), Prefer HEVC, Prefer H.264 | Automatic. Prefer AV1 is greyed: the Frame decodes H.264 and HEVC only. |
| Unlock all possible frame rates | greyed | The stream is capped at the rate the headset runs at, 144 Hz at most. |
| Force full range video (Experimental) | on, off | off |
| Force software decoding | on, off | off |

The last line of the page says where the headset's own settings are saved (`~/.config/moonlight-xr/config.ini`).

## In the headset: the bar

The bar sits under the screen and shows its buttons only while the ray is over it (see [Controllers](#controllers) if you run with the ray off). From left to right:

| Button | What it does |
| --- | --- |
| Head aim | Only shown with Head locked on and outside a room. Your head moves the mouse. |
| Gamepad / Pointer | Switches the controllers between pointer mode and gamepad mode. |
| Exit | Asks "Exit the stream?" with three choices. **Quit app** quits the app on the PC and ends the stream. **Disconnect** ends the stream and leaves the app running on the PC (use **Resume Game** in the flat window to go back). **Cancel** closes the question. |
| Environment | Opens the Rooms picker. |
| Move pill | Grab it to move the screen. See [Moving and sizing the screen](#moving-and-sizing-the-screen). |
| Cog | Opens the cog menu. |
| Keyboard | Opens or closes the headset keyboard. |
| Ray | Shows or hides the controller ray for this session only. The cog's Ray row is the saved one. |
| 3D | Only shown when the depth model is ready. Turns 3D on or off. |
| Host | Opens the Host sheet: Esc, Win, Alt+Enter and the other keys that are awkward to reach, plus your own macros. A button can open it too: `app:host` in `bindings.ini`. See [Host: keys for the PC](#host-keys-for-the-pc). |

## The cog menu

Tabs: **Screen** (called **Room** inside a room), **Display**, **3D**, **Picture**, **About**. A press outside the menu closes it. The cog opens on the tab you used last, and remembers it for the next session (`cog_tab` in `config.ini`).

### Screen

| Row | Range | Default |
| --- | --- | --- |
| Distance | 0.2 to 8 m | 3 m |
| Height | -2 to 2 m | 0 |
| Tilt | -40 to 40° | 0 |
| Rotate | -90 to 90° | 0 |
| Curve | 0 to 100 % | 0 |
| Size | 0.8 to 8 m wide | 3 m |
| Aim sensitivity | 1 to 30 | 8. Greyed unless Head locked is on. |
| Aim dead zone | 0 to 20 | 2. Greyed unless Head locked is on. |
| Preset | Desk, Couch, Cinema, Bring here | Desk is 1.2 m away and 1.6 m wide, Couch 2.5 m and 2.6 m, Cinema 4 m and 5 m. A preset sets only the distance and the width: the way the screen sits, its tilt, roll and curve stay as they were. **Bring here** puts the screen where you are looking, as far from you as it was and the same size. It does nothing in a room or with Head locked on. |
| Remember per app | Off, On, Forget | On. Each PC and app keeps its own placement, head lock and 3D state, and gets them back the next time it streams. An app that has none starts from what is in force. **Forget** drops what is kept for the app now streaming. |
| Angular-size lock | Off, On | On. Pushing or pulling the held screen with the thumbsticks resizes it with its distance, so it keeps the same size in your view. Off, a pushed screen gets smaller as it goes away. |

**Reset** puts the screen back to the defaults.

The presets and Bring here also work from a controller button: bind `app:bringhere` in `bindings.ini` (see [the binding notes](dev/bindings.md)).

### Room

| Row | Notes |
| --- | --- |
| Brightness | Per room. |
| Glow | Light from the picture onto the room. |
| Screen light | The screen lights the room. Shared by all rooms. |
| Light level | |
| Size | 25 to 100 %. Greyed in Grand Cinema, whose screen has a fixed size. |

### Display

| Row | Options | Default |
| --- | --- | --- |
| Sharpen | Off, Normal, Quality | Off |
| Supersample | Off, Normal, Quality | Normal. Not used while the screen is curved or 3D is on. |
| Stats | Off, On | Off |
| Head locked | Off, On, Follow | Off. On: the screen is fixed to your head. Follow: the screen stays where it is until your head has turned more than the leash (25° by default, `follow_leash_deg`) away from it, in yaw or in pitch, then it eases after where you look over about half a second and stays there. See [Follow and comfort](#follow-and-comfort). |
| Head aim | Off, On | Off. Needs Head locked; not in a room. |
| Mode | Pointer, Gamepad | Pointer. Every session starts in pointer mode. The row also shows the switch shortcut. |
| Show controllers | Off, Frame, Generic, Gun | Off. Which controller is drawn in each hand; see [Controllers](#controllers). |
| Ray | Off, Dot, Ray | Ray. Dot shows only the dot where the ray lands. Off shows neither; the PC's own cursor is always in the video. Bar = this session, cog = saved: the choice here is kept for the next session, the bar's Ray button is not. |
| Pointer sleep | Off, On | On. The pointer pauses while the controller is still. |
| Click steadiness | Off, Normal, Steady | Normal. How still the PC's cursor is held around a click. Normal sends a click where you aimed before the trigger pull moved the controller, and holds the cursor for a double click. Steady also keeps the cursor still from the moment you touch the trigger until you aim a few pixels away. Off sends the aim as it is, for light gun games and fast clicking. |
| Click sound | Off, On | On |
| Glow | Off, On | Off |
| Screen light | Off, On | On |
| Notices | All, Important, Off | All. The small pop-ups that appear below the picture for about 2.5 seconds. Important leaves out "Display N Hz" and "3D on/off" and keeps mode changes (gamepad, head aim, hands) and messages such as errors. Off shows none. The flat window's Settings page has the same choice. |
| Glow level | 0 to 100 % | 50 % |

### 3D

| Row | Notes |
| --- | --- |
| Preset | Comfort, Balanced, Strong. |
| Depth | How far apart the eyes' pictures are. The part of the track past the model's default is marked "harder on the eyes". |
| Convergence | Where the screen plane sits in depth. Default 50. |
| 3D | On or Off. Remembered between sessions. |
| Reset | Back to the model's values. |

The tab is greyed when 3D cannot run. It shows why: 3D is off in settings, the depth model is not installed, or it did not load.

### Picture

Brightness (-50 to 50), Contrast (50 to 150 %), Gamma (0.50 to 2.00) and Saturation (0 to 200 %), with **Reset**.

### About

The version, the log file, a link to support Moonlight XR on Ko-fi, and **Report a problem**. The report sheet has three buttons. **Save** keeps your note on the Frame in `~/Games/MoonlightXRNative/reports`. **Save and zip** does that and also runs `bug-report.sh` in the background, so the masked zip is made without leaving the headset. A notice shows where the zip is (`~/Games/MoonlightXRNative/reports/mlxr-report-<date>.zip`) and the log says so too. A note is not needed for the zip. Nothing leaves the headset: copy the zip off the Frame and open an issue on GitHub with it. The [README](../README.md#reporting-a-problem) lists what the zip holds and what is left out.

### Stats styles, battery and the slow connection badge

The Stats row turns the overlay on and off. What it shows is `stats_style` in `config.ini` (`[display]`): `full` is every line the session writes, `hud` is a few lines (latency with its parts, round trip, loss, bit rate, frame rate, and the compositor's frame times and dropped frames when SteamVR reports them), and `lite` is one line: `54 Mb/s | 11 ms | dec 3.1 | loss 0.2% | 90 fps` (speed, latency from the host's capture to the end of decoding, decode time, frames lost on the network, pictures drawn a second). `stats = hud` in the file means stats on in the HUD style. While stats are on and a stream is up, `perf-<date>-<time>.csv` next to `vrtiming.log` gets a row every 5 seconds with the figures and the settings they were measured in (size, rate, bit rate, codec, display rate, 3D, room, sharpening), for comparing runs.

Over the cog the clock strip also shows the headset's battery (a cell, the percentage, a bolt while it charges; read every 30 seconds, nothing on a PC with no battery) and a **Slow connection** badge once the connection has been poor, or more than 1.5 % of frames lost, for 5 seconds on end. The badge goes when it has been fine for 5 seconds. The HUD line starts with `!` or says slow connection while it is up.

## Controllers

The right Frame controller has A, B, X, Y and a menu button. The left one has a d-pad and a View button. Both have a trigger, a grip, a bumper and a thumbstick.

### Pointer mode

This is the default. The ray from either controller is the mouse.

| Control | Action |
| --- | --- |
| Trigger | Left click. Hold to drag. |
| A, or d-pad down | Right click |
| B, or d-pad right | Middle click |
| Thumbstick click | Right click |
| Thumbstick | Scroll, while the ray is on the picture: up and down, and sideways. The scroll is smooth; `scroll_speed` in `config.ini` sets how fast. |
| Grip | Grab the move pill, a corner of the screen, the keyboard's bar or the stats window |
| Left bumper, right bumper | Back and Forward (the mouse's side buttons, as in a web browser) |
| Y | Show or hide the keyboard |
| X | 3D on or off |
| d-pad up | Recentre: the screen comes round to where you face |
| Menu (right) | Open or close the cog menu |
| d-pad left, View | Nothing (View with the left grip is the mode switch) |

Every button can be changed in `bindings.ini`, for every app or for one app. See [Changing the buttons](#changing-the-buttons).

A click is sent at the point you aimed at before the trigger pull moved the controller. After a click the cursor waits for half a second while you keep aiming near it, so the second click of a double click lands on the first one. Move the ray away and the cursor follows at once. The Display tab's **Click steadiness** row turns this off or makes it stronger.

### Gamepad mode

The two controllers become one Xbox controller on the PC.

| Frame control | Xbox button |
| --- | --- |
| A, B, X, Y (right) | A, B, X, Y |
| Menu (right) | Start |
| View (left) | Back (Select) |
| d-pad (left) | d-pad |
| Left and right thumbsticks, stick clicks | Left and right sticks, LS, RS |
| Left and right triggers | LT, RT |
| Left and right bumpers | LB, RB |
| Left and right grips | LB, RB (change with `pad_grip`, see [below](#advanced-configini)) |
| Left View and right Menu, held together for a quarter of a second | Guide (the Xbox button) |

Start and Back reach the PC as you press them. If you keep both down for a quarter of a second they turn into Guide, which stays down while you hold them, and neither of them is sent after that until you let go of both. The Start and Back button presses of the first quarter of a second still reach the game. If you also press both bumpers (Start, Back, LB and RB is the disconnect chord) it is the disconnect chord and not Guide.

The stick dead zone is 7 % of the way out. Move the launcher's **Adjust analog stick deadzone** slider above 0 and the controllers use that value instead (1 to 20 %). Values below 0 are dead zone compensation, which only physical gamepads use.

The PC's rumble drives the controllers in gamepad mode. By default both controllers get the stronger of the PC's two motors, so a game that uses one motor is felt in both hands; the trigger motors (when the PC sends them) add to the controller on their side. `rumble_mix` and `rumble_intensity` in `config.ini` change this (see [below](#advanced-configini)). If you feel nothing, `moonlight.log` has `rumble: the host's first request` when the PC's first rumble arrives, and `rumble on the left controller ... runtime said` for what the headset's runtime did with it. No first-request line means the PC sent no rumble (check Enable Rumble in the launcher and the PC's gamepad type).

### Changing the buttons

`~/.config/moonlight-xr/bindings.ini` says what each control does. The first start writes it with every default as a line that starts with `#`, so it changes nothing until you take the `#` off a line and edit it. For example:

```ini
[pointer]
right.y = key:F                ; Y presses F on the PC
left.dpad_left = key:ctrl+shift+esc

[gamepad]
right.grip = key:R             ; the right grip presses R instead of RB

[app "Time Crisis 2"]          ; only while this app (its name in the app list) is streamed
pointer.right.trigger = mouse:left
```

An action is `key:` (a key, with `ctrl+`, `shift+`, `alt+`, `win+` in front), `mouse:left|right|middle|x1|x2`, `wheel:up|down|left|right`, `pad:` (a gamepad button, gamepad mode only), `stick:` (what a whole thumbstick does: `scroll`, `mouse`, `wasd`, `arrows`, `dpad`) or `app:keyboard|ray|recentre|cog|3d|gamepad|bringhere|host`. A line that is not right is left out: `moonlight.log` says which line and why, and the headset shows "bindings.ini: N errors, see log". The mode switch shortcuts, the disconnect chord and the grips' grab over the bar and corners cannot be changed. The full list is in the file itself. The flat launcher's **Controller bindings** page edits the same file ([BINDINGS.md](BINDINGS.md)). The headset reads the file when a stream starts, so a change takes effect at the next stream start.

### Switching between the modes

- Hold the **left View button and the left grip** together for half a second.
- Or select the Gamepad/Pointer button on the bar, or the Controllers row in the Display tab.

The headset shows "Gamepad mode" or "Pointer mode". In gamepad mode the menus close. The shortcut can be changed in `config.ini` (`gamepad_toggle`, see [below](#advanced-configini)).

## Moving and sizing the screen

| What | How |
| --- | --- |
| Move the screen | Grip the move pill under the screen and move your hand. |
| Push it away or pull it closer | While holding the pill, push either thumbstick up (farther) or down (closer). A push on a slant counts, and most of the way to the rim is full speed: 1.5 m a second either way. 0.3 to 8 m. |
| Resize | Grip a corner of the screen. In a room this sets the room's screen size. |
| Curve | Screen tab, Curve. |
| Recentre | Use SteamVR's recentre. The screen turns to your new forward direction and keeps its distance, size and curve. |
| Move the headset keyboard | Grip or trigger on the keyboard's bar. The thumbstick pushes and pulls it the same way. |
| Move the stats window | Grip anywhere on it and drag. |
| Reset | Screen tab, Reset. |

### Follow and comfort

Follow is a lazy head lock. A screen fixed to the head never leaves your view, which some people find tiring or queasy; a screen fixed in the world makes you turn all the way round to find it again. Follow sits between: look around inside the leash and the screen holds still, look further and it glides after you (a slerp over 0.6 s, slow at the start and the end) and stops. It keeps its distance from you, its size and its tilt, and it waits while a menu is open, while you hold the screen, in a room and in the launcher's lists.

If it makes you uneasy, make the leash larger (up to 60°) so it moves less often, or use Off or On. Sit still for the first try: a screen that moves while you turn your head is the most noticeable thing to get used to. Following is a world-locked mode, so Head aim (which needs Head locked) is off while it is.

## Rooms and passthrough

The Environment button opens the Rooms picker:

| Choice | What you see |
| --- | --- |
| Passthrough | The Frame's cameras around the screen |
| Black void | Black (the default) |
| Home Theater | A home cinema room |
| Grand Cinema | A large cinema. The screen size is fixed. |
| Synthwave | A neon landscape |

The choice is remembered. Inside a room the bar's move pill does nothing, and the Screen tab becomes the Room tab. Controllers are hidden in passthrough.

## 3D

The headset turns the 2D stream into 3D with a depth model running on the Frame. It works with any game or video. It adds about 80 ms between the picture and its depth, so fast motion can show soft edges.

- Turn it on or off with the bar's 3D button, the 3D tab, or Render Mode on the flat Settings page.
- Comfort, Balanced and Strong set the depth. Depth and Convergence fine-tune it.
- 3D is on by default and remembered.
- The default model is ZipDepth. `depth_source` in `config.ini` chooses `zipdepth`, `zipdepth-int8` (lighter on the CPU) or `midas`.

### Edges

Each eye sees a little more of what is behind a person or an object on one side: the left eye on the left, the right eye on the right. Earlier versions filled that sliver by stretching the person's own edge across it, which showed as a fuzzy, furry outline in one eye only, most of all on hair or a dark outline against a bright sky or page. That gap is now filled from the background beside it, so the edge is as sharp in both eyes. This is on by default; `MLXR_HOLE_FILL=0` brings back the old look.

These four are on the flat window's Settings page and in `config.ini` (`[3d]`). They are not on the headset's 3D tab yet.

| Setting | Key | Values | What it does |
| --- | --- | --- | --- |
| Edge Sharpness | `edge_sharpness` | 0 to 100 | 0. How closely the depth keeps to the edges in the picture. 0 is the soft edges the 3D always had. Higher keeps hair, outlines and thin text on their own depth instead of blending them into what is behind; too high can make edges look cut out or blocky on text. |
| Motion Reset | `motion_reset` | 0 to 50 (%) | 0, off. The depth is smoothed over time so it does not flicker, which leaves a short trail of the old depth behind something moving fast. With this set, where the picture changes by more than this percentage between two depth maps the smoothing starts again there, so fast edges leave no trail. About 10 suits fast games; too low brings back flicker. |
| Depth Detail | `depth_scale` | 0 to 200 (%) | 100. Moonlight XR's depth scale. The depth between things near each other, such as a face in front of a wall, scaled without moving the picture as a whole further in or out. |
| Depth Shape | `depth_global` | 0 to 200 (%) | 100. The depth of the picture as a whole, from the near ground to the far sky, scaled toward the screen plane. Lower is easier on the eyes; Depth Detail keeps things standing apart. |

At their defaults all four leave the picture as before.

## Controllers

The cog's **Display** tab has the controller rows together: **Mode**, **Show controllers**, **Ray**, **Pointer sleep**, **Click steadiness** and **Click sound**.

| Row | What it does |
| --- | --- |
| Show controllers | **Off** (the default) draws no controller in the headset. The other cells draw one in each hand: **Frame** is the Steam Frame controller itself (the installer converts SteamVR's own models on your Frame, so they match the real controllers and move with your buttons and sticks; Valve's models are never in the download; if the conversion failed you get the generic controller instead), **Generic** is a plain Touch-style controller that is always available, and **Gun** is a pistol held like a light gun: it is drawn at the ray, with the barrel pointing along it and the muzzle where the dot is, and its trigger moves as you pull yours. Controllers are hidden in passthrough. The choice takes effect at once and is kept for the next session. |
| Mode | Pointer or Gamepad. Every session starts in pointer mode. |
| Ray | Off, Dot or Ray: how the pointer shows. |

**A model of your own.** `controller_model = custom` in `config.ini` (or the launcher's Settings page, under Input Settings, **Show controllers**: press the row until it says Your own model) draws the model you put in `~/.config/moonlight-xr/models/`. The cog has no cell for it. If the files are missing or cannot be read, the Frame controllers are drawn and `moonlight.log` says why. How to make the files: [Custom controller models](CUSTOM_MODELS.md). Older config files that say `controller_model = true` or `false` still work (Frame and Off).

**Bar = this session, cog = saved.** The bar's Ray, Gamepad / Pointer and Head aim buttons change this session only. The cog's Ray row and Show controllers are kept for the next session.

The bar is hidden until the ray is over it. With **Ray** set to **Off** (no beam and no dot), a faint beam shows only while the controller points into the bar's area under the screen, so you can still find the bar. Anywhere else, and with a panel open, nothing extra is drawn.

## Keyboards, mice and gamepads

| Device | Notes |
| --- | --- |
| Headset keyboard | The bar's Keyboard button. **Compact** has letter, symbol and function sheets. **Full** is a tenkeyless layout. On both, tap a modifier (Shift, Ctrl, Alt, Win) for one key press, hold it half a second to lock it (a locked key turns blue; tap it again to let it go). On Compact, a locked Shift keeps the capitals on. The key at the right switches between the layouts. Under the keys, on both layouts, every sheet and every keyboard map, a row of media keys: previous, play/pause, next, mute, volume down and volume up. They press the PC's own media keys, so they change the PC's volume and the PC's player, not the stream's. Hide, the bar button or a press elsewhere closes it. |
| Bluetooth keyboard | Pair it in SteamOS. Keys go to the PC while the stream is in front. The app takes the keyboard for itself during the stream. |
| Bluetooth mouse | Same as the keyboard. While you use the mouse, the controller ray does not move the cursor. |
| Bluetooth gamepad | Pair it in SteamOS. It shows up as a gamepad on the PC; once you use it the Frame controllers' gamepad steps aside ([below](#frame-controllers-and-a-gamepad-together)). The Steam Virtual Gamepad copy of the same pad is ignored. Hold Start for a second and it becomes the PC's mouse ([below](#a-gamepad-as-the-pcs-mouse)). Start+Select+L1+R1 disconnects. |

Gyro from Bluetooth pads reaches the PC as a DualSense-class pad. A pad with a gyroscope (a DualSense, DualShock 4 or Switch Pro pad, for example) tells the PC so when it arrives, and the PC asks for the motion data only when a game or Steam Input wants it. This needs Apollo or Sunshine with DS4 emulation on the PC (Apollo: ViGEmBus installed). The Frame controllers in gamepad mode are an Xbox pad and never send motion. The same pad's battery level, touchpad (DualSense) and light bar colour are passed on too.

### A gamepad as the PC's mouse

Hold **Start** on a Bluetooth or USB gamepad for a second and the gamepad becomes the PC's mouse, for the desktop, launchers and menus a game pad cannot reach. A notice in the headset says so. The game sees that gamepad at rest for as long as it is a mouse. Hold Start again for a second to give it back.

| Control | While it is a mouse |
| --- | --- |
| Left stick | Moves the pointer, slowly at first and faster the further you push |
| Right stick | Scrolls (up and down, or sideways, whichever way you lean) |
| A, B, X | Left, right and middle click |
| LB, RB | Back and forward (the mouse's side buttons) |
| Y | Show or hide the headset keyboard |
| D-pad | The arrow keys |

Everything the PC had down is let go when the mode ends, when the stream goes out of focus (the dashboard, taking the headset off) and when the stream ends; the gamepad is a gamepad again after any of them. `pad_mouse_chord` in `config.ini` (or **Gamepad as mouse** on the launcher's Settings page) picks the chord: Start held, Start and Back held, or off. `pad_mouse_scroll` picks the scrolling stick (the other one points). The notice follows the **Notices** setting and is left out when that is **Off**.

### Frame controllers and a gamepad together

With the Frame controllers in gamepad mode and a Bluetooth gamepad in use, the PC would see two gamepads. `pad_arbitration` (or **When a gamepad is connected** in the launcher) decides: **pointer** (default) takes the controllers' gamepad off the PC the moment the gamepad is first used, so they point again; turn gamepad mode off and on to bring it back. **also_gamepad** leaves both. **sleep** is pointer, and the controllers' pointer rests while the gamepad was used in the last 20 seconds.

### AZERTY and QWERTZ

The headset keyboard can show French (AZERTY) or German (QWERTZ) letters. Pick it in the launcher's Settings page under Input Settings ("Headset keyboard layout"), or set `[vr] keyboard_keymap` in `config.ini` to `qwerty` (the default), `azerty` or `qwertz`.

**The PC has to be on the matching Windows keyboard layout.** The keys do not send letters. Like a real keyboard, each one sends the position of a key on a US keyboard, and Windows turns that position into a character with the layout the PC is set to. This is the same for a Bluetooth keyboard. With AZERTY picked and the PC on a US layout, the key labelled "a" types "q". Pick the layout here that is active on the PC (Windows: Settings, Time & language, Language & region, Typing, and the language bar). The keyboard cannot see the PC's layout, so it does not switch by itself.

- **AZERTY** matches Windows "French (France)". The number row shows `& é " ' ( - è _ ç à`, and the Shift sheet shows the digits. The Compact keyboard has `ù` on the symbols sheet and the letter `m` at the end of the bottom letter row. The `^` key of the Full keyboard is a dead key, as on a real French keyboard.
- **QWERTZ** matches Windows "German (Germany)". The Compact keyboard has `ä ö ü ß` and the capitals on the symbols sheet, and the brackets `[ ] { } \ |`, `<` and `>` on the Fn sheet. `^` and the acute accent are dead keys on the Full keyboard, as on a real German keyboard: press a letter after them, or space for the accent alone.
- The Full keyboard grows the extra key that French and German keyboards have beside the left Shift (`<` and `>`), so the left Shift is shorter. For the characters on AltGr (`@`, `#`, `{`, `[`, `|`, `\`, `€`), tap the right Alt: Windows reads it as AltGr on these layouts. The Compact keyboard types those characters as text, which works on any layout.
- The report sheet's note field always shows the QWERTY keys.

## Host: keys for the PC

The **Host** sheet is a grid of keys to send to the PC, for the ones a headset keyboard makes awkward to reach. Press a cell and it is sent once, and the sheet closes. Every key goes the way the headset keyboard's keys go.

| Cell | Sends | Handy for |
| --- | --- | --- |
| Esc | Esc | |
| Win | The Windows key | Start menu |
| Win+D | Win+D | Show the desktop |
| Alt+Enter | Alt+Enter | Full screen in many games |
| Ctrl+Alt+Tab | Ctrl+Alt+Tab | App switcher that stays up |
| Shift+Tab | Shift+Tab | Steam overlay |
| Win+G | Win+G | Xbox Game bar |
| Win+Shift+Left | Win+Shift+Left | Move a window to the other monitor |
| Ctrl+V | Ctrl+V | Paste |
| Ctrl+Shift+Esc | Ctrl+Shift+Esc | Task manager |
| Alt+F4 | Alt+F4 | Close the window. It asks first: **Send** or **Cancel**. |
| Send keys... | Whatever you type next | Opens the headset keyboard waiting for one combo. Tap the modifiers (Ctrl, Alt, Win, Shift), then the key: the whole combo goes once. Hide cancels. |

After those come your own macros, up to eight.

In the header, left of **Close**, is **PC VR**: it hands the headset to Valve's Steam Link, see [PC VR](#pc-vr).

### Macros

Macros live in `macros.ini` next to `config.ini` (`~/.config/moonlight-xr/macros.ini`, or `$MLXR_CONFIG_DIR/macros.ini`). The Host sheet reads it each time it opens, so you can edit it and open the sheet again without restarting. A line is `name = steps`. The steps are separated by spaces or commas:

| Step | Does |
| --- | --- |
| `ctrl+shift+esc` | Taps that key with those modifiers held. Names are the same as in `bindings.ini` (`key:` names): letters, digits, `F1` to `F24`, `esc`, `tab`, `enter`, `space`, arrows, `volumeup` and so on, or a Windows virtual key as `0x41`. Modifiers are `ctrl`, `shift`, `alt` and `win`. |
| `down:alt` and `up:alt` | Presses a key and leaves it down, and lets it go. For when the order matters, like holding Alt while tapping Tab twice. A macro that ends with something still down lets it go. |
| `wait:200` | Waits that many milliseconds (1 to 5000) before the next step. |

A `!` in front of the first step makes the macro ask first, like Alt+F4 does. A line starting with `#` or `;` is a comment, and so is the end of a line after ` ;` or ` #`. `[macros]` as a section heading is allowed and not needed.

```ini
[macros]
Task manager = ctrl+shift+esc
Alt-Tab twice = down:alt, tab, tab, up:alt
Snip = win+shift+s
Mute mic = ctrl+alt+m
Paste and send = ctrl+v, wait:150, enter
Lock PC = !win+l
```

A line that is not right is left out, the rest still work, and `moonlight.log` names the first one and why. Up to 16 macros of 32 steps each; the sheet shows the first eight.

## PC VR

The **PC VR** button on the Host sheet ends this stream and has the headset's Steam stream a VR game from your PC. **This is Valve's Steam Link, not Moonlight.** The picture, the head and controller tracking, the eye-tracked foveation and the audio all come from Valve's own streaming; Moonlight XR for Frame only gets out of the way. Moonlight and Apollo cannot carry a headset's position to the PC, so there is no PC VR inside this client, and none is planned.

What you need: the headset and the PC signed in to the same Steam account, the headset paired with the PC in Steam (Valve's setup: plug the Steam Frame Wireless Adapter into the PC, start SteamVR on it, pair), and the VR game installed on the PC. This works only when the stream was started from the flat Moonlight XR window (the Steam shortcut). Started straight from the headset's own PC list it says so and carries on.

**What happens.** You press **PC VR** and confirm (**Switch** or **Cancel**). The Moonlight stream stops, but your session on the PC keeps running, as with **Disconnect**. The flat window then asks Steam, through Steam's local debugging port, to stream the game you chose on the Settings page. If you chose none, it opens Steam's library instead and you press **Stream from your PC** on a game yourself. When Valve's stream ends, the flat window comes back as after any other stream.

**Choosing the game.** On the flat window's Settings page, under **PC VR**:

| Row | What it does |
| --- | --- |
| Switch to PC VR | Press it to check Steam and list the PCs you paired with and their games. It also shows what is wrong when something is: Steam's debugging port not answering, the call missing on this version of Steam. |
| PC to stream from | One of the listed PCs. The default is the only paired PC. |
| PC VR game | One of the listed games (VR ones are marked), or none: Steam's library opens. |
| PC VR game: Steam app id | The same choice typed by hand, for when Steam does not list the PC's games. The id is the number in the game's store page address. |

**Limits, honestly.**
- It needs Steam's debugging port (port 8080 on the headset), the same one the flat window's return from a stream uses. SteamOS updates can move or close it. The launcher checks it when it starts in Game Mode and logs `Steam CEF preflight: ...` in `moonlight-launcher.log`; the Settings page shows the same sentence. If it fails, the hand-off cannot work; `python3 -I steam_cef.py --probe` in a terminal on the headset says why, and `STEAM_CEF_PORT=<port>` points it elsewhere.
- The calls it makes (`SteamClient.Apps.StreamGame` and the Remote Play device list) are Steam's own client functions that Valve does not document. Their exact form on the Frame's Steam can change with an update. If Steam refuses, nothing is harmed: the stream has already stopped, the flat window is still there, and the log has the reason.
- Whether Steam starts the stream while this app's shortcut is still the running game is not yet confirmed on the headset. If it does not, quit this app from the Steam library and use Valve's own **Stream from your PC** on the game.
- For developers: after a SteamOS or SteamVR update, `python3 tools/probe/doctor.py` in the source tree (in a terminal on the headset, with the client closed, `mlxr-hello` built) compares what the headset's OpenXR runtime offers with the last known good probe and prints what changed, and checks Steam's debugging port. `--save` keeps the current probe as the new baseline.

## Ending a stream

| How | What happens on the PC |
| --- | --- |
| Bar, Exit, then Quit app | The app on the PC is quit. |
| Bar, Exit, then Disconnect | The stream ends. The game keeps running on the PC. Use **Resume Game** in the flat window to go back. |
| Start+Select+L1+R1 (gamepad mode, or a Bluetooth gamepad) | The stream ends. The game keeps running on the PC. Use **Resume Game** in the flat window to go back. |
| Ctrl+Alt+Shift+Q on a Bluetooth keyboard | Same as Start+Select+L1+R1. |
| The PC ends the stream | The flat window comes back. |
| The network drops | The headset keeps the room and the screen where they were and tries the same app again, twice (after 2 seconds, then 5 seconds later). The flat window comes back if both tries fail. |
| The headset stays off | After `doff_disconnect` seconds (60 by default) the stream ends. The game keeps running on the PC. |
| Nothing is touched for `idle_disconnect` minutes (off by default) | The stream ends. The game keeps running on the PC. |

On the Frame controllers, Start is the right menu button, Select is the left View button, and L1 and R1 are the bumpers. Hold all four.

### When the connection drops

If the stream ends because the link was lost (a Wi-Fi dropout, the USB cable pulled out, the PC not answering), the headset says "Connection lost, reconnecting (1/2)..." (the Notices setting decides whether you see it), keeps the room, the screen's place and the 3D state, and asks the PC to resume the app that was running. The first try comes 2 seconds after the drop and the second 5 seconds after the first one fails. When one works the picture comes back and nothing more is said. The tries choose the network link again, so a stream that was on Wi-Fi can come back on the USB cable.

It does not start the app a second time: if the app is no longer running on the PC (it was closed there, or the PC restarted), or the PC refuses the headset, it gives up at once. It also does not try when the PC ended the stream on purpose (you quit the app on the PC, or it exited). Pressing Exit or Disconnect while it is trying stops the tries. When the tries are over the flat window comes back, as before. `moonlight.log` has a line for each step, starting with `reconnect:`.

### Taking the headset off

When the headset comes off, the sound mutes at once and everything held down on the PC is let go (keys of the headset keyboard, the controllers' mouse buttons, the Frame's gamepad). Put it back on and the sound returns and the stream carries on. If it stays off for `doff_disconnect` seconds (60 by default, in the `[stream]` section of `config.ini`, 0 means never) the stream is disconnected, leaving the game running on the PC, and the flat window comes back; **Resume Game** goes back in. The Settings page has the same choice under **Disconnect when the headset is off**.

`idle_disconnect` (minutes, 0 by default which is off; **Disconnect when idle** on the Settings page) does the same when nothing has been pressed or moved for that long: the controllers' ray or buttons, the headset keyboard, a physical keyboard or mouse, or a gamepad. The clock starts over when the headset goes back on or the dashboard is closed. Opening the dashboard also mutes the sound, as it always did.

This uses the headset sensor through `XR_EXT_user_presence`. `moonlight.log` has `presence:` lines: `headset off`, `headset back on after N s`, and the disconnect. If the runtime does not offer the extension, the log says so and none of this happens.

## Recommended PC settings

| Setting | Recommendation |
| --- | --- |
| Bitrate | 40 to 60 Mbps over the Steam Frame Wireless Adapter. Higher rates lost packets on that link in testing. On a strong Wi-Fi connection 80 Mbps at 1440p worked. The default is higher (98 Mbps at 1440p and 90 Hz), so lower it in Settings if you see lost packets. |
| Codec | Automatic or HEVC. HEVC recovers from lost packets without waiting for a full new frame. |
| Display refresh rate on the PC | The stream's rate or higher (90 Hz for a 90 Hz stream, 120 or 144 Hz for those). A 60 Hz desktop gives a 60 fps stream. |
| Apollo | Use Virtual Display. The virtual display is made at the stream's rate (90, 120 or 144 Hz), so it matches the stream. |
| Sunshine or Apollo encoder | The fastest preset of your encoder (for NVENC, the lowest P number). |
| FEC | Leave Sunshine's FEC Percentage at its default (20) or raise it a little on a weak link. |

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Judder or stutter | Turn on Stats (Display tab). `Display 90 Hz, 90 fps` means the headset draws every frame. If `Video ... fps` is low, the PC is sending fewer frames: check the PC's display rate. If `idr requests` keeps rising, packets are being lost: lower the bitrate or try the other link. `vrtiming.log` shows SteamVR's dropped and reprojected frames. |
| Crackling sound | The stats line `audio ... underruns, ... lost` counts gaps. Lost packets are the network. On the adapter link, lower the bitrate. |
| Which link is in use | `moonlight.log` has `network path: using Wi-Fi (...)` or the adapter, when the stream starts. The line before it, `network path: chose ...`, gives both scores and what decided it (latency, failed probes or the last stream's losses), and `network path: history: ...` shows what the last stream on each link lost. The headset shows a notice when it falls back to the other link. |
| The USB cable is never used | `moonlight.log` has a line `network path: USB cable: ...`. `usb0 up, carrier on, 3 peer candidates` means the headset sees the cable and tried three addresses on it: if none answered, check on the PC that the new network adapter is up, set to Private, and that Apollo or Sunshine is allowed through the firewall. `usb0 down (no cable, or the PC end is not up)` means no PC is on the other end: try another cable (it must carry data) or another USB port. No `USB cable` line at all means the headset is not in developer mode, so it has no USB network. In a terminal on the headset, `mlxr-cli probe-paths --network usb` prints the same check and the result for each address, and starts no stream. |
| The adapter is never used | `moonlight.log` has a line `network path: Steam Frame Wireless Adapter: ...`. `present (...)` means the PC is connected to the headset's adapter network. `no adapter found (wlanap up, no station)` means the headset's adapter network is on but no PC has joined it: plug the Steam Frame Wireless Adapter into the PC and wait for it to connect. `no adapter found (wlanap down)` or `(wlanap missing)` means the headset's adapter network is off. In a terminal, `mlxr-cli adapter` prints the same check and does not start a stream. |
| Cannot reach the PC | `moonlight.log` has `host ... not reachable at ...` for each address tried. Check the PC is on and Sunshine or Apollo is running. |
| PC VR does nothing, or Steam's library opens instead of the game | The Settings page's first PC VR row shows the reason. `moonlight-launcher.log` has `Steam CEF preflight:` at start (Game Mode) and `Asked Steam for PC VR:` after the button. `python3 -I steam_cef.py --probe` prints whether Steam's debugging port answers and whether the calls are there; `--devices` lists the paired PCs; `--apps --client <id>` lists their games. The library opens when no game is chosen. |
| The flat window does not come back after a stream | Open the app again from the Steam library. `moonlight-launcher.log` shows `Asked Steam to show the flat screen again` and the `flat window` lines (what the window did after the stream). |
| Grey icon in the SteamVR dock | Restart Steam once after the first install. |
| 3D tab greyed | The tab says why. If the depth model did not load, the log has the error. |
| The stream did not start | The flat window shows the client's error. `moonlight-console.log` has the details. |

Logs are in `~/Games/MoonlightXRNative`. They are not masked. To share them, use the zip from `bug-report.sh`. It holds the logs, your headset note, the settings you changed and a short version summary, all masked, and nothing else. It leaves out `hosts.ini`, keys and certificates. Look inside before you share it.

## Advanced: config.ini

`~/.config/moonlight-xr/config.ini` holds the headset settings. The cog menu writes most of them. A few are only in the file:

| Section, key | Values | Default |
| --- | --- | --- |
| `[display] head_follow` | `true`, `false` | `false`. The Head locked row's Follow cell. Head locked wins when both are on. |
| `[display] follow_leash_deg` | 10 to 60 | 25. How far, in degrees of yaw or pitch, your head turns from a followed screen before it eases after you. |
| `[screen] per_app` | `true`, `false` | `true`. Remember the layout per PC and app. |
| `[screen] angular_lock` | `true`, `false` | `true`. Pushing or pulling the held screen keeps its size in your view. |
| `[screen.<hash>]` | the `[screen]` keys, `head_locked`, `head_follow`, `stereo`, `separation`, `convergence` | One section for each remembered PC and app, named by a hash of their names. The program writes them (up to 32, the oldest dropped); delete a section to forget the app. |
| `[vr] gamepad_toggle` | `menu_grip` (left View + left grip), `sticks` (click both sticks), `triggers_grips` (both triggers and both grips) | `menu_grip` |
| `[vr] rumble_mix` | `both` (each controller gets the stronger of the PC's two motors), `split` (low frequency motor on the left, high frequency motor on the right) | `both` |
| `[vr] rumble_intensity` | 0 to 100 (%), the scale on the PC's rumble on the controllers | 100 |
| `[vr] pad_grip` | `bumpers` (the grips are LB and RB), `none` (the grips do nothing in gamepad mode), `stick_clicks` (the grips are LS and RS) | `bumpers` |
| `[vr] pad_mouse_chord` | `start_hold` (hold Start for a second), `start_back` (hold Start and Back), `off`. Turns a physical gamepad into the PC's mouse and back. | `start_hold` |
| `[vr] pad_mouse_scroll` | `right`, `left`, `none`. The stick that scrolls while a gamepad is a mouse; the other stick points. | `right` |
| `[vr] pad_arbitration` | `pointer` (a used gamepad takes the Frame controllers' gamepad off the PC), `also_gamepad` (both), `sleep` (pointer, and the controllers' pointer rests) | `pointer` |
| `[vr] scroll_speed` | 25 to 400 (%) | 100, which is 7.5 notches a second with the thumbstick pushed all the way. The flat window's Settings page has it too. |
| `[vr] click_steadiness` | `off`, `normal`, `steady` | `normal`. The Display tab's Click steadiness row. |
| `[vr] keyboard_hold_ms` | 200 to 1500 | 500 |
| `[vr] keyboard_keymap` | `qwerty`, `azerty` (French), `qwertz` (German). What the headset keyboard's keys say. The PC must be on the matching Windows layout. | `qwerty` |
| `[vr] depth_source` | `zipdepth`, `zipdepth-int8`, `midas` | `zipdepth` |
| `[vr] hand_tracking` | `true`, `false` | `false`. Untested on the Frame. |
| `[vr] gaze` | `true`, `false` | `false`. Untested on the Frame. |
| `[vr] pointer` | `true`, `false` | `true`. `false` turns the controllers' pointer off, so only the switch to gamepad mode is read. |
| `[vr] env_res` | `low`, `standard`, `high`, `ultra` | `standard`. How sharp the rooms are drawn: `low` is half the headset's recommended size, `ultra` the largest the runtime allows. Read when a room is built, so it applies from the next stream. |
| `[debug] depth_rate` | 5 to 45 | 20. The most depth maps made per second in 3D. Lower saves the CPU; the picture still follows the video. |
| `[debug] depth_threads` | 1 to 4 | 2. CPU threads the depth model runs on. 3 makes each depth map a few milliseconds sooner (up to about 10) and uses another core. `MLXR_DEPTH_THREADS` overrides it. Read when the model starts. |
| `[3d] edge_sharpness`, `motion_reset`, `depth_scale`, `depth_global` | see [3D](#edges) | 0, 0, 100, 100 |
| `[debug] depth_debug` | `true`, `false` | `false`. Shows the depth map in place of the picture. |
| `[debug] eye_swap` | `true`, `false` | `false`. Swaps the left and right eye in 3D. For checking a wrong-way picture. |
| `[stream] bitrate_kbps` | 0, or 500 to 500000 | 0, which follows the resolution and refresh rate. A bitrate the flat window passes for a stream overrides it. The flat window's Custom bitrate goes up to 500 Mbps, and so does this. |
| `[stream] doff_disconnect` | 0 to 3600 (seconds) | 60. How long the headset can stay off before the stream is disconnected (the game keeps running on the PC). 0 never. |
| `[stream] idle_disconnect` | 0 to 1440 (minutes) | 0 (off). Disconnects the stream after this long with no input. |
| `[stream] virtual_surround` | `true`, `false` | `false`. Places 5.1 and 7.1 speakers around you in stereo. |
| `[debug] file_log` | `off`, `basic`, `verbose` | `basic` |

Close the app before you edit the file.

## Advanced: environment variables

These are for testing and debugging. Most users never need them. Put them in front of the command, or in the Steam shortcut's launch options as `VAR=value %command%`.

| Variable | Default | What it does |
| --- | --- | --- |
| `MLXR_NETWORK` | from the setting | `auto`, `home` (Wi-Fi), `adapter` or `usb` (USB cable). Overrides the Network connection setting. |
| `MLXR_VIDEO_PACING` | 2 | 2 low latency, 1 smooth, 0 always the newest frame. |
| `MLXR_VIDEO_PRIORITY` | `nice` | `off`, `nice` or `rt` for the video threads. |
| `MLXR_VIDEO_RFI` | 1 | 0 turns off reference frame invalidation (HEVC loss recovery). |
| `MLXR_VIDEO_RFI_IDR_MS` | 100 | How long a recovery may take before a full frame is asked for. |
| `MLXR_VIDEO_IDR_MIN_MS` | 100 | After a decoder error, a full frame is asked for at most this often (0: no limit). |
| `MLXR_PACER_CUSHION` | 3 | Decoded frames held for pacing (2 or 3). |
| `MLXR_VIDEO_LATCH_MS` | 4 | 0 to 8 ms the pacer waits for a frame in decode. |
| `MLXR_AUDIO_JITTER` | | `lo,hi` in ms: the audio buffer's range. |
| `MLXR_GAMEPADS` | 1 | 0 ignores Bluetooth gamepads. |
| `MLXR_GAMEPAD_IGNORE` | | Gamepad GUIDs to ignore. |
| `MLXR_MOUSE` | 1 | 0 ignores Bluetooth mice. |
| `MLXR_MOUSE_IGNORE` | | Skip mice whose name contains this text. |
| `MLXR_MOUSE_PARK` | 1 | 0 lets the controller ray move the cursor while a mouse is in use. |
| `MLXR_STICK_RIGHT_CLICK` | 1 | 0 makes the thumbstick click switch the pointer on and off, as in Moonlight XR. |
| `MLXR_DOUBLE_CLICK_PX` | 24 | After a click, the cursor waits while the ray stays this many pixels from it, and a second click lands on the first. 0 turns it off. |
| `MLXR_DOUBLE_CLICK_MS` | 500 | How long that wait can last, from the first press. Match it to the PC's double-click speed. 0 turns it off. |
| `MLXR_PRESS_STABILISE` | 1 | 0 sends a click where the ray is at the moment of the press. |
| `MLXR_CLICK_HOLD_PX` | 0 | Hold the cursor still from the trigger touch until the aim moves this many pixels. |

The four click variables above override the Click steadiness row for their own part when they are set.
| `MLXR_DEPTH_THREADS` | 2 | Threads for the depth model. Overrides `[debug] depth_threads`. |
| `MLXR_HOLE_FILL` | 1 | 0 fills the gaps beside edges in 3D by stretching the edge, as earlier versions did (the fuzzy outline in one eye). |
| `MLXR_FRAME_MODEL_GAIN` | 1 | Brightens the Steam Frame controller models (set at install). |
| `MLXR_CONFIG_DIR` | `~/.config/moonlight-xr` | Where settings and pairing live. |
| `MLXR_LOG_DIR` | `~/Games/MoonlightXRNative` | Where the logs go. |
| `MLXR_VRTIMING` | 1 | 0 stops the flat window from logging SteamVR's frame timing to `vrtiming.log`. |
| `MLXR_HANDOFF` | 1 | 0 makes the flat window stream flat, as stock Moonlight. |
| `MLXR_LAUNCHER_SCALE` | 1.5 | How much larger the flat window is drawn in Game Mode (1.0 to 2.5). A `QT_SCALE_FACTOR` you set yourself is used when this is not set. |
| `MLXR_LAUNCHER_FADE_WINDOW` | 1 | In Game Mode the flat window fades to opacity 0 during a stream and stays mapped, so gamescope keeps a window for keyboard focus. 0 hides the window instead, which can crash gamescope on a volume key press. |
