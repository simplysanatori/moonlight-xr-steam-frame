# Lane launcher-return: the flat window after a stream

## The report
Owner's run 2026-10-11, 06:20 to 06:23: the stream ended with the in-VR Exit button and the VR client exited with code 0. The flat Moonlight window did not come back in the headset. The launcher kept running until the owner closed the app from Steam about 40 s later. In the run before it (05:33, stream ended the same way after 46 s) the window came back and a second stream was started from it. The same symptom was reported once on 2026-10-10 ("the flat UI did not return after the second exit"); the three ShowDashboard requests added then (0, 1000, 2500 ms) did not help here.

## Evidence
From `moonlight-launcher.log` of both runs, `moonlight-console.log`, the VR client's `moonlight.log` and `vrtiming.log` (SteamVR's per-second timing for the whole launcher run). Only lines without personal values are quoted.

1. **The launcher did the same in both runs.** After "The VR client exited with 0" both logs have the same lines in the same order, within the same seconds: the gamepad mappings loaded again ("Loaded 436 new gamepad mappings", from `SdlGamepadKeyNavigation.enable()`, the first statement of stock's `sessionFinished` handler, which pops the segue and sets `window.visible = true` right after it), host polling started again (patch 0003's restart in the same handler), and the three "Asked Steam to show the flat screen again after 0/1000/2500 ms: done" with Steam answering `null`. Nothing in the launcher's own lines tells the two runs apart.
2. **The "Destroying swapchain" line belongs to the app close, not to the return.** Gamescope WSI lines carry no time. The log file was last written at 06:24:31.9, about 41 s after the VR client exited (06:23:50.8 in its own log). The wrapper writes nothing after the launcher ends, and it stops the timing probe right after the launcher exits: `vrtiming.log` has its last line at 06:24:32. The Qt log handler (`main.cpp`, a mutex and a flush per message) and the WSI layer both write to stderr unbuffered; their lines interleave in event order all through the log. The destroy lines are the last bytes of the file, so they were written at 06:24:31.9, when the owner closed the app.
3. **The window was neither destroyed nor recreated during the stream or the return.** Its swapchain was created at startup (the fifth surface the process made). In neither run was a surface or swapchain created after startup, and none was destroyed during the stream or after it, until the close. Hiding a window under Qt's basic render loop (`QSG_RENDER_LOOP=basic`, set by moonlight-qt) keeps its GL surface: the hide at the stream's start left no swapchain line in either run. A recreated window would have shown a new "Creating Gamescope surface" line.
4. **The UI thread was running for at least 2.5 s after the exit.** The three ShowDashboard requests are started by timers and logged from their `finished` handlers on the UI thread, and a host request was sent at the same time. So the UI thread did not block right when the window was shown.
5. **SteamVR looked the same after both exits.** `vrtiming.log`: in both runs the compositor kept 90 frames a second at about 3 ms GPU with no scene application frames. The 40 s after the bad exit look like the 40 s after the good one.

## Cause
Proven from the code (patch 0003, `mlxrhandoff.cpp`):
* `connectionStarted()` (the hide) cannot be emitted after `sessionFinished()`. It comes only from the `readyRead` handler in `run()`, guarded by `streaming`, connected to the local `QProcess`. That connection goes away with the process object when `run()` returns. `proc.waitForFinished()` (the quit path) can emit `readyRead`, but only inside `run()`. The dry run calls `onStreaming` synchronously, also inside `run()`. `sessionFinished` is queued from `execVr` after `run()` returned.
* Nothing else in the hand-off touches the window: the StreamSegue hunk of patch 0003 changes the tip text and restarts polling; `fullScreenInGameMode()` only writes a setting at the first start and does not recreate the window.

Proven from the logs (above): the window was not destroyed or recreated after the show, and the launcher's side of the return ran the same way as in the run that worked.

