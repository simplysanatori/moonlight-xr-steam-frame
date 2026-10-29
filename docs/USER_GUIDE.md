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

In the headset (Game Mode) the window is drawn 1.5 times larger than on a desktop, so the text is easier to read. Set `MLXR_LAUNCHER_SCALE` to change it, for example `MLXR_LAUNCHER_SCALE=1.25`. It takes 1.0 (no scaling) to 2.5. On a desktop the window is not scaled.

The **Help** button (the question mark at the top) opens this project's readme: <https://github.com/simplysanatori/moonlight-xr-steam-frame#readme>.

If you used the Moonlight flatpak on the Frame before, its PC list and pairing are copied the first time the flat window starts. The flatpak's own files are not changed.

## Settings page

The page has the categories below, in this order. A greyed row shows "Not available:" and the reason. Settings are saved when you leave the page.

### Connection

| Setting | Options | Notes |
| --- | --- | --- |
| Network connection | **Automatic**, **Wi-Fi (*network name*, *band*)**, **Steam Frame Wireless Adapter (6 GHz)** | Automatic (the default) tries both links when a stream starts and uses the one with the lower latency and the fewer losses. It sends about 15 short probes down each link (about a second in all) and gives each link a score in milliseconds, lower is better: the median round trip, plus the jitter, plus how far the slowest probes run above the median, plus 50 ms for every probe that failed, plus a penalty from the last stream on that link. That penalty applies when the last stream lost more than 0.5 percent of its frames on the network: 20 ms for each percent, at most 150 ms. A link with no history gets no penalty. The lowest score wins. When two scores are within 3 ms of each other, the stronger signal and faster link rate break the tie, and Wi-Fi wins a tie after that. Signal and link rate no longer decide on their own. A link that does not answer is never used. The client keeps the last stream's numbers for each link in `netpath-history.ini` next to `config.ini` (frames received, frames lost, jitter, round trip and the time; no addresses or names). It writes them when a stream ends, after at least 500 frames. Delete the file to forget them. History older than 14 days is ignored. Wi-Fi streams over your home network. The adapter option streams over the Steam Frame Wireless Adapter plugged into your PC. Before it tries the adapter, the client checks that the adapter is there: the headset's own adapter network (`wlanap`) must be up and your PC must be connected to it. With no adapter found, Automatic uses Wi-Fi without trying the adapter, and if you chose the adapter the client uses Wi-Fi and shows a notice in the headset ("Steam Frame Wireless Adapter not reachable (no adapter found: ...). Using Wi-Fi."). If the chosen link does not answer, the client uses the other one and says so in the headset. |

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
| Enable HDR (Experimental) | greyed | The Frame's video path is 8-bit SDR. |

### Audio, gamepad, input, host, general, UI

| Setting | Options | Default |
| --- | --- | --- |
| Surround sound configuration | Stereo, 5.1 Surround Sound, 7.1 Surround Sound | Stereo |
| Adjust analog stick deadzone | -20 to 20 % | 0 %. Above 0 it is also the dead zone of the Frame controllers in gamepad mode, which use 7 % at 0. |
| Flip face buttons | on, off | off. Swaps A/B and X/Y. |
| Enable Rumble | on, off | on |
| Enable forward/back mouse buttons | on, off | on |
| Optimize game settings | on, off | on |
| Play audio on PC | on, off | off |
| Smart Clipboard Sync (Experimental) | on, off | off. Needs Apollo. |
| Quick Resume | on, off | off. Selecting a running app resumes it at once. |
| Enable Full Screen | on, off | Takes effect at the next start. |

### Performance Monitor and Advanced Settings

| Setting | Options | Default |
| --- | --- | --- |
| Show performance stats while streaming | on, off | off. Same as Stats in the headset's Display tab. |
| Change codec settings | Automatic (Recommended), Prefer HEVC, Prefer H.264 | Automatic. Prefer AV1 is greyed: the Frame decodes H.264 and HEVC only. |
| Unlock all possible frame rates | greyed | The stream is capped at the rate the headset runs at, 144 Hz at most. |
| Force full range video (Experimental) | on, off | off |
| Force software decoding | on, off | off |

The last line of the page says where the headset's own settings are saved (`~/.config/moonlight-xr/config.ini`).

## In the headset: the bar

The bar sits under the screen. From left to right:

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

**Reset** puts the screen back to the defaults.

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
| Head locked | Off, On | Off. The screen follows your head. |
| Head aim | Off, On | Off. Needs Head locked; not in a room. |
| Mode | Pointer, Gamepad | Pointer. Every session starts in pointer mode. The row also shows the switch shortcut. |
| Show controllers | Off, On | Off |
| Ray | Off, Dot, Ray | Ray. Dot shows only the dot where the ray lands. Off shows neither; the PC's own cursor is always in the video. Bar = this session, cog = saved: the choice here is kept for the next session, the bar's Ray button is not. |
| Pointer sleep | Off, On | On. The pointer pauses while the controller is still. |
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

