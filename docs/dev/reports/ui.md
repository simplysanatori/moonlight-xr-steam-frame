# ui lane report (lane U, branch lane/ui)

Base: main b7a431a. Owns `ui/`, `tests/ui/`, `assets/fonts/`. Nothing outside them was edited.
Library `mlxr_ui` (static, C11, `-Wall -Wextra`, warning free), links `mlxr_xrcore` and libm only
(`tools/ldd-audit.sh` passes: libc, libm). No OpenXR, no Vulkan, no threads, no globals except the
font and image caches.

## What is in it

| File | What |
| --- | --- |
| `canvas.h/.c` | Software canvas, RGBA8 **premultiplied**, top-left origin. Anti-aliased rounded rects (fill, centred stroke), circles, round-capped lines, polylines (one composite, no double alpha at joins), arcs, polygons, bilinear "cover" images through a rounded mask, nearest image (QR), radial gradient. Colours are `0xAARRGGBB` straight alpha, composited source-over in sRGB space like Android's Canvas. `ui_cv_sub` makes a canvas over a rectangle of a bigger buffer (the atlas). |
| `text.h/.c` | stb_truetype text: UTF-8 decode/encode, `ui_text_width` (advance + GPOS/kern pairs), `ui_text_draw` (left/centre/right, quarter-pixel phase, 8-bit alpha atlas 1024x1024 with a shelf packer and a hash cache that flushes when full), `ui_text_ellipsize` (end / middle / start, whole code points, trims a trailing space like upstream), `ui_text_wrap` (line breaks, last space that fits, else between code points; returns the number of lines needed), `ui_text_fit_size`. One family, Regular and Medium. |
| `settings.h/.c` | `Settings` struct, descriptor table (key, section, type, range, default, doc), INI save/load, derived values, the Screen tab's slider maths. |
| `ui_strings.h/.c` | English strings table, X-macro, ids `UIS_*`, keyed by the upstream resource name. (Named `ui_strings` because `<strings.h>` is a system header.) |
| `panels.h` + `panels_common.c`, `panel_cog.c`, `panel_keyboard.c`, `panel_picker.c`, `panel_bar.c`, `icons.c`, `panel_dialogs.c`, `panel_misc.c` | The panel rasterisers and their hit regions. |
| `third_party/` | `stb_truetype.h` 1.26, `stb_image_write.h` 1.16 (panel test PNGs), `stb_image.h` 2.30 (PNG/JPEG for the room thumbnails and settings icon; **not in the brief**: vendored so the lane links nothing beyond libc and libm). Licence blocks are at the end of each header, `README.txt` lists them. |
| `assets/fonts/` | `NotoSans-Regular.ttf`, `NotoSans-Medium.ttf` (Noto Sans 2.013, copied unchanged from `/usr/share/fonts/noto` on the Frame), `OFL.txt` (SIL OFL 1.1, from notofonts/latin-greek-cyrillic), `README.txt`. The SteamOS `noto-fonts` package licence file is Apache 2.0 text, but the font's own name table says OFL 1.1, so the OFL is what ships. |

### API in five lines
```c
UiView v;  ui_view_init(&v, &settings);          // + set cog_tab, room_env, hover, kb_sheet, kb_mods, bar_show_*, ...
UiCanvas cv; ui_cv_sub(&cv, atlas, UI_ATLAS_W, r.x, r.y, r.w, r.h);   // r = ui_atlas_rect(id); or ui_cv_init on your own buffer
ui_cv_clear(&cv);  UiHits hits;  ui_draw_panel(UI_PANEL_COG, &cv, &v, &hits);   // then upload the rectangle
int i = ui_hits_at(&hits, u, v);  // hits.hit[i] = { kind, a, b, u0,v0,u1,v1, enabled }
```
`ui_draw_report`, `ui_draw_toast`, `ui_draw_clock`, `ui_draw_pc_list`, `ui_draw_app_list` take their own data structs (`UiReport`, text, `UiPcList`, `UiAppList`). Everything about `UiView`, `UiHit` kinds and `UiButton` ids is in `panels.h`.

Every draw function paints onto transparent: clear the canvas first. Draw at any size with the native aspect ratio (`ui_panel_native_size`): pixel quantities scale with canvas width / native width; positions are the fractions from `xr_shared.h` / `xr_layout.h`, so hit regions do not depend on the pixel size.

