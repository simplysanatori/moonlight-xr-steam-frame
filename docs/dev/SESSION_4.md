# Session 4: start here

Read this file first, then docs/dev/HANDOFF.md sections 2, 3 and 7 (paths, commands, rules), then docs/dev/STATUS.md from the bottom. Everything older is history.

## 1. State on 2026-10-11

- The repository is public: https://github.com/simplysanatori/moonlight-xr-native, branch `main` only. Latest release: `v0.6.0-alpha.2` (pre-release). The same build is installed on the Frame (`~/Games/MoonlightXRNative/app`, `VERSION` says 0.6.0-alpha.2).
- History was rewritten for privacy. Commit hashes in older notes are stale. Commit identity is the repo-local `simplysanatori` no-reply address. Nothing personal may enter the tree: `tools/privacy-check.sh` runs in ctest and in `tools/release.sh`, reading the owner's private patterns from `~/.config/moonlight-xr-privacy-patterns.txt` (never commit that file, never print its values).
- The owner's PC runs Apollo (a Sunshine fork; it reports itself as Sunshine). The Frame reaches it over home Wi-Fi and over the Steam Frame Wireless Adapter. The "Network connection" setting (Automatic / Wi-Fi / Steam Frame Wireless Adapter) picks the link before each stream and logs why.
- The Lepton route (the Android fork) is retired on this Frame; its shortcut and install were removed. The native client is the only Moonlight on the headset.

## 2. The problem to fix first: black screen in the void and in rooms

Owner's report, 2026-10-11 about 05:33 and 05:34 (two runs): the headset entered VR and the room, but the picture stayed black the whole time, in the void and in the room. The stats overlay showed very bad numbers.

What the logs say (`~/Games/MoonlightXRNative/moonlight.log`, `moonlight-console.log`, `moonlight-launcher.log`, `vrtiming.log`; all four are from these runs, keep them until this is solved):

