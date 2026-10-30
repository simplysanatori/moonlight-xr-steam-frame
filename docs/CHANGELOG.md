# Changelog

## 1.1.0 (October 2026)

### Streaming

- Each connection can start at its own bitrate: **Bitrate over Wi-Fi**, **Bitrate over the Steam Frame Wireless Adapter**, **Bitrate over a wired network** and **Bitrate over the USB cable** under Connection. **Same as above** (the default) uses the Video bitrate.
- The **Video bitrate** row and `config.ini` go up to 500 Mbps.
- The Settings page's **Video bitrate** slider moved to Connection, right above the per-connection bitrate rows (which stay **Same as above**).
- A dropped connection reconnects by itself: the headset keeps the room, the screen's place and the 3D, and asks the PC to resume the same app twice (after 2 and 5 seconds) before the flat window comes back.
- Taking the headset off mutes the sound and lets go of every key and button held on the PC. After 60 seconds off, the stream disconnects and the game keeps running on the PC (`[stream] doff_disconnect`, 0 for never).
- **Disconnect when idle** ends the stream after a set time with no input; the game keeps running (off by default).
- After lost video the headset waits for the PC's quick repair as long as the link needs before it asks for a full frame, so a bad moment on Wi-Fi does not turn into a longer freeze.
- **Full-frame requests after lost video** limits how often the headset asks the PC for a full frame (500 ms by default).
- **Virtual surround** keeps the 5.1 and 7.1 speakers in place as you turn your head, and has a switch on the Settings page (off by default).
- **Virtual surround** now works with Stereo too: the two channels become speakers in front of the screen that stay put as you turn your head.
- The sound mutes while the SteamVR dashboard is open, and everything held down on the PC is let go.
- The Moonlight XR logo with "Connecting to *PC name*..." shows while a stream starts, and "Reconnecting..." while it reconnects.

### Network

- A USB data cable between the headset and the PC is a third connection: **USB cable** under Network connection, and Automatic tries it too. It needs the headset in developer mode.
- **Use VPN addresses only away from home** under Connection (on by default). A PC's VPN address is used only when the PC does not answer on a direct address; at home the home network is always used, and the log says so in one line.
- **Switch network when video keeps breaking up** moves a stream that keeps losing video to another link that measured clearly better when it started; **Video loss before switching** sets how much loss that takes. Off by default, and only with Network connection on Automatic.
- `moonlight.log` tells video lost on the network apart from Wi-Fi power saving and packets dropped inside the headset, with a summary of each burst of loss, for bug reports.

### The picture

- **Sharper small text** averages the stream under each of the headset's pixels on a curved or 3D screen, so small text is sharper (on by default).
- **Sharper coloured text** keeps red, blue and syntax-coloured text in colour to its edge on a curved or 3D screen (on by default).
- **Sharpen** on a curved or 3D screen now works at the headset's own pixels, crisps black text on white and never rings past an edge.
- **Sharpen** now defaults to **Normal** and sharpens brightness only, so colour stays exactly as with Off.
- **Sharpen**'s **Quality** holds back on black-and-white edges, so white text on black shimmers about as little as with Normal.
- Zoom and pan: bind `app:zoomin`, `app:zoomout` and `app:zoomreset` to show a part of the picture filling the screen, from 100 to 400 %. Zoom in aims at where the controller points, and the zoom is kept for each app.
- The Settings page's **Zoom** row sets the zoom a stream starts with.
- The Screen tab's **Preset** row: **Desk** (1.2 m away, 1.6 m wide), **Couch** (2.5 m, 2.6 m) and **Cinema** (4 m, 5 m).
- The Screen tab's **Bring here** puts the screen where you look, as far away and as large as it was (also `app:bringhere` in `bindings.ini`).
- **Remember per app** keeps the screen's place, the head lock, the 3D state and the zoom for each PC and app (on by default; **Forget** drops what is kept for the app now streaming).
- **Follow**, a third choice of the Head locked row: the screen stays put until your head turns more than 25° away, then glides after you (**Follow after turning**, 10 to 60°).
- **Angular-size lock**: pushing or pulling the held screen with the thumbstick keeps its size in your view (on by default).
- The Picture tab's **Background** (Settings page: **Background dim**) dims the room, the passthrough or the void from 0 to 100 % (0 by default).
- The Picture tab's **Glow edge** fades the picture's edge into the glow, **Soft** or **Wide** (off by default).
- **Void colour** on the Settings page (`[vr] void_color`) sets the colour of the black void.

