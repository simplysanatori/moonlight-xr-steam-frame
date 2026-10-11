# Refresh rates up to 144 Hz (lane refresh)

The owner asked for the headset's 120 and 144 Hz, the Steam Frame's faster display rates, with the appropriate scaling. Before this lane every rate was capped at 90.

## How the rate is chosen

SteamVR runs one display rate per app. The per-app setting `preferredRefreshRate` picks it (72, 90, 120 or 144 on the Frame), and the OpenXR runtime then offers only that rate through `XR_FB_display_refresh_rate`. The VR client cannot ask for another one. So:

* The flat Settings page's Refresh rate now offers Match headset, 144, 120, 90, 72, 60 and 30 Hz, and Custom.
* A fixed 72, 90, 120 or 144 Hz is written to SteamVR before the VR client starts: the launcher runs `vrcmd --set-settings-int steam.app.<SteamAppId>.preferredRefreshRate N`, the form `packaging/install.sh` uses, and logs `SteamVR refresh rate: steam.app.<appid>.preferredRefreshRate set to N Hz (vrcmd), ...`. It needs the Steam shortcut's `SteamAppId` and a running SteamVR. When either is missing, or vrcmd is missing, fails or takes more than 5 s, the launcher logs why and the stream goes on at the rate SteamVR reports.
* Match headset, 60, 30 and a custom (Apollo) rate leave SteamVR's per-app rate alone. Match headset asks the host for the rate SteamVR reports, as before; with SteamVR at 90 it still asks for 90.
* The brief named 120 and 144. 72 and 90 are written too, so going back from 144 to 90 in Settings also brings SteamVR back to 90. Without that the app would stay at 144 and show a 90 fps stream on a 144 Hz display, which judders.
* `packaging/install.sh` still sets 90 for a new install.

## The cap

