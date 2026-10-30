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