### 3D

- **3D source** shows a side-by-side or top-bottom pair the PC renders itself (3D films, games with an SBS mode, ReShade's SuperDepth3D) instead of the depth model, with no depth lag. The bar's 3D button reads SBS or TB.
- **Full-size SBS** is for a PC pair with a full picture per eye (off by default).
- The sliver each eye sees behind a person or an object is filled from the background, so outlines are as sharp in both eyes (on by default).
- **Edge** on the 3D tab (Settings page: **3D edge**) keeps hair, outlines and thin text on their own depth instead of blending them into what is behind.
- **Motion reset** (Settings page: **3D motion reset**) restarts the depth smoothing where the picture changes fast, so moving edges leave no trail.
- **Depth detail** (Settings page: **3D depth detail**) scales the depth between things near each other without moving the whole picture in or out.
- **Depth shape** (Settings page: **3D depth shape**) scales the depth of the picture as a whole toward the screen.
- **Eye balance** (Settings page: **3D eye balance**) shares the 3D shift between the two eyes, or puts all of it in one.
- At their defaults the five new 3D rows leave the picture as before.

### Controllers and input

- New pointer-mode buttons: X turns 3D on or off, Y shows or hides the headset keyboard, the right Menu button opens the cog, d-pad up turns the screen round to where you face, and the bumpers are Back and Forward. D-pad left stays free.
- `bindings.ini` changes what each controller button does, in pointer and gamepad mode, for every app or for one app, and a change applies at the next stream.
- The flat window's **Controller bindings** page edits `bindings.ini`.
- **Show controllers** on the Display tab chooses what is drawn in each hand: **Off** (the default), **Frame** or **Gun**.
- The **Gun** is a G17 Gen 5 with a weapon light, held like a light gun with the muzzle on the dot and a trigger that moves with yours (model by 8sianDude, CC BY 4.0; see CREDITS.md). A `gun_left.model` and `gun_right.model` pair of your own in the models folder replaces it.
- **Your own model** (Settings page's **Show controllers**, or `controller_model = custom`) draws a controller model you made yourself from `~/.config/moonlight-xr/models/`; [CUSTOM_MODELS.md](CUSTOM_MODELS.md) explains how to convert one.
- Lightgun mode: with Show controllers on Gun, clicks land exactly where the gun points and each shot gives a recoil pulse (**Lightgun recoil**, on by default).
- **Lightgun reload** makes a trigger pull that starts off the picture a right click, for games that reload with a shot off the screen (off by default).
- **Gyro from the headset** sends the right controller's or your head's turn to the PC as the gyro of a PlayStation gamepad in gamepad mode, for gyro aiming in Steam Input (off by default). **When the gyro is sent** can limit it to while your thumb touches the right stick.
- Bluetooth gamepads pass their gyro, accelerometer, battery, touchpad and light bar to the PC (needs DS4 emulation on the PC).
- **Gamepad as mouse**: hold Start on a Bluetooth or USB gamepad for a second and it becomes the PC's mouse; hold it again to give it back.
- **When a gamepad is connected**: a Bluetooth gamepad in use takes the Frame controllers' gamepad off the PC, so the controllers point again.
- **Flip face buttons** now applies to Bluetooth and USB gamepads too.
- In gamepad mode, left View and right Menu held together for a quarter of a second is the Guide button.
- **Back button as Guide** makes View the Guide button on its own (off by default).
- **Grips in gamepad mode**: the grips can be the bumpers (the default), the stick clicks or nothing.
- **Adjust analog stick dead zone** now applies to the Frame controllers too.
- Rumble reaches both hands by default, the trigger motors included.
- **Rumble strength on the headset controllers** turns the controllers' rumble down.
- **Click steadiness** (Off, Normal, Steady) holds the cursor still around a click so double clicks land on the same spot.
- Thumbstick scrolling is smooth, with **Scroll speed** on the Settings page (`scroll_speed` in `config.ini`) for its speed.
- **Mouse mode** Relative sends how far the pointer moves instead of where it is, for games that capture the mouse (Absolute by default).
- **Head-look (experimental)**: with the screen head locked or in a room, your head's turn moves the PC's mouse for first-person games; hold a grip to turn back without turning the game (off by default).
- **Head tracking to opentrack** sends your head's position and turn to opentrack on the PC, for simulators that take TrackIR or FreeTrack (off by default).
- **Accessibility** (early): a new Accessibility tab in the cog and a matching section on the Settings page. Point with your head instead of a controller (**What points**), click by resting the dot (**Dwell click**, 0.6 to 1.5 s), and set Head-look, Back as Guide and Mouse mode there, with short instructions on the tab. Nothing turns on by itself.
- Your eyes can point when SteamVR provides eye tracking: the Accessibility tab shows whether eye tracking reaches the app, the **Eyes** choice appears only then, and the head takes over while the eyes are lost.
<!-- pending lane: accessibility round two (Eyes in the menu, dot size, hover texts, side-button clicks, radial dwell menu) -->
- A gamepad card hangs in your lap while a Bluetooth or USB gamepad is in use and lights up what you press on it (**Gamepad card in the headset**: Automatic, On, Off).

### The bar and menus

- **Bring here**, outermost on the bar's left, switches the screen between the Screen tab's Desk preset and the place it had before, exactly as you left it (Cinema when nothing is remembered). It does not follow your head, and the place it goes back to is kept across restarts.
- A **Host** button on the bar opens the Host sheet (also `app:host` in `bindings.ini`).
- Exit on the bar asks **Quit app**, **Disconnect** (the game keeps running on the PC) or **Cancel**.
- The cog opens on the tab you used last.
- The Display tab has the controller rows together: **Mode**, **Show controllers**, **Ray**, **Pointer sleep**, **Click steadiness** and **Click sound**.
- **Notices** (All, Important, Off) chooses which pop-ups appear under the picture.
- The cog's **Ray** row (Off, Dot, Ray) is kept for the next session and shows at once with the cog open.
- With the ray off, a faint beam still shows over the bar so you can find it.
- Stats has two shorter styles, **HUD** and **Lite** (**Stats style** on the Settings page, `stats_style`). The HUD also shows the compositor's frame times and dropped frames when SteamVR reports them.
- With Stats on, `perf-<date>-<time>.csv` next to `vrtiming.log` gets a row of figures every 5 seconds, for comparing runs.
- The clock strip shows the headset's battery.
- A **Slow connection** badge shows in the clock strip while frames are being lost.
- A card lists the controls the first time you stream after installing (**Controls card on the next stream** shows it again).
- The headset's menus, keyboard and notices can be in 19 languages besides English (**Headset language**). Chinese, Japanese and Korean need the Noto Sans CJK fonts on the Frame.
- The report sheet has **Save and zip**, which runs `bug-report.sh` and shows where the zip is.

### Keyboard and host

- The **Host** sheet sends keys that are awkward to reach in one press (Esc, Win, Win+D, Alt+Enter, Ctrl+Alt+Tab, Ctrl+Shift+Esc, Alt+F4 after asking, and more), and **Send keys...** for any combo.
- Up to eight macros of your own from `macros.ini` show on the Host sheet.
- With an Apollo PC, the Host sheet's **Clipboard, commands** page has **Paste from PC** (types the PC's clipboard), **Send text to PC** (puts what you type on the PC's clipboard) and the server commands set up in Apollo.
- The headset keyboards have AZERTY (French) and QWERTZ (German) layouts (**Headset keyboard layout**), with dead keys on the Full keyboard.
- A row of media keys under both keyboards: previous, play/pause, next, mute, volume down and up, for the PC's own player and volume.
- The Compact keyboard's Shift, Ctrl, Alt and Win work like the Full keyboard's: tap for one key, hold half a second to lock (**Hold to lock Shift, Ctrl, Alt or Win** sets the time).

### The flat window

- Stream settings profiles: save the page's settings under a name and use them for a PC or for one app (**Profiles**, or **Stream settings profile...** in an app's menu).
- Before a stream starts, the flat window asks the PC what will go wrong (Apollo permissions, the virtual display, another app already running) and says so (**Check the PC before streaming**, on by default).
- The Settings page has a **Search settings** field and a button for each category.
- The Settings page has rows for the headset settings that were only in `config.ini` or the cog menu: the picture, the pointer, the keyboard, gamepad mode, the environment and the screen.
- The flat window checks once a day whether a newer release is out on GitHub and then shows an update button that opens its release page. Nothing is downloaded or sent; **Check for updates** in Settings turns it off.
- **On start** in Settings opens the last PC's Desktop, or an app you choose, as soon as the PC is online, and wakes the PC first.
- **Help** opens the readme in Steam's browser in Game Mode, as the Ko-fi page does.
- Apollo apps are started by their UUID as well as their number, as Artemis does, so Apollo knows when that app is already running.

### Install

- `install.sh --desktop-shortcut` adds a second Steam shortcut, **Desktop in VR**, that goes straight to the PC's Desktop.
- The release is also written as a `.zip` with the same files and modes as the `.tar.gz`, for the community installers Frametanium and FrameDrop.
- `install.sh` keeps a log in `~/Games/MoonlightXRNative/install.log` and never waits for input; when Steam is not running it installs the files, says that the Steam shortcut was skipped and exits 0.

### Changed

- The Settings page is tidied up. Rows that were filed under Performance Monitor or Gamepad move to where they belong (Scroll speed to Input; 3D eye balance to the 3D rows; Notices, Zoom, Background dim, Glow edge and Void colour to UI; the two disconnect rows and the controls card to General), the 3D switch, 3D source and depth model come before the rows they grey out, and a row that depends on another is greyed with the reason while that one is off.
- Settings page names follow the headset's cog where it has the same setting (**Sharpen**, **Supersample**, **Screen size**, **Remember per app**, **Angular-size lock**, **Head aim**, **Pointer sleep**, **Notices**, **Stats**, **3D depth**, **3D convergence** and more), in sentence case. **Head locked** (Off, On, Follow) and **Ray** (Off, Dot, Ray) are one row each, as in the cog, and Render Mode is the **3D** switch. Custom resolution is the **Custom...** entry of Video resolution. The rows that could never be used on the Frame are gone (HDR, Unlock all possible frame rates, the AV1 codec choice). Apollo-only rows say "Needs Apollo".
- The headset remembers the PC's home address the first time it finds it and never removes a saved VPN address. Switching networks when video keeps breaking up never moves a stream from the home network to a VPN. Streams over a VPN are not counted in the per-link history.
- The Display tab's Controller model row (Off, On) is now **Show controllers** (Off, Frame, Gun). When SteamVR's Frame models could not be converted, no controller is drawn instead of a plain one.
- The About tab's Ko-fi button reads **Support simplysanatori on Ko-fi** and its sheet opens this port's page in Steam's browser; the QR code is gone.

### Fixed

- At home, a stream could go through a VPN when its address answered faster, with dropped audio and a low frame rate.
- A white beam no longer runs across the picture when you move the ray along the bar with the other controller pointing at the picture: the whole bar row now belongs to the hand on it.
- The ray and the controller model no longer flicker or jump when tracking drops for a moment: the last aim is held for up to 300 ms.
- The controller bindings are offered to SteamVR under both names it gives the Frame controllers, the older one and the one SteamVR uses since 2.15.1.
- A dwell click no longer repeats on the same spot every dwell time, and a dwell on an open menu no longer clicks the picture behind it.

## 1.0.1 (October 2026)

### Fixed

- Pressing a volume button on the headset during a stream crashed SteamOS and rebooted the Frame. The launcher hid its window for the stream, which left the compositor with nothing to give keyboard focus to when the volume key arrived. The window now stays open but invisible. `MLXR_LAUNCHER_FADE_WINDOW=0` restores the old behaviour.

## 1.0.0 (October 2026)

The first release of Moonlight XR for Frame, a native game-streaming client for the Valve Steam Frame. It streams from a PC running Sunshine or Apollo into SteamVR.

### Streaming

- Hardware H.264 and HEVC decoding on the Frame's video decoder, up to 2560x1440.
- Refresh rates of 72, 90, 120 and 144 Hz. "Match headset" follows the rate SteamVR runs at, and picking a rate sets SteamVR's per-app rate before the stream starts.
- Frame pacing with a small queue, so an uneven network does not turn into repeated frames.
- Reference frame invalidation, so a lost frame is repaired without waiting for a full new one.
- Surround sound (5.1 and 7.1) rendered for headphones, and an audio buffer that grows only when the network needs it.

### Network

- Streams over Wi-Fi or over the Steam Frame Wireless Adapter. The "Network connection" setting offers Automatic, Wi-Fi and Steam Frame Wireless Adapter.
- Automatic probes both links before each stream and picks the better one, from round-trip time, jitter, failed probes and the losses seen on that link last time.

### The picture

- A flat or curved screen in a black void, in passthrough, or in one of three rooms: Home Theater, Grand Cinema and Synthwave.
- 3D from a 2D stream with a depth model (ZipDepth or MiDaS), with strength and convergence controls.
- Supersampling and sharpening options, glow around the picture, and a movable screen: grab the bar under it and push or pull it with the thumbstick.
- One eye texture per eye for every layer, sized within SteamVR's limits.

### Controls

- Pointer mode: the controller ray is the mouse, the trigger clicks, a thumbstick click right-clicks, and double clicks land on the first click's spot.
- Gamepad mode: the Frame controllers act as an Xbox gamepad. Hold a grip and press Menu or View to switch modes.
- An in-headset keyboard with a compact and a full layout, Hacker's-Keyboard-style modifiers and key repeat. It has its own move bar, so it can be placed anywhere or laid flat.
- Bluetooth keyboards, mice and gamepads connected to the Frame reach the PC, with rumble for gamepads.
- Disconnect without closing the game on the PC: Start+Select+L1+R1 on a gamepad, or Ctrl+Alt+Shift+Q on a keyboard. Exit on the bar ends the game.
- Steam Frame controller models drawn from Valve's own files, matte, with the trigger, grips, sticks and buttons moving as you press them.

### The flat window

- Moonlight's own window for the PC list, pairing and app list, with a Settings page in the style of Artemis: connection, video, 3D, audio, gamepads and Apollo options.
- Apollo features: a virtual display made for the headset, a resolution scale factor, a custom refresh rate and clipboard sync in both directions.
- A Steam shortcut with library art and a dock icon, created by the installer.

### In the headset

- A stats window you can grab and move, with the stream, decoder, network and frame timing.
- A Report a problem sheet that saves your note on the Frame.
- bug-report.sh gathers the logs, your note, the version and a filtered copy of the settings into one zip, with addresses, host names, ids, serial numbers, Wi-Fi names, keys and certificates masked or left out.
