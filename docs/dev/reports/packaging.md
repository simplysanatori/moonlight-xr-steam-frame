# packaging/ report (lane P, branch lane/packaging)

Groundwork for M8. Everything below was run on the Frame in the lane worktree; nothing touched `~/Games/MoonlightXRNative/app`, the Steam shortcut (appid 2761093165), or Steam itself. The live shortcut path of `install.sh` is therefore the old, unchanged CEF code and was not run (only its "Steam not reachable" branch was).

## What each file does

| File | What |
| --- | --- |
| `build.sh` | Checks host tools, updates submodules, builds LiteRT with `depth/tools/build-litert.sh` if `dist/lib/libmlxr_litert.a` is missing (`NO_DEPTH=1` opts out; `MLXR_LITERT_ROOT` moves it), configures every time (so a new LiteRT is noticed; passes `-DMLXR_LITERT_DIR` when it exists), builds with `JOBS` (default 3), then runs `tools/ldd-audit.sh` on `moonlight-xr` and `backend/mlxr-cli`. With no target given, a missing binary is an error. The audit is skipped by itself when `pacman` is absent (CI) or with `SKIP_AUDIT=1`. `mlxr-hello` is no longer audited (it is still built). |
| `packaging/install.sh` | Source checkout or unpacked release (detects `app/moonlight-xr` next to itself). Installs `moonlight-xr`, `mlxr-cli`, `mlxr-hello` (source mode), the launcher, `seed-from-moonlight-qt.sh` + `seed_moonlight_qt.py`, `bug-report.sh`, `docs/probe/SUMMARY.md`, `VERSION`, `LICENSE.txt`, `CREDITS.md` and a mirror of `assets/` (rsync --delete, or copy-and-swap) into `DEST` (default `~/Games/MoonlightXRNative/app`). `--seed` imports from Moonlight Qt only when `$MLXR_CONFIG_DIR` (default `~/.config/moonlight-xr`) has no `client.pem`+`client.key`; it never replaces an identity. `NO_SHORTCUT=1` / `--no-shortcut` skips Steam. Otherwise it adds or updates the shortcut "Moonlight XR Native" with the same JS as before; if Steam is unreachable it says so and exits 0 (files are installed). `--art DIR` (or `packaging/art/`, none exists) copies `poster|wide|hero|logo|icon`.png/jpg into Steam's `grid/` as `<appid>p`, `<appid>`, `_hero`, `_logo`, `_icon`; untested because no art exists and the live path was off limits. If a release is unpacked into `DEST` itself, the copy is skipped. Idempotent; prints each step. |
| `packaging/steam_cef.py` | Unchanged except that an unreachable CEF port now gives one line instead of a traceback. |
| `tools/release.sh` | `moonlight-xr-steam-frame-<version>-linux-arm64.tar.gz` plus `SHA256SUMS` in `$OUT` (default `~/Games/MoonlightXRNative/releases`). Version = `git describe --tags --dirty` minus a leading `v`; with no tag, `0.0.0-<commit count>-g<sha>` (there are no tags yet, so: tag `v0.1.0` before the first real release). Binaries are `strip --strip-unneeded` copies (build is RelWithDebInfo; 9 MB unstripped for `moonlight-xr`). Deterministic ordering, owner 0, mtime = commit time. Layout: `MoonlightXRNative/{install.sh, steam_cef.py, README.md, LICENSE.txt, CREDITS.md, VERSION, app/{moonlight-xr, mlxr-cli, moonlight-xr.sh, seed-from-moonlight-qt.sh, seed_moonlight_qt.py, bug-report.sh, VERSION, docs/probe/SUMMARY.md, assets/}}`. Unless `NO_VERIFY=1` it then checks the checksum, unpacks to a temp dir, runs the unpacked `install.sh` twice (temp `DEST`, temp config dir, `NO_SHORTCUT=1`), diffs the installed tree against the tarball, runs `mlxr-cli` (usage), and runs the ldd audit on the installed binaries. |
| `tools/bug-report.sh` | `~/Games/MoonlightXRNative/reports/mlxr-report-<date>.zip` (`REPORT_DIR` overrides). Contains both logs (hex strings after key-like names masked), `config.ini` (secret-looking keys masked; none exist today), `hosts.ini` without `cert` and `mac` lines, `docs/probe/SUMMARY.md`, `system.txt` (version, git, `uname -a`, SteamOS build id, binary, log file sizes, disk, OpenXR runtime), `kernel.txt` (`journalctl -k --since -2h` filtered for iris/msm/vidc/smmu/hangcheck/gpu fault, plus a count line). Never `client.key`, `client.pem`, `uniqueid`, `servercerts/`. Uses `zip`, or `python3 -I zipfile` if absent. Works from the repo (`tools/`) and from the install dir. |
| `.github/workflows/build.yml` | See below. |
| `README.md`, `CREDITS.md`, `assets/licenses/README.md` | Front page; the one table of every component/asset (same table in both files). Added licence texts to `assets/licenses/`: `CC-BY-4.0.txt`, `moonlight-common-c_LICENSE.txt`, `enet_LICENSE.txt`, `nanors_LICENSE.txt`, `openxr-sdk_LICENSE.txt`, `jsoncpp_LICENSE.txt` (copied from the submodules; they get installed with `assets/`). |

