# Moonlight XR for Frame: user guide

This guide covers version 1.1.0. For install, update and uninstall, see the [README](../README.md).

## Contents

- [How it fits together](#how-it-fits-together)
- [The flat window](#the-flat-window)
- [Settings page](#settings-page)
- [In the headset: the bar](#in-the-headset-the-bar)
- [The cog menu](#the-cog-menu)
- [Accessibility](#accessibility)
- [Controllers](#controllers)
- [Moving and sizing the screen](#moving-and-sizing-the-screen)
- [Zoom and pan](#zoom-and-pan)
- [Rooms and passthrough](#rooms-and-passthrough)
- [3D](#3d)
- [SBS 3D](#sbs-3d)
- [Keyboards, mice and gamepads](#keyboards-mice-and-gamepads)
- [Host: keys for the PC](#host-keys-for-the-pc)
- [Language](#language)
- [Ending a stream](#ending-a-stream)
- [Apollo on the PC: recipes](#apollo-on-the-pc-recipes)
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
| App list | The apps your PC offers. Select one to start it. A running app has **Resume Game** and **Quit Game** in its menu. Every app's menu has **Stream settings profile...**, which opens the [Stream profiles](#stream-profiles) page for that app. |
| Settings | The cog at the top. See [Settings page](#settings-page). |

While a stream starts, the window shows a tip: "Tip: Press Start+Select+L1+R1 to disconnect your session". With no gamepad connected it names Ctrl+Alt+Shift+Q instead.

In the headset, the Moonlight XR logo with "Connecting to *PC name*..." under it hangs in front of you from the moment a stream is the picture's source until the first frame arrives, then fades out over about a third of a second. It shows again, as "Reconnecting...", while the headset reconnects after a dropped link (see When the connection drops). It has no setting and is not shown once a picture is there.

In the headset (Game Mode) the window is drawn 1.5 times larger than on a desktop, so the text is easier to read. Set `MLXR_LAUNCHER_SCALE` to change it, for example `MLXR_LAUNCHER_SCALE=1.25`. It takes 1.0 (no scaling) to 2.5. On a desktop the window is not scaled.

The **Help** button (the question mark at the top) opens this project's readme: <https://github.com/simplysanatori/moonlight-xr-steam-frame#readme>. In Game Mode it opens in Steam's browser (`steam steam://openurl/...` on the Frame), which SteamVR shows in the dashboard; on a desktop it is your normal browser.

If you used the Moonlight flatpak on the Frame before, its PC list and pairing are copied the first time the flat window starts. The flatpak's own files are not changed.

### Updates

Once a day at most, the flat window asks GitHub for this project's latest release (`api.github.com`, nothing else). When it is newer than the installed version, an update button (the round arrow icon) appears at the top, with "Update available for Moonlight XR for Frame: Version N" for a few seconds; it opens the release page, in Steam's browser in Game Mode. Nothing is downloaded or installed: update as under "Update" in the readme. No information about you, the headset or your PCs is sent, and if GitHub cannot be reached nothing is shown (`moonlight-launcher.log` has one `MLXR_LAUNCHER_UPDATE: check failed` line). Between checks the last answer is used. Turn it off with **Check for updates** (General settings).

### Opening a stream by itself

**On start** (General settings) makes the flat window start a stream as soon as it opens and the PC answers, as if you had selected the app: **Last PC's Desktop** (the Desktop app of the PC you streamed from last) or **An app** (choose it in **App to open on start** from the apps of your PCs). The default is **Nothing**. It waits up to 2 minutes for the PC (and wakes it, as Moonlight's own command line does), then gives up quietly. If another app is already running on that PC, it does not start, so nothing of yours is quit. When the stream ends you are back in the PC list. `moonlight-launcher.log` has `MLXR_LAUNCHER_ONSTART:` lines for each step.

The installer can add a second Steam shortcut, **Desktop in VR**, that goes straight to the Desktop: `./install.sh --desktop-shortcut` (see the readme). It runs `moonlight-xr-launcher.sh stream "PC" "Desktop"`, Moonlight's own command line. The flat window shows "Establishing connection to PC..." and closes when the stream ends. The PC is the one you streamed from last, or the only one, or `--desktop-pc "PC name"`. Run the installer again with another `--desktop-pc` to change it, or edit the shortcut's launch options in Steam.

## Settings page

The page has the categories below, in this order. A greyed row shows "Not available:" and the reason. Settings are saved when you leave the page.

### Search and jump list

The top of the page has a **Search settings** field and a row of buttons, one for each category.

- Type in the field and the page shows only the settings whose name, value or description has every word you typed, and the categories they are in. A category's name finds all its settings (for example `audio`). Case does not matter. The cross next to the field, or Esc, clears it and the whole page is back. "No setting matches" means nothing has all the words.
- A category button scrolls the page to that category and puts the focus on its first setting, so a gamepad carries on from there. A button disappears while the search leaves its category empty.
- Each category heading has an arrow at its right that goes back to the top of the page and the search field.

### Connection

| Setting | Options | Notes |
| --- | --- | --- |
| Network connection | **Automatic**, **Wi-Fi (*network name*, *band*)**, **Steam Frame Wireless Adapter (6 GHz)**, **USB cable** | Automatic (the default) tries every link that is there when a stream starts (Wi-Fi, the adapter and a USB cable, see [USB cable](#usb-cable)) and uses the one with the lower latency and the fewer losses. It sends about 15 short probes down each link (about a second in all) and gives each link a score in milliseconds, lower is better: the median round trip, plus the jitter, plus how far the slowest probes run above the median, plus 50 ms for every probe that failed, plus a penalty from the last stream on that link. That penalty applies when the last stream lost more than 0.5 percent of its frames on the network: 20 ms for each percent, at most 150 ms. A link with no history gets no penalty. The lowest score wins. When two scores are within 3 ms of each other, the stronger signal and faster link rate break the tie, and Wi-Fi wins a tie after that. Signal and link rate no longer decide on their own. A link that does not answer is never used. The client keeps the last stream's numbers for each link in `netpath-history.ini` next to `config.ini` (frames received, frames lost, jitter, round trip and the time; no addresses or names). It writes them when a stream ends, after at least 500 frames. Delete the file to forget them. History older than 14 days is ignored. Wi-Fi streams over your home network. The adapter option streams over the Steam Frame Wireless Adapter plugged into your PC. The USB cable option streams over a USB data cable between the headset and your PC. Before it tries the adapter, the client checks that the adapter is there: the headset's own adapter network (`wlanap`) must be up and your PC must be connected to it. With no adapter found, Automatic uses Wi-Fi without trying the adapter, and if you chose the adapter the client uses Wi-Fi and shows a notice in the headset ("Steam Frame Wireless Adapter not reachable (no adapter found: ...). Using Wi-Fi."). If the chosen link does not answer, the client uses the other one and says so in the headset. The Steam Frame Wireless Adapter can have worse latency than a good 5 GHz network. Automatic measures latency and drops on each link before it chooses, so do not assume the adapter wins. |
| Video bitrate | 0.5 to 500 Mbps | The stream's bitrate, and what **Same as above** in the four rows below it means. The default is twice Moonlight's own default for the size and rate you pick under Video settings: 98 Mbps at 1440p and 90 Hz, 113 Mbps at 1440p and 120 Hz, 124 Mbps at 1440p and 144 Hz, 49 Mbps at 1080p and 90 Hz, 80 Mbps at 1440p and 60 Hz. With Match headset it is the 90 Hz figure. It is set again to this value whenever you change the resolution or the refresh rate, and the default itself tops out at 150 Mbps (4K). The slider goes up to 500 Mbps, for a good 5 GHz Wi-Fi or a wired link. A value you saved earlier, up to 500 Mbps, is kept until then. See [Recommended PC settings](#recommended-pc-settings). |
| Bitrate over Wi-Fi | **Same as above**, then 10 to 500 Mbps in steps | The bitrate a stream starts with when it goes over Wi-Fi. **Same as above** uses the Video bitrate. Tap the row to step up; past 500 Mbps it goes back to Same as above. See [Bitrate for each connection](#bitrate-for-each-connection). |
| Bitrate over the Steam Frame Wireless Adapter | the same | The same for the Steam Frame Wireless Adapter. |
| Bitrate over a wired network | the same | The same for a wired network (the headset on ethernet). |
| Bitrate over the USB cable | the same | The same for the [USB cable](#usb-cable). |
| Full-frame requests after lost video | **No limit**, **250 ms**, **500 ms**, **1 s**, **2 s** | 500 ms (the default). When video is lost and the PC's quick repair does not come, the headset asks the PC for a full picture (an IDR frame). A full picture is large: asked for again while the Wi-Fi is still losing packets, it lands in the same bad moment and makes the freeze longer. This is the least time between two such requests of the headset's own. Pressing the row steps through the choices. It is `rfi_idr_ms` in `config.ini`'s `[stream]` section. |
| Switch network when video keeps breaking up | **Off**, **On** | Off (the default). With **On** and Network connection on **Automatic**, a stream that keeps losing video on its link moves to another link. See [Switching links when video keeps breaking up](#switching-links-when-video-keeps-breaking-up). It is `failover` in `config.ini`'s `[stream]` section. |
| Video loss before switching | **1.5 s**, **3 s**, **5 s**, **8 s** | 3 s (the default). How much lost video within ten seconds makes the stream leave its link. Tapping the row steps through the choices. Greyed while the switch above is off. It is `failover_burst_ms` in `config.ini` (500 to 9000 ms). |

### Switching links when video keeps breaking up

The client picks one link when a stream starts, and the PC sends the video to that one address, so it cannot use two links at once. A link that measured well at the start can still go bad later, for example the 6 GHz link in one corner of the room. With **Switch network when video keeps breaking up** on, the client watches for this. When the video has lost 3 seconds (or what you set) within the last 10 seconds, it looks at what it measured on the other links when the stream started. It moves the stream only if all of this holds:

- Network connection is **Automatic**. A link you chose yourself is never left.
- Another link was there at the start and answered the probe: the Steam Frame Wireless Adapter with a station connected, Wi-Fi, or a USB cable that was plugged in. A link that was not there is never used.
- That link scored clearly better than the current link, at least 15 ms lower, where the current link's score includes what its recent losses cost (20 ms for each percent of frames lost, at most 150 ms). So a link with a worse round trip, such as the adapter against a good 5 GHz link, wins only because the current link is really losing frames; it does not win on its name.
- The stream has run for at least 20 seconds and the last switch was at least 5 minutes ago, so it cannot flap between two bad links.

Then the client writes `network path: switching from Wi-Fi to Steam Frame Wireless Adapter after 3450 ms of loss` to `moonlight.log`, shows a notice with the new link's name (the Notices setting decides whether you see it), and reconnects like after a dropped connection: the room, the screen's place and the 3D state stay, the PC resumes the same app, and the picture is back in a few seconds. The loss burst it left is written to the log with `network path switched from X to Y` at its end. The link it left is tried again only at the next stream start, when Automatic measures every link again. If the new link does not answer, the client falls back to whatever answers, and no new switch is tried for 5 minutes. It is off by default because it has not been measured on the Frame yet.

### Bitrate for each connection

A stream's bitrate cannot be changed once it is running (Apollo and Sunshine take it only when the stream starts), so the one moment to choose it is the start. The client already knows which link a stream will use at that moment, after Automatic has decided, so each link can have its own starting bitrate: a lower one for a Wi-Fi that drops frames above 60 Mbps, a higher one for the adapter or the cable.

- Set it in Settings, Connection, under the Video bitrate slider: one row each for Wi-Fi, the Steam Frame Wireless Adapter, a wired network and the USB cable. **Same as above** (the default) means that link uses the Video bitrate just above, or the bitrate the flat window passes.
- A link's own bitrate replaces that bitrate only when a stream goes over that link. When the client did not choose the link (a stream started by a direct address, or a probe that found nothing), no link is known and the Video bitrate is used.
- The values are held to 0.5 to 500 Mbps. In `config.ini` they are `bitrate_wifi`, `bitrate_adapter`, `bitrate_wired` and `bitrate_usb` in the `[stream]` section, in kbps; 0 is Same as above.
- The log has one line at the start of every stream, for example `bitrate for Wi-Fi: 60000 kbps (override)` or `bitrate for USB cable: 98000 kbps (launcher)`. The Stats overlay in the HUD style shows the rate it started with beside the rate arriving ("54 of 98 Mb/s"), and the full style's stream line says `bitrate set` and the number in kbps.
- A link that is rated higher than the connection can carry will drop frames; the slow connection badge and the loss figure in the Stats overlay show it. Lower that link's row and try again.

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

### Stream profiles

A profile is a saved copy of the stream settings on this page, with a name. It holds the Connection rows (with the bitrate for each connection), every Video setting (with the 3D rows, the eye balance and where the 3D comes from), the sound, the gamepad and mouse rows (with the lightgun and the gamepad gyro), Optimize game settings, Play audio on PC, Smart clipboard sync, stats, the codec, full range, software decoding, and the headset's own rows on this page (notices, scroll speed, the disconnect times, gamepad as mouse). For example a desktop profile at 1080p and 60 Hz, and a game profile at 1440p and 120 Hz with Apollo's virtual display. Not in a profile: the room's look (background dim, void colour, glow edge), the headset keyboard layout, Show controllers, Quick resume, Enable full screen, Language and Check the PC before streaming.

The **Profiles** row (or **Stream settings profile...** in an app's menu) opens the Stream profiles page:

- **Save the settings now in force as a profile** asks for a name and saves the page's settings as they are. Set the Settings page the way you want the profile first. "Default" cannot be a name: it means the Settings page itself.
- Select a profile for **Use for** (the PC or app chosen below), **Rename...**, **Duplicate**, **Update from the settings in force now**, **Load into the Settings page** (its values become the page's own, saved), or **Delete...**.
- **PC**, **App** and **Profile for ...** choose which profile a PC uses for every app, or one app uses. **Default (the Settings page's settings)** takes it away. An app's own profile wins over its PC's. The **In use** list shows every PC and app that has one; select a line to change it. Opened from an app's menu, the page starts on that PC and app.

When a stream starts, the profile of the app, or else of its PC, is in force for that stream: the flat window passes its values to the VR client, and writes the headset's settings it holds into `config.ini` for the stream. When the stream ends, the Settings page's own values are back and `config.ini` is as it was; a headset setting you changed in the cog during the stream keeps your change. If the flat window is closed or killed during a stream, `config.ini` is put back the next time it starts. `moonlight-launcher.log` has `MLXR_LAUNCHER_PROFILE: using <name> for <PC> / <app>` for each stream. A PC and an app are matched by name, case ignored, as the headset matches its per-app bindings. A stream started from the command line (`moonlight-xr-launcher stream PC App`) uses the profile too, over the command line's own options.

### Video settings

| Setting | Options | Default |
| --- | --- | --- |
| Video resolution | 360p, 480p, 720p, 1080p, 1440p, 4K, Custom... | 1440p (2560x1440). **Custom...** asks for any width x height, such as 3840x1080 for full-size SBS. |
| Refresh rate | **Match headset**, 144 Hz, 120 Hz, 90 Hz, 72 Hz, 60 Hz, 30 Hz, Custom... | Match headset. It uses the rate SteamVR runs the headset's display at for this app: 90 Hz unless you changed it in SteamVR's settings. Pick 72, 90, 120 or 144 Hz here to choose the headset's rate: when the stream starts, the launcher sets that rate in SteamVR's per-app video settings for this app, and the stream runs at it. 120 and 144 Hz need the stream and your PC to keep up: the PC has to render and encode that many frames a second, and the network has to carry the higher bitrate. The stream never goes above the rate the headset runs at, 144 Hz at most. If SteamVR still runs the app at its old rate, the log says so and the stream uses that rate; start the app again. Custom takes values such as 59.94 and needs Apollo. |
| Video frame pacing | Prefer lowest latency, Balanced, Prefer smoothest video | Balanced. Lowest latency shows the newest frame at once; smoothest shows every frame in turn, a frame or two later. |
| Ultra low latency (experimental) | on, off | off. Runs the network and decoder threads at realtime priority. |
| Use virtual display | on, off | off. Needs Apollo. Apollo makes a display that matches the stream. |
| Resolution scale factor | 20 to 200 % | 100 %. Needs Apollo. The virtual display is made this much of the stream's size. |
| Custom bitrate | 0.5 to 500 Mbps | The Video bitrate (Connection), typed in. |
| 3D | on, off | on. This is the 3D switch; the bar's 3D button and the headset's 3D tab change it too. See [3D](#3d). |
| 3D source | Depth model, SBS from the PC, Top-bottom from the PC | Depth model. Press to step through them. See [SBS 3D](#sbs-3d). The headset's 3D tab has the same row. |
| Full-size SBS | on, off | off. On: the PC's pair has a full picture per eye (3840x1080 for two 1920x1080 eyes). Off: half-size, both eyes squeezed into one 1920x1080 frame. Greyed with the depth model. |
| Depth model | ZipDepth, ZipDepth lighter on the CPU, MiDaS (older), Off (2D only) | ZipDepth. Greyed with SBS or Top-bottom. In `config.ini`: `[vr] depth_source`. |
| 3D depth | 1 to 100 | The depth model's own value. The headset's 3D tab calls it Depth. Greyed with the 3D off, and with SBS or Top-bottom as the 3D source. |
| 3D convergence | 0 to 100 | 50. Greyed with the 3D off. With SBS or Top-bottom it moves the two halves apart or together. |
| 3D edge | 0, 25, 50, 75, 100 (press to step) | 0. See [3D](#3d). Greyed with the 3D off. |
| 3D motion reset | Off, 5, 10, 20, 30 % (press to step) | Off. See [3D](#3d). Greyed with the 3D off. |
| 3D depth detail | 50 to 200 % (press to step) | 100 %. See [3D](#3d). Greyed with the 3D off. |
| 3D depth shape | 50 to 200 % (press to step) | 100 %. See [3D](#3d). Greyed with the 3D off. |
| 3D eye balance | Right eye 100 %, 50 %, Even, Left eye 50 %, 100 % (press to step) | Even. See `[3d] eye_balance` under [Advanced: config.ini](#advanced-configini). Greyed with the 3D off. |
| Sharpen | Off, Normal, Quality | Normal. In `config.ini`: `[display] sharpening`. |
| Supersample | Off, Normal, Quality | Normal. Not used while the 3D is on. In `config.ini`: `[display] supersampling`. |
| Sharper small text, Sharper coloured text | on, off | on, on. On a curved or 3D screen. In `config.ini`: `[display] picture_filter`, `chroma_from_luma`. |
| Picture brightness, contrast, gamma, saturation | -50 to 50; 50 to 150 %; 0.50 to 2.00; 0 to 200 % | 0, 100 %, 1.00, 100 %. In `config.ini`: `[vr] picture_brightness`, `picture_contrast`, `picture_gamma` (hundredths), `picture_saturation`. |

The Frame's video is 8-bit SDR and the stream never goes above the headset's rate, so the page has no HDR switch and no "Unlock all possible frame rates" row.

### Audio, gamepad, input, host, general, UI

| Setting | Options | Default |
| --- | --- | --- |
| Surround sound configuration | Stereo, 5.1 surround sound, 7.1 surround sound | Stereo. 5.1 and 7.1 are mixed down to stereo unless Virtual surround places them around you. |
| Adjust analog stick dead zone | -20 to 20 % | 0 %. Above 0 it is also the dead zone of the Frame controllers in gamepad mode, which use 7 % at 0. |
| Flip face buttons | on, off | off. Swaps A/B and X/Y. |
| Back button as Guide (Accessibility) | on, off | off. In the headset's gamepad mode the View (Back) button is Guide, see [Gamepad mode](#gamepad-mode). |
| Mouse mode (Accessibility) | Absolute, Relative | Absolute. See [Relative mouse](#relative-mouse). |
| What points, Dwell click (Accessibility) | Controller, Head, Eyes; Off, 0.6, 0.9, 1.2, 1.5 s | Controller, Off. See [Accessibility](#accessibility). |
| Headset keyboard layout | QWERTY (US), AZERTY (French), QWERTZ (German) | QWERTY. The PC must be on the same Windows layout, see [AZERTY and QWERTZ](#azerty-and-qwertz). |
| Enable rumble | on, off | on. Gamepads and the headset controllers. |
| Enable forward/back mouse buttons | on, off | on |
| Check the PC before streaming (Host) | on, off | on. See [Check before a stream](#check-before-a-stream). |
| Optimize game settings (Host) | on, off | on. Lets the PC switch its display to the stream's resolution, where the host is set to. |
| Play audio on PC (Host) | on, off | off |
| Smart clipboard sync (experimental) (General) | on, off | off. Needs Apollo. |
| On start (General) | Nothing, Last PC's Desktop, An app | Nothing. See [Opening a stream by itself](#opening-a-stream-by-itself). |
| App to open on start (General) | the apps of your PCs | Shown with On start set to An app. |
| Check for updates (General) | on, off | on. See [Updates](#updates). |
| Controls card on the next stream (General) | on, off | off. Brings back the [first-run card](#the-first-run-card). |
| Disconnect when the headset is off (General) | Never, 15 s, 30 s, 1 min, 2 min, 5 min (press to step) | 1 min. See [Ending a stream](#ending-a-stream). |
| Disconnect when idle (General) | Never, 10 min, 30 min, 60 min, 120 min (press to step) | Never. |
| Quick resume (UI) | on, off | off. Selecting a running app resumes it at once. |
| Enable full screen (UI) | on, off | Takes effect at the next start. |
| Language (Misc) | Automatic and 28 languages | Automatic. The flat window's own language. |
| Headset language (Misc) | Automatic, English and 19 more | Automatic. The headset's own menus, see [Language](#language). |

### Headset settings on this page

These rows change the headset's own settings in `config.ini`, the same ones its cog menu and the file have. Only a row you change is written; a change takes effect at the next stream (the depth model and Room detail when the model or the room is loaded). Which of them a stream profile holds is listed under [Stream profiles](#stream-profiles). Where the cog has the same setting, the row has the cog's name. The 3D and picture rows of the Video settings are in the table above.

| Row (category) | Options | Default | `config.ini` |
| --- | --- | --- | --- |
| Virtual surround (Audio) | on, off | off. Works with Stereo as well as 5.1 and 7.1: stereo becomes two speakers in front of the screen, so turning your head moves the sound. | `[stream] virtual_surround` |
| Lightgun mode, Lightgun reload, Lightgun recoil (Gamepad) | Automatic, On, Off; on, off | Automatic, off, on. Reload and recoil are greyed with Lightgun mode Off. | `[vr] lightgun`, `lightgun_reload`, `lightgun_recoil` |
| Gyro from the headset, When the gyro is sent (Gamepad) | Off, Controller, Head; Always, While the thumb touches the right stick | Off, Always. The second is greyed with the gyro Off. | `[vr] pad_gyro`, `pad_gyro_ratchet` |
| Gamepad mode shortcut (Gamepad) | Left View and left grip, Click both thumbsticks, Both triggers and both grips | Left View and left grip | `[vr] gamepad_toggle` |
| Grips in gamepad mode (Gamepad) | Bumpers (LB, RB), Nothing, Stick clicks (LS, RS) | Bumpers | `[vr] pad_grip` |
| Rumble on the headset controllers (Gamepad) | Stronger motor in both hands, Low motor left, high motor right | Stronger motor in both hands. Greyed with Enable rumble off. | `[vr] rumble_mix` |
| Rumble strength on the headset controllers (Gamepad) | 0 to 100 % | 100 %. Greyed with Enable rumble off. | `[vr] rumble_intensity` |
| Headset keyboard size (Input) | Compact, Full (tenkeyless) | Compact | `[vr] keyboard_layout` |
| Hold to lock Shift, Ctrl, Alt or Win (Input) | 200 to 1500 ms | 500 ms, on both keyboards | `[vr] keyboard_hold_ms` |
| Controllers as a mouse (Input) | on, off | on | `[vr] pointer` |
| Ray (Input) | Off, Dot, Ray | Ray. Dot shows only where the ray lands; the cog's Ray row. | `[vr] show_ray`, `pointer_dot` |
| Pointer sleep (Input) | on, off | on. The pointer rests while the controller is still. | `[vr] pointer_sleep` |
| Click steadiness (Input) | Off, Normal, Steady | Normal | `[vr] click_steadiness` |
| Click sound (Input) | on, off | on | `[vr] click_sound` |
| Scroll speed (Input) | 50 to 300 % (press to step) | 100 %. The thumbstick's scroll in pointer mode. | `[vr] scroll_speed` |
| Show controllers (Input) | Off, Frame, Gun, Your own model (press to step) | Off. See [Controllers](#controllers). | `[vr] controller_model` |
| Head aim, Aim sensitivity, Aim dead zone (Input) | on, off; 1 to 30 px/°; 0 to 20 °/s | off, 8, 2 | `[vr] head_aim`, `head_aim_sensitivity`, `head_aim_deadzone` |
| Look to point, Hand tracking (Input) | on, off | off, off. Look to point needs eye tracking from SteamVR; the Accessibility tab shows whether it reaches the app. | `[vr] gaze`, `hand_tracking` |
| Head tracking to opentrack, opentrack address, opentrack port (Input) | on, off; address or name; 1 to 65535 | off, the streaming PC, 4242. See [opentrack](#head-tracking-for-pc-games-opentrack). | `[vr] opentrack`, `opentrack_host`, `opentrack_port` |
| Environment (UI) | Automatic, Passthrough, Black void, Home Theater, Grand Cinema, Synthwave | Automatic: the black void, or the passthrough with the Passthrough switch on | `[vr] environment` |
| Passthrough (UI) | on, off | off. Only Environment's Automatic reads it, so it is greyed with any other environment. | `[vr] passthrough` |
| Void colour, Background dim (UI) | #rrggbb; 0 to 100 % (press to step) | #000000, 0 %. The cog's Picture tab has Background. | `[vr] void_color`, `background_dim` |
| Room detail (UI) | Low, Standard, High, Ultra | Standard | `[vr] env_res` |
| Screen light, Glow, Glow level, Glow edge (UI) | on, off; 0 to 100 %; Off, Soft, Wide | on, off, 50 %, Off. Glow level and Glow edge are greyed with the glow off. | `[display] screen_light`, `glow`, `glow_level`, `glow_feather` |
| Screen distance, Screen size, Screen curve (UI) | 20 to 800 cm; 80 to 800 cm wide; 0 to 100 % | 300 cm, 300 cm, 0 % | `[screen] distance_cm`, `width_cm`, `curve` |
| Zoom (UI) | 100 to 400 % (press to step) | 100 %. The zoom a stream starts at. | `[display] zoom` |
| Remember per app (UI) | on, off | on. An app without its own place starts from the rows above. | `[screen] per_app` |
| Angular-size lock (UI) | on, off | on. The screen looks the same size when you push it away or pull it closer. | `[screen] angular_lock` |
| Head locked, Follow after turning (UI) | Off, On, Follow; 10 to 60° | Off, 25°. The cog's Head locked row; Follow after turning is greyed unless it is Follow. | `[display] head_locked`, `head_follow`, `follow_leash_deg` |
| Notices (UI) | All, Important, Off (press to step) | All | `[vr] notices` |

Left out on purpose: the `[debug]` keys, what the headset keeps by itself (the last PC and app, the cog's last tab, the zoom's centre, the hand hint), the screen's height, direction, tilt and rotation (set by grabbing the screen) and the place the bar's Bring here switch goes back to and each room's own brightness, glow, light and picture size (the cog's Room tab, while you look at the room).

### Language

The headset's menus, keyboard and notices can be in English or in one of 19 other languages:
Czech, Danish, Dutch, Finnish, French, German, Italian, Japanese, Korean, Norwegian Bokmål, Polish,
Portuguese (Brazil), Russian, Simplified Chinese, Spanish, Swedish, Traditional Chinese, Turkish and
Ukrainian. Choose it with **Headset language** on the flat window's Settings page (at the end, under
the flat window's own Language), or with `[ui] language` in `config.ini`. **Automatic** follows the
system's language (`LC_ALL`, `LC_MESSAGES` or `LANG`) and is English when it has no translation.
The language is read when the app starts, so a change shows the next time you start a stream from
the flat window or start the app.

The translations are Moonlight XR's, so the words this client added (the Host sheet, the PC and app
lists' own lines, the newer Display rows and most notices) are still English in every language.
Chinese, Japanese and Korean need the Noto Sans CJK fonts in the Frame's `/usr/share/fonts`; the app
does not bring them. Without them those languages show English, and the log says
`needs Noto Sans CJK`. The flat window has its own Language row, which does not change the headset's.

### Check before a stream

Just before a stream starts, the flat window asks the PC (serverinfo and the app list, about 2 seconds at most) what it would say about this stream, and shows what will go wrong as a message at the top of the "Starting ..." screen:

- Apollo does not let this headset launch apps, view streams, or use its gamepad, mouse or keyboard input (Apollo's **Clients** page, the permissions of this client). Apollo gives a client paired after the first one only List Apps and View Streams.
- Use virtual display is on, but the PC is not Apollo, cannot make a virtual display, or Apollo's virtual display driver is not ready.
- Smart clipboard sync is on with a PC that is not Apollo, or without Apollo's clipboard permissions.
- The PC is already running another app (maybe for another client).
- The resolution is not one the PC lists (GameStream PCs list them; Apollo and Sunshine do not), or Prefer HEVC is set and the PC has no HEVC.

The stream starts anyway. If it then fails, the error message repeats these under "Found before the stream started:". The last check is also shown at the bottom of the Stream profiles page, and `moonlight-launcher.log` has `MLXR_LAUNCHER_PREFLIGHT:` and one `MLXR_LAUNCHER_HINT:` line for each message. A PC that does not answer in time gives no message. Turn it off with **Check the PC before streaming** (Host Settings) or `MLXR_LAUNCHER_PREFLIGHT=0`.

The same question gives the app's UUID on Apollo. The flat window passes it to the VR client (`MLXR_APP_UUID`), which sends it with the launch request next to the app's number, as Artemis does, so Apollo finds the app by its UUID and knows when that app is already running.

### Performance monitor and Advanced settings

| Setting | Options | Default |
| --- | --- | --- |
| Stats | on, off | off. Same as Stats in the headset's Display tab. In `config.ini`, `stats = hud` or `stats = lite` turns them on in that style (see Stats styles below). |
| Stats style | Full, HUD, Lite | Full. In `config.ini`: `[display] stats_style`. |
| Change codec settings | Automatic (Recommended), Prefer HEVC, Prefer H.264 | Automatic: HEVC when the PC has it, else H.264. The Frame decodes H.264 and HEVC only, so there is no AV1 choice. |
| Force full range video (experimental) | on, off | off. Hardware decoding only. |
| Force software decoding | on, off | off. Slow; for troubleshooting. |

The last line of the page says where the headset's own settings are saved (`~/.config/moonlight-xr/config.ini`).

## In the headset: the bar

The bar sits under the screen and shows its buttons only while the ray is over it (see [Controllers](#controllers) if you run with the ray off). From left to right:

| Button | What it does |
| --- | --- |
| Head aim | Only shown with Head locked on and outside a room. Your head moves the mouse. |
| Bring here | In the same place as Head aim, shown when Head aim is not: with the screen in the room (Head locked off) and outside a room. A switch between the Screen tab's **Desk** preset and where the screen was before. Pressed anywhere but at Desk, it remembers where the screen is (place, height, direction, tilt, size and curve) and puts it at Desk, exactly as the Screen tab's Desk cell does: 1.2 m away and 1.6 m wide, in the direction it already sat. Pressed at Desk, it puts the screen back exactly where it was. With nothing remembered (the first press ever, at Desk) it goes to the **Cinema** preset, as the Cinema cell does. It never follows your head or where you look. If you move or resize the screen while it is at Desk so that it is no longer the Desk preset, the next press remembers that new place and goes to Desk. The remembered place is kept in `config.ini` (`[screen] back_*`), so it survives a restart. No toast: the screen moving is the sign. |
| Gamepad / Pointer | Switches the controllers between pointer mode and gamepad mode. |
| Exit | Asks "Exit the stream?" with three choices. **Quit app** quits the app on the PC and ends the stream. **Disconnect** ends the stream and leaves the app running on the PC (use **Resume Game** in the flat window to go back). **Cancel** closes the question. |
| Environment | Opens the Rooms picker. |
| Move pill | Grab it to move the screen. See [Moving and sizing the screen](#moving-and-sizing-the-screen). |
| Cog | Opens the cog menu. |
| Keyboard | Opens or closes the headset keyboard. |
| Ray | Shows or hides the controller ray for this session only. The cog's Ray row is the saved one. |
| 3D | Only shown when the depth model is ready, or when the 3D source is SBS or Top-bottom. Turns 3D on or off. With the PC's own pair it reads **SBS** or **TB** while on. |
| Host | Opens the Host sheet: Esc, Win, Alt+Enter and the other keys that are awkward to reach, plus your own macros. A button can open it too: `app:host` in `bindings.ini`. See [Host: keys for the PC](#host-keys-for-the-pc). |

### Finding the bar

The bar is not drawn all the time. Its buttons and the move pill appear only while the ray is over the strip under the picture, and they go again when it leaves. That keeps the picture clear, but it means you have to point at the strip before the bar is there.

- Point either controller just below the picture. The buttons appear, the one under the ray grows a little, and a press lands on it. The whole row counts as the bar, gaps and all, so the buttons stay up while the ray moves along it and the pointer stays with that controller even when the other one is pointing at the picture.
- With **Ray** on Off (no beam and no dot), a faint beam shows only while the controller points into that strip, so the bar can still be found. Anywhere else nothing extra is drawn. With a menu open the faint beam and the dot show, so the menu can still be aimed at.
- In gamepad mode the controllers are not pointers, so there is nothing to point at the bar with: switch back to pointer mode first (see [Switching between the modes](#switching-between-the-modes)).
- Most of the bar can also go on a controller button without the bar: `app:cog`, `app:keyboard`, `app:gamepad`, `app:host`, `app:3d` and `app:ray` in `bindings.ini` ([Changing the buttons](#changing-the-buttons)). Exit, Environment, Head aim, the bar's Bring here switch and the move pill have no binding. Two more actions are not on the bar: `app:recentre` turns the screen round to where you face, and `app:bringhere` puts the screen where you look (the Screen tab's Bring here cell, not the bar's Desk switch).

### The first-run card

The first time you stream after installing, a card appears over the middle of the picture and lists the controls: where the bar is, what opens the cog and the keyboard, the shortcut for gamepad mode (worded for your `gamepad_toggle`), the Host button, the keyboard chord that disconnects, and how a Bluetooth gamepad becomes the PC's mouse (worded for the gamepad you have used: Menu, Options or +).

It has no buttons. It goes by itself after 20 seconds, or the moment you open any menu, and it does not come back: `first_run_shown = true` is written to `config.ini` as it appears. Set it to `false` there, or switch on **Controls card on the next stream** on the flat window's Settings page, to see the card again.

## The cog menu

Tabs: **Screen** (called **Room** inside a room), **Display**, **3D**, **Picture**, **Accessibility** (see [Accessibility](#accessibility)), **About**. A press outside the menu closes it. The cog opens on the tab you used last, and remembers it for the next session (`cog_tab` in `config.ini`).

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

The Screen tab's Bring here also works from a controller button: bind `app:bringhere` in `bindings.ini` (see [Changing the buttons](#changing-the-buttons)). Desk, Couch and Cinema have no binding.

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
| Sharpen | Off, Normal, Quality | Normal. With the screen curved or 3D on, it sharpens at the headset's own pixels, where it also crisps black text on white, and never rings past an edge. It sharpens brightness only and leaves colour as Off has it; text looks a little bolder, and light text on a dark background keeps more of its light than with Off. Quality is sharper still and holds back on black and white edges so white text on black does not shimmer. With a flat screen it sharpens the stream's pixels and leaves black on white alone. |
| Supersample | Off, Normal, Quality | Normal. Not used while the screen is curved or 3D is on. |
| Stats | Off, On | Off |
| Head locked | Off, On, Follow | Off. On: the screen is fixed to your head. Follow: the screen stays where it is until your head has turned more than the leash (25° by default, `follow_leash_deg`) away from it, in yaw or in pitch, then it eases after where you look over about half a second and stays there. See [Follow and comfort](#follow-and-comfort). |
| Head aim | Off, On | Off. Needs Head locked; not in a room. |
| Mode | Pointer, Gamepad | Pointer. Every session starts in pointer mode. The row also shows the switch shortcut. |
| Show controllers | Off, Frame, Gun | Off. Which controller is drawn in each hand; see [Controllers](#controllers). |
| Ray | Off, Dot, Ray | Ray. Dot shows only the dot where the ray lands. Off shows neither; the PC's own cursor is always in the video. Bar = this session, cog = saved: the choice here is kept for the next session, the bar's Ray button is not. A press shows at once, with the cog still open: while a menu is up, Ray is the beam and the dot, Dot the dot alone, and Off a faint beam with the dot, so the menu can still be aimed at. |
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
| Depth | How far apart the eyes' pictures are. The part of the track past the model's default is amber: depth there is harder on the eyes. |
| Convergence | Where the screen plane sits in depth. Default 50. With SBS or Top-bottom it moves the two halves apart (above 50, the picture goes behind the screen) or together (below 50, it comes out). |
| 3D | On or Off. Remembered between sessions. |
| 3D source | Depth, SBS, Top-bottom. Where the 3D comes from: the depth model, or a pair the PC renders itself. Takes effect at once and is remembered. See [SBS 3D](#sbs-3d). |
| Reset | Back to the model's values. |

The tab is greyed when 3D cannot run. It shows why: 3D is off in settings, the depth model is not installed, or it did not load. The 3D source row stays live, so SBS and Top-bottom work without the depth model. With SBS or Top-bottom, Preset and Depth are greyed ("the PC draws the depth").

### Picture

| Row | Values | Notes |
| --- | --- | --- |
| Brightness | -50 to 50 | |
| Contrast | 50 to 150 % | |
| Gamma | 0.50 to 2.00 | |
| Saturation | 0 to 200 % | |
| Background | 0 to 100 %, in steps of 5 | How far the surroundings are dimmed. In a room it turns the whole room down, its lighting and the light the picture throws on it too, to black at 100 %. In passthrough it lays a black layer of that strength over the cameras (100 % hides them). In the void it darkens the void's colour (`void_color`, below). 0 leaves all three as they were. The picture itself is not dimmed. |
| Glow edge | Off, Soft, Wide | Fades the picture's outer edge into the glow around it, so the screen has no hard border. Soft fades the outer 6 % of the picture's width and height, Wide the outer 15 %. Off by default. It needs the glow on (Display tab), and it takes the glow's level: at a low glow level the edge fades toward a dim colour. |

**Reset** puts all six rows back, and not the void's colour, which only the file has (or the launcher's Settings page: **Void colour**, under UI settings, next to **Background dim** and **Glow edge**; press a row to step it).

### About

The version, the log file, **Support simplysanatori on Ko-fi**, and **Report a problem**. The Ko-fi button opens a small sheet with two buttons. **Open page** shows this port's Ko-fi page in Steam's browser in the dashboard (the Frame runs `steam steam://openurl/https://ko-fi.com/sana748653`, or `xdg-open` when `steam` is not found). **Not now**, or a press anywhere off the buttons, closes the sheet and opens nothing. Upstream Moonlight XR's own Ko-fi is linked from the [README](../README.md#support). The report sheet has three buttons. **Save** keeps your note on the Frame in `~/Games/MoonlightXRNative/reports`. **Save and zip** does that and also runs `bug-report.sh` in the background, so the masked zip is made without leaving the headset. A notice shows where the zip is (`~/Games/MoonlightXRNative/reports/mlxr-report-<date>.zip`) and the log says so too. A note is not needed for the zip. Nothing leaves the headset: copy the zip off the Frame and open an issue on GitHub with it. The [README](../README.md#reporting-a-problem) lists what the zip holds and what is left out.

### Stats styles, battery and the slow connection badge

The Stats row turns the overlay on and off. What it shows is `stats_style` in `config.ini` (`[display]`): `full` is every line the session writes, `hud` is a few lines (latency with its parts, round trip, loss, bit rate, frame rate, and the compositor's frame times and dropped frames when SteamVR reports them), and `lite` is one line: `54 Mb/s | 11 ms | dec 3.1 | loss 0.2% | 90 fps` (speed, latency from the host's capture to the end of decoding, decode time, frames lost on the network, pictures drawn a second). `stats = hud` in the file means stats on in the HUD style. While stats are on and a stream is up, `perf-<date>-<time>.csv` next to `vrtiming.log` gets a row every 5 seconds with the figures and the settings they were measured in (size, rate, bit rate, codec, display rate, 3D, room, sharpening), for comparing runs.

Over the cog the clock strip also shows the headset's battery (a cell, the percentage, a bolt while it charges; read every 30 seconds, nothing on a PC with no battery) and a **Slow connection** badge once the connection has been poor, or more than 1.5 % of frames lost, for 5 seconds on end. The badge goes when it has been fine for 5 seconds. The HUD line starts with `!` or says slow connection while it is up.

## Accessibility

For people who cannot use their hands, or can use them only a little. The cog's **Accessibility** tab, and the same rows under **Accessibility** on the flat window's Settings page, let the head (and, where SteamVR allows it, the eyes) point and click. Nothing here turns on by itself: each setting changes only when its row is pressed, by you or by the person helping you. Accessibility is early in this version: head pointing and the dwell click are done; the eyes depend on SteamVR (below).

**For the person setting it up.** With a controller in hand, open the cog (the bar's cog button), go to **Accessibility**, press **Head** on the Pointer row, then a time on the **Dwell click** row (**0.9 s** is a good start; **0.6 s** once practised). From then on the controllers no longer point; the head does. On the flat window's Settings page the rows are **What points** and **Dwell click**; they take effect at the next stream. The tab's rows:

| Row | Cells | What it does |
| --- | --- | --- |
| Pointer | Controller, Head, Eyes | What points. **Eyes** shows only once SteamVR has given the app eye gaze in this session (or when it is already the setting). `[vr] pointer_source` |
| Dwell click | Off, 0.6 s, 0.9 s, 1.2 s, 1.5 s | Resting the dot this long on one spot clicks it. Greyed while the controllers point. `[vr] head_dwell_ms` |
| Head-look | Off, On | The head's turn moves the PC's mouse, for first-person games, see [Head-look](#head-look-experimental). `[vr] head_look` |
| Back as Guide | Off, On | Gamepad mode's View (Back) button is Guide, see [Gamepad mode](#gamepad-mode). `[vr] back_as_guide` |
| Mouse mode | Absolute, Relative | Where the pointer is, or how far it moved, see [Relative mouse](#relative-mouse). `[vr] mouse_mode` |

Under the rows a line says whether eye tracking reaches the app, and a few lines say how to use the tab.

**For the person using it.** A dot sits in the middle of your view; turn your head to move it. Hold it still on a button, a cell of the cog or a spot on the picture, and a ring closes in on the dot: when it closes, that is a click (the left mouse button on the picture). To click the same spot again, look away and back. A controller's trigger, if someone is holding one, clicks too. The dwell never clicks beside an open menu, so a look away cannot close it.

**Turning it off.** Open the cog's Accessibility tab and rest the dot on **Controller** (or press it with a trigger). Or set `pointer_source = controller` in `config.ini` (`~/.config/moonlight-xr/`), or **What points: Controller** on the flat window's Settings page.

**Eyes.** With **Eyes** the dot follows your eyes while SteamVR gives eye gaze to the app, and your head while it does not: when the eyes are lost for more than 0.2 s (a long blink, the headset lifted) the head takes over, and the eyes take it back after 0.3 s. The status line on the tab says which it is:

- **Eye tracking: available**: eye gaze reached the app this session; Eyes can be picked.
- **Eye tracking: not asked for**: `gaze = true` (or `pointer_source = gaze`) is not in `config.ini`, so the app did not ask SteamVR for it. Set one and restart the stream.
- **Eye tracking: offered, but no gaze has arrived yet**: SteamVR says it can, but nothing has come yet.
- **Eye tracking: not available to apps yet**: SteamVR does not give eye gaze to this app.

On the Frame, eye gaze has not reached the app in testing so far, so treat Eyes as a feature for when SteamVR provides it. What may help (reported by others, not confirmed by Valve): wear the headset; in SteamVR Settings > General turn **Eye Tracking** on and **Track Dominant Eye Only** off (show advanced settings if they are hidden); then restart the app. `moonlight.log` says once a session whether eye gaze arrived and, if not, what SteamVR answered (`eye gaze: ...`).

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

### Lightgun mode

With **Show controllers** set to **Gun**, the pointer becomes a light gun. The headset does this by itself (`lightgun = auto` in `config.ini`; `on` and `off` force it whatever the controller looks like). In lightgun mode:

- **Pointer sleep** is off, so the ray is always there between shots. The Pointer sleep row keeps showing what you chose and is used again when you leave lightgun mode.
- Clicks go to the PC exactly where the gun points this frame: the click smoothing of **Click steadiness** (the aim from before the trigger pull, the double-click wait and the hold) is off.
- The trigger never takes hold of the screen, so a shot that lands on the move pill below the picture is just a shot. The grip still grabs the pill, a corner, the keyboard's bar and the stats window.
- A short, strong pulse runs through the controller on every shot (the recoil). `lightgun_recoil = false` turns it off.
- The dot where the gun points stays as the **Ray** row has it. Set Ray to Dot to keep the dot and hide the beam.
- Optional reload: with `lightgun_reload = true`, a trigger pull that starts off the picture (in the room, in the margin round the picture or on the pill) is a right click for as long as you hold the trigger. Many light gun games and emulators use a shot off the screen to reload, and a right click is the button they ask for. A pull that starts on the picture is a left click even if you swing off it.

The PC sees one mouse, so this is a single-player light gun; the host cursor is also drawn into the video by Sunshine and Apollo, which the Moonlight protocol cannot hide.

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

`back_as_guide = true` in `config.ini` (or **Back button as Guide** on the launcher's Settings page) makes the left View (Back) button the Guide button on its own, for games and Steam's Big Picture that want Guide often. The PC then gets no Back from it, except while Start is held: Start with View stays Start and Back, so the quarter-second Guide hold and the disconnect chord work as before. Pressing View first and Start after sends a short Guide before the chord; press Start first if that matters.

The stick dead zone is 7 % of the way out. Move the launcher's **Adjust analog stick deadzone** slider above 0 and the controllers use that value instead (1 to 20 %). Values below 0 are dead zone compensation, which only physical gamepads use.

The PC's rumble drives the controllers in gamepad mode. By default both controllers get the stronger of the PC's two motors, so a game that uses one motor is felt in both hands; the trigger motors (when the PC sends them) add to the controller on their side. `rumble_mix` and `rumble_intensity` in `config.ini` change this (see [below](#advanced-configini)). If you feel nothing, `moonlight.log` has `rumble: the host's first request` when the PC's first rumble arrives, and `rumble on the left controller ... runtime said` for what the headset's runtime did with it. No first-request line means the PC sent no rumble (check Enable Rumble in the launcher and the PC's gamepad type).

### Gyro in gamepad mode

The Frame controllers' gamepad has no gyro unless you ask for it. `pad_gyro` in `config.ini` (or **Gyro from the headset** on the launcher's Settings page) sends the turn of one of two things to the PC as the gyro of a gamepad, for gyro aiming in Steam Input:

- `controller`: the right controller's turn (the left controller's when the right one is not tracked), as it points.
- `head`: the turn of your head.
- `off` (the default): the gamepad is an Xbox pad with no motion, as before.

With it on, the gamepad tells the PC it is a PlayStation pad with a gyroscope and an accelerometer. The PC then makes a DualShock 4 for it, so games and Steam Input show PlayStation button names for the controllers. The setting is read when gamepad mode turns on (turn gamepad mode off and on after changing it); a physical gamepad's own motion is a separate thing and is never mixed with this one. Motion goes out only in gamepad mode, only when the PC asks for it, and only for the controllers' own gamepad. With a panel open the gyro is held still.

The gyro is in degrees per second about the controller's own axes, the way a DualShock 4's is: turning the controller right is a negative yaw. The accelerometer is gravity alone, the way a still controller or head tilted like this would read; it does not send the movement of your hand.

`pad_gyro_ratchet` picks when the motion is sent. `always` (the default) sends it all the time. `stick_touch` sends it only while the right thumb touches the right thumbstick, the usual Steam Input ratchet, so you can lift the thumb to reposition the controller. While the thumb is off, the PC is told the gyro is still. The touch is read on the Steam Frame controllers; with other controllers, with hand tracking, or if SteamVR refuses the touch binding, `stick_touch` behaves as `always`, and `moonlight.log` says so.

**On the PC**, this needs a host that emulates a DualShock 4 for a PlayStation pad: Apollo (or Sunshine) with ViGEmBus installed and the gamepad type left on Automatic (or set to DS4). Steam Input only sees native gyro from that DualShock 4, so on a host that always makes an Xbox 360 pad the gyro cannot arrive.

### Head-look (experimental)

`head_look = true` in `config.ini` (or **Head-look** on the cog's Accessibility tab, or **Head-look (experimental)** on the launcher's Settings page, under Accessibility) makes your head the mouse for first-person games, the way a game's look control works: turn your head right and the PC's mouse moves right, look up and it moves up. It is meant for games streamed in side by side 3D in a room, where the screen stays put and you look around inside the game. It only works in pointer mode, and only with the screen locked to your head (**Head locked**) or with a room up; a screen placed in the world without a room is left alone. The speed is `head_look_px_deg` (pixels a degree, 1 to 30, default 8; the launcher's **Head-look speed** row steps through it), and the dead zone is Head aim's (`head_aim_deadzone`, degrees a second: a slower drift sends nothing). It is 3DoF: only turning counts, not leaning. The ray keeps working on top: while head-look is on, moving the ray nudges the cursor by how far its point moves instead of placing it.

To turn back without turning the game, hold a **grip** (either one, as long as it is bound to nothing, which it is by default in pointer mode) and turn your head; let go and it carries on from there, like lifting a mouse off the desk. Any other control can be the pause too: bind it to `app:lookhold` in `bindings.ini`. A panel opening (cog, keyboard, Host sheet) also pauses it. Head aim (the bar's button and the Display tab's row) wins where both could act, so with the screen head locked and Head aim on, Head aim's speed is used; in a room Head aim cannot act and head-look takes over. `moonlight.log` says `head-look on`, `head-look paused while its hold is down` and how far it moved the mouse every few seconds.

### Relative mouse

`mouse_mode = relative` in `config.ini` (or **Mouse mode** on the cog's Accessibility tab or on the launcher's Settings page, under Accessibility) sends how far the pointer moves on the picture instead of where it is, the way a real mouse does. Use it for games that capture the mouse and ignore its position: first-person games, and games whose cursor jumps or sticks to an edge with the default. Moving the ray across the picture moves the PC's mouse by the same number of stream pixels; the dot still shows where the ray lands, but the game's own cursor (if it draws one) goes its own way. Taking the ray off the picture and back somewhere else does not throw the mouse. `absolute` (the default) puts the PC's cursor where the ray lands, Moonlight's remote desktop mouse, which is what desktops and menus want. Head aim and head-look already move the mouse this way while they are on.

### Pointing with your head

`pointer_source = head` in `config.ini` (or **Pointer: Head** on the cog's [Accessibility](#accessibility) tab, or **What points** on the launcher's Settings page, under Accessibility) points with the middle of your view instead of a controller, for when holding a controller up is hard. A dot shows where you point (the beam would run out of your face, so there is none), smoothed so a still head holds it still; the PC's cursor follows it. Either controller's trigger clicks, on the picture and on the headset's own menus. With `head_dwell_ms` set (400 to 2000; 0, the default, is off), resting the dot on one spot for that long clicks it: a square ring closes in on the dot while it counts, and moving it more than about a degree starts the count again. After a click, move the dot away and back to click again. Dwell never clicks the move pill, the corners or the keyboard's bar (a press there would grab them), and never lands beside an open menu (a press there would close it). Gamepad mode is unchanged. `pointer_source = gaze` is the Accessibility tab's **Eyes**: the same, with the dot following your eyes while SteamVR gives eye tracking to apps, and your head while it does not.

### Hand tracking

`hand_tracking = true` in `config.ini` (or **Hand tracking** on the launcher's Settings page, under Input settings; off by default) lets you point with your hands instead of the controllers. A hand's ray is the pointer, and a pinch of thumb and index finger is the trigger: a left click on the picture, a press on the bar and the menus.

**The triple pinch** locks the hands out, so a stray pinch does not click: pinch three times quickly on one hand (the third within 0.9 seconds of the first). The headset shows "Hands locked"; locked hands have no ray and send no clicks. The same triple pinch unlocks them ("Hands unlocked"). The first two pinches are ordinary clicks, the third is never sent to the PC. Controllers are never locked. The first time a hand points in a session, a sheet explains this; **Don't show this again** keeps it away.

### Head tracking for PC games (opentrack)

Games with head tracking (flight and racing simulators that take TrackIR or FreeTrack) can follow your real head through [opentrack](https://github.com/opentrack/opentrack) on the PC. With `opentrack = true` in `config.ini` (or **Head tracking to opentrack** on the launcher's Settings page, under Input settings), the headset sends your head's position and turn to the PC every frame while a stream is running, as opentrack's "UDP over network" packet: x, y, z in centimetres, then yaw, pitch and roll in degrees. It goes to the PC you are streaming from unless `opentrack_host` names another address or name, on UDP port `opentrack_port` (4242, opentrack's own default). Sending never holds up a frame: a packet that cannot go out at once is dropped, and the next frame sends a newer one.

**On the PC:** install opentrack and start it. Under **Input** choose **UDP over network** and press the hammer button next to it: the port must be the same as `opentrack_port` (4242). Under **Output** choose what the game reads: **freetrack 2.0 Enhanced** for most TrackIR and FreeTrack games, or **Virtual joystick** for games that take a joystick. Press **Start**; the octopus in opentrack's window turns with your head once the stream is up. Let Windows Firewall allow opentrack on private networks when it asks (or allow UDP port 4242 in). Press opentrack's **Center** key (set one under Options, Shortcuts) while you look straight at the screen; opentrack then measures from there. Yaw is positive turning left, pitch positive looking up and roll positive tilting your head to the left; if a game turns the wrong way, tick **Invert** for that axis on opentrack's Output tab. The curves under **Mapping** set how far the game turns for each degree. `moonlight.log` says `opentrack: the head pose goes to <address>:4242 every frame` once the first packet is out, and how many went when the stream stops.

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

An action is `key:` (a key, with `ctrl+`, `shift+`, `alt+`, `win+` in front), `mouse:left|right|middle|x1|x2`, `wheel:up|down|left|right`, `pad:` (a gamepad button, gamepad mode only), `stick:` (what a whole thumbstick does: `scroll`, `mouse`, `wasd`, `arrows`, `dpad`) or `app:keyboard|ray|recentre|cog|3d|gamepad|bringhere|host|zoomin|zoomout|zoomreset|lookhold` (`zoomin`, `zoomout` and `zoomreset` are [Zoom and pan](#zoom-and-pan); `lookhold` pauses head-look while held). A line that is not right is left out: `moonlight.log` says which line and why, and the headset shows "bindings.ini: N errors, see log". The mode switch shortcuts, the disconnect chord and the grips' grab over the bar and corners cannot be changed. The full list is in the file itself. The flat launcher's **Controller bindings** page edits the same file ([BINDINGS.md](BINDINGS.md)). The headset reads the file when a stream starts, so a change takes effect at the next stream start.

### Switching between the modes

- Hold the **left View button and the left grip** together for half a second (the default shortcut).
- Or, with the other two shortcuts, **click both thumbsticks**, or **squeeze both triggers and both grips**: press the second within 0.15 seconds of the first and hold them all for 0.3 seconds.
- Or, in pointer mode, select the Gamepad / Pointer button on the bar, or the **Mode** row in the Display tab.

Only the shortcut you chose works; the other two do nothing. Choose it with **Gamepad mode shortcut** on the flat window's Settings page (Gamepad Settings) or `gamepad_toggle` in `config.ini` (see [below](#advanced-configini)); the Display tab's Mode row shows the one in use. A shortcut works both ways and cannot be rebound in `bindings.ini`.

The headset shows "Gamepad mode" or "Pointer mode". In gamepad mode the menus close.

## Moving and sizing the screen

| What | How |
| --- | --- |
| Move the screen | Grip the move pill under the screen and move your hand. |
| Push it away or pull it closer | While holding the pill, push either thumbstick up (farther) or down (closer). A push on a slant counts, and most of the way to the rim is full speed: 1.5 m a second either way. 0.3 to 8 m. |
| Resize | Grip a corner of the screen. In a room this sets the room's screen size. |
| Curve | Screen tab, Curve. |
| Recentre | Press d-pad up in pointer mode (`app:recentre`), or use SteamVR's recentre. The screen turns to where you face and keeps its distance, size and curve. |
| Move the headset keyboard | Grip or trigger on the keyboard's bar. The thumbstick pushes and pulls it the same way. |
| Move the stats window | Grip anywhere on it and drag. |
| Reset | Screen tab, Reset. |

### Follow and comfort

Follow is a lazy head lock. A screen fixed to the head never leaves your view, which some people find tiring or queasy; a screen fixed in the world makes you turn all the way round to find it again. Follow sits between: look around inside the leash and the screen holds still, look further and it glides after you (a slerp over 0.6 s, slow at the start and the end) and stops. It keeps its distance from you, its size and its tilt, and it waits while a menu is open, while you hold the screen, in a room and in the launcher's lists.

If it makes you uneasy, make the leash larger (up to 60°) so it moves less often, or use Off or On. Sit still for the first try: a screen that moves while you turn your head is the most noticeable thing to get used to. Following is a world-locked mode, so Head aim (which needs Head locked) is off while it is.

## Zoom and pan

Zoom shows a part of the picture filling the screen: 100 % is the whole picture, 200 % the middle quarter of it (half across, half down), 400 % a sixteenth. It is for a desktop with small text, a game's map or a corner of the screen you keep looking at. The steps are 100, 125, 150, 175, 200, 250, 300, 350 and 400 %.

There is no cog row for it. Bind it to a button in `bindings.ini` (see [Changing the buttons](#changing-the-buttons)):

```ini
[pointer]
right.y = app:zoomin           ; one step in, towards where the controller points
right.x = app:zoomout          ; one step out
left.dpad_left = app:zoomreset ; back to the whole picture
```

**Zoom in aims.** The point of the picture the controller is pointing at stays under it, so to look at a corner, point at the corner and press zoom in, a few times if you want. To move around inside the zoom, zoom out a step, point at the new place and zoom in again. If the controller is not pointing at the screen, the zoom goes about the middle of what you see. A part never goes past the picture's edge: pointing at the very edge and zooming puts the edge on the screen's edge.

**The pointer follows.** The PC's mouse lands where you point, in the picture as it is shown, so it clicks what you see. With a 3D film in SBS or Top-bottom the zoom is inside one eye's half: both eyes zoom into the same part and neither reads the other's half. The glow around the screen is made from the part you see, and the black bars of a letterboxed film are still found over the whole frame.

**It is remembered for each app**, with the screen's place (the Screen tab's Remember per app): zoom into a game's map, quit, start the game again and it is zoomed. An app you have not set one for starts with what is on. Forget on the Screen tab drops the remembered zoom with the rest of the layout. The flat launcher's Settings page has a **Zoom** row (press it to step) for the zoom a stream starts with, and `[display] zoom`, `zoom_x` and `zoom_y` in `config.ini` hold the zoom and where its middle is, in percent across and down the picture (0 the left or top edge, 100 the right or bottom edge).

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

- Turn it on or off with the bar's 3D button, the 3D tab, or the 3D switch on the flat Settings page.
- Comfort, Balanced and Strong set the depth. Depth and Convergence fine-tune it.
- 3D is on by default and remembered.
- The default model is ZipDepth. `depth_source` in `config.ini` chooses `zipdepth`, `zipdepth-int8` (lighter on the CPU) or `midas`.

### Edges

Each eye sees a little more of what is behind a person or an object on one side: the left eye on the left, the right eye on the right. Earlier versions filled that sliver by stretching the person's own edge across it, which showed as a fuzzy, furry outline in one eye only, most of all on hair or a dark outline against a bright sky or page. That gap is now filled from the background beside it, so the edge is as sharp in both eyes. This is on by default; `MLXR_HOLE_FILL=0` brings back the old look.

These four are on the flat window's Settings page, in `config.ini` (`[3d]`) and on the headset's 3D tab (Edge, Depth detail, Depth shape, Motion reset, under the 3D source row; they move the picture as you drag them, and are greyed while the 3D source is SBS or Top-bottom, since the PC then draws the pair). On the tab Edge steps by 25, Depth detail and Depth shape by 10 and Motion reset by 5.

| Setting | Key | Values | What it does |
| --- | --- | --- | --- |
| Edge Sharpness | `edge_sharpness` | 0 to 100 | 0. How closely the depth keeps to the edges in the picture. 0 is the soft edges the 3D always had. Higher keeps hair, outlines and thin text on their own depth instead of blending them into what is behind; too high can make edges look cut out or blocky on text. |
| Motion Reset | `motion_reset` | 0 to 50 (%) | 0, off. The depth is smoothed over time so it does not flicker, which leaves a short trail of the old depth behind something moving fast. With this set, where the picture changes by more than this percentage between two depth maps the smoothing starts again there, so fast edges leave no trail. About 10 suits fast games; too low brings back flicker. |
| Depth Detail | `depth_scale` | 0 to 200 (%) | 100. Moonlight XR's depth scale. The depth between things near each other, such as a face in front of a wall, scaled without moving the picture as a whole further in or out. |
| Depth Shape | `depth_global` | 0 to 200 (%) | 100. The depth of the picture as a whole, from the near ground to the far sky, scaled toward the screen plane. Lower is easier on the eyes; Depth Detail keeps things standing apart. |

At their defaults all four leave the picture as before.

## SBS 3D

If the PC renders the 3D itself, the headset can show it instead of making its own. This is for 3D films, games with their own side-by-side (SBS) mode, and games run through ReShade's SuperDepth3D or a similar 3D shader. Each eye sees its own half of the stream. The depth model does not run, so there is no 80 ms depth lag and no soft edges, and the CPU is free.

This is "3D cinema", not PC VR: the picture stays on the screen in front of you and does not follow your head. For PC VR games, use Valve's Steam Link on its own, outside this app.

Turn it on:

1. On the cog's 3D tab, set **3D source** to **SBS** (left eye in the left half) or **Top-bottom** (left eye in the top half). The flat window's Settings page has the same row. The bar's 3D button then reads SBS or TB.
2. Make the PC send the pair (see below).
3. If the depth looks inside out, the PC's pair has the right eye first: set `[debug] eye_swap = true` in `config.ini`.
4. **Convergence** moves the two halves apart or together. 50 is the pair as the PC drew it.

With the 3D switched off, you see the left eye's half on its own, as a flat picture.

**Half-size or full-size.** A half-size pair squeezes both eyes into one normal frame, so each eye has half the width (SBS) or half the height (Top-bottom). The headset stretches each half back to 16:9. This is what most 3D films and ReShade use, and it needs no change on the PC. A full-size pair puts two whole pictures side by side, for example 3840x1080 for two 1920x1080 eyes. It is sharper, but the stream is twice as large. Turn on `[3d] sbs_full` (Full-size SBS on the Settings page) for it. The screen still shows a 16:9 picture.

**The Apollo virtual display.** With Apollo, the PC can make a display of exactly the stream's size:

- Half-size (recommended): keep the stream at 1920x1080 (or 2560x1440). Turn on Use virtual display. Set the game or player to its SBS (or Top-bottom) mode, full screen.
- Full-size: set Video resolution to Custom... 3840x1080 in the flat window's Settings page and turn on Use virtual display. Apollo makes a 3840x1080 display. Set the game to render side by side across it, and turn on Full-size SBS.

Do not use 4K (3840x2160) full-size SBS for now. The headset decodes 4K, but a test on a home network could not carry a 4K stream at 150 Mbps: the picture stalled and lost frames. 3840x1080 is half of that and is the largest pair to try.

The mouse pointer lands on the left eye's half of the PC's desktop. The ambient glow is made from the left eye's half, and the black-bar detection is off in this mode, since the seam between the halves is not a letterbox. [Zoom](#zoom-and-pan) works inside each eye's half.

## Controllers

The cog's **Display** tab has the controller rows together: **Mode**, **Show controllers**, **Ray**, **Pointer sleep**, **Click steadiness** and **Click sound**.

| Row | What it does |
| --- | --- |
| Show controllers | **Off** (the default) draws no controller in the headset. The other cells draw one in each hand: **Frame** is the Steam Frame controller itself (the installer converts SteamVR's own models on your Frame, so they match the real controllers and move with your buttons and sticks; Valve's models are never in the download; if the conversion failed no controller is drawn and `moonlight.log` says why), and **Gun** is a Glock 17 with a weapon light, held like a light gun: it is drawn at the ray, with the barrel pointing along it and the muzzle where the dot is, and its trigger moves as you pull yours (the model is by 8sianDude, CC BY 4.0; a `gun_left.model` and `gun_right.model` of your own in `~/.config/moonlight-xr/models/` are drawn instead, see [Custom controller models](CUSTOM_MODELS.md#the-gun-model)). Controllers are hidden in passthrough. The choice takes effect at once and is kept for the next session. |
| Mode | Pointer or Gamepad. Every session starts in pointer mode. |
| Ray | Off, Dot or Ray: how the pointer shows. |

**A model of your own.** `controller_model = custom` in `config.ini` (or the launcher's Settings page, under Input settings, **Show controllers**: press the row until it says Your own model) draws the model you put in `~/.config/moonlight-xr/models/`. The cog has no cell for it. If the files are missing or cannot be read, the Frame controllers are drawn and `moonlight.log` says why. How to make the files: [Custom controller models](CUSTOM_MODELS.md). Older config files that say `controller_model = true` or `false` still work (Frame and Off), and `generic`, the plain controller earlier versions had, reads as Frame.

**Bar = this session, cog = saved.** The bar's Ray, Gamepad / Pointer and Head aim buttons change this session only. The cog's Ray row and Show controllers are kept for the next session.

The bar is hidden until the ray is over it. With **Ray** set to **Off** (no beam and no dot), a faint beam shows only while the controller points into the bar's area under the screen, so you can still find the bar. Anywhere else nothing extra is drawn; with a panel open the faint beam and the dot show, so the panel can be aimed at.

## Keyboards, mice and gamepads

| Device | Notes |
| --- | --- |
| Headset keyboard | The bar's Keyboard button. **Compact** has letter, symbol and function sheets. **Full** is a tenkeyless layout. On both, tap a modifier (Shift, Ctrl, Alt, Win) for one key press, hold it half a second to lock it (a locked key turns blue; tap it again to let it go). On Compact, a locked Shift keeps the capitals on. The key at the right switches between the layouts. Under the keys, on both layouts, every sheet and every keyboard map, a row of media keys: previous, play/pause, next, mute, volume down and volume up. They press the PC's own media keys, so they change the PC's volume and the PC's player, not the stream's. Hide, the bar button or a press elsewhere closes it. |
| Bluetooth keyboard | Pair it in SteamOS. Keys go to the PC while the stream is in front. The app takes the keyboard for itself during the stream. |
| Bluetooth mouse | Same as the keyboard. While you use the mouse, the controller ray does not move the cursor. |
| Bluetooth gamepad | Pair it in SteamOS. It shows up as a gamepad on the PC; once you use it the Frame controllers' gamepad steps aside ([below](#frame-controllers-and-a-gamepad-together)). The Steam Virtual Gamepad copy of the same pad is ignored. Hold Start for a second and it becomes the PC's mouse ([below](#a-gamepad-as-the-pcs-mouse)). Start+Select+L1+R1 disconnects. |

Gyro from Bluetooth pads reaches the PC as a DualSense-class pad. A pad with a gyroscope (a DualSense, DualShock 4 or Switch Pro pad, for example) tells the PC so when it arrives, and the PC asks for the motion data only when a game or Steam Input wants it. This needs Apollo or Sunshine with DS4 emulation on the PC (Apollo: ViGEmBus installed). The Frame controllers in gamepad mode are an Xbox pad and send no motion, unless you turn on `pad_gyro` ([Gyro in gamepad mode](#gyro-in-gamepad-mode)), and then only on their own gamepad. The same pad's battery level, touchpad (DualSense) and light bar colour are passed on too.

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

### The gamepad card

While a Bluetooth or USB gamepad is in use, a small card hangs in your lap: 45 cm below your eyes and 35 cm ahead of you in the direction you are facing, leaning back so its face looks at you. It follows your heading, not your head's tilt, so it stays in your lap when you look up at the picture, and it turns with you when you turn.

The card is a flat drawing of the gamepad, picked from the type the pad announces: Xbox, PlayStation, Nintendo, Steam Controller, or a plain generic pad. Whatever you do on the real pad lights up on the drawing: buttons and d-pad, bumpers, a trigger (a bar fills as you pull), a thumbstick (its cap follows the stick, and the ring lights when it is pulled or clicked) and the touchpad (a dot where your finger is). A **Gyro** mark lights while motion data from the pad is reaching the PC, which is the quick way to see that gyro works. Use it to learn which button is which, or to see that a pad is working at all.

**Gamepad card in the headset** on the flat window's Settings page (pressing the row steps it), or `pad_card` in `config.ini`, decides when it shows (the cog has no row for it: its Display tab is full): `auto` (default) shows it when you use the gamepad and takes it away 4 seconds after the last input; `on` keeps it up for as long as the gamepad counts as in use (20 seconds after its last input); `off` never shows it. It is for physical gamepads only: the Frame controllers in gamepad mode do not get one.

### AZERTY and QWERTZ

The headset keyboard can show French (AZERTY) or German (QWERTZ) letters. Pick it in the launcher's Settings page under Input settings ("Headset keyboard layout"), or set `[vr] keyboard_keymap` in `config.ini` to `qwerty` (the default), `azerty` or `qwertz`.

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

In the header, left of **Close**, **Clipboard, commands** opens a second page of the sheet (**Back** returns; **Close** is there too): the PC's clipboard and the commands the PC offers. Both need an Apollo PC; see [Clipboard and PC commands](#clipboard-and-pc-commands-apollo) for what to set up on it.

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

### Clipboard and PC commands (Apollo)

Open the Host sheet, then **Clipboard, commands**. The page has:

| Cell | Does |
| --- | --- |
| Paste from PC | Reads the PC's clipboard text and types it on the PC, as if you had typed it on the headset keyboard (line ends are the Enter key, tabs the Tab key). It types at most the first 4096 bytes. The toast says how many characters went. Use it where Ctrl+V does not work, such as a remote console, a game's chat box or a password box that blocks pasting. |
| Send text to PC | Closes the sheet and puts the Compact keyboard up as a text field of its own: what you type is kept on the keyboard (its space bar shows the end of it) and nothing goes to the PC. **Enter** sends the text to the PC's clipboard; **Hide** throws it away. On the PC, Ctrl+V pastes it. The text is one line. |
| One cell per command | The commands set up on the PC (up to eight). A press asks **Run** or **Cancel**, because a command can do anything the PC can: sleep, lock, change the audio device, start a script. The toast says it was sent. The PC does not answer, so a command that fails on the PC fails there. |

The toast says why when something cannot be done: "Not supported by this host" (the PC runs Sunshine or another host, not Apollo; the cells are dimmed and the page says so), "This host has not allowed it for the Frame" (the permission is off), "This host has no commands set up", or "The host sees no stream from this client". The clipboard works only while a stream is up.

On the PC, in Apollo:

- **Server commands.** Add them in Apollo's web UI settings (Configuration, General, Server Commands: a name, a command, and whether it runs elevated), or as `server_cmd` in its config file. The first one is number 0. The headset shows the names only.
- **Permissions.** Apollo gives each paired client its own permissions (the Clients page of its web UI). The Frame needs **Server Command** for the commands, **Clipboard Read** for Paste from PC and **Clipboard Set** for Send text to PC. The first client paired gets everything; a client paired later gets only View and List until you allow more. Apollo lists commands only to a client that has the Server Command permission, so with it off the page says the host has not allowed them.
- **Apollo 0.2.4 or newer** has the clipboard. Older ones answer "Not supported by this host".

The clipboard sync at the start and end of a stream (Smart clipboard sync on the Settings page) is separate and works as before.

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

On the Frame controllers, Start is the right menu button, Select is the left View button, and L1 and R1 are the bumpers. Hold all four. With **Grips in gamepad mode** on Bumpers (the default) the grips count as L1 and R1 too; with Nothing or Stick clicks only the bumpers do.

### When the connection drops

If the stream ends because the link was lost (a Wi-Fi dropout, the USB cable pulled out, the PC not answering), the headset says "Connection lost, reconnecting (1/2)..." (the Notices setting decides whether you see it), keeps the room, the screen's place and the 3D state, and asks the PC to resume the app that was running. The first try comes 2 seconds after the drop and the second 5 seconds after the first one fails. When one works the picture comes back and nothing more is said. The tries choose the network link again, so a stream that was on Wi-Fi can come back on the USB cable.

It does not start the app a second time: if the app is no longer running on the PC (it was closed there, or the PC restarted), or the PC refuses the headset, it gives up at once. It also does not try when the PC ended the stream on purpose (you quit the app on the PC, or it exited). Pressing Exit or Disconnect while it is trying stops the tries. When the tries are over the flat window comes back, as before. `moonlight.log` has a line for each step, starting with `reconnect:`.

### Taking the headset off

When the headset comes off, the sound mutes at once and everything held down on the PC is let go (keys of the headset keyboard, the controllers' mouse buttons, the Frame's gamepad). Put it back on and the sound returns and the stream carries on. If it stays off for `doff_disconnect` seconds (60 by default, in the `[stream]` section of `config.ini`, 0 means never) the stream is disconnected, leaving the game running on the PC, and the flat window comes back; **Resume Game** goes back in. The Settings page has the same choice under **Disconnect when the headset is off**.

`idle_disconnect` (minutes, 0 by default which is off; **Disconnect when idle** on the Settings page) does the same when nothing has been pressed or moved for that long: the controllers' ray or buttons, the headset keyboard, a physical keyboard or mouse, or a gamepad. The clock starts over when the headset goes back on or the dashboard is closed. Opening the dashboard also mutes the sound, as it always did.

This uses the headset sensor through `XR_EXT_user_presence`. `moonlight.log` has `presence:` lines: `headset off`, `headset back on after N s`, and the disconnect. If the runtime does not offer the extension, the log says so and none of this happens.

### The SteamVR dashboard

Opening the SteamVR dashboard (or any system menu) takes the focus away from the VR client, and the stream carries on behind it:

- The stream is not paused. Video keeps arriving, and the game keeps running on the PC.
- The sound is muted, and everything held down on the PC is let go: keys, mouse buttons, and the gamepad, which goes to rest. A gamepad that was the PC's mouse is a gamepad again.
- Nothing from the controllers, the headset keyboard, a Bluetooth keyboard, mouse or gamepad reaches the PC until you close the dashboard. The idle timer (`idle_disconnect`) starts over when you do.

If you want the game to stop while you look away, the dashboard does not do it: use Disconnect, or the pause recipe in [Apollo on the PC: recipes](#apollo-on-the-pc-recipes).

## Apollo on the PC: recipes

Apollo is the Sunshine fork this app is built against. Several of its options help a headset stream, and they live on the PC, in Apollo's web interface (`https://localhost:47990`), not in this app. The names below are Apollo 0.4's; if yours are different, look for the same words. These come from Apollo's documentation and from reading what this app sends. They have not all been measured on the Frame, and each recipe says what to look at in `moonlight.log` so you can tell for yourself.

### Pin the display mode

By default the virtual display is made at whatever resolution and rate the headset asks for ([Use virtual display](#video-settings) in the flat window). **Display Mode Override** takes that choice away from the client: in Apollo's web interface, on the client's entry, set a mode such as 2560x1440x90, and the display is made at that mode whatever the headset asks for.

- Keep the rate the same as the headset's. A 90 Hz override with the headset at 90 Hz is on the grid; a 144 Hz desktop feeding a 72 or 90 Hz headset hands it frames it has to drop or repeat.
- If you use **Always create virtual display** for this client (Apollo 0.3.7 and later), Windows remembers where the display sits for that client, so windows come back where you left them.
- Check: the Stats window's `Video ...` line shows the size and rate that actually arrived.

### Pause the PC while you are away, resume when you are back

Apollo can run commands when the last client disconnects and again when a client comes back: its global **Pause** and **Resume** commands (not the ones set on a single client): Pause runs when all clients have disconnected, Resume when one reconnects. Use them for the things you would do by hand when you take the headset off: mute the PC's speakers, pause the player, lock the screen, or suspend the game's process.

- They only run when the stream is disconnected, not when the app quits: **Exit, Disconnect**, the Start+Select+L1+R1 chord, the headset left off for `doff_disconnect` seconds, or `idle_disconnect`. **Quit app** ends the game, so there is nothing to pause. See [Ending a stream](#ending-a-stream).
- Leave **Terminate on Pause** off, or Pause ends the app and **Resume Game** in the flat window has nothing to resume.
- Apollo gives the command the client's name and the stream's size and rate in `APOLLO_` environment variables, so one script can tell the headset from a phone. A command set on the client itself (rather than the global pair) has been reported not to run after a disconnect in some Apollo versions; if it does not fire, use the global pair.
- Opening the SteamVR dashboard does not disconnect the stream, so it does not run these. See [The SteamVR dashboard](#the-steamvr-dashboard).

### Double refresh rate: an A/B test

Apollo has a server option, **Double refresh rate**, that makes it capture at twice the stream's rate and send the newest frame. The idea is that each frame is younger when it is sent; the cost is that the frames land on a finer time grid, and the intervals between them can scatter more. Whether that is better on your PC and link is something to measure, not assume.

1. Stream for a few minutes with the option off. Pick something steady, like a game standing still or a looping scene. In `moonlight.log`, find a `host frames ... fps by their timestamps` line (one every few seconds with the latency line) and write down: the host fps, the counts in each interval bucket, the number on the 2.78 ms grid, and the `arrival jitter`. Note the Stats window's `Video` fps and `decode` time too.
2. Turn **Double refresh rate** on in Apollo and start the stream again (the option is read when a stream starts). Repeat the same scene for the same time.
3. Compare. With the option on, expect the intervals to scatter between neighbouring buckets instead of sitting in the stream's own, and expect the grid count to rise. It is better if the host fps stays at the stream's rate, the number of one-refresh repeats does not grow, and the arrival jitter stays about the same. If the host fps drops or the jitter grows, turn it off again.

Do the A and B on the same link, at the same bitrate, in the same scene. A different time of day on Wi-Fi can move the jitter by as much as the option does.

## Recommended PC settings

| Setting | Recommendation |
| --- | --- |
| Bitrate | 40 to 60 Mbps over the Steam Frame Wireless Adapter. Higher rates lost packets on that link in testing. On a strong Wi-Fi connection 80 Mbps at 1440p worked. The default is higher (98 Mbps at 1440p and 90 Hz), so lower it in Settings if you see lost packets. |
| Codec | Automatic or HEVC. HEVC recovers from lost packets without waiting for a full new frame. |
| Display refresh rate on the PC | The stream's rate or higher (90 Hz for a 90 Hz stream, 120 or 144 Hz for those). A 60 Hz desktop gives a 60 fps stream. |
| Apollo | Use virtual display. The virtual display is made at the stream's rate (90, 120 or 144 Hz), so it matches the stream. |
| Sunshine or Apollo encoder | The fastest preset of your encoder (for NVENC, the lowest P number). |
| FEC | Leave Sunshine's FEC Percentage at its default (20) or raise it a little on a weak link. |

## Troubleshooting

| Problem | What to check |
| --- | --- |
| The picture freezes or drops to a few frames a second for a moment, the sound lags | Packets are lost in bursts. `moonlight.log` has one `video: burst at <time> for <N> ms: ...` line when such a burst ends: how many frames were lost on the network, how many whole frames were thrown away waiting for a repair (`dropped waiting for RFI / IDR`), how many full frames were asked for (`IDR requests sent`, `asked here`, `held back ... by rfi_idr_ms`) and whether the picture came back by the quick repair (`recovered by RFI`), by a full frame (`recovered by IDR`) or neither. During the burst the per-frame lines (`Waiting for RFI frame` and the like) are written once each and then only counted. Many bursts with `recovered by IDR` and several `IDR requests sent` each: try a larger `rfi_idr_ms`, a lower bitrate or another link. The 5 s line in the log adds `rx drop misc`: packets the headset's network interface or the system threw away before the app could read them (`drop`, `err`, `fifo` on the interface, `udp mem` and `csum`); they are not the network's losses. `own sockets` says how many of the receive-buffer drops were this app's. |
| Judder or stutter | Turn on Stats (Display tab). `Display 90 Hz, 90 fps` means the headset draws every frame. If `Video ... fps` is low, the PC is sending fewer frames: check the PC's display rate. If `idr requests` keeps rising, packets are being lost: lower the bitrate or try the other link. `vrtiming.log` shows SteamVR's dropped and reprojected frames. |
| Crackling sound | The stats line `audio ... underruns, ... lost` counts gaps. Lost packets are the network. On the adapter link, lower the bitrate. |
| Which link is in use | `moonlight.log` has `network path: using Wi-Fi (...)` or the adapter, when the stream starts. The line before it, `network path: chose ...`, gives both scores and what decided it (latency, failed probes or the last stream's losses), and `network path: history: ...` shows what the last stream on each link lost. The headset shows a notice when it falls back to the other link. |
| The stream switched links in the middle | `moonlight.log` has `network path: switching from X to Y after N ms of loss` and, just before it, the burst line ending in `network path switched from X to Y`. Then a `reconnect: try 1 of 2` and a new `network path: using ...` for the new link. It happens only with Switch network when video keeps breaking up on, Network connection on Automatic and another link measured at the start. If it never switches in a bad spot, the log's choice lines show whether the other link answered at the start (an adapter with no station is not tried). Raise Video loss before switching if it moves on brief dropouts. |
| The USB cable is never used | `moonlight.log` has a line `network path: USB cable: ...`. `usb0 up, carrier on, 3 peer candidates` means the headset sees the cable and tried three addresses on it: if none answered, check on the PC that the new network adapter is up, set to Private, and that Apollo or Sunshine is allowed through the firewall. `usb0 down (no cable, or the PC end is not up)` means no PC is on the other end: try another cable (it must carry data) or another USB port. No `USB cable` line at all means the headset is not in developer mode, so it has no USB network. In a terminal on the headset, `mlxr-cli probe-paths --network usb` prints the same check and the result for each address, and starts no stream. |
| The adapter is never used | `moonlight.log` has a line `network path: Steam Frame Wireless Adapter: ...`. `present (...)` means the PC is connected to the headset's adapter network. `no adapter found (wlanap up, no station)` means the headset's adapter network is on but no PC has joined it: plug the Steam Frame Wireless Adapter into the PC and wait for it to connect. `no adapter found (wlanap down)` or `(wlanap missing)` means the headset's adapter network is off. In a terminal, `mlxr-cli adapter` prints the same check and does not start a stream. |
| Cannot reach the PC | `moonlight.log` has `host ... not reachable at ...` for each address tried. Check the PC is on and Sunshine or Apollo is running. |
| Something stopped working after a SteamOS or SteamVR update (for developers) | `python3 tools/probe/doctor.py` in the source tree (in a terminal on the headset, with the client closed, `mlxr-hello` built) compares what the headset's OpenXR runtime offers with the last known good probe and prints what changed, and checks Steam's debugging port (the flat window's return after a stream uses it). `--save` keeps the current probe as the new baseline. |
| The flat window does not come back after a stream | Open the app again from the Steam library. `moonlight-launcher.log` shows `Asked Steam to show the flat screen again` and the `flat window` lines (what the window did after the stream). |
| Grey icon in the SteamVR dock | Restart Steam once after the first install. |
| 3D tab greyed | The tab says why. If the depth model did not load, the log has the error. |
| The stream did not start | The flat window shows the client's error, and under "Found before the stream started:" what the PC said just before (see [Check before a stream](#check-before-a-stream)). `moonlight-console.log` has the details. |
| The stream used other settings than the Settings page | The app or its PC has a stream profile. `moonlight-launcher.log` says `MLXR_LAUNCHER_PROFILE: using <name>`. See [Stream profiles](#stream-profiles). |

Logs are in `~/Games/MoonlightXRNative`. They are not masked. When you report freezes or stutter, say roughly when they happened: `moonlight.log`'s `video: burst at` lines carry the time, and the line written every 5 seconds has the round trip, the frames lost and dropped, `bursts N (longest N ms)`, `udp rcvbuf errors` and `rx drop misc` for those 5 seconds. To share them, use the zip from `bug-report.sh`. It holds the logs, your headset note, the settings you changed and a short version summary, all masked, and nothing else. It leaves out `hosts.ini`, keys and certificates. Look inside before you share it.

## Advanced: config.ini

`~/.config/moonlight-xr/config.ini` holds the headset settings. The cog menu writes most of them. A few are only in the file:

| Section, key | Values | Default |
| --- | --- | --- |
| `[display] head_follow` | `true`, `false` | `false`. The Head locked row's Follow cell. Head locked wins when both are on. |
| `[display] follow_leash_deg` | 10 to 60 | 25. How far, in degrees of yaw or pitch, your head turns from a followed screen before it eases after you. |
| `[display] zoom` | 100 to 400 | 100. Zoom in percent: 100 the whole picture, 400 a quarter of it across and down. See [Zoom and pan](#zoom-and-pan). Kept for each PC and app with the layout. |
| `[display] zoom_x`, `zoom_y` | 0 to 100 | 50. Where the zoomed part is centred, in percent across and down the picture; held where the part still fits. |
| `[screen] per_app` | `true`, `false` | `true`. Remember the layout per PC and app. |
| `[screen] angular_lock` | `true`, `false` | `true`. Pushing or pulling the held screen keeps its size in your view. |
| `[screen.<hash>]` | the `[screen]` keys, `head_locked`, `head_follow`, `stereo`, `separation`, `convergence`, `zoom`, `zoom_x`, `zoom_y` | One section for each remembered PC and app, named by a hash of their names. The program writes them (up to 32, the oldest dropped); delete a section to forget the app. |
| `[vr] gamepad_toggle` | `menu_grip` (left View + left grip), `sticks` (click both sticks), `triggers_grips` (both triggers and both grips) | `menu_grip` |
| `[vr] rumble_mix` | `both` (each controller gets the stronger of the PC's two motors), `split` (low frequency motor on the left, high frequency motor on the right) | `both` |
| `[vr] rumble_intensity` | 0 to 100 (%), the scale on the PC's rumble on the controllers | 100 |
| `[vr] pad_grip` | `bumpers` (the grips are LB and RB), `none` (the grips do nothing in gamepad mode), `stick_clicks` (the grips are LS and RS) | `bumpers` |
| `[vr] pad_mouse_chord` | `start_hold` (hold Start for a second), `start_back` (hold Start and Back), `off`. Turns a physical gamepad into the PC's mouse and back. | `start_hold` |
| `[vr] pad_mouse_scroll` | `right`, `left`, `none`. The stick that scrolls while a gamepad is a mouse; the other stick points. | `right` |
| `[vr] pad_arbitration` | `pointer` (a used gamepad takes the Frame controllers' gamepad off the PC), `also_gamepad` (both), `sleep` (pointer, and the controllers' pointer rests) | `pointer` |
| `[vr] first_run_shown` | `true`, `false` | `false`. Becomes `true` as the first-run controls card appears (once per install, on the first stream). Set it to `false` to see the card again. |
| `[vr] pad_card` | `auto`, `on`, `off` | `auto`. The gamepad card in your lap: `auto` shows it while a Bluetooth or USB gamepad is in use and takes it away 4 seconds after the last input, `on` keeps it up while the gamepad counts as in use, `off` never shows it. |
| `[vr] lightgun` | `auto` (the lightgun mode is on while Show controllers is Gun), `on`, `off`. See [Lightgun mode](#lightgun-mode). The launcher's Settings page has it too (**Lightgun mode**). | `auto` |
| `[vr] lightgun_reload` | `true`, `false`. In lightgun mode, a trigger pull that starts off the picture is a right click while it is held. | `false` |
| `[vr] lightgun_recoil` | `true`, `false`. In lightgun mode, a short strong pulse in the controller on every shot. | `true` |
| `[vr] pad_gyro` | `off`, `controller`, `head`. Sends the right controller's (or the head's) turn to the PC as the gyro of a PlayStation gamepad in gamepad mode. See [Gyro in gamepad mode](#gyro-in-gamepad-mode); the launcher has it too (**Gyro from the headset**). Read when gamepad mode turns on. | `off` |
| `[vr] pad_gyro_ratchet` | `always`, `stick_touch` (only while the right thumb touches the right thumbstick of a Steam Frame controller; the same as `always` where that touch is not read). | `always` |
| `[vr] head_look` | `true`, `false`. Experimental: your head's turn moves the PC's mouse in pointer mode with the screen head locked or a room up. See [Head-look](#head-look-experimental); the launcher has it too. | `false` |
| `[vr] head_look_px_deg` | 1 to 30. Head-look's mouse pixels for each degree the head turns. | 8 |
| `[vr] opentrack` | `true`, `false`. While streaming, the head pose goes to opentrack on the PC every frame. See [Head tracking for PC games](#head-tracking-for-pc-games-opentrack); the launcher has it too. | `false` |
| `[vr] opentrack_host` | An address or a name | Empty: the PC you are streaming from. |
| `[vr] opentrack_port` | 1 to 65535 | 4242, opentrack's UDP over network default. |
| `[vr] pointer_source` | `controller`, `head`, `gaze`. What points; `gaze` is the Accessibility tab's Eyes and asks SteamVR for eye tracking, as `gaze = true` does. See [Accessibility](#accessibility). | `controller` |
| `[vr] head_dwell_ms` | 0, or 400 to 2000 | 0 (off). With `pointer_source = head` or `gaze`, resting the dot this long clicks. The Accessibility tab offers 600, 900, 1200 and 1500. |
| `[vr] mouse_mode` | `absolute`, `relative`. Where the ray lands, or how far it moves. See [Relative mouse](#relative-mouse); the launcher has it too. | `absolute` |
| `[vr] back_as_guide` | `true`, `false`. Gamepad mode's View (Back) button is Guide, except with Start held. The launcher has it too. | `false` |
| `[vr] background_dim` | 0 to 100 (%) | 0. The Picture tab's Background row. |
| `[vr] void_color` | `#rrggbb` (also `rrggbb`, `#rgb`) | `#000000`. The colour of the void environment, as you would type it on a web page. Anything that is not a colour is read as black. Black draws nothing, so the default costs nothing; any other colour is drawn behind everything as a solid layer. Dimmed by `background_dim`. |
| `[display] glow_feather` | `off`, `soft`, `wide` | `off`. The Picture tab's Glow edge row. |
| `[display] picture_filter` | `true`, `false` | `true`. A curved or 3D screen is drawn smaller than the stream's pixels, and each of the headset's pixels then averages the stream under it, so small text keeps its strokes. `false` is the softer single read of before. The `MLXR_PICTURE_FILTER=0` or `1` launch option overrides it for a comparison. |
| `[display] chroma_from_luma` | `true`, `false` | `true`. The stream carries colour at half resolution. With this on, colour edges follow the sharp brightness edges, so red, blue and syntax-coloured text keeps its colour to the edge. `false` reads the colour plainly (softer coloured text). `MLXR_CHROMA_FROM_LUMA=0` or `1` overrides it. |
| `[3d] eye_balance` | -100 to 100 | 0. 3D Eye Balance: how the 3D's shift is shared by the two eyes. 0 shifts each eye by the same amount in opposite directions. +100 puts all of it in the left eye and leaves the right eye's picture as streamed, -100 the other way round, and in between the left eye takes (100 + n) / 2 percent of the total and the right eye (100 - n) / 2. The total difference between the eyes, so the depth you see, is the same at every setting. Useful with a dominant eye. The 3D tab's **Eye balance** row moves it in steps of 10 (greyed with SBS or Top-bottom); the launcher's Settings page has **3D eye balance**, which steps -100, -50, 0, 50, 100. |
| `[vr] scroll_speed` | 25 to 400 (%) | 100, which is 7.5 notches a second with the thumbstick pushed all the way. The flat window's Settings page has it too. |
| `[vr] click_steadiness` | `off`, `normal`, `steady` | `normal`. The Display tab's Click steadiness row. |
| `[vr] keyboard_hold_ms` | 200 to 1500 | 500 |
| `[ui] language` | `auto`, `en`, `cs`, `da`, `de`, `es`, `fi`, `fr`, `it`, `nb`, `nl`, `pl`, `pt-BR`, `sv`, `tr`, `ru`, `uk`, `ja`, `ko`, `zh-CN`, `zh-TW` | `auto`. The headset's language, read at start; see [Language](#language). |
| `[vr] keyboard_keymap` | `qwerty`, `azerty` (French), `qwertz` (German). What the headset keyboard's keys say. The PC must be on the matching Windows layout. | `qwerty` |
| `[vr] depth_source` | `zipdepth`, `zipdepth-int8`, `midas` | `zipdepth` |
| `[3d] stereo_source` | `depth`, `sbs`, `top-bottom` | `depth`. The 3D tab's 3D source row. See [SBS 3D](#sbs-3d). |
| `[3d] sbs_full` | `true`, `false` | `false`. The PC's pair has a full-size picture per eye (3840x1080 side by side, 1920x2160 top-bottom). |
| `[vr] hand_tracking` | `true`, `false` | `false`. See [Hand tracking](#hand-tracking). |
| `[vr] gaze` | `true`, `false` | `false`. Needs eye tracking from SteamVR; see [Accessibility](#accessibility). |
| `[vr] pointer` | `true`, `false` | `true`. `false` turns the controllers' pointer off, so only the switch to gamepad mode is read. |
| `[vr] env_res` | `low`, `standard`, `high`, `ultra` | `standard`. How sharp the rooms are drawn: `low` is half the headset's recommended size, `ultra` the largest the runtime allows. Read when a room is built, so it applies from the next stream. |
| `[debug] depth_rate` | 5 to 45 | 20. The most depth maps made per second in 3D. Lower saves the CPU; the picture still follows the video. |
| `[debug] depth_threads` | 1 to 4 | 2. CPU threads the depth model runs on. 3 makes each depth map a few milliseconds sooner (up to about 10) and uses another core. `MLXR_DEPTH_THREADS` overrides it. Read when the model starts. |
| `[3d] edge_sharpness`, `motion_reset`, `depth_scale`, `depth_global` | see [3D](#edges) | 0, 0, 100, 100 |
| `[debug] depth_debug` | `true`, `false` | `false`. Shows the depth map in place of the picture. |
| `[debug] eye_swap` | `true`, `false` | `false`. Swaps the left and right eye in 3D. For checking a wrong-way picture, and for an SBS or Top-bottom pair with the right eye first. |
| `[stream] bitrate_kbps` | 0, or 500 to 500000 | 0, which follows the resolution and refresh rate. A bitrate the flat window passes for a stream overrides it. The flat window's Custom bitrate goes up to 500 Mbps, and so does this. |
| `[stream] bitrate_wifi` | 0, or 500 to 500000 | 0, which uses `bitrate_kbps` or the bitrate the flat window passes. The bitrate in kbps a stream starts with when it goes over Wi-Fi. See [Bitrate for each connection](#bitrate-for-each-connection). |
| `[stream] bitrate_adapter` | 0, or 500 to 500000 | 0. The same for the Steam Frame Wireless Adapter. |
| `[stream] bitrate_wired` | 0, or 500 to 500000 | 0. The same for a wired network. |
| `[stream] bitrate_usb` | 0, or 500 to 500000 | 0. The same for the USB cable. |
| `[stream] doff_disconnect` | 0 to 3600 (seconds) | 60. How long the headset can stay off before the stream is disconnected (the game keeps running on the PC). 0 never. |
| `[stream] idle_disconnect` | 0 to 1440 (minutes) | 0 (off). Disconnects the stream after this long with no input. |
| `[stream] rfi_idr_ms` | 0 to 5000 (ms) | 500. After lost video, the headset asks the PC for a full (IDR) frame at most once in this time. 0 no limit. The PC's own requests from the streaming library cannot be held back; the log counts them. The flat window's Settings page has the same choice under Connection. |
| `[stream] failover` | `off`, `on` | `off`. With `on` and `network_path = auto`, a stream that keeps losing video on its link moves to another link that measured clearly better when the stream started. See [Switching links when video keeps breaking up](#switching-links-when-video-keeps-breaking-up). The flat window's Settings page has the same switch under Connection. |
| `[stream] failover_burst_ms` | 500 to 9000 (ms) | 3000. How much lost video within 10 seconds makes `failover` leave the link. |
| `[stream] virtual_surround` | `true`, `false` | `false`. Places the speakers around you in your headphones and keeps them with the screen as you turn your head: 5.1 and 7.1 all round you, stereo as two speakers in front of the screen, 30° either side. |
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
| `MLXR_VIDEO_RFI_IDR_MS` | 100 | The shortest time a recovery may take before a full frame is asked for. The wait grows to three round trips on a slow link, and while the link is still losing frames it lasts until the link has been clean that long. |
| `MLXR_VIDEO_RFI_IDR_MAX_MS` | 1000 | The longest that wait grows to: after it a full frame is asked for even if frames are still being lost. |
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
| `MLXR_LAUNCHER_PREFLIGHT` | 1 | 0 stops the flat window from asking the PC before a stream (the check and the app's UUID). |
| `MLXR_LAUNCHER_UPDATE` | 1 | 0 stops the flat window's daily update check, whatever **Check for updates** says. |
| `MLXR_APP_UUID` | | Set by the flat window for the VR client: the app's UUID on Apollo, sent in the launch request as `appuuid`. Anything that is not a UUID is ignored. |
| `MLXR_LAUNCHER_SCALE` | 1.5 | How much larger the flat window is drawn in Game Mode (1.0 to 2.5). A `QT_SCALE_FACTOR` you set yourself is used when this is not set. |
| `MLXR_LAUNCHER_FADE_WINDOW` | 1 | In Game Mode the flat window fades to opacity 0 during a stream and stays mapped, so gamescope keeps a window for keyboard focus. 0 hides the window instead, which can crash gamescope on a volume key press. |