* `backend/fps_cap.h`: `MLXR_FPS_CAP` is 144. `mlxr_fps_cap_for_display()` returns the display's rate up to 144, 144 for a faster display, and 90 (`MLXR_FPS_CAP_UNKNOWN`, install.sh's rate) while SteamVR has not reported one. `mlxr_launch_params_init()` starts at that 90 too, so a caller that forgets the cap cannot ask for more than 90.
* `app/main.c launchStream()`: the requested rate is capped at the display's rate. A higher rate gets the warning `requested N fps is above the headset's M (display rate reported by SteamVR; SteamVR's per-app setting picks it): asking the host for M fps`, as the 90 cap did. A 144 fps stream therefore only happens when SteamVR runs the app at 144. When the display rate is not known yet the launch waits for it up to 3 s. In the owner's logs it is known about 1 s before the launch.
* `stream/session.c`: the last guard before `LiStartConnection` uses the 144 ceiling. The display's own rate is applied in `launchStream()`.
* The launcher caps `--mode`'s rate and a custom rate at 144 (`MlxrHandoff::kMaxHeadsetFps`, `MlxrPrefs::headsetMaxFps()`). `--refresh-rate` in the VR client is capped at 144 when parsed and at the display's rate at launch.
* The VR client's own `config.ini` list (`[stream] fps`) has 144 at the end (indexes unchanged).

## The display request

* The VR client asks the runtime for the stream's own rate: `--mode`'s (the flat launcher's choice) or the custom rate. Before this lane it asked for `config.ini`'s rate, whatever `--mode` said. With Match headset it asks for none, so the display stays on SteamVR's rate.
* When the runtime offers one rate (SteamVR always does), the log says: `display rate: the runtime offers only N Hz; the headset's rate is set in SteamVR's per-app settings (the flat Settings page sets it when a rate is picked there), and the stream rate follows it`.
* When the stream's rate is above the one offered: `display rate: 144 Hz is not offered, the display stays on 90 Hz and the stream is capped there; set 144 Hz in SteamVR's per-app video settings for this app (or pick it in the flat Settings page), then start the app again`.
* Without the refresh rate extension (another runtime) the frame loop's period is reported as the display rate, so the launch does not wait for nothing.

## Bitrate

The launcher2 rule is unchanged: twice stock moonlight-qt's `getDefaultBitrate`, which grows with the square root of the rate above 60. Its `bitrate:` self test gives 113000 kbps at 1440p120 and 124000 kbps at 1440p144 (stock: 57000 and 62000). Both are now in `tests/launcher/run_tests.sh`. With Match headset the page uses the 90 Hz figure (98000 at 1440p), since the launcher does not know SteamVR's rate.

The VR client has its own fallback for a stream started without `--bitrate` (`ui/settings.c settings_default_bitrate_kbps`). At 1440p it was 80000 for 60 to 90 fps and Moonlight's single curve above that, 57000 at 120. Above 90 fps at 1440p it now uses the launcher's rule: 113000 at 120 and 124000 at 144.

## What assumed 90, and what was done

| Where | Assumption | Now |
| --- | --- | --- |
| `backend/fps_cap.h`, `nvhttp.c`, `main.c`, `session.c`, launcher | 90 ceiling | 144 ceiling, display's rate, 90 while unknown |
| `app/main.c` XR config | display asked for `config.ini`'s fps | `--mode`'s fps; none for Match headset |
| `xr/xr_display.c` | no word on SteamVR's one rate | logs the per-app setting; period fallback without the extension |
| `stream/hw_video.c` latch (`MLXR_VIDEO_LATCH_MS`, 4 ms) | tuned for an 11.1 ms refresh | same share of the refresh above 90 Hz: 3.0 ms at 120, 2.5 ms at 144; 90 Hz and below unchanged |
| `stream/cadence.c` | "off the 11.1 ms grid" (90 fps) | the stream's own period (8.3 ms at 120, 6.9 ms at 144). A steady 120 fps host sits on the 2.78 ms grid and was counted as off it |
| `ui/settings.c` | 120 the highest `config.ini` rate; 1440p default bitrate drops above 90 | 144 added; launcher's rule above 90 |

Left as they are:

* `stream/pacer.c`, `MLXR_VIDEO_PACING`: the history is half a second of refreshes, taken from the display's rate on each refresh (72 at 144 Hz, under its 256 limit). The stall rule counts refreshes, so it scales by itself. `hw_video.c` still starts the pacer at 90 until the first refresh gives the real rate.
* The 3D depth governor's floor `RATE_FLOOR_HZ` (72): a floor, not a ceiling. The frame budget is judged against the period of the rate the display is on. With several rates offered the warp steps 144 to 120 to 90 to 72; SteamVR offers one, so it never steps. New `xrcore_rate` checks cover both.
* The depth model's rate (maps a second) is counted in time, not frames.
* The hw_video latch's 12 ms "picture on its way" window is about decode time, not the refresh.
* Comments that give 90 Hz frame costs as examples (`xr_panels.c`, `xr_session.c` panel drawing, `au_gather.h`, `stream/session.c` history notes).
* The cadence buckets are fixed in ms. 120 and 144 fps frames both land in the 6-9.7 ms bucket.

## Tests

* `backend_parse` (`test_fps_cap`): the cap follows the display (72, 90, 120, 144), the 144 ceiling (165 and 360 give 144), 90 while unknown, Match headset (SteamVR at 90 gives 90 for a 144 request; at 120 or 144 gives that), a rate above the display is cut to the display's, launch query `mode=...x120` and `x144`.
* `backend_flow`: what the fake host receives for 120/144 requests on 90, 120, 144 and 165 Hz displays.
* `backend_glue`: `fps` and `clientRefreshRateX100` at 144 and 120.
* `xrcore_rate`: the Frame's rates, one rate offered, 144 to 120 to 90 to 72 with the warp over budget.
* `stream_cadence`: 120 and 144 fps hosts, resampled 360 Hz gaps at 120.
* `ui_settings`: `fps = 144` in `config.ini`, the 1440p bitrate fallback at 120 and 144.
* `launcher_handoff`: see `launcher.md` (section 3c, fake vrcmd).
* Headless run with `test-1080p60.mp4` (the owner was not streaming): the runtime offered one rate and the new line said so. With `config.ini` at 144 and the runtime at 72: `display rate: 144 Hz is not offered, the display stays on 72 Hz and the stream is capped there; ...`.

## The owner's check

1. In the flat launcher, Settings, Refresh rate: pick **120 Hz**. Start a stream.
2. `~/Games/MoonlightXRNative/moonlight-launcher.log` must have `SteamVR refresh rate: steam.app.<appid>.preferredRefreshRate set to 120 Hz (vrcmd)`.
3. `~/Games/MoonlightXRNative/moonlight.log` must have `display rates offered: 120 Hz, display on 120 Hz, stream 120 fps` and `stream: 2560x1440 120 fps, 113000 kbps` (or the bitrate you set). With Stats on, the headset shows `Display 120 Hz`.
4. If the log still says `display on 90 Hz` and `requested 120 fps is above the headset's 90`, SteamVR applied the new per-app rate only for the next session: exit and start the app again, and say so (then the launcher should set the rate earlier, for example when Settings closes).
5. Do the same with **144 Hz**: `display on 144 Hz`, `stream ... 144 fps`, 124000 kbps by default.
6. Set **Match headset** or **90 Hz** to go back. 90 Hz sets SteamVR back to 90; Match headset keeps whatever SteamVR has.

At 120 and 144 the PC has to render and encode that many frames, and the link has to carry 113 to 124 Mbps. Watch `frames lost on the network` and `idr requests` in the stats line, and lower the bitrate if they climb.

## Open risks

* Whether SteamVR applies a per-app rate written just before the VR client's session starts, or only from the app's next launch, is not tested here (step 4 above). The flat launcher is the shortcut's process, and the VR client inherits its `SteamAppId`.
* vrcmd runs on the flat UI's thread for up to 5 s (3 s more for `pgrep`). It usually returns at once.
* Picking 90 or 72 Hz overwrites a rate the owner set by hand in SteamVR for this app. Match headset is the way to keep SteamVR's own choice.
* The Frame's GPU and decoder at 144 Hz: the frame loop has 6.9 ms per refresh. The headless run decoded the 1080p test clip in about 1.5 ms a frame; 1440p at 144 fps, the 3D depth warp and the rooms at 144 Hz are untested. The depth governor cuts the depth model's rate first; SteamVR offers no lower display rate to step to.
* A 120 or 144 Hz stream needs the PC's display (or Apollo's virtual display) at that rate. Apollo builds its virtual display from `clientRefreshRateX100`, which now carries 12000 or 14400.