## How it was tested

- `JOBS=2 ./build.sh` in the worktree: full build, then
  `ldd audit ok: build/moonlight-xr (135 SteamOS libraries ...)` and `ldd audit ok: build/backend/mlxr-cli (28 SteamOS libraries ...)`. The 135 count is large because FFmpeg's transitive libraries are counted; all are pacman-owned, none FOREIGN or MISSING. LiteRT was already present, so the build-LiteRT branch was not run (it is a plain call to the existing script).
- `install.sh` into `DEST=<scratch>/inst1/app NO_SHORTCUT=1 MLXR_CONFIG_DIR=<scratch>/cfg1 --seed`, twice: first run imported the real Moonlight Qt pairing into the temp config dir (read-only on the Qt side; the temp copy of the key was deleted after), second run printed "already has a client identity, not importing". Without `--seed`, with no identity: prints the `mlxr-cli pair` hint. With `STEAM_CEF_PORT=1`: the "NOT added (is Steam running?)" branch, exit 0.
- `tools/release.sh` round trip (output in scratchpad): tarball 66 MB, `sha256sum -c` OK, two installs from the unpacked tarball, installed tree identical to the tarball, `ldd audit ok` for the unpacked `moonlight-xr` and `mlxr-cli`. Also unpacked into a directory and installed with `DEST` = its own `app/`: "nothing to copy".
- `tools/bug-report.sh`: zip with the files listed above, checked there is no key/cert file in it; masking `sed` expressions checked on sample lines (`pinch_x` and `keyboard` are not masked, `client_key`, `aes key`, `secretthing` are); the no-`zip` Python path produced a valid zip (`testzip()` None).
- Workflow: parsed with PyYAML (structure fine); not run (no GitHub here).
- `ctest -E '^(audio_pulse|video_|shaders_)'` on the Frame: 42/42 passed in 25 s (-j2).

## GitHub Actions (`ubuntu-24.04-arm`)

Steps: checkout with submodules and tags, apt `-dev` packages, cached LiteRT (`~/Games/.../litert/dist`, key = hash of `build-litert.sh`; built by that script on a miss, ~12 min), `JOBS=4 SKIP_AUDIT=1 ./build.sh`, `ctest -E '^(audio_pulse|video_|shaders_)'`, `OUT=dist NO_VERIFY=1 tools/release.sh`, upload artifact.