Not known: whether Xwayland/gamescope mapped and exposed the window again, whether Qt drew a frame, whether gamescope gave it the focus, and what SteamVR's dashboard showed. The old log has no line for any of these. So the cause is not proven. What remains is outside the launcher's logged actions: the window shown by Qt but not exposed, not drawn or not focused under gamescope, or SteamVR not showing it in the dashboard. The brief's reading (a swapchain destroyed 3 s after the exit, so the surface went away) does not hold, see Evidence 2.

## The change
Patch `0008` and `launcher/src/mlxrhandoff.{h,cpp}` (class `FlatWindowWatch`):
* `Session::execVr()` calls `MlxrHandoff::watchFlatWindow(window)` at each hand-off. The first call installs the watch on the QQuickWindow. From then on it logs, at Qt Info level, "flat window shown/hidden", "flat window exposed/no longer exposed", "flat window active/inactive", "flat window platform surface created/about to be destroyed", "flat window scene graph released" and "flat window drew its first frame N ms after it was shown". Each line ends with the state: visible, exposed, active, `QWindow::Visibility`. A later hand-off cancels anything a previous return still had pending.
* After `sessionFinished`/`readyForDeletion`, `execVr`'s queued call runs `flatWindowReturn(window)` (a `QPointer`; the session is deleted after `readyForDeletion`, the window is not). It logs "flat window after the stream: ...". It then waits until the window is visible, exposed and has drawn a frame, and logs "flat window exposed after N ms and drawn".
* If that has not happened after 1 s, it logs "flat window not exposed, showing it again (...)" and hides and shows the window, raises it and asks for activation. It does this once more 2 s later. After 6 s it logs "flat window still not exposed and drawn ...; nothing more to try". After a re-show it restarts host polling if `main.qml` stopped it (as patch 0003 does after the stream).
* Once the window has drawn, Steam is asked once more for the flat screen ("Asked Steam to show the flat screen again once the window had drawn"). The three timed requests are unchanged.
* A hide after the stream that the launcher did not do is logged as "flat window hidden again N ms after the stream". The 1 s check then shows the window again.
* A 100 ms timer runs for 10 s after the return. If the UI thread stalls for more than 1 s it logs "flat window: the UI thread did not run for N ms".
* Desktop (not gamescope): the watch only logs. The re-show and the Steam requests run only in Game Mode. With the hand-off off (`MLXR_HANDOFF=0`, no client) `execVr` is never called, so nothing of this runs.
* Test hook `MLXR_LAUNCHER_TEST_RETURN_MISS=N`: the first N checks count as missed and the Game Mode steps run anywhere.

Tests (`tests/launcher/run_tests.sh`, section 5): a segue demo whose fake client prints `MLXR_STREAMING` and exits 0 checks the order "hidden", "VR client exited with 0", "shown", "exposed after N ms and drawn", and checks that there is no re-show. A second demo with `MLXR_LAUNCHER_TEST_RETURN_MISS=2` checks two re-shows and "exposed after ... (shown again 2 time(s))".

## Owner's check
1. Start a stream from the flat window, leave it with the bar's Exit. Start another, leave it with the chord (Start+Select+L1+R1). Each time, note whether the flat window came back.
2. Read `~/Games/MoonlightXRNative/moonlight-launcher.log` from "The VR client exited with" on: `grep -E "flat window|exited with|Asked Steam" moonlight-launcher.log`.

What the lines mean:
* "flat window exposed after N ms and drawn" and no window in the headset: Qt did its part. Look at "flat window active": gamescope gives its focused window the input focus. If the window never became active, gamescope did not focus it; if it did, SteamVR's dashboard is the place to look (`vrshot.py` right after the exit).
* "flat window not exposed, showing it again" then "exposed after ... (shown again 1 time(s))", and the window came back: the re-show fixed it.
* "flat window still not exposed and drawn ...": Xwayland/gamescope did not map the window even after two re-shows.
* "flat window hidden again N ms after the stream": something hid the window after the stream.
* "flat window: the UI thread did not run for N ms", or no line at all for more than a second after "flat window after the stream": the UI thread was blocked (for example in a frame swap).
* "flat window platform surface about to be destroyed" before the app is closed: the window lost its surface.
