# The Host sheet, macros, sticky modifiers and media keys

PLAN.md Wave 4b rows 4b.1, 4b.2 (data half), 4b.13 and 3.11. User side: USER_GUIDE "Host: keys for the PC".

## Where things are

| What | File |
| --- | --- |
| Quick actions, `macros.ini` parser, sequence runner, "Send keys" chord capture, the sheet's cell and zone geometry | `xr/core/xr_hostkeys.[ch]` (pure; `tests/xrcore/test_hostkeys.c`) |
| The sheet's art and hits (`UI_PANEL_HOST`, `UI_HIT_HOST_CELL`, `UI_BTN_HOST_*`) | `ui/panel_host.c`, `UiView.host_*` in `ui/panels.h` |
| Context side: pose, open/close, press, one key event a frame | `xr/xr_host.[ch]`; fields `host*`, `macros`, `hostRun`, `hostChord` in `XrCtx` |
| Layer and panel drawing | `addHostLayers` in `xr/xr_layers.c` (`FADE_HOST`), `updateHost` in `xr/xr_panels.c` |
| Media keys row | `kbMedia*` in `xr/core/xr_keys.[ch]`; Compact keys 44..49 (`ui/panel_keyboard.c`), Full keys `KBF_MEDIA_FIRST`.. (`xr/core/xr_kbfull.c`) |
| Sticky modifiers on Compact | `KbSticky` in `xr/core/xr_keys.[ch]`; `compactSticky` and `xrKeyboardPress` in `xr/xr_keyboard.c` |
| New key flags for the host path | `KBF_FLAG_CHORD/HOLD/LET` in `xr/core/xr_kbfull.h`, handled in `stream/vr_keyboard.c` |

## The PC page (lane wave4-apollo: PLAN 4.6, 4b.4, 4b.5)

A second page of the same sheet, reached by `XR_HOST_ZONE_PAGE` (a header button left of PC VR; the label flips to Back). The header (Close, PC VR, the page button) is the same on both pages, so `xrHostPress` handles PC VR and its confirmation before it hands a PC page press to `pcPress`. `ctx->hostPage` is the page showing, `xrHostOpen` starts on the main one. Geometry and cell kinds are in the "PC page" block at the end of `xr/core/xr_hostkeys.[ch]` (`xrHostZoneAtPage`, `xrHostPcCellKind`), so it stays apart from the main page's entries: cells 0 and 1 are Paste and Send text, cells 5..12 are the host's commands. A command cell always asks first (the same confirmation box; `UiView.host_confirm_cmd` makes it say Run and name the command).

| What | Where |
| --- | --- |
| What the host offers (`XrHostInfo`: Apollo or not, clipboard read/set, command names, why none) | filled by `hostInfoFrom` in `app/main.c` from serverinfo (`mlxr_clipboard_gate`, `mlxr_server_cmd_gate` in `backend/hostcmd.c`) when a stream starts, `xr_app_set_host_info` (any thread) to `ctx->hostInfo` |
| A press on the page | `pcPress` in `xr/xr_host.c` sets `ctx->hostActKind` (`XR_HOSTACT_PASTE`, `_COPY` with `hostActText`, `_CMD` with `hostActArg`); `dispatchInput` in `xr/xr_session.c` hands it once to `XrAppHooks.hostAction` (or toasts "Not streaming") |
| The app's side | `hostActionHook` in `app/main.c`: a command goes at once (`stream_exec_server_cmd`); the two clipboard calls run on a thread each over `mlxr_server_clone` of the stream's server handle, one at a time. Results come back as `xr_app_notice` toasts |
| Paste from PC | `mlxr_server_get_clipboard`, then `vrkb_type_text` (stream/vr_keyboard.c) on a local `VrKeyboard`: the keyboard's own text sink, pieces of 64 bytes between characters, Enter and Tab as keys, at most 4096 bytes |
| Send text to PC | `XrHostCompose` (`xr_hostkeys.c`): the keyboard is forced to Compact/QWERTY like for the report sheet (`textEntry` in `xr/xr_keyboard.c`), the characters the input pass emits in `IN_KEY` are taken by `xrHostComposeFeed` in `dispatchInput` instead of `hk->key` (and no modifiers are held on the host meanwhile), Enter makes `XR_HOSTACT_COPY`, `mlxr_server_set_clipboard` does the upload. The space bar shows the tail (`UiView.kb_compose_text`) |