Excluded tests and why:
- `audio_pulse`: needs a PulseAudio server (exits 77 = skip without one anyway).
- `video_*` (`video_samples`, `video_decode_*`, `video_lockstep_hevc`, `video_reschange_hevc`, `video_colour_check_negative`, `video_reset_hevc`, `video_q08c_hevc_1080`): need `/dev/video-dec0` (Qualcomm Iris); `video_samples` also needs an ffmpeg binary with x264/x265.
- `shaders_*` (17): need a Vulkan device (Turnip on the Frame). Lavapipe could do some, untested.
Everything else runs on CPU and passed locally: backend (fake Sunshine on localhost), xrcore, ui incl. ASan, depth (LiteRT on CPU), other audio tests, `stream_evkbd`.

Not validated: the apt package list (names from memory of Ubuntu 24.04; `glslc` is in universe; the OpenXR-SDK configure may want more X/Wayland/GL headers than listed; I added libwayland-bin, wayland-protocols, libgl/egl/x11/xcb dev), clang 18 vs the Frame's 19, and whether LiteRT's configure works on the runner.

**The CI tarball is not usable on the Frame.** Ubuntu 24.04 has FFmpeg 6.1 (`libavcodec.so.60`), SteamOS has 7.0 (`.61`), so the binary would not start there. The artifact is named accordingly and nothing is attached to releases; real releases come from `tools/release.sh` on the Frame (`gh release create` by hand).

## Proposed changes to `packaging/moonlight-xr.sh` (for the M4 agent / orchestrator; not made)

1. Log rollover. Today the wrapper moves `moonlight.log` to `moonlight.previous.log` on every launch, so a second launch right after a crash (or a launch that exits at once, such as SteamVR not running) destroys the log that mattered. Keep this in the wrapper (the app only writes to stdout/stderr) but only rotate when the old log has more than the `== ... launching` banner, e.g. `[ "$(wc -l < "$LOG")" -gt 1 ] && mv -f ...`. `bug-report.sh` reads exactly `$MLXR_LOG_DIR/moonlight.log` and `moonlight.previous.log`.
2. Banner: add the version and SteamOS build to the first log line: `echo "== $(date -Is) moonlight-xr-steam-frame $(cat "$HERE/VERSION" 2>/dev/null) on SteamOS $(. /etc/os-release; echo $BUILD_ID)"`. `install.sh` writes `VERSION` next to the wrapper.
3. Environment: the wrapper already honours `MLXR_LOG_DIR`; it should leave `MLXR_CONFIG_DIR` and `MLXR_VERBOSE` alone (it does) and document them. `bug-report.sh` honours `MLXR_LOG_DIR`, `MLXR_CONFIG_DIR`, `REPORT_DIR`. No new variables needed.
4. The `mlxr-hello` fallback (`BIN=./mlxr-hello` when `moonlight-xr` is missing) can go: `install.sh` refuses to install without `moonlight-xr`.
5. Steam launch options reach the app through `"$@"` already; README documents `--host/--app/--mode/--wake`. If M4 adds a settings-driven host choice, README's "first host in hosts.ini, Desktop, 2560x1440x90" sentence needs updating.

## Open questions

- Version scheme: no git tags exist. Suggest tagging `v0.1.0` at M4 so `git describe` is meaningful; until then tarballs are named `0.0.0-<n>-g<sha>`.
- Shortcut art: none provided. If wanted, drop `poster.png`, `wide.png`, `hero.png`, `logo.png`, `icon.png` into `packaging/art/` (it is picked up and also shipped in the tarball). I did not run the grid-copy path.
- Upstream says nothing separate about `settings_icon.png`/`kofi_qr.png`; listed as "no separate licence, shipped under GPLv3". The Ko-fi QR and the support sheet belong to Sean Gilleece's project; keep or drop is a policy call.
- CC BY 4.0 attribution for the rooms/controller is in README and CREDITS; the app itself could show it on an About panel (M4).
- `libmlxr_litert.a` links statically, so LiteRT's dependency licences must ship: they are in `assets/depth/third_party_LICENSES.txt`, which the installer copies.
- The release tarball is 66 MB (assets are 91 MB uncompressed, mostly the depth models and ASTC atlases). A depth-less variant would be `NO_DEPTH`-style packaging; not done.
- `ldd-audit.sh` is not usable outside SteamOS (pacman); CI skips it.
