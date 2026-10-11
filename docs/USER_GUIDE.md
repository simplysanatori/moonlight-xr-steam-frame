# Moonlight XR for Frame: user guide

This guide covers version 0.6.0-alpha.2. For install, update and uninstall, see the [README](../README.md).

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

If you used the Moonlight flatpak on the Frame before, its PC list and pairing are copied the first time the flat window starts. The flatpak's own files are not changed.

## Settings page

The page has the categories below, in this order. A greyed row shows "Not available:" and the reason. Settings are saved when you leave the page.

### Connection

| Setting | Options | Notes |
| --- | --- | --- |
| Network connection | **Automatic**, **Wi-Fi (*network name*, *band*)**, **Steam Frame Wireless Adapter (6 GHz)** | Automatic (the default) tries both links when a stream starts and uses the faster one that answers. Wi-Fi streams over your home network. The adapter option streams over the Steam Frame Wireless Adapter plugged into your PC. If the chosen link does not answer, the client uses the other one and says so in the headset. |

### Video Settings

| Setting | Options | Default |
| --- | --- | --- |
| Video resolution | 360p, 480p, 720p, 1080p, 1440p, 4K, or a custom size | 1440p (2560x1440) |
| Refresh rate | **Match headset**, 90 Hz, 72 Hz, 60 Hz, 30 Hz, Custom | Match headset. It uses the rate SteamVR runs the display at. The stream never goes above 90 Hz. Custom takes values such as 59.94 and needs Apollo. |
| Video bitrate | 0.5 to 150 Mbps | 80 Mbps. See [Recommended PC settings](#recommended-pc-settings). |
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
| Adjust analog stick deadzone | -20 to 20 % | 0 % |
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
| Unlock all possible frame rates | greyed | The display runs at up to 90 Hz, so the stream is capped there. |
| Force full range video (Experimental) | on, off | off |
| Force software decoding | on, off | off |

The last line of the page says where the headset's own settings are saved (`~/.config/moonlight-xr/config.ini`).

## In the headset: the bar

The bar sits under the screen. From left to right:

| Button | What it does |
| --- | --- |
| Head aim | Only shown with Head locked on and outside a room. Your head moves the mouse. |
| Gamepad / Pointer | Switches the controllers between pointer mode and gamepad mode. |
| Exit | Asks "Exit the stream?". **Exit** quits the app on the PC and ends the stream. |
| Environment | Opens the Rooms picker. |
| Move pill | Grab it to move the screen. See [Moving and sizing the screen](#moving-and-sizing-the-screen). |
| Cog | Opens the cog menu. |
| Keyboard | Opens or closes the headset keyboard. |
| Ray | Shows or hides the controller ray for this session. |
| 3D | Only shown when the depth model is ready. Turns 3D on or off. |

## The cog menu

Tabs: **Screen** (called **Room** inside a room), **Display**, **3D**, **Picture**, **About**. A press outside the menu closes it.

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
| Controllers | Pointer, Gamepad | Pointer. Every session starts in pointer mode. The row also shows the switch shortcut. |
| Pointer sleep | Off, On | On. The pointer pauses while the controller is still. |
| Ray | Off, Dot, Ray | Ray. Dot shows only the dot where the ray lands. Off shows neither; the PC's own cursor is always in the video. |
| Controller model | Off, On | Off |
| Click sound | Off, On | On |
| Glow | Off, On | Off |
| Screen light | Off, On | On |
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

The version, the log file, a link to support Moonlight XR on Ko-fi, and **Report a problem**. The report sheet saves your note (and an email address if you give one) on the Frame in `~/Games/MoonlightXRNative/reports`. Nothing leaves the headset. Run `bug-report.sh` to bundle it with the logs. The [README](../README.md#reporting-a-problem) lists what the zip holds and what is left out.

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

A click is sent at the point you aimed at before the trigger pull moved the controller. The second click of a double click lands on the first one, so double clicks work.

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
| Left and right grips | LB, RB |

The PC's rumble drives the controllers in gamepad mode.

### Switching between the modes

- Hold the **left View button and the left grip** together for half a second.
- Or select the Gamepad/Pointer button on the bar, or the Controllers row in the Display tab.

The headset shows "Gamepad mode" or "Pointer mode". In gamepad mode the menus close. The shortcut can be changed in `config.ini` (`gamepad_toggle`, see [below](#advanced-configini)).

## Moving and sizing the screen

| What | How |
| --- | --- |
| Move the screen | Grip the move pill under the screen and move your hand. |
| Push it away or pull it closer | While holding the pill, push the thumbstick up (farther) or down (closer). 0.3 to 8 m. |
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

Display tab, Controller model. The installer converts SteamVR's own Steam Frame controller models on your Frame, so the models match the real controllers and move with your buttons and sticks. Valve's models are never in the download. If the conversion fails, a generic controller is shown.

## Ending a stream

| How | What happens on the PC |
| --- | --- |
| Bar, Exit, then Exit | The app on the PC is quit. |
| Start+Select+L1+R1 (gamepad mode, or a Bluetooth gamepad) | The stream ends. The game keeps running on the PC. Use **Resume Game** in the flat window to go back. |
| Ctrl+Alt+Shift+Q on a Bluetooth keyboard | Same as Start+Select+L1+R1. |
| The PC ends the stream, or the network drops | The flat window comes back. |

On the Frame controllers, Start is the right menu button, Select is the left View button, and L1 and R1 are the bumpers. Hold all four.

## Recommended PC settings

| Setting | Recommendation |
| --- | --- |
| Bitrate | 40 to 60 Mbps over the Steam Frame Wireless Adapter. Higher rates lost packets on that link in testing. On a strong Wi-Fi connection 80 Mbps at 1440p worked. |
| Codec | Automatic or HEVC. HEVC recovers from lost packets without waiting for a full new frame. |
| Display refresh rate on the PC | 90 Hz or higher. A 60 Hz desktop gives a 60 fps stream. |
| Apollo | Use Virtual Display with the refresh rate at 90 Hz. The virtual display then matches the stream. |
| Sunshine or Apollo encoder | The fastest preset of your encoder (for NVENC, the lowest P number). |
| FEC | Leave Sunshine's FEC Percentage at its default (20) or raise it a little on a weak link. |

## Troubleshooting

| Problem | What to check |
| --- | --- |
| Judder or stutter | Turn on Stats (Display tab). `Display 90 Hz, 90 fps` means the headset draws every frame. If `Video ... fps` is low, the PC is sending fewer frames: check the PC's display rate. If `idr requests` keeps rising, packets are being lost: lower the bitrate or try the other link. `vrtiming.log` shows SteamVR's dropped and reprojected frames. |
| Crackling sound | The stats line `audio ... underruns, ... lost` counts gaps. Lost packets are the network. On the adapter link, lower the bitrate. |
| Which link is in use | `moonlight.log` has `network path: using Wi-Fi (...)` or the adapter, when the stream starts. The headset shows a notice when it falls back to the other link. |
| Cannot reach the PC | `moonlight.log` has `host ... not reachable at ...` for each address tried. Check the PC is on and Sunshine or Apollo is running. |
| The flat window does not come back after a stream | Open the app again from the Steam library. `moonlight-launcher.log` shows `Asked Steam to show the flat screen again`. |
| Grey icon in the SteamVR dock | Restart Steam once after the first install. |
| 3D tab greyed | The tab says why. If the depth model did not load, the log has the error. |
| The stream did not start | The flat window shows the client's error. `moonlight-console.log` has the details. |

Logs are in `~/Games/MoonlightXRNative`. They are not masked. To share them, use the zip from `bug-report.sh`. It holds the logs, your headset note, the settings you changed and a short version summary, all masked, and nothing else. It leaves out `hosts.ini`, keys and certificates. Look inside before you share it.

## Advanced: config.ini

`~/.config/moonlight-xr/config.ini` holds the headset settings. The cog menu writes most of them. A few are only in the file:

| Section, key | Values | Default |
| --- | --- | --- |
| `[vr] gamepad_toggle` | `menu_grip` (left View + left grip), `sticks` (click both sticks), `triggers_grips` (both triggers and both grips) | `menu_grip` |
| `[vr] keyboard_hold_ms` | 200 to 1500 | 500 |
| `[vr] depth_source` | `zipdepth`, `zipdepth-int8`, `midas` | `zipdepth` |
| `[vr] hand_tracking` | `true`, `false` | `false`. Untested on the Frame. |
| `[vr] gaze` | `true`, `false` | `false`. Untested on the Frame. |
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
| `MLXR_PACER_CUSHION` | 3 | Decoded frames held for pacing (2 or 3). |
| `MLXR_VIDEO_LATCH_MS` | 4 | 0 to 8 ms the pacer waits for a frame in decode. |
| `MLXR_AUDIO_JITTER` | | `lo,hi` in ms: the audio buffer's range. |
| `MLXR_GAMEPADS` | 1 | 0 ignores Bluetooth gamepads. |
| `MLXR_GAMEPAD_IGNORE` | | Gamepad GUIDs to ignore. |
| `MLXR_MOUSE` | 1 | 0 ignores Bluetooth mice. |
| `MLXR_MOUSE_IGNORE` | | Skip mice whose name contains this text. |
| `MLXR_MOUSE_PARK` | 1 | 0 lets the controller ray move the cursor while a mouse is in use. |
| `MLXR_STICK_RIGHT_CLICK` | 1 | 0 makes the thumbstick click switch the pointer on and off, as in Moonlight XR. |
| `MLXR_DOUBLE_CLICK_PX` | 5 | Pixels within which a second click lands on the first. |
| `MLXR_PRESS_STABILISE` | 1 | 0 sends a click where the ray is at the moment of the press. |
| `MLXR_CLICK_HOLD_PX` | 0 | Hold the cursor still from the trigger touch until the aim moves this many pixels. |
| `MLXR_DEPTH_THREADS` | 2 | Threads for the depth model. |
| `MLXR_FRAME_MODEL_GAIN` | 1 | Brightens the Steam Frame controller models (set at install). |
| `MLXR_CONFIG_DIR` | `~/.config/moonlight-xr` | Where settings and pairing live. |
| `MLXR_LOG_DIR` | `~/Games/MoonlightXRNative` | Where the logs go. |
| `MLXR_VRTIMING` | 1 | 0 stops the flat window from logging SteamVR's frame timing to `vrtiming.log`. |
| `MLXR_HANDOFF` | 1 | 0 makes the flat window stream flat, as stock Moonlight. |