The report sheet's note field is the only other text field there is; there is no field on the VR keyboard itself, so "Paste from PC" cannot insert into one: it types on the PC. `xr_input.c` is untouched, which is why there are no `app:paste_pc` / `app:copy_pc` binding actions: `bindAppCommand` there is the only dispatcher for `app:` codes, so adding them needs its `switch` extended (two cases that set `hostActKind` as `pcPress` does) and `APP_NAMES` in `xr/core/xr_bind.c`.

### Server commands on the wire

Apollo runs a command when it gets a reliable 4-byte control packet `{index, 0, 0, 0}` of type `0x3000` on the encrypted control stream (ClassicOldSong's `LiSendExecServerCmd`; types `0x3001` set clipboard and `0x3002` file transfer nonce are in the same table). The vendored moonlight-common-c is upstream's and has none of it, and only `ControlStream.c` can send on that stream, so `stream/CMakeLists.txt` builds that one file through a generated wrapper that includes it and then `stream/commonc_servercmd.inc`, which defines `mlxrSendExecServerCmd` (declared in `stream/commonc_ext.h`). The submodule is not touched; configure stops with a message if upstream renames what the `.inc` uses. `index` is the command's place in serverinfo's `<ServerCommand>` list (the order Apollo gave). The host sends no answer. `tests/stream/test_servercmd.c` holds the packet's shape and that nothing is sent without a connection; `tests/backend` has the list and the gating against the fake Apollo (`more_cmds`, `cmd_denied`, `clipboard_404`).

## `macros.ini`

`app:macro:<name>` is **not** reserved by the binding core (`docs/dev/bindings.md` lists `keyboard ray recentre cog 3d gamepad bringhere`), so the macros have their own file: `$MLXR_CONFIG_DIR/macros.ini`, else `~/.config/moonlight-xr/macros.ini`. Nothing is written on first run. When `app:macro` is added to `bindings.ini`, `xrMacrosParse` is the thing to call for the names; the `[macros]` section here uses the same key syntax (`xrBindActionParse("key:...")`) so the two can merge.

```
[macros]                       ; optional heading
name = step step, step ...     ; steps are separated by spaces or commas
```

- Step `mods+key`: tap (`ctrl+shift+esc`, `esc`, `volumeup`, `0x41`), a `key:` prefix is allowed.
- `down:<key>` / `up:<key>`: one key, no modifiers in front. A modifier key holds or lets go of that modifier for the taps after it; the run lets go of whatever it still holds when it ends, is aborted or is replaced.
- `wait:<ms>`: 1 to 5000.
- A `!` as the first character of the first step makes the macro ask first.
- Comments: a line starting `#` or `;`, or ` ;` / ` #` after the steps. UTF-8 BOM and `\r\n` are fine. Names are at most 27 characters, case blind, the first of two with one name stands. At most 16 macros and 32 steps, the sheet has room for 8.
- Errors: `line N: why`, a bad line is left out, `XrMacros.errors` counts them and `.error` has the first. `xrHostLoadMacros` logs both.

## How a key reaches the host

One event a frame goes into `out[IN_KEY]` / `out[IN_KEY_MODS]` (the key slot the keyboards use), then `keyHook` -> `vrkb_type`:

| Emit | `IN_KEY_MODS` | `vrkb_type` does |
| --- | --- | --- |
| tap of `vk` with `mods` | `mods` plus `KBF_FLAG_CHORD` | holds `mods`, presses `vk`, lets go of the modifiers it brought (it never leaves one held, so no `IN_KB_MODS` bookkeeping is needed) |
| `DOWN` / `UP` | `mods` plus `KBF_FLAG_DOWN` or `KBF_FLAG_UP` | the Full keyboard's path |
| `HOLD` / `LET` | `bits` plus `KBF_FLAG_HOLD` or `KBF_FLAG_LET` | puts modifiers down / up on the host, no key |

`xrHostFrame` is called from the top of `xrKeyboardFrame` (end of each input pass), so macro playback needs no hook in `xr_input.c`.

## Media keys

Six keys under the main block on both keyboards, every sheet, every map: previous `0xB1`, play/pause `0xB3`, next `0xB0`, mute `0xAD`, volume down `0xAE`, volume up `0xAF`, sent as `KB_CODE_VK + vk`. The keyboard panel is now `KB_TEX_H` 512 tall; the main block keeps its pixel layout in the first `KB_MAIN_TEX_H` 448 rows (`kbMainV`). The atlas slot moved to the bottom of the atlas (`UI_ATLAS_H` 3472); the Host sheet took its old place. Whether the Windows host passes the media VKs needs the headset check (the host may need them as extended keys: `vkExtended` in `app/main.c` does not list them).

## Sticky modifiers on Compact

`kbPress` (tap = lit for the next key, a second tap puts it out) is unchanged. `KbSticky` adds the lock: `xrKeyboardPress` calls `kbStickyPress` (a tap on a locked modifier lets it go; a press on a modifier starts its hold clock, with the hand), `compactSticky` in `xrKeyboardFrame` ticks it with the Full keyboard's hold time (`keyboard_hold_ms`), then re-lights the locked bits in `kbMods` and keeps the capitals sheet for a locked Shift. `kbMods` goes to the host as it always did (`IN_KB_MODS`), so a locked Ctrl is held down on the PC.

## Wiring (done in lane wave2-bind-wire)

The bar button, the press branch and `app:host` are in. What was asked for, as built:

1. **A bar button.** `BAR_SLOT_HOST` in `xr/core/xr_layout.[ch]` (`BAR_SLOTS` 9, its `OUT`/`LEFT` placement entries), a `BTN_CELL_HOST` face in `ui/icons.c` and `panel_bar.c`'s `FACE_CELL`, a `HOVER_HOSTBUTTON` in `furnitureHover`'s `OUTER` list, `addBarButton` in `xr_layers.c`, and `barSlotShown` in `xr_ui.c`.
2. **A press branch.** On the button: `xrHostOpen(ctx)`. A modal branch next to `updateKofiSheet`: project the ray on `ctx->hostPose` (`screenProject` as `updateExitPrompt` does), set `ctx->hostHoverZone = xrHostZone(ctx, u, v)`, on `ctx->triggerEdge[h]` call `xrHostPress(ctx, u, v, &tick)` (set `ctx->clickPending = tick`, `swallowTrigger`), and a press off the sheet calls `xrHostClose`. Add `hostOpen` to `panelUp`, to the places that list `kofiOpen` (`xr_input.c`) and to `closeAllPanels`-style resets.
3. A binding action `app:host` is the natural second way in (PLAN 4b.2), once the binding integration lane lands.

## PC VR hand-off: what to confirm on the Frame

**VR** (lane pcvr-steamvr) is the first entry of every PC's app list, in the headset (`LAUNCH_APP_VR` in `app/launcher.c`, never sent to the host) and in the flat launcher's app grid (a tile before the apps, `launcher/patches/0018-*`). It and the Host sheet's PC VR button ("Switch to VR? The desktop closes.") do the same: end what runs on the PC (Moonlight's Quit Session; skipped when nothing runs), ask the PC until it reports no running app (every 0.5 s, 10 s at most; the log says `VR: <PC> reports no running app` or that it gave up and asks Steam anyway), then have Steam stream SteamVR itself from that PC to the headset, so Steam Link takes over in SteamVR Home.

