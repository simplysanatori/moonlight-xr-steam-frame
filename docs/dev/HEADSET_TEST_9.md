# Headset test 9 (batch 5: the owner's feedback on the 2026-10-30 dev build)

For the dev build on main after the batch 5 merges (check `app-dev/VERSION`), installed with `tools/frame-deploy.sh install` (the dev copy). The real install is untouched. Part A is the quick look the owner asked for: about 15 minutes in the headset. Part B is optional depth for each change. HEADSET_TEST_7 and 8 still apply and are not repeated. Do not paste addresses, host names or Wi-Fi names from the logs into a report.

Keep the Frame on 5 GHz and the launcher at 2560x1440, 90 fps, 98 Mbps, as in test 8.

# Part A: the quick look (about 15 minutes)

## 1. Taglines and Ko-fi (1 minute)

- Cog, 3D tab: no "harder on the eyes" under Depth; the strong end of the track is still amber.
- Host sheet: no "Your own buttons go in macros.ini". Its "Clipboard, commands" page: the cells have no lines under their names.
- Cog, About, Ko-fi: the button reads "Support simplysanatori on Ko-fi"; the sheet has the title, Open page and Not now only, no QR code. Open page shows ko-fi.com/sana748653.
- First-run card (set `[vr] first_run_shown = false` once): no "Shown once..." line at the foot.

Record: any tagline still there (where), the Ko-fi page that opened.

## 2. Show controllers and the G17 (3 minutes)

- Cog, Display, Show controllers: three cells, Off, Frame, Gun. No Generic.
- Gun: a G17 with the light under the barrel and no spare magazines. Hold it as usual: the grip lies along the controller's handle under your palm. The beam leaves the front of the slide; the dot is straight ahead of the barrel. Pull the trigger slowly in both hands: it swings back and returns.
- Ray row (Off, Dot, Ray) with the cog open: each cell shows at once, without closing the cog.

Record: fit (cm too high, low, forward, back), muzzle on the dot or the offset, trigger right or wrong, any part in bright yellow, too dark in a dark room.

## 3. The bar: no white beam, Bring here near and far (3 minutes)

- Ray on, both controllers in hand, the left held up and pointing at the picture. Move the right ray slowly along the whole bar, under the icons and under the pill. Good: no beam jumps from the left hand across the picture, the bar never blinks out, the PC cursor stays put.
- Head locked off, once in the black void and once in passthrough. The button outermost on the left (a screen, a two-way arrow, a person). Put the screen somewhere of your own (drag it, resize it), then press: it goes to Desk exactly as the cog's Screen tab Desk cell puts it (the Desk cell shows its ring). Press again: it goes back exactly where you had it, wherever you look. On a fresh config (or right after Reset, at Desk) the press from Desk goes to Cinema, as the Cinema cell does. Nothing follows your head, nothing is placed by the bar.

Record: any white beam (and what your hands were doing), Desk and back (or Cinema) right yes or no in the void and in passthrough, icon readable.

## 4. Text (3 minutes)

- A page of small text (Notepad, a browser at 100 %, File Explorer) and a code editor with coloured text. Small text should be darker and crisper than the last build; thin red and blue strokes keep their full colour.
- Cog, Display, Sharpen: Normal, then Quality. Crisper with no halos? Turn your head slowly: does text shimmer more?

Record: better than the last build yes or no, Sharpen off / Normal / Quality preferred (this decides the default), shimmer.

## 5. VR

PC VR was removed in 1.1.0 (SteamVR takes the Frame over and cannot stream beside Moonlight): nothing to test; there is no VR tile and no PC VR button.

## 6. The launcher (1 minute)

- Settings, General: "Check for updates" (on) and "On start" (none, the last PC's Desktop, or an app). Video Settings: "Sharper small text" and "Sharper coloured text" switches. The search finds the new rows.
- PC VR was removed: no PC VR section on the Settings page, no VR tile above a PC's apps.

# Part B: more on each change (optional)

## B1. Text A/B with launch options

`MLXR_PICTURE_FILTER=0 %command%`, then `MLXR_CHROMA_FROM_LUMA=0 %command%` in the dev shortcut's Launch Options (keep `MLXR_RAY_DEBUG=1` in front if it is there). Compare the same page. With Stats on, record `gpu curve` with the defaults and with both at 0. 3D on: both eyes equally sharp; SBS: no colour line at the side edges.

## B2. The G17's cost and your own gun

Both guns shown vs the Frame kind: frame time in vrtiming.log, any stall when switching to Gun. Your own gun: `gun_left.model` + `gun_right.model` in `~/.config/moonlight-xr/models/`: the log says `the gun is your own`; remove one and it says `the gun is the bundled one`.

## B3. Bring here edge cases

Go near, drag or resize a little, press: near again; press once more: Cinema (4 m, 5 m wide). Go near, restart the stream, press: back to your far placement (`[screen] far_distance_cm`, `far_width_cm` in config.ini). Head locked on: head aim's button takes that place. In a room: neither shows.

## B4. Head input (all off by default)

- Head-look: `[vr] head_look = true`, a room or Head locked, a first-person game: turning the head turns the camera; hold a grip, turn back, release: the camera stays. Log `head-look on`.
- opentrack: on the PC, opentrack input "UDP over network" port 4242, output freetrack, Start. `[vr] opentrack = true` (the host defaults to the streaming PC now). The octopus follows the head; record which axes need inverting.
- Head Point mode (accessibility): `[vr] pointer_source = head`, `head_dwell_ms = 800`: a dot follows the middle of the view; resting it closes a ring and clicks once; move away and back to click again. It must never come on unless set.
- `[vr] mouse_mode = relative` in a game that captures the mouse: the camera turns with the pointer instead of the cursor jumping. `back_as_guide = true`: in gamepad mode Back is the Guide button.

## B5. Launcher

- Update check: with a newer release on GitHub the launcher shows its update button with the version; off in General Settings shows nothing.
- On start = the last PC's Desktop: start the launcher, the Desktop stream starts by itself once the PC is online.
- `install.sh --desktop-shortcut`: a "Desktop in VR" shortcut in Steam that starts straight into Desktop.

## Read the logs (at a desk)

```sh
tools/frame-deploy.sh logs out
grep -E "the gun is|controller model ready" out/moonlight.log | tail -6
grep -E "curved picture: (footprint|picture)" out/moonlight.log | tail -3
grep -E "^.*VR: |Asked Steam for PC VR|Switching to VR" out/moonlight.log out/moonlight-launcher.log | tail -10
grep -E "head-look|opentrack:|mouse mode" out/moonlight.log | tail -8
grep -E "update check|On start" out/moonlight-launcher.log | tail -6
```

## Results

| # | Item | Result (fine, or what was wrong) |
|---|---|---|
| 1 | Taglines gone; Ko-fi sheet and page | |
| 2a | Show controllers: Off, Frame, Gun | |
| 2b | G17: fit, muzzle on the dot, trigger, looks | |
| 2c | Ray row live with the cog open | |
| 3a | No white beam near the bar | |
| 3b | Bring here near and far | |
| 4 | Small and coloured text; Sharpen preference | |
| 6 | Launcher rows | |
| B | Anything from Part B | |