## Build and test
```
cmake -S . -B build-ui -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
nice -n 5 ninja -C build-ui -j2                       # everything, incl. the ASan variants
nice -n 5 ninja -C build-ui panels                    # renders every panel to build-ui/panels/*.png (46 files)
env -u LD_LIBRARY_PATH ctest --test-dir build-ui -R ui --output-on-failure
```
| ctest | checks | covers |
| --- | --- | --- |
| `ui_text` | 124 | golden advances (Noto 2.013, tolerance 0.01 px), UTF-8 incl. malformed input, ellipsize (3 modes, tiny buffers), wrap, alignment ink boxes, premultiplied output, clipping, atlas cache hit == miss and a forced flush |
| `ui_settings` | 3469 | defaults vs upstream (every key), table covers the README, one-setting-at-a-time and 60 random round trips, defaults commented out and a new default reaching an old file, clamping, tolerant parsing (BOM, CRLF, junk, unknown keys and sections, long lines), `MLXR_CONFIG_DIR`, atomic save, slider maths |
| `ui_panels` | 18890 | every panel at 5 scales and in 40 random states with guard bytes either side of the buffer; hit regions inside [0,1] and non-degenerate; **every row of every tab present** (Screen 8, Display 12 + glow level, 3D 4, Picture 4, Room 5, About 2 buttons, 5 tabs); row bands and cell/track parts equal `cogRowV`/`cogRowHalf`/`cogTrackPart`; bar slot centres equal `barSlotPlacement`; hint/Ko-fi/report zones equal `handHintZone`/`kofiSheetZone`/`reportZone`; keys never overlap; a changed setting changes the pixels of its row; the atlas has non-overlapping slots and drawing in a slot is pixel-identical to drawing alone |
| `ui_*_asan` | same | the three suites rebuilt with `-fsanitize=address,undefined` (including the ui sources): 6/6 pass, no leaks reported |

Cost of one redraw at native size, `-O2` on the Frame: cog 4 ms, bar 1.6 ms, button strip 2.6 ms, exit 1.7 ms, hint 3.7 ms, Ko-fi 6.5 ms, keyboard 12.7 ms, picker 14.7 ms (the two image blits dominate it; the picker only needs drawing once or when the selection/hover changes). Nothing here needs to run per frame.

## Panel by panel: what is drawn and where upstream does it
Line numbers are `app/src/main/java/com/limelight/binding/video/XrPanels.java` unless said otherwise. Colours, radii, stroke widths, text sizes and row positions are upstream's numbers (in native texture pixels).

