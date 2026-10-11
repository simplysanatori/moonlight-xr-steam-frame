# double-click report (lane double-click, branch lane/double-click)

Base: main 3aa78d3. The owner's run of 2026-10-11: "double clicking was still
slightly difficult" with the Frame controller as the mouse.

Files: `xr/core/xr_pressfix.c/.h` (the pointer's click logic, where the change
is), `xr/xr_prefs.c` (reads the new variable), `xr/xr_input.c` (start-up log
line, comments), `tests/xrcore/test_pressfix.c`, `docs/USER_GUIDE.md`.

## What Windows wants

Windows makes a double click out of two presses of the left button when the
second press comes within the double-click time of the first press (500 ms by
default) and inside the double-click rectangle centred on the first press
(4 x 4 px by default, so about 2 px each way). The host cursor only moves when
we send a position. So the second press has to go down at the first press's
point, within 500 ms of it.

## Why double clicks still failed

The code on main (press stabilisation and a 5 px double-click snap in
`xr_pressfix.c`, the click hold off) had four weak points.

1. **The snap circle was 5 px.** Between the clicks the cursor followed the
   hand. The hand drifts over the 200 to 400 ms between clicks, and the second
   trigger pull jolts the aim again. When the second press was more than 5 px
   from the first click, it was sent at its own point, more than 2 px away,
   and Windows saw two single clicks.
2. **The snap was off after a click that moved.** A click whose aim left the
   3 px press circle while the trigger was held counted as a drag
   (`prevValid = !left`), and no second click snapped to it. The jolt goes on
   after the press point of the trigger, as the finger pulls through to the
   stop, so this happened often.
3. **The release of such a click went at the jolted aim.** Once the aim had
   left the 3 px circle, the release frame sent wherever the let-go jolted the
   aim, a few more pixels away.
4. **The window ran from the release.** Windows counts from the first press.
   This made the snap last longer than Windows' window after a long press,
   which is harmless, but it is now press to press.

Motion between the clicks also moved the host cursor. The snap then put it
back in the same frame as the second press (the position goes before the
button in `dispatchInput`), so this was not the main fault. It did make the
cursor visibly hop back and forth.

The 0.35 s window in `xr_clickhold.c` plays no part: the click hold is off by
default and stays off.

## The change

After a click's release the cursor waits where the release left it, as long as
the aim stays within `MLXR_DOUBLE_CLICK_PX` (now 24 px of the video) of that
point and less than `MLXR_DOUBLE_CLICK_MS` (new, 500) has passed since the
click's press. Nothing is sent to the host during the wait.

- The aim leaving the 24 px circle, or the time running out, ends the wait at
  once and the cursor goes to the aim. Someone who clicks and moves on feels
  nothing: at an ordinary hand speed the cursor follows within two or three
  frames (about 25 ms).
- A press during the wait goes down at exactly the point the first click went
  down at, with no motion in between. It stays there (its release too) until
  the aim leaves the 24 px circle, when it goes live: a double-click drag
  selects by words as before.
- A click whose aim wandered while held (the jolt) still starts the wait,
  unless the aim went further than 24 px from the press point. That is a drag,
  and a drag starts no wait.
- A release after the aim left the 3 px press circle is sent where the cursor
  already is, not at the jolted aim.
- A slow click (held longer than 500 ms) starts no wait: a second press could
  not be a double click anyway.
- Coming back into the circle after the wait has ended does not hold or snap
  again.
- `MLXR_DOUBLE_CLICK_PX=0` or `MLXR_DOUBLE_CLICK_MS=0` turns the wait and the
  snap off. `MLXR_CLICK_HOLD_PX` stays 0 (off), as the owner chose.
  `MLXR_PRESS_STABILISE` is unchanged.

The start of each run logs the values:
`pointer clicks: MLXR_DOUBLE_CLICK_PX=24 MLXR_DOUBLE_CLICK_MS=500 MLXR_PRESS_STABILISE=1 MLXR_CLICK_HOLD_PX=0`.

## Tests

`xrcore_pressfix` (tests/xrcore/test_pressfix.c), new cases:

| Case | Checks |
| --- | --- |
| `testDoubleClickJitter` | 3 px of jitter each way, second press at 400 ms: no motion between the clicks, both presses at one point. With jitter inside 3 px: one point from the first press to the second release. |
| `testDoubleClickDrift` | 20 px of drift, second press at 450 ms: every frame sends the first click's point. |
| `testTwoSingleClicks` | Second click at 600 ms, 13 px away: the wait ends at 500 ms, the second click goes at its own point. |
| `testDragFromPress` | Press, 30 px move, release: motion is sent from 3 px on, the release goes at the drag's end, no wait after it. A double-click drag goes live past 24 px. |
| `testMoveAwayPromptly` | A click, then a move at 1.5 px/ms: the cursor is live 22 ms after the release; coming back within the time does not hold or snap. |
| `testDoubleClickAfterJolt` | The first click's aim leaves the 3 px circle while held: the cursor waits where it was let go, and the second press goes at the first press's point. |
| `testSlowClick` | A press held 580 ms starts no wait; `MLXR_DOUBLE_CLICK_MS=0` turns the wait off. |

Existing cases changed where the behaviour changed on purpose: the cursor now
waits between the clicks of `testDoubleClick`, and a release after the aim left
the press circle goes where the cursor was (`testDrag`, `testStabiliseOff`).
`testEnv` covers `MLXR_DOUBLE_CLICK_MS` (0, 700, negative, over 5000, not a
number).

Full suite: `ctest --test-dir build-double-click -E audio_pulse`, 133 of 133
passed (the two launcher tests skip with `NO_LAUNCHER=1`).

## What the owner should try

1. Double click desktop icons and files in Explorer, at a normal pace and a
   slow one (just under half a second). Each should open on the first try.
2. Select text by dragging in a browser or editor: press, drag across a few
   words, release. The selection should start at the press point and follow
   the ray from the first few pixels on.
3. A slow single click, then move away: hold the trigger for about a second,
   release, move to something else. Nothing should feel stuck.
4. A quick single click, then move on at once: the cursor should follow
   straight away.
5. Two different small buttons next to each other (under 24 px apart), clicked
   quickly one after the other: see the risk below.

If double clicks are now fine but the cursor feels held after clicks,
`MLXR_DOUBLE_CLICK_PX=12` makes the circle smaller. If the PC's double-click
speed has been changed in Windows, set `MLXR_DOUBLE_CLICK_MS` to match.

## Risks

- Two different targets less than 24 px apart, clicked within half a second,
  become a double click on the first one. Small toolbar icons at a high
  resolution can be that close. `MLXR_DOUBLE_CLICK_PX` lower is the remedy.
- For up to half a second after a click, slow aiming that stays within 24 px
  sends nothing. When the wait ends the cursor jumps to the aim (at most
  24 px). The ray's dot waits with the cursor, so what is seen matches the
  host.
- 24 px is in video pixels. At a stream resolution under the PC's desktop
  resolution, it is more desktop pixels than that.
- Some programs count double clicks themselves and may also look at motion.
  With no motion sent between the clicks, they see what Windows sees.
- Checked only by the unit tests, not on the headset.
