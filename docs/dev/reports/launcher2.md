# Lane launcher2: bitrate default, larger flat UI, Help link

Three changes to the flat launcher. All are hand-off mode only. The desktop and `MLXR_HANDOFF=0` stay stock. Patches `0009`, `0010` and `0011`; code in `launcher/src/mlxrhandoff.{h,cpp}`. The list of differences from stock is in `launcher.md`.

## 1. Default bitrate
Rule: twice stock's `getDefaultBitrate` for the same width, height, frame rate and 4:4:4 flag, from the figure before stock rounds it to whole Mbps. Rounded to the nearest 500 kbps. Capped at 150000, the maximum of the Settings page's slider.

| Mode | Stock | Hand-off |
| --- | --- | --- |
| 1920x1080 at 60 | 20000 | 40000 |
| 1920x1080 at 90 | 24000 | 49000 |
| 2560x1440 at 60 | 40000 | 80000 |
| 2560x1440 at 72 | 44000 | 87500 |
| 2560x1440 at 90 | 49000 | 98000 |
| 3840x2160 at 90 | 98000 | 150000 (capped) |

Why not twice the whole-Mbps value: the brief gives 87500 for 1440p72, and twice the rounded 44 Mbps would be 88000.

It applies wherever stock calls `getDefaultBitrate`, because the function itself changed (patch `0009`): the saved-value fallback in `reload()`, the command line parser, the Session 4:4:4 fallback, and the three resets in stock's page and the one in `XrSettingsView.qml` when the resolution, frame rate or 4:4:4 option changes. The page has no "default" label. The first-run seed calls the same rule (98000). The owner's stored value stays until he changes resolution or frame rate. The slider's maximum is 150 Mbps in the Artemis page, which has no "unlock" switch, so the cap is a constant. Custom bitrate can still go to 500 Mbps by hand.

Note for the owner: the adapter link lost packets above 40 to 60 Mbps in testing (USER_GUIDE). The new default is above that. The slider is one drag away.

## 2. Larger flat UI
`applyGameModeScale()` runs in `main()` before the `QGuiApplication` exists and sets `QT_SCALE_FACTOR`. Game Mode means `XDG_CURRENT_DESKTOP=gamescope`, as elsewhere in the launcher. It is set in C++ and not in the wrapper so the test can read the value and so it only applies to the launcher itself; the VR client's environment has `QT_SCALE_FACTOR` removed.

* Default 1.5. `MLXR_LAUNCHER_SCALE` overrides it (float, clamped to 1.0..2.5, junk gives 1.5).
* A `QT_SCALE_FACTOR` the owner set is kept unless `MLXR_LAUNCHER_SCALE` is also set.
* `MLXR_HANDOFF=0` leaves it alone (stock). `enabled()` needs the `QCoreApplication` for the client path, so only that switch is checked this early. If no client is installed in Game Mode, the scale still applies.

Check at 1.5 on 1920x1080 (window 1280x720 logical, offscreen platform, software scene graph, `QT_SCALE_FACTOR=1.5`; the grab is 1920x1080 in device pixels):
* PC grid: three tiles with names, nothing clipped, the header buttons at the right edge.
* Settings page (Artemis style): the column is capped at 980 logical (1470 px), so there is still room on each side of 225 px. Summaries wrap. The slider and its value fit. Scrolled to the end (MLXR_LAUNCHER_SCROLL=3000): the last row and the footer are reachable, so the page scrolls to its end.
* A list dialog (Video resolution) is 560 logical wide and its list is at most the window height minus 220 logical. It fits.
* No QML errors.

Other scales, looked at for the same page: 2.0 fits, with the title and the version label in the header about 45 px apart. 2.5 (logical 768 wide) draws the title over the version label. So 1.5 does not clip and stays the default. Anything above about 2.0 is crowded in the header. The clamp stays at 2.5 as asked.

Not tested: the real window in gamescope. Whether gamescope hands Qt a 1920x1080 screen with a device pixel ratio of 1, and how the dashboard flat screen looks in the headset, needs the owner. If the window is not full screen there, the scale would not change that.

## 3. Help button
`main.qml`: hand-off mode opens `https://github.com/simplysanatori/moonlight-xr-steam-frame#readme`. Else stock's Setup-Guide. The decoder-problem, gamepad-mapping and PC-list dialog links are untouched. The test greps the patched `main.qml` in the build tree for the three links. A click was not run (it would open a browser).

## Tests
`tests/launcher/run_tests.sh`, section 3c: the default bitrate for 1080p60, 1080p90, 1440p60, 1440p72, 1440p90, 4K60, 4K90, 1440p90 with 4:4:4 (all four expected values above, and the caps), stock values with `MLXR_HANDOFF=0`, first-run seed equals the rule (the section 1 checks now expect `bitrate=98000` and `--bitrate 98000`), the scale in Game Mode (default, 2, clamps 9 and 0.3, junk), on a desktop (untouched), and an owner's `QT_SCALE_FACTOR`. Section 5: pc and settings pages rendered at 1.5 without QML errors. A Help link check before section 4. The hooks are `MLXR_LAUNCHER_SELFTEST=bitrate:WxHxFPS[x444]` and `=scale`, which print and exit before any UI.

## From the stopped lane (launcher-defaults)
Reused: the `streamingpreferences` split and the 500 kbps rule (checked against the six values), the `applyGameModeScale()` shape, the self-test hooks and the tests. Changed: scale skipped when `MLXR_HANDOFF=0`, `QT_SCALE_FACTOR` removed from the VR client's environment, Help link and its check added, renamed the comments to lane launcher2, patches regenerated through `launcher/patches.sh`.