| Panel (`UiPanelId`, native px) | Upstream | Notes |
| --- | --- | --- |
| `COG` 768x640 | `buildCogSheet` 637, `drawCogChrome` 674, `drawCogSliderRows` 717, `drawCogRoomRows` 792, `drawCog3dRows` 1218, `drawCogOptionRows` 1320, `drawCogPictureRows` 1379, `drawCogAbout` 1427, `drawCogCells`/`drawCogChevrons` 880-950 | One panel, the tab chosen by `UiView.cog_tab`; the Room tab replaces Screen when `room_env` is a baked room (`ui_cog_face`). Upstream's 13 pre-drawn sheets collapse into one 4 ms redraw. Everything upstream puts on **separate quads** is drawn into the same buffer from `Settings`: slider thumbs (`xr_assets.c` 288-300: white disc 0.42 x `cogThumbSize`, alpha 235), the Display tab's in-force rings (`Marks` 1044), the preset and switch rings (3D), the Room ring, the readout strip (`Readout` 951: `8 px/°`, `+12`, `130%`, `1.30`, room percents) and hover rings/washes (from `UiView.hover`). The head-aim rows and the Display tab's head-aim row grey out with the same reasons ("needs Head locked" / "not in a room"); Curve greys out when `curve_ok` is 0; 3D rows grey out with "3D is off in settings" when `stereo_ok` is 0; the Room tab's Size row greys out in the Grand Cinema with "This room's screen is a fixed size". The 3D depth track is amber past the model's default (ZipDepth 6, MiDaS 5) with "harder on the eyes" under it. |
| `PICKER` 1280x296 | `buildPickerGrid` 432 | Five tiles from `PICKER_*`/`ENV_CELL_*`, room thumbnails from `assets/rooms/thumbs/*.jpg` (missing file: flat colour), label shrinks 21 to 15 px to fit. Selection ring (`env_selected`) and hover ring are drawn here (upstream: outline quads). |
| `KEYBOARD` 1120x448 | `buildKeyboard` 1787, `buildKeyRects` 1832, `buildKeyboardSheet` 1875, tables 120-258 | Lower, upper, symbols, Fn sheets (`kb_sheet`); lit Ctrl/Alt/Win (`kb_mods`) are white caps on the Fn sheet and a white tag on the space bar elsewhere. The four Fn-sheet arrows are drawn as shapes: Noto Sans has no arrow glyphs (checked with `ui_font_has`). 44 key rectangles shared by all sheets (`ui_kb_key_rect`), codes per sheet (`ui_kb_key_code`); `kbPress` from `xr_keys` is the state machine, this only draws it. |
| `BAR` 1792x128 | `buildEnvButton` 571, `buildCogFallback`, `buildKeyboardButton` 1968, `buildStereoButton` 2000+, ray 2048+, pad 2095+, aim 2145+, `buildExitButton` 2217; the move pill is `xr_assets.c` 205-224 | The whole row in one strip, 0.72 of the frame's width wide, buttons at `barSlotPlacement` positions, white 0xEBFFFFFF pill (0.14 x 0.0131 of the frame). State faces: 3D (`session.stereo_on`), ray, pad, aim, struck through when off; aim and 3D omitted unless `bar_show_aim` / `bar_show_stereo`. Gear is `assets/images/settings_icon.png` with upstream's drawn fallback. |
| `BUTTON_ATLAS` 1536x128 | same buttons | The 12 `BTN_CELL_*` cells of `xr_layout.h` side by side, for a renderer that wants upstream's one quad per button. Rows are top-down; `buttonCellPut` flips them for GL, a Vulkan upload needs no flip. |
| `EXIT` 512x256 | `buildExitPrompt` 2243 | Question, red Exit and white Cancel; hovering fills (`hover`), as upstream's three pre-drawn sheets. |
| `HINT` 1024x448 | `buildHandHint` 2305 | Wrapped body shrinking 30 to 20 px to fit. |
| `KOFI` 1024x256 | `buildKofiSheet` 2379 | The title ("Support simplysanatori on Ko-fi") over two buttons since lane fix-polish took the QR code, the address and the lines of text out (it was 1024x512): Open page (`UI_BTN_KOFI_OPEN`, `KOFI_ZONE_OPEN`, runs `kofiOpenPage` with `XR_KOFI_URL`, https://ko-fi.com/sana748653, xr_kofi.c) and Not now (`UI_BTN_KOFI_CLOSE`). |
| `REPORT` (`ui_draw_report`) 1024x640 | `ReportSheet` 1493 | Note (last wrapped lines, caret), what-goes text, Cancel/Send. |
| `TOAST` (`ui_draw_toast`) 1024x160 | `Toast` 1127 | One or two lines; a path is cut from the middle. |
| `CLOCK` (`ui_draw_clock`) 384x48 | `ClockStrip` 1091 | The caller formats "10:42   Battery 87%". |
| `PC_LIST`, `APP_LIST` 1024x640 | none | New, for the native launcher, in the sheet style (dark sheet, white strokes). 5 rows a page, row = icon/letter tile, name (cut to fit), "Online · Paired" with a state dot, "Running" pill, chevron; Add PC, page up/down and "n / m" for the PC list; Back for the app list; loading ("Refreshing apps…"), failed and empty states. Items come from `UiPcItem`/`UiAppItem` arrays the caller owns. |

### Hit regions (panel UV, origin top left, `UiHit`)
`ui_hits_at` returns the last (top-most) region containing a point. `enabled == 0` means a greyed row: it is reported so a row can be found, but input should ignore it.

| `kind` | `a`, `b` | Same as upstream's | 
| --- | --- | --- |
| `TAB` | tab 0..4 | `COG_TAB_BAR_B` strip split evenly (`xr_layout.h` comment) |
| `TRACK_DOWN` / `TRACK_RUN` / `TRACK_UP` | row (face-specific, see below) | `cogTrackPart(u)`: u ranges are exactly `[TRACK_L - REACH, TRACK_L + CHEVRON_W]`, `(.., ..)`, `[TRACK_R - CHEVRON_W, TRACK_R + REACH]`; v band is `cogRowV(face,row) ± cogRowHalf(face)` (xr_input.c 2346). A press on RUN gives the thumb place `cogRunPlace(u)`. |
| `CELL` | row, cell | `cogCellAt`: the track's width split evenly in `ui_cog_row_cells` cells, same v band |
| `BUTTON` | `UiButton` id | `cogReportButtonAt`/`cogKofiButtonAt`, `COG_RESET_*`, `EXIT_*`, `HINT_*`/`handHintZone`, `KOFI_*`/`kofiSheetZone` |
| `FIELD` | `REPORT_ZONE_*` | `reportZone` |
| `KEY` | key index, code | `kbKeyAt` (blanks have no hit); `code` is the `KB_CODE_*`/char/`KB_CODE_VK + vk` value to hand to `kbPress` |
| `ENV_TILE` | `ENV_CELL_*` | the picker cell (whole cell, not the padded tile) |
| `BAR_BUTTON` | `BAR_SLOT_*` | `barSlotHit` (reach `HOVER_MARGIN` x side, clipped to half the pitch so neighbours do not overlap) |
| `BAR_MOVE` | - | `BAR_WIDTH_FRAC * BAR_HOVER` wide, full strip height |
| `LIST_ITEM` | index in the whole list | native launcher |

Row numbering of `TRACK_*`/`CELL` follows `xr_shared.h`: Screen `COG_SLIDER_*` 0-7; Display `COG_OPTION_*` 0-11 (cells) and `COG_DISPLAY_SLIDER_ROW` 12 (track); 3D `COG_ROW3D_*` (preset cells, two tracks, switch cells); Picture `PICTURE_*` 0-3; Room `COG_ROOM_ROW_*` (`ui_cog_face` returns `COG_FACE_ROOM` for it; `ui_cog_row_is_track/cells/live` give the per-row facts that `xr_ui.c` computes).

## Settings
`Settings` (see `settings.h`): 66 settings from a descriptor table, so the file format, clamping, defaults and tests all come from one place. Sections: `[stream]` host, app, resolution, fps, bitrate (0 = Moonlight's default curve, `settings_default_bitrate_kbps`, 1440p60..90 = 80000, the owner's figure (main de55376); elsewhere Moonlight's curve), codec, audio_config, virtual_surround, frame_pacing (auto_desktop, check_updates, language and vr_mode, doff_grace were stored here and read by nothing; removed, see PLAN item 1.6); `[vr]` every row of the README's VR Settings table plus the picture grade and the hand-lock hint flag; `[debug]` depth_rate, depth_debug, eye_swap, file_log; `[display]` head_locked, sharpening, supersampling, stats, glow, glow_level, screen_light; `[3d]` separation, convergence (-1 = the depth model's own, which is how upstream stores "nothing"); `[screen]` distance, height, azimuth, tilt, rotate, curve, width (upstream stores a 10 float pose string; these seven integers are the same information in units a person can edit); `[rooms]` brightness, glow, light, screen for rooms 4, 5, 6 (the upstream environment ids).

* Defaults are upstream's: `PreferenceConfiguration` (`DEFAULT_*`), `XrShared` (`ROOM_*`, `PICTURE_*`, `HEAD_AIM_*`, `DEPTH_RATE_DEFAULT`), `MidasDepthSource` (ZipDepth 6/50, MiDaS 5/50). The only deliberate change: fps stays 90 but the native client follows what SteamVR offers (72 or 90), as docs/dev/STATUS.md says.
* File: `~/.config/moonlight-xr/config.ini`, directory overridden by `$MLXR_CONFIG_DIR`. Every setting is written, preceded by a comment with its upstream key, range and default; a setting at its default is written as `#key = value`. Loading ignores `#`/`;` lines, so **a default changed in a later build reaches an old file**; an active line that happens to equal the default is the user's choice and sticks. Written through `config.ini.tmp` and renamed, directories created.
* Load is tolerant and range-clamping: BOM, CRLF, quotes, `key=value` without spaces, case-insensitive keys, enum by name or index, bool as true/false/on/off/yes/no/1/0, a key in the wrong section still applies (unique keys), unknown keys/sections and malformed lines are **ignored and counted** (`SettingsLoadInfo`), unreadable values keep the default. Unknown keys are **not kept**: the next save drops them (stated in the file's header). Strings are trimmed and one line.
* Not persisted on purpose (session only, as upstream): head aim, ray and gamepad toggles and the 3D switch (`SessionToggles`, started from the settings by `session_from_settings`).

Excerpt of a fresh file: see `settings_to_text` output; every block is `# <upstream key>: <what> [range] (default x)` then `#key = x`.

## Strings table
`ui_strings.h`: 127 ids (`UIS_*`) with the upstream resource name (`ui_str_key`) and English text. Upstream names are kept (`vr_panel_*`, `vr_key_*`, `vr_toast_*`, `vr_kofi_*`, `vr_hand_hint_*`, `bug_report_*`, `pcview_*`, `applist_*`); format strings use C `%s`/`%d` instead of Android's `%1$s`. Native-only strings are `native_*` (PC/app list: "Computers", "Paired", "Not paired", "Running", "Back", "n / m", empty messages). Translations later: a second table of the same length indexed by `UiStr`, picked in `ui_str`. Not in the table because the panels never draw them: the 2D-settings summaries and the toast kinds that go through the OS notification path.

## What I could not reproduce, or did differently
* **Splash** (`buildSplash` 2466, the logo with six wedges) and the **Logo** class: not drawn by lane U; it is not in the brief's panel list. (Lane wave6-splash, PLAN 6.3, added a splash of its own: see "Splash" below. It does not use `buildSplash`'s art or its `Splash` timing in `xr_notice.c`, which stay unused.) Geometry is fully specified in `Logo` (disc, 6 wedges of 45 degrees from straight down, spokes 0.0308 r, X and R cut-outs) and `canvas.h` has the radial gradient it needs; say if you want it (about 150 lines).
* Fonts: Noto Sans is wider than the Roboto Android draws with (about 10 %), so tight upstream labels ("Aim sensitivity", "Controllers" + its hint) sit closer to their neighbours; nothing overlaps or clips, and long strings go through `ui_text_ellipsize`/`ui_text_fit_size`. Android's drawText has no GPOS kerning and does hinting; stb has GPOS pair kerning and no hinting. Bold is not shipped: the "3D" button letters use Medium.
* The **arrows** on the Fn sheet are vector shapes, not glyphs (see above).
* **No CJK/emoji**: the font is Latin/Greek/Cyrillic only; code points it lacks draw the `.notdef` box. Add Noto Sans CJK as a fallback face when translations return (`UiFont` has two slots; a third would be a small change). Since PLAN 6.1 (lane wave6-i18n) `UiFont` has the fallback face: the system's Noto Sans CJK, found at start for a Chinese, Japanese or Korean UI language ([i18n.md](../i18n.md)).
* Text is not shaped (no ligatures, no bidi, no Indic): fine for the 19 languages upstream ships except Arabic/Hebrew, which it does not ship either.
* The text gamma is a fixed 0.8 coverage curve; looks slightly bolder than Android's and is easy to change (`TEXT_GAMMA` in `text.c`).
* The fork's `fewSwapchains` trimming is not reproduced: the atlas below replaces it.
* Hover on tracks only grows the thumb; upstream also draws a ring quad under the ray for tracks. Cells, buttons, keys, tiles, bar buttons and tabs highlight.
* Android rasterises with its own AA; mine is analytic SDF coverage. Edges differ by under a pixel.

## Open questions for the orchestrator

**1. Pixel sizes vs metres.** Physical sizes come from `xr_renderer.h` (fractions of the 3 m stand-in frame, `furnitureWidth`):

| Panel | native px | width | height | px/m | px per degree at its distance |
| --- | --- | --- | --- | --- | --- |
| cog | 768x640 | 0.36 x 3 = 1.08 m | 0.90 m | 711 | 37 at 3 m |
| keyboard | 1120x448 | 0.55 x 3 = 1.65 m | 0.66 m | 679 | 36 at 3 m |
| exit | 512x256 | 0.30 x 3 = 0.90 m | 0.45 m | 569 | 30 at 3 m |
| report / hint / Ko-fi | 1024 wide | 0.40 x 3 = 1.20 m | 0.75 / 0.53 / 0.60 m | 853 | 45 at 3 m |
| picker | 1280x296 | 0.91 x 3 = 2.73 m | 0.63 m | 469 | 25 at 3 m |
| toast | 1024x160 | 0.56 m (head locked at 0.8 m) | 0.09 m | 1829 | 27 at 0.8 m |
| bar strip | 1792x128 | 0.72 x 3 = 2.16 m | 0.15 m | 830 | 45 at 3 m |

Assuming the Frame's 1728 px per eye cover roughly 100 degrees (about 17 px per degree; not measured), native size is already about 2x oversampled; **0.75x of native (576x480 cog, etc.) is indistinguishable and saves 44 % of the memory**. Native is the safe default and every panel redraws at any scale with the same hit regions. Upstream chose its sizes for a Quest 3 (about 25 px per degree), so these are on the generous side. I did not look at any panel in the headset.

**2. How many swapchains?** The whole UI fits **one** swapchain: `UI_ATLAS_W x UI_ATLAS_H` = 2048 x 2960 RGBA8 (24 MB), slots from `ui_atlas_rect(id)`, drawn through `ui_cv_sub`, shown by quads whose `XrSwapchainSubImage.imageRect` is the slot. Upstream needs well over 16 (13 cog sheets, 4 keyboard sheets, 3 exit sheets, readout, marks, clock, picker, buttons, report, hint, Ko-fi, toast, thumbs, outline, corner, bar, splash, video, depth overlay...), which is why the fork added `fewSwapchains`. With the atlas: **video (1), UI atlas (1)**, plus whatever you keep outside (a splash, debug overlay) is 3-4 of the 16. Upload only the rectangles that changed (a `vkCmdCopyBufferToImage` with an image offset), hover redraws included. If a single 24 MB image is awkward, the shelf layout splits cleanly after row C into two (2048 x 1792 and 2048 x 1168). Quad layers cost nothing extra for sharing a swapchain. Use `R8G8B8A8_UNORM` or `_SRGB`? The pixels are authored and blended in sRGB space, so with an `_SRGB` swapchain the compositor decodes to linear before blending and the dark sheet's alpha edges come out slightly brighter than Android's; use `_UNORM` if you want the Android look, `_SRGB` if you want correct blending. Layers should be premultiplied (the default; do not set `UNPREMULTIPLIED_ALPHA_BIT`).

**3. State the panels need from you** (all in `UiView`): `settings` (live copy, kept current as the input changes values), `session` toggles, `cog_tab`, `room_env`, `curve_ok` (the Frame has no `XR_KHR_composition_layer_cylinder`, so Curve is greyed unless you curve the picture yourself), `stereo_ok`, `env_selected`, `bar_show_aim`/`bar_show_stereo`, `kb_sheet`/`kb_mods` (from `kbPress`), `hover` (the hit under the ray, optional), `version`/`commit`/`log_path`. Rows that change the screen pose (distance, height, tilt, rotate, curve, size) read `Settings.screen_*`; keep it in step with the live pose (upstream reads the pose back each frame) and call `settings_save` when a grab ends. `settings_set_screen_slider` turns a drag position into stored units with upstream's level snaps; `cogStepIndex`/`cogTrackSteps` in xr_layout still give the step-button targets.

**4. Redraw policy.** Redraw a panel when what it shows changes (tab, value, hover, state) and upload its rectangle. Typical drag: cog at 4 ms.

**5. Text ownership.** Callers that put typed text on screen (report sheet, a future host-address field) need `ui_text_wrap`/`ui_text_width` for their own caret logic; they are public.

**6. Asset install.** `assets/fonts/*.ttf` + `OFL.txt` should ship with the package (`ui_font_default` looks in `$MLXR_FONT_DIR`, `$MLXR_ASSETS_DIR/fonts`, `<exe>/assets/fonts`, `<exe>/../assets/fonts`, the compiled-in assets dir, then `/usr/share/fonts/noto`). `ui_panels_set_assets_dir` sets where thumbnails, QR and the gear are read.

**7. Top-level CMake.** Nothing needed: `ui/CMakeLists.txt` adds `tests/ui` itself, as audio does. The tests include `tests/xrcore/check.h` by path.

## Splash (lane wave6-splash, PLAN 6.3)

`xr/xr_splash.c/.h`. While a stream is the picture's source (`ctx->streaming`), the launcher's list is not up and no frame has been shown (`ctx->videoShown` 0), a head-locked quad (0.9 m wide, 1.5 m ahead, same space as the toast) shows the app's icon (`assets/branding/icon-256.png`, found through `ui_asset_image` like the settings icon, so it ships with the assets mirror) and a pill with "Connecting to <PC>..." under it. The PC name is `xrSetStreamIdentity`'s host (`xrStreamIdentityHost`). When the picture goes away while the stream stays (`xr_app_release_video` in the reconnect path sets `videoShown` 0 again) the same quad shows "Reconnecting...". The first frame starts a 300 ms fade; a new stream (`streaming` 0 then 1) is a first connect again. No setting.

* State: `xrSplashStep` is pure (`tests/m4/test_splash.c`). One call per frame from `xr_session.c`, right after `xrBuildLayers`: `xrSplashLayers(ctx, &fout, nowNs())`. It also asks for the art; nothing else of the session knows about the splash.
* Art: `JOB_SPLASH` on the panel worker (the worker owns the font and the image cache), into `ATLAS_SPLASH` (640 x 360 at x 1408, y 3072, right of the keyboard). SteamVR has no layer colour scale, so the fade is five redraws of the art at 100, 80, 60, 40 and 20 % alpha; the worker keeps only the newest and the quad shows whatever has landed, never a paler one than the frame's step asks for. The frame loop never waits for the worker.
* Layers: no splash layer when a picture is shown and no fade is running, and none when the list is up or no stream. During the fade it is one layer over the picture; at the layer limit it is not added (it is the first to go, ahead of the toast). With no picture it replaces the picture's layer, so the count is the same.
* Not covered: the seconds of `stream_start` itself, while the app list shows its loading state (or the flat UI shows "Starting ...", which is the flat window's own), are not the splash's; it begins when the stream is up and waits for the picture.


## The bar's row and Bring here (lane bar-glare)

* **The white beam over the picture near the bar.** The bar's row used to be the move pill's zone (`hoverTest`, 0.28 of the width) plus each button's own box; everything else under the picture, including under the pill, under the buttons and a hidden button's place, was the picture's margin (`HOVER_HALO`). The pointer's pick (`pickHand`, xr/core/xr_ray.c) counts the margin as merely near something, and an awake controller whose ray is on the picture takes the pointer at once from a hand that is only near. So with the right hand aimed along the bar and the left held up with its ray on the picture, the pointer, the beam and the PC's cursor jumped to the left hand the moment the right's ray crossed one of those spots: a full white beam from the left controller across the middle of the picture, and the bar hid. Now `hoverTest` gives the whole row (`barRowHolds`: from the outer edge of the leftmost button's zone to the rightmost's, and as deep as the buttons' zones reach) that no button claims to `HOVER_BAR`, so the ray along the bar is on something all the way. The same rule in a room (`roomHover` runs `hoverTest` on the bar's frame). Tests: `xrcore_layout` (`testTheWholeRowIsTheBar`) and `m4_replay_bar` (`tests/m4/data/make_bar_replay.py`: the recorder's CSV of the right hand along the bar while the left points at the picture; both fail on the old zone).
* **Bring here (`BAR_SLOT_HERE`, `BTN_CELL_HERE`, `HOVER_HEREBUTTON`).** Outermost on the left, in head aim's place: head aim shows only with the screen on the head and Bring here only with it in the world (`xrBringHereCanAct`, xr_place.c), and neither in a room, so the two never show together. A press is `xrBringScreenHereFromBar`: the Screen tab's Bring here (`xrBringScreenHere`), except that the eyes are on the bar when it is pressed, so `layoutBringHereFromBar` lifts the gaze by the angle the bar's row hangs under the picture's middle; the picture comes round with its bar where the eyes were, at its height, and a second press leaves it put (the plain one would drop the picture's middle onto the bar every press). The trigger is held back from the PC, since the screen lands under the ray. No toast, as the panel's and the binding's give none. Lit by `hoverKind` in xr_layers.c, so there is no hot flag. The face is a PC screen, an arrow down and a head and shoulders under it.
* **Slots, left to right:** Head aim or Bring here (out 3), Gamepad (out 2), Exit (out 1), Environment (out 0), move pill, Cog (out 0), Keyboard (out 1), Ray (out 2), 3D (out 3), Host (out 4). Outside a room the left has four buttons and the right five; in a room the left has three.