Who asks Steam: the flat launcher's grid tile does it all itself (`MlxrHandoff::startVr`: `NvHTTP` serverinfo and `quitApp` on a thread, then `startPcvr(<PC name>)`). The headset's own list and the Host sheet go through the client's worker (`vrHandOff` in `app/main.c`): started by the flat launcher (`--exit-to-caller`) the client ends with exit code 12 (`EXIT_PCVR`) and the launcher runs `startPcvr` with the stream's PC name; started on its own it runs `python3 -I <its dir>/steam_cef.py --pcvr --pc <PC name>` itself (`app/vr_handoff.c`, output in `$MLXR_LOG_DIR/pcvr-handoff.log`) and quits; with neither (no `steam_cef.py`) it says "PC VR needs the Moonlight XR launcher" and leaves the stream alone. `startPcvr` runs `steam_cef.py --pcvr --pc <PC name> [--client <Settings' PC>] --confirm 10`, which sends Steam's own client a JavaScript call over the CEF debugging port.

What `--pcvr` asks Steam (`plan_pcvr` in `packaging/steam_cef.py`, unit-tested in `tests/tools/test_steam_cef.py`), and the `"path"` its JSON reports. The PC is the one chosen on the Settings page, else Steam's paired PC whose name is the Moonlight PC's name (any case), else the only paired PC:

