# keyboard lane report (lane/keyboard)

Base main 0ec47c6. Two features: a Full (tenkeyless) keyboard switchable from the
Compact one, and the gamepad disconnect chord. Nothing here has been seen in the
headset (headless tests and snapshots only).

## What changed where

| File | What |
| --- | --- |
| `xr/core/xr_kbfull.h/.c` (new, pure) | The 87-key TKL table (label, shifted label, Windows VK, extended flag, rectangle in key units; Backspace 2, Tab 1.5, Caps 1.75, Enter 2.25, Shift 2.25/2.75, Space 6.25, inverted-T arrows, Ins/Home/PgUp and Del/End/PgDn clusters) plus a "Compact" layout key where the numpad would end. `KbFull` state machine: `kbfPress/kbfRelease/kbfTick/kbfReleaseAll`; modifier bits `KBF_LSHIFT..KBF_RWIN` (left Ctrl/Alt/Win equal the old `KB_MOD_*`), `kbfLabel` (shifted symbol under Shift, capital under Shift xor Caps), `kbfModifierByte`. |
| `xr/core/xr_padexit.h/.c` (new, pure) | Chord detector: Start + Back + LT and RT >= 204 (80 %); fires on the completing frame; re-arms only when Start, Back are up and both triggers under 128 (50 %). |
| `stream/vr_keyboard.c/.h` | `vrkb_hold` now handles Shift, Ctrl, Alt, Win both sides (VK A0..A5, 5B, 5C; modifier byte from `kbfModifierByte`); `vrkb_type` takes `KBF_FLAG_DOWN` / `KBF_FLAG_UP` in `mods` for one key event (so a key can be held and repeat); `heldKey` is released by `vrkb_release_all` (stream end already calls it). Compact paths are byte-for-byte as before (m4_vrkb unchanged). |
| `ui/panel_keyboard.c`, `ui/panels.h`, `ui/ui_strings.h` | `panelFull` draws the Full keyboard in the existing key style (lit = white cap, locked = blue cap + underline + dot, Caps = lit + green dot, arrows as shapes, labels shrink to fit); Compact gets a small "Full" chip in the half-key gap right of the home row (only when `UiView.kb_chip`, which the headset sets; the 44 keys and their rectangles are untouched). New `UiView` fields `kb_layout, kb_oneshot, kb_locked, kb_caps, kb_chip`, `UI_BTN_KB_LAYOUT`, `ui_kb_layout_chip_rect`, strings "Full", "Compact", "Disconnected: Start+Select+L1+R1". |
| `ui/settings.c/.h` | `[vr] keyboard_layout = compact\|full` (default compact), `keyboard_hold_ms = 500` (200..1500, the long-press lock time). |
| `xr/xr_keyboard.h/.c` (new) | The input-side glue: `xrKeyboardFillTables` (kbKeyRects/kbCodes/kbKeyCount for the layout showing; Compact's chip is a 45th hit at `KB_KEY_LAYOUT`), `xrKeyboardPress`, `xrKeyboardFrame` (trigger release, long-press lock, repeat, release-all), `xrKeyboardSync`, `xrKeyboardSetLayout` (asks `IN_SETTING = SETTING_KEYBOARD_LAYOUT (61)` to be stored). While the report sheet is up the layout showing is Compact (its fields take characters); the choice is kept. |
| `xr/xr_input.c` | 3 small edits: `pressKey` first asks `xrKeyboardPress`; `xrUpdateInput` calls `xrKeyboardSync` after the slots are cleared; `handBack` calls `xrKeyboardFrame` before it writes `IN_KB_MODS`. `kbKeyAt` is not touched in xr_input. |
| `xr/xr_ui.c` | `kbKeyAt` also finds the Compact chip; `kbPanelPose` hangs the Full keyboard 1.2x larger (same texture, same hit UVs). |
| `xr/xr_panels.c` | table fill replaced by `xrKeyboardFillTables`; `buildView` passes layout/modifier state; the keyboard redraw key includes them. |
| `xr/xr_prefs.c` | `kbfInit`, `keyboard_layout` and the hold time in `xrApplySettings`, back in `xrSettingsFromCtx`, `SETTING_KEYBOARD_LAYOUT` case first in `xrStoreSetting`. |
| `xr/xr_renderer.h` | `KB_MAX_KEYS` 64 to 96, `KB_KEY_LAYOUT`, ctx fields `kbLayout, kbLayoutShown, kbFull`. |
| `xr/xr_pad_bridge.c/.h` | `xrPadBridgeSetDisconnectHook(fn, user)`; on the chord frame the host gets the pad at rest (`padOutFrame` with `padRest`) and then `fn(user)` is called once, outside the bridge lock; the pad stays at rest until the chord is let go. Log line: "gamepad: Disconnected: Start+Select+L1+R1 (disconnect your session)". `xrPadBridgeStartWithSink` for tests. |
| `app/launcher.c/.h` | `launcher_disconnect`: from a stream, state STOPPING, toast "Disconnected: Start+Select+L1+R1", `LCMD_STOP` with `quitHost 0` (then the existing `launcher_stream_ended` brings back the PC's app list); not in a stream: nothing; file mode: same as exit. |
| `xr/CMakeLists.txt`, `xr/core/CMakeLists.txt` | the new sources, `tests/keyboard`. |
| `tests/keyboard/` (new) | see below. |

## How the Full keyboard reaches the host (no hook needed in main.c)

Full keys use the key slot the Compact keyboard already uses: `IN_KEY = KB_CODE_VK + vk`, `IN_KEY_MODS = down modifiers | KBF_FLAG_DOWN` (press and each repeat) or `KBF_FLAG_UP` (trigger let go), delivered by `xr_session` to `keyHook` -> `vrkb_type`. The modifiers down are `IN_KB_MODS` -> `modifiersHook` -> `vrkb_hold` as before. Session order is key, then modifiers, so a one-shot Shift goes up after the key's up. The wire carries Windows VKs only (LiSendKeyboardEvent has no extended bit; Sunshine derives scan code and extended flag from the VK), so `ext` in the table is the layout's record, tested, not sent.

Behaviours: tap a modifier = lit one-shot and down on the host at once; next non-modifier key goes with it, the modifier comes up after that key's up; tap a lit one-shot = cancel; hold >= `keyboard_hold_ms` = locked (blue, underline) until tapped; Ctrl+Alt+Del, Win+R, Ctrl+Shift+Esc are tested as host event sequences. Normal keys go down on the trigger, repeat after 0.45 s at about 30/s, come up on release (a held key keeps going when the ray slides off it). Caps Lock sends VK 14 down/up and toggles the lit state; it does not repeat and is kept when the keyboard closes (the host keeps its Caps state, there is no way to read it back, so the light follows our own presses). Everything held (key, one-shot, locked) is let go when the keyboard closes (Hide, press on empty space, gamepad mode), focus is lost, the launcher list is up (stream ended) or the report sheet opens; the stream stopping also calls `vrkb_release_all`.

## Lines for app/main.c (orchestrator)

Keyboard: nothing. Chord: three additions.

```c
#include "xr_pad_bridge.h"          // with the other includes

// next to uiHook: the gamepad chord Start+Select+L1+R1 (frame thread)
static void padDisconnectHook(void* user) {
    (void)user;
    pthread_mutex_lock(&g_lock);
    MLOG_I("disconnect chord in %s: leaving the stream, the host's session stays", launcher_state_name(g_launch.state));
    launcher_disconnect(&g_launch);
    pthread_cond_signal(&g_wake);
    pthread_mutex_unlock(&g_lock);
}

// in main(), after vrkb_init(&g_vrkb, sink);
xrPadBridgeSetDisconnectHook(padDisconnectHook, NULL);
```
The toast comes from `g_launch.notice`, which `uiHook` already shows on the next frame.

## Tests (`tests/keyboard`, ctest `keyboard_*`)

`build-kb`: `ctest -E audio_pulse` 90 of 90 pass (the 85 existing + 5 new).

| ctest | checks | covers |
| --- | --- | --- |
| `keyboard_layout` | 8284 | every TKL key once with its VK and extended flag, 8 modifiers, geometry (no overlaps, Backspace/Tab/Caps/Enter/Shift/Space proportions, stagger, inverted T), shifted labels (Shift, Caps, both) |
| `keyboard_rules` | 54 | tap/one-shot/cancel, long-press lock/unlock, hold-time threshold and setting, Ctrl+Alt+Del, Win+R, Ctrl+Shift+Esc (right-hand keys), Shift+Ctrl one-shot pair, locked + one-shot mix, auto-repeat timing (0.45 s, 28-31/s), repeat carries locked modifiers, Caps, nothing sticks (leave with key + one-shot + lock held, stream end), second press ignored, Compact sequences unchanged. All as host event logs through a fake sink |
| `keyboard_input` | 1411 | `kbKeyAt` for every key of both layouts and every Compact sheet (centre, both corners, a ray through `screenProject` from a hand, the drawn panel's hit at the same point), the Compact chip, layout flip from either layout, `IN_SETTING` -> `xrStoreSetting` -> config.ini text -> load -> `xrApplySettings`, panel size, key events and repeats from `xrKeyboardFrame`, one-shot Ctrl in `IN_KB_MODS`, locked Shift, Caps, release on close / focus loss / launcher up / report sheet, report forces Compact |
| `keyboard_chord` | 62 | detector (Start+Select alone, triggers alone, three of four, 203 vs 204 thresholds, edge, no re-fire until all let go, trigger wobble) and the bridge (rest packet first then the hook, rest while held, re-fire after release, no hook set, hook inside the bridge call path, stopped bridge) |
| `keyboard_launcher` | 10 | `launcher_disconnect` pushes `LCMD_STOP` with `quitHost 0`, toast text, then the app list; exit still `quitHost 1`; idempotent; nothing outside a stream; file mode quits |

Snapshots: `ninja -C build-kb kb-snapshots` (also run by `ui-snapshots`) writes
`build-kb/ui-snapshots/kb-*.png`: `kb-compact-chip`, `kb-compact-fn-chip`, `kb-full-plain`, `kb-full-shift-oneshot`, `kb-full-rshift-win-oneshot`, `kb-full-ctrl-locked-shift`, `kb-full-caps`, `kb-full-caps-shift`, `kb-full-in-space` (ray on E, press swell). Inspected with the Read tool; found and fixed a "@" label drawn as an arrow, and "Home/PgUp/PrtSc" being ellipsized.

## Deviations and limits

* Compact is unchanged (44 keys, same rectangles, the four sheets, existing tests untouched); its switch is a "Full" chip in the free half-key gap beside the home row (outside the key table, so `ui_panels` key counts still hold). The chip gets no hover ring: `xr_layers.c`'s ring guard is `kbHoverKey < kbKeyCount` and I did not edit that file (the click tick still plays). One line in `addKeyboardLayers` (`|| ctx->kbHoverKey == KB_KEY_LAYOUT`) adds it if wanted.
* Full is drawn into the same 1120x448 atlas slot (keys 59x65 px) and hangs 1.2x larger in space (`KB_FULL_SCALE`).
* Single pointer: the existing input picks one pointing hand, so a modifier cannot be held with one hand while another presses a key; one-shot/lock is the way to chord (as Hacker's Keyboard on a phone).
* Caps Lock light follows our presses; a host that already has Caps on will show the opposite until Caps is pressed once.
* A Win one-shot cancelled without a key is a Win down/up on the host (opens Start), same as tapping the key.
* While the report sheet is up the keyboard is Compact (its fields take characters); the Full choice returns afterwards.

## Headset test script

1. Stream a desktop, open the keyboard from the bar: the Compact keyboard is as before, with a small "Full" chip to the right of "l". Press it: the Full keyboard appears (a little larger), Esc..Pause on top, arrows in the inverted T.
2. Type in a text field: letters, digits, `;`, `'`, Enter, Backspace. Hold a letter: after about half a second it repeats, fast, and stops when you let go of the trigger.
3. Tap Shift: it lights and labels change (`!@#`, capitals); the next key is a capital/symbol and Shift lets go. Tap Shift, tap Shift again: cancelled. Hold Shift for a second: blue with an underline (locked); type; tap it to unlock.
4. Caps: tap, green dot, letters capital on the PC; tap again.
5. Ctrl then C / V (copy, paste); Win then R (Run dialog); Ctrl, Alt, Del; Ctrl+Shift+Esc (Task Manager). Try right-hand Ctrl/Alt/Win.
6. Lock Ctrl, close the keyboard with Hide (or press empty space): on the PC Ctrl must not be stuck (click a link: no new tab).
7. Press "Compact" (right of Enter): Compact returns. Quit and relaunch the app: it opens the layout you last used (`keyboard_layout` in config.ini).
8. Gamepad mode (controllers as a pad): hold Start (right menu) + Back (left view) + both bumpers (L1, R1; owner 2026-10-10, as Moonlight): the stream drops, a toast "Disconnected: Start+Select+L1+R1", the app list shows the PC with the game still running; open it again to resume. Start+Select alone, or the triggers alone, do nothing.

## Added 2026-10-10: Ctrl+Alt+Shift+Q
Moonlight's own keyboard disconnect combo (moonlight-qt `KeyComboQuit`, any side's Ctrl, Alt and Shift with Q, matched by scancode) on a physical keyboard while it is forwarding: the Q is not sent, every held key is released on the host, then `launcher_disconnect_keys` leaves the stream with the host's session kept (exit code 11 to the flat UI), toast "Disconnected: Ctrl+Alt+Shift+Q". The headset keyboard does not take it (stock has no on-screen keyboard). Headset check: stream Desktop, hold Ctrl+Alt+Shift on the physical keyboard and press Q: the stream drops, the toast shows, and the flat UI offers Resume.