The version, the log file, a link to support Moonlight XR on Ko-fi, and **Report a problem**. The report sheet saves your note on the Frame in `~/Games/MoonlightXRNative/reports`. Nothing leaves the headset. To report it, run `bug-report.sh` and open an issue on GitHub with the zip. The [README](../README.md#reporting-a-problem) lists what the zip holds and what is left out.

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
| Thumbstick up or down | Scroll, while the ray is on the picture |
| Grip | Grab the move pill, a corner of the screen, the keyboard's bar or the stats window |
| X, Y, d-pad up and left, bumpers, menu | Nothing in pointer mode |

A click is sent at the point you aimed at before the trigger pull moved the controller. After a click the cursor waits for half a second while you keep aiming near it, so the second click of a double click lands on the first one. Move the ray away and the cursor follows at once.

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

## Rooms and passthrough

The Environment button opens the Rooms picker:

| Choice | What you see |
| --- | --- |
| Passthrough | The Frame's cameras around the screen |
| Black void | Black (the default) |
| Home Theater | A home cinema room |
| Grand Cinema | A large cinema. The screen size is fixed. |
| Synthwave | A neon landscape |

The choice is remembered. Inside a room the bar's move pill does nothing, and the Screen tab becomes the Room tab. Controller models are hidden in passthrough.

## 3D

The headset turns the 2D stream into 3D with a depth model running on the Frame. It works with any game or video. It adds about 80 ms between the picture and its depth, so fast motion can show soft edges.

- Turn it on or off with the bar's 3D button, the 3D tab, or Render Mode on the flat Settings page.
- Comfort, Balanced and Strong set the depth. Depth and Convergence fine-tune it.
- 3D is on by default and remembered.
- The default model is ZipDepth. `depth_source` in `config.ini` chooses `zipdepth`, `zipdepth-int8` (lighter on the CPU) or `midas`.

## Keyboards, mice and gamepads

| Device | Notes |
| --- | --- |
| Headset keyboard | The bar's Keyboard button. **Compact** has letter, symbol and function sheets. **Full** is a tenkeyless layout: tap a modifier for one key press, hold it half a second to lock it. The key at the right switches between them. Hide, the bar button or a press elsewhere closes it. |
| Bluetooth keyboard | Pair it in SteamOS. Keys go to the PC while the stream is in front. The app takes the keyboard for itself during the stream. |
| Bluetooth mouse | Same as the keyboard. While you use the mouse, the controller ray does not move the cursor. |
| Bluetooth gamepad | Pair it in SteamOS. It shows up as a gamepad on the PC, next to the Frame controllers. The Steam Virtual Gamepad copy of the same pad is ignored. Start+Select+L1+R1 disconnects. |

### Controller models

Display tab, Show controllers. The installer converts SteamVR's own Steam Frame controller models on your Frame, so the models match the real controllers and move with your buttons and sticks. Valve's models are never in the download. If the conversion fails, a generic controller is shown.

## Ending a stream

| How | What happens on the PC |
| --- | --- |
| Bar, Exit, then Quit app | The app on the PC is quit. |
| Bar, Exit, then Disconnect | The stream ends. The game keeps running on the PC. Use **Resume Game** in the flat window to go back. |
| Start+Select+L1+R1 (gamepad mode, or a Bluetooth gamepad) | The stream ends. The game keeps running on the PC. Use **Resume Game** in the flat window to go back. |
| Ctrl+Alt+Shift+Q on a Bluetooth keyboard | Same as Start+Select+L1+R1. |
| The PC ends the stream, or the network drops | The flat window comes back. |

On the Frame controllers, Start is the right menu button, Select is the left View button, and L1 and R1 are the bumpers. Hold all four.

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
| The adapter is never used | `moonlight.log` has a line `network path: Steam Frame Wireless Adapter: ...`. `present (...)` means the PC is connected to the headset's adapter network. `no adapter found (wlanap up, no station)` means the headset's adapter network is on but no PC has joined it: plug the Steam Frame Wireless Adapter into the PC and wait for it to connect. `no adapter found (wlanap down)` or `(wlanap missing)` means the headset's adapter network is off. In a terminal, `mlxr-cli adapter` prints the same check and does not start a stream. |
| Cannot reach the PC | `moonlight.log` has `host ... not reachable at ...` for each address tried. Check the PC is on and Sunshine or Apollo is running. |
| The flat window does not come back after a stream | Open the app again from the Steam library. `moonlight-launcher.log` shows `Asked Steam to show the flat screen again` and the `flat window` lines (what the window did after the stream). |
| Grey icon in the SteamVR dock | Restart Steam once after the first install. |
| 3D tab greyed | The tab says why. If the depth model did not load, the log has the error. |
| The stream did not start | The flat window shows the client's error. `moonlight-console.log` has the details. |

Logs are in `~/Games/MoonlightXRNative`. They are not masked. To share them, use the zip from `bug-report.sh`. It holds the logs, your headset note, the settings you changed and a short version summary, all masked, and nothing else. It leaves out `hosts.ini`, keys and certificates. Look inside before you share it.

## Advanced: config.ini

`~/.config/moonlight-xr/config.ini` holds the headset settings. The cog menu writes most of them. A few are only in the file:

| Section, key | Values | Default |
| --- | --- | --- |
| `[vr] gamepad_toggle` | `menu_grip` (left View + left grip), `sticks` (click both sticks), `triggers_grips` (both triggers and both grips) | `menu_grip` |
| `[vr] rumble_mix` | `both` (each controller gets the stronger of the PC's two motors), `split` (low frequency motor on the left, high frequency motor on the right) | `both` |
| `[vr] rumble_intensity` | 0 to 100 (%), the scale on the PC's rumble on the controllers | 100 |
| `[vr] pad_grip` | `bumpers` (the grips are LB and RB), `none` (the grips do nothing in gamepad mode), `stick_clicks` (the grips are LS and RS) | `bumpers` |
| `[vr] keyboard_hold_ms` | 200 to 1500 | 500 |
| `[vr] depth_source` | `zipdepth`, `zipdepth-int8`, `midas` | `zipdepth` |
| `[vr] hand_tracking` | `true`, `false` | `false`. Untested on the Frame. |
| `[vr] gaze` | `true`, `false` | `false`. Untested on the Frame. |
| `[vr] pointer` | `true`, `false` | `true`. `false` turns the controllers' pointer off, so only the switch to gamepad mode is read. |
| `[vr] env_res` | `low`, `standard`, `high`, `ultra` | `standard`. How sharp the rooms are drawn: `low` is half the headset's recommended size, `ultra` the largest the runtime allows. Read when a room is built, so it applies from the next stream. |
| `[debug] depth_rate` | 5 to 45 | 20. The most depth maps made per second in 3D. Lower saves the CPU; the picture still follows the video. |
| `[debug] depth_debug` | `true`, `false` | `false`. Shows the depth map in place of the picture. |
| `[debug] eye_swap` | `true`, `false` | `false`. Swaps the left and right eye in 3D. For checking a wrong-way picture. |
| `[stream] bitrate_kbps` | 0, or 500 to 500000 | 0, which follows the resolution and refresh rate. A bitrate the flat window passes for a stream overrides it. The flat window's Custom bitrate goes up to 500 Mbps, and so does this. |
| `[stream] virtual_surround` | `true`, `false` | `false`. Places 5.1 and 7.1 speakers around you in stereo. |
| `[debug] file_log` | `off`, `basic`, `verbose` | `basic` |

Close the app before you edit the file.

## Advanced: environment variables

These are for testing and debugging. Most users never need them. Put them in front of the command, or in the Steam shortcut's launch options as `VAR=value %command%`.

| Variable | Default | What it does |
| --- | --- | --- |
| `MLXR_NETWORK` | from the setting | `auto`, `home` (Wi-Fi) or `adapter`. Overrides the Network connection setting. |
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
| `MLXR_DEPTH_THREADS` | 2 | Threads for the depth model. |
| `MLXR_FRAME_MODEL_GAIN` | 1 | Brightens the Steam Frame controller models (set at install). |
| `MLXR_CONFIG_DIR` | `~/.config/moonlight-xr` | Where settings and pairing live. |
| `MLXR_LOG_DIR` | `~/Games/MoonlightXRNative` | Where the logs go. |
| `MLXR_VRTIMING` | 1 | 0 stops the flat window from logging SteamVR's frame timing to `vrtiming.log`. |
| `MLXR_HANDOFF` | 1 | 0 makes the flat window stream flat, as stock Moonlight. |
| `MLXR_LAUNCHER_SCALE` | 1.5 | How much larger the flat window is drawn in Game Mode (1.0 to 2.5). A `QT_SCALE_FACTOR` you set yourself is used when this is not set. |
| `MLXR_LAUNCHER_FADE_WINDOW` | 1 | In Game Mode the flat window fades to opacity 0 during a stream and stays mapped, so gamescope keeps a window for keyboard focus. 0 hides the window instead, which can crash gamescope on a volume key press. |