| Situation | Call | `path` |
| --- | --- | --- |
| A PC found as above | `SteamClient.Apps.StreamGame(250820, "<clientId>", 0)` (SteamVR; `--app` names another app, for the command line only) | `stream` |
| `StreamGame` throws (missing, or a changed form) | then `ExecuteSteamURL('steam://nav/games/details/250820')`: SteamVR's page, Stream one press away | `app-page` |
| Several PCs paired, none named like the Moonlight PC, none chosen | `ExecuteSteamURL('steam://nav/games/details/250820')` | `app-page` |
| No paired PC found (or the device list call fails) | `ExecuteSteamURL('steam://nav/games')`, the last way out | `library` |

SteamVR is a Tool, so `--apps` (the library's `appStore.allApps`) may not list it; it does not need to be listed: the call names 250820 directly. `steam://rungameid/250820` is deliberately not used: on the Frame SteamVR is the local runtime, and that URL would start or restart it on the headset rather than stream it from the PC. `--confirm N` then reads the device list for N seconds and reports `"streaming"` for the chosen PC (from `unStreamingSessionID`); a broken connection then (Steam switching to the stream) is `"streaming": null` with `"confirmError"`, not a failure. The launcher logs `Asked Steam for PC VR: <path> (<reason>); Steam reports the PC streaming|not streaming yet` with the whole JSON.

**Before the owner tries it (overseer, over SSH, once, in the installed app folder):**
- Dry run, ends nothing and starts nothing; reads the PC's serverinfo and Steam's paired PCs only: `env -u LD_LIBRARY_PATH QT_QPA_PLATFORM=offscreen MLXR_LAUNCHER_DRYRUN=1 MLXR_LAUNCHER_SELFTEST="vr:<PC name>" ./moonlight-xr-launcher` (the binary, not the `.sh` wrapper, which would rotate `moonlight-launcher.log`). Expected: `MLXR_VR: would quit app <id> on <PC>` when something runs there (nothing when not), `MLXR_PCVR: python3 -I steam_cef.py --pcvr --pc <PC> --confirm 10`, and `SELFTEST: vr steam rc=0 {... "path": "stream", "reason": "the paired PC named <PC>", "js": "SteamClient.Apps.StreamGame(250820, \"<clientId>\", 0)", "dryRun": true}`. The Steam half alone: `python3 -I steam_cef.py --pcvr --pc "<PC name>" --dry-run`.
- Live, with the owner in the headset (this closes what runs on the PC and switches the headset to Steam Link): the same launcher line without `MLXR_LAUNCHER_DRYRUN=1` (this is exactly what the grid's VR tile does), or only Steam's half, `python3 -I steam_cef.py --pcvr --pc "<PC name>" --confirm 12; echo rc=$?`. Expected: `rc=0`, `"path": "stream"`, `"tried"` with one entry and `"error": null`, and `"streaming": true` once the PC reports the session (`false` or `null` with the headset in SteamVR Home is still a success: record it). `"path": "app-page"` with an error in `"tried"` means Steam refused `StreamGame`: paste the line.

The Settings page's PC row (PLAN 4.2) uses `--devices` (`--apps` stays for the command line); the preflight (PLAN 4.9) is `--probe`. The names and signatures are the community-documented ones (decky-frontend-lib typings), checked against a fake endpoint in `tests/tools`; on the Frame's build (probe of 2026-10-30) `StreamGame`, `RunGame`, `RegisterForDevicesChanges`, `ExecuteSteamURL` and `ShowDashboard` exist, `--devices` finds the PC and `--apps` lists its games with the VR flag. The queries below are run over SSH on the headset, in the installed app folder (where `steam_cef.py` sits), with Steam running in Game Mode, in order. Every one only reads: none starts a stream, runs a game or changes a setting. (`STEAM_CEF_PORT=<port>` if SteamOS moved the port.)

1. **The port and the calls exist.** `python3 -I steam_cef.py --probe; echo rc=$?`
   Expected: one JSON line, `"reachable": true, "context": true, "ok": true`, `"api"` with `"StreamGame": "function"` and `"RegisterForDevicesChanges": "function"` (these two are required), and `"RunGame"`, `"ExecuteSteamURL"`, `"ShowDashboard"` each `"function"` or `"missing"`; `userAgent` names the Steam client; `hint` is "Steam's debugging port is fine."; `rc=0`. `rc=3` is the port not answering (check `~/.steam/steam/.cef-enable-remote-debugging`, then restart Steam), `rc=4` is a required call missing (paste the whole line into the report: it is the finding).
2. **The names as the Frame's CEF exposes them.** Read-only `typeof` queries, one each:
   - `python3 -I steam_cef.py 'typeof SteamClient.Apps.StreamGame'` expect `"function"`
   - `python3 -I steam_cef.py 'typeof SteamClient.Apps.RunGame'` expect `"function"`
   - `python3 -I steam_cef.py 'typeof SteamClient.OpenVR.VROverlay.ShowDashboard'` expect `"function"`
   - Their arity, to compare with what the hand-off passes (`StreamGame(appId, "clientId", 0)`, `RunGame("appId", "", -1, 100)`, `ShowDashboard('valve.steam.desktopgame.<appid>')`): `python3 -I steam_cef.py '[SteamClient.Apps.StreamGame.length, SteamClient.Apps.RunGame.length, SteamClient.OpenVR.VROverlay.ShowDashboard.length]'` expect three small numbers, about `[3,4,1]` (a wrapper may report 0: then the numbers say nothing and the headset test decides).
   - If a name is `"undefined"`, find where it lives: `python3 -I steam_cef.py 'Object.keys(SteamClient.Apps).filter(function(k){return /stream|run/i.test(k)})'` and the same for `SteamClient.RemotePlay`, `SteamClient.OpenVR.VROverlay`; the right name goes into `API_CALLS` and the `js_*` builders in `steam_cef.py`.
3. **The device list.** `python3 -I steam_cef.py --devices --seconds 6`
   Expected: a JSON list with one object per paired Remote Play PC, `[{"clientId": "<digits>", "clientName": "<PC name>", "streaming": false}]`, the name matching the PC's name in Steam's own Settings > Remote Play. `[]` with the PC paired and awake means the callback's shape differs: look at the raw one with `python3 -I steam_cef.py 'new Promise(function(r){var x=SteamClient.RemotePlay.RegisterForDevicesChanges(function(d){r(d)});setTimeout(function(){r("none")},5000)})'` and report it (`parse_devices` reads `clientId`/`clientName`, also `clientid`/`client_id`/`name`).
4. **The app list.** `python3 -I steam_cef.py --apps --client <clientId from step 3>`
   Expected: a JSON list `[{"appid": N, "name": "...", "vr": true|false|null}]` of the games installed on that PC, VR ones first. `vr` is `null` for every row when the build gives no store categories (53 VR Supported, 54 VR Only): then the picker shows them unmarked, which is acceptable. `[]` or an `"the app store: ..."` error means `window.appStore.allApps` / `per_client_data` is not what this build calls them: report the output of `python3 -I steam_cef.py 'Object.keys(appStore.allApps[0] || {})'`.
5. **Whether a stream runs.** `SteamClient.Apps.GetRunningApps` does not exist on the Frame's Steam build (probe of 2026-10-30: the `Apps` keys matching running/launch are `CancelLaunch`, `SetAppLaunchOptions`, `GetLaunchOptionsForApp`, `SetShortcutLaunchOptions`, `LaunchNonSteamApp`), so there is no running-app list to read. What does show a Valve stream is the Remote Play device list: `python3 -I steam_cef.py --devices --seconds 6` prints `"streaming": true` for the PC while a session runs (`unStreamingSessionID` non-zero; `false` with nothing streaming, as on 2026-10-30). Run it once during the live test above and record what it says. Whether Steam starts the stream while this app's own shortcut is the running game is only answerable in the headset; the hand-off runs after the client has exited. If Steam refuses `StreamGame`, `moonlight-launcher.log` has the `Asked Steam for PC VR:` line with `app-page` and the reason.