- The frame loop, SteamVR and the network were fine: 90 fps, 1 to 12 layers, compositor clean, round trip 3 ms, 0 frames lost on the network, 0 waiting for IDR.
- The hardware HEVC decoder failed on almost every frame: the 5 s stats say `frames 84, skipped 0, errors 6673` by the end, and the console log has 7171 lines of `[vdec] CAPTURE buffer N flagged V4L2_BUF_FLAG_ERROR (sequence 0)` plus a few `submit: no free OUTPUT buffer, AU of ~84 KB dropped`. Video drawn: 0 to 2 fps, once 11.6 fps. So the black screen is decoded frames never arriving, not a rendering problem.
- moonlight-common-c logged, right at the start, `Sending speculative RFI request for predicted loss of frame 2`, `Leaving speculative RFI mode after OOS video data at frame 2`, `Sending RFI request for unrecoverable frame 3`, then `IDR frame request sent` over and over.
- What was different from the runs that worked on 2026-10-10 (1440p, 90 fps, HEVC, 49 to 80 Mbps, hardware decode 2 to 4 ms, no decoder errors):
  1. Reference frame invalidation is now advertised for HEVC (commit "RFI advertised", `MLXR_VIDEO_RFI`, default on). With RFI the host may use more reference frames and recovery frames instead of IDRs.
  2. The flat launcher now passes `--virtual-display` (Apollo creates a virtual display for the client) and `--fps match`; the stream options line says `virtual display 1, scale factor 0, limited range (Apollo host)`.
  3. Bitrate was 126000 kbps (the launcher's conf has `bitrate=126000`; the working runs used 49000 to 80000). Launch line: `--mode 2560x1440x90 --bitrate 126000 --virtual-display --fps match --deadzone 2 --network auto`.
  4. Low-latency pacing (`MLXR_VIDEO_PACING` default 2), a 4 ms decode latch, and the audio cushion were added the same evening; they ran fine in the 2026-10-10 23:xx runs, so they are less suspect.

Ranked hypotheses:

1. RFI. The Iris V4L2 stateful decoder, or the way `video/` feeds it (number of CAPTURE/OUTPUT buffers, reference handling), cannot decode a stream that uses reference frame invalidation. The errors start at the first frames, which matches "the bitstream references something the decoder does not have". Test: `MLXR_VIDEO_RFI=0`.
2. Apollo's virtual display changes the encoded stream (different display mode, colour format or HEVC profile/level than the real desktop). Test: Settings, turn "Use virtual display" off.
3. 126 Mbps at 1440p90 overruns the decoder's OUTPUT buffers (the "no free OUTPUT buffer" lines) and the dropped access units make every following frame undecodable until an IDR, which keeps happening. Test: bitrate 50000.
4. H.264 instead of HEVC decodes fine, which would point at HEVC-specific handling in `video/`.

How to test without the owner: a headless run against the test clip (`build/moonlight-xr --file ~/Games/MoonlightXRNative/test-1080p60.mp4 --seconds 15`) proves the decoder still works on a plain stream; it does not reproduce RFI or Apollo. The owner's runs are the real test. Give the owner a matrix of four short runs (each setting changed alone) and read the four logs: decoder `errors` must be 0 and `video N fps drawn` near 90. Env switches go in the Steam shortcut's launch options as `VAR=value %command%`, or in the flat Settings page where a row exists. Then fix the cause in code, not by leaving a switch off: if RFI is the cause, either make the V4L2 path handle it (buffer counts, keeping reference frames mapped) or stop advertising RFI for this decoder and say so in the docs. If the virtual display is the cause, find what Apollo changes (its web UI logs the virtual display mode) and either request a matching mode or default the option off.

## 3. Also open

- Headset checks never done by the owner: the 90 Hz "Match headset" refresh default, the Apollo options (virtual display, scale, clipboard sync), physical gamepads, the stats window grip-drag, the keyboard move bar, the controller models at rest and moving. Each has a line in docs/dev/STATUS.md.
- Apollo's server commands are not implemented (needs a moonlight-common-c patch and a menu in VR).
- `launcher_vs_stock` test is skipped without a stock moonlight-qt build.
- The SteamVR dock icon: Steam was restarted on 2026-10-11 and now holds the shortcut's icon; confirm it shows.

## 4. How this session works (the owner's rules)

- This session runs on Fable 5.1 at high effort for planning and review only. All implementation goes to agents: Sonnet for simple, self-contained work (scripts, docs, small UI changes, ports); Opus 5.5 for anything with judgment (the decoder path in `video/`, the frame loop in `xr/`, Vulkan, threading, headset debugging). Run independent agents in parallel, one git worktree each (`git worktree add ../lanes/<name> -b lane/<name> main`), build with `BUILD=build-<name>`, never two agents on the same files. The orchestrator reruns the tests, merges, installs (`NO_SHORTCUT=1 ./packaging/install.sh`, only when the app is not running) and pushes.
- Pushing: the owner pastes a GitHub token for the `simplysanatori` account when a push is due. Use it only in the push command (`git -c http.extraheader="Authorization: Basic $(printf 'simplysanatori:%s' <token> | base64 -w0)" push origin main`), never store it in files, config or memory, and do not revoke it unless the owner says so. The owner has a second account for private work; nothing from this project goes there.
- Writing: plain and factual, short sentences, no em dashes, no "it's not X, it's Y" phrasing, first person where the author speaks. The README's authorship paragraph is the owner's own wording; keep it.
- Privacy: no names, addresses, host names, IDs, serial numbers, Wi-Fi names or tokens in commits, docs, reports or hand-off notes. Logs on the Frame may contain them; `bug-report.sh` masks them. Agents must not quote log lines that carry such values.
- Safety: no sudo, pacman or steamos-readonly; never restart Steam or SteamVR without the owner asking (a Steam restart is `systemctl --user restart steam.service`, run in the background because the calling shell is killed with it); no streams against the owner's PC; `--wake` only when asked.
- Releases: `tools/release.sh` builds the tarball on the Frame (the CI artefact cannot run here) and refuses anything personal; GitHub releases are pre-releases below 1.0.0, created with the API using the owner's token; release notes live in `docs/RELEASE_NOTES_<tag>.md`.

## 5. Prompt for the next session

```
Continue Moonlight XR for Frame on this Steam Frame. Read ~/Games/MoonlightXRNative/src/docs/dev/SESSION_4.md,
then HANDOFF.md sections 2, 3 and 7 and the end of STATUS.md. Plan on Fable, implement with agents
(Sonnet for simple work, Opus 5.5 for the decoder, frame loop and headset debugging), one worktree per lane.
First job: the black screen (SESSION_4.md section 2). Start with the owner's four-run test matrix and the
logs from 2026-10-11 05:33 and 05:34, find the cause in the decoder path, fix it in code, and cut
v0.6.0-alpha.3. I will paste a GitHub token when it is time to push.
```
