# audio lane report (lane A, branch lane/audio)

Opus decode, HRTF virtual surround, low-latency PulseAudio playback, UI click, and a by-ear tone tool,
as a static library `mlxr_audio` (audio/CMakeLists.txt; links `mlxr_xrcore`, libopus, libpulse,
libpulse-simple for the tests). Built and tested on the Frame itself (aarch64), clang and gcc, no warnings.
Nothing outside `audio/` and `tests/audio/` was touched. No requests for other lanes except the notes in
"For the orchestrator".

## Files

| file | what |
|---|---|
| `hrtf.c/.h` | C port of `Hrtf.java`: loads the 37 KEMAR WAVs from a directory, diffuse field EQ, resample to the stream rate (175 taps at 48 kHz), 72-azimuth mirrored ring, reversed for xr_audio |
| `surround.c/.h` | C port of `BinauralRenderer.java` + `VirtualSurround.java` + `HeadYaw`: 5.1/7.1 -> 2 ears over `xr_audio` (FFT kernel, direct kernel fallback), head-relative azimuths, crossfade on turning, runtime enable/disable, thread-safe pose |
| `pcmq.c/.h` | bounded float PCM FIFO (mutex), drop-oldest at the cap, fade-in after a splice or underrun |
| `pulse_out.c/.h` | shared PulseAudio threaded mainloop + context (refcounted) and a float32 playback stream with a small pump-driven queue |
| `opus_out.c/.h` | `AUDIO_RENDERER_CALLBACKS` for moonlight-common-c: multistream decode -> stereo / surround / downmix -> `pulse_out`; stats, mute, pose |
| `click.c/.h` | synthesised UI click (two kinds), own stream on the same context, `play()` never blocks |
| `opus_layouts.h` | header-only: the Opus multistream parameters a Sunshine/GFE host uses (encode side, for tests and the tool) |
| `audio_test_main.c` | `mlxr-audio-test` |

## What the stream session (orchestrator) must call, and when

```c
#include "opus_out.h"
#include "click.h"

/* once, before LiStartConnection */
AUDIO_RENDERER_CALLBACKS ar;
mlxr_opus_out_callbacks(&ar);                  /* capabilities: DIRECT_SUBMIT | ARBITRARY_AUDIO_DURATION */
static MlxrOpusOutConfig acfg;                 /* must outlive LiStartConnection (read in init) */
mlxr_opus_out_config_default(&acfg);
acfg.hrtfDir   = "<assets>/hrtf/kemar";        /* NULL = never use HRTFs, 5.1/7.1 get a plain downmix */
acfg.surround  = true;                         /* initial state of the toggle */
LiStartConnection(..., &ar, ..., &acfg /* audioContext */, 0 /* arFlags */);
```
`StreamConfiguration.audioConfiguration` can be `AUDIO_CONFIGURATION_STEREO`, `_51_SURROUND` or
`_71_SURROUND`; the renderer takes channel count, mapping and `samplesPerFrame` (5 or 10 ms) from the
`OPUS_MULTISTREAM_CONFIGURATION` common-c hands to `init`, so nothing else has to be passed. `init` fails only for a
bad channel count or Opus config; if PulseAudio cannot be opened the stream still runs silently
(`stats.sinkDead = 1`), and missing HRTF files fall back to the plain downmix (logged on stderr).

During the session (any thread, lock-free, cheap enough to call every frame):

| call | when |
|---|---|
| `mlxr_opus_out_set_pose(yawRad, pitchRad)` | every frame loop iteration: the head pose **relative to the screen** (the stream quad): yaw positive when the head is turned LEFT of the screen centre, 0 when the screen is locked to the head (e.g. headlocked/2D mode). Pitch is stored but not used (KEMAR has only the horizontal plane). NaN/inf are ignored. |
| `mlxr_opus_out_set_muted(true/false)` | headset-off (proximity) / back on. The audio thread corks and flushes the stream on its next packet (<= 5 ms) and drops packets; unmute resets the decoder and convolver state. |
| `mlxr_opus_out_set_surround_enabled(b)` | the user's virtual-surround setting, any time; crossfaded over one 5 ms block. Off = plain stereo downmix of 5.1/7.1. No effect on stereo streams. |
| `mlxr_opus_out_stats(&s)` | perf overlay: `underruns`, `overruns`/`droppedFrames` (queue cap), `queueMs`, `latencyMs`, `bufferedMs`, `processUs`/`processMaxUs`, `lostPackets`, `decodeErrors`, `mutedPackets`, `surroundActive`, `sinkDead`. After `cleanup` it returns the totals of the stream that ended (log "audio: N underruns over the stream"). |
| `mlxr_click_init(NULL, err, n)` once at app start (or at stream start), `mlxr_click_play(MLXR_CLICK_PRESS/TICK)` from the frame loop on UI press/hover, `mlxr_click_set_volume(0..1)`, `mlxr_click_shutdown()` at exit | the click is independent of the stream session and works in the menus before any stream; it needs only the PulseAudio connection |

`LiStopConnection` calls `stop` then `cleanup`; nothing else to do. A new `LiStartConnection` re-inits cleanly.

### Threading
- `init`/`start` run on the thread that called `LiStartConnection`, `stop`/`cleanup` on the one that called
  `LiStopConnection` (after the audio threads are joined). `init` blocks up to a few hundred ms (HRTF load + FFT
  spectra ~ 40 ms for 7.1, PulseAudio connect).
- `decodeAndPlaySample` is declared `CAPABILITY_DIRECT_SUBMIT`, so common-c calls it on its audio receive thread, one
  packet at a time (every 5 ms). It does Opus decode + convolution on that thread (0.3-0.5 ms on this device) and
  never waits for the sound server (it takes the PulseAudio mainloop lock for a few microseconds to push). `data == NULL` is a lost
  packet: packet-loss concealment for one frame.
- Everything in the table above is callable from any thread. `mlxr_surround_*` used directly: `set_pose`,
  `set_enabled`, getters are atomics; `create/destroy/reset/render` must not overlap (one audio thread).
- `mlxr_click_play` only sets an atomic and posts a semaphore; a helper thread writes and corks the click stream
  ~0.5 s after the last click so an idle UI does not keep the sound device awake. Measured 0.04 ms per call, 20000 calls
  in 1-3 ms.

## Latency (measured on this device)

Buffering we control: server buffer `tlength` asked 20 ms, PipeWire grants 15.3-16.0 ms (3 x 256-frame
quanta, `minreq` 5 ms), plus our own queue capped at 10 ms, which is empty in steady state (data goes straight
into the server buffer whenever it has asked for it). **Worst case 25 ms, typical 15 ms, queue 0.0 ms** in every run, so the
"<= 30 ms buffered" target holds. Bounds are enforced, not hoped for: a 400 ms burst written in one go keeps
15 ms in the server + 10 ms in the queue and drops the other 375 ms (counted); one second of paced writes afterwards
is back to 14.9 ms.

`latencyMs` is PulseAudio's own report for the whole path (our buffer plus the device chain behind it):
- into a temporary null sink: 10-15 ms; end to end (packet handed to the renderer -> sample seen on the sink monitor,
  which itself adds ~5 ms of capture chunk): 12-22 ms for stereo, 7.1 downmix and 7.1 virtual surround.
- into the real headset speakers (one deliberate run, bypass on): **60 ms** reported, 1 underrun at start (the sink
  was suspended and woke up). That is the SteamOS device chain behind our stream (loopback -> Valve speaker filter chain
  -> ALSA), which this lane cannot change; our own part is the 15 ms above. Worth knowing for A/V sync: the video
  lane may want to add ~40-60 ms of intentional video delay or accept the offset. If the headset/DAC route differs
  (e.g. USB/BT audio) the number will differ.
- Opus + HRTF cost per 5 ms packet on the Frame CPU: stereo ~0.3 ms, 5.1 ~0.38 ms, 7.1 ~0.4 ms average (5-8% of one
  core), worst single packet 1-2.5 ms (first packet). The convolution adds no block delay (output frames come out of
  the call that took the input), only the filters' own onset of ~1 ms (32-sample EQ shift + 0.5 ms KEMAR onset).

## Adaptive jitter cushion and thread priority (lane av-latency, 2026-10-10)

Owner: "audio crackling when playing a video". In the 22:30 run the stream's underrun counter (a total, not
per window) stayed at 2 for three minutes, then rose by 21-78 per 5 s from 22:34:32 to 22:35:37 while a video
played at 40-75 fps: the same windows show "connection status: poor", rtt 24-318 ms and unrecoverable audio FEC
blocks; 3D was off for half of it and the panel worker idle. So: late packets over the Frame's hotspot, not CPU.
The fixed path above (16 ms in the server, our queue empty in steady state and capped at 10 ms, oldest dropped)
runs dry on any gap over ~16-25 ms, and moonlight-common-c's FEC queue alone holds packets up to 30 ms while it
waits for a lost one.

Now (`jitter.c`, driven by `pulse_out.c` under the mainloop lock, on for the stream, off for the click):
- a cushion kept in our queue on top of the server buffer, 0 ms to start (latency on a clean link unchanged);
- an underrun whose data comes back within 400 ms (late, not the host going quiet) grows it to cover the gap
  seen (at least +10 ms, at most 40 ms), and playback then waits until server buffer + cushion are queued;
- 30 s without such an underrun takes 5 ms off; once a second the queue's low point above cushion + 4 ms is
  spliced out with a 2 ms crossfade (`mlxr_pcmq_skip`), so latency comes back down (clock drift too);
- bound: ours <= 16 + 40 + one quantum ~ 64 ms at worst; stock moonlight-qt allows 15 ms device + 50 ms SDL
  queue + 30 ms common-c queue. `MLXR_AUDIO_JITTER=min,max` overrides; `fixedBuffer` turns it off.
- Simulation in `audio_jitter` (Wi-Fi stalls of 25-60 ms every 700 ms, 60 s): fixed buffer 86 underruns,
  cushion 4 (then 40 ms target); after 5 clean minutes back to 0 ms target, 16 ms max.

common-c's audio receive thread (which decodes and writes, DIRECT_SUBMIT) asks rtkit for nice -10 on its first
packet, as SDL's audio thread does for stock moonlight-qt (`thread_prio.c`, sd-bus through dlopen, no new link
dependency). `MLXR_AUDIO_PRIORITY=rt` asks for SCHED_RR 10 instead (bounds RLIMIT_RTTIME as rtkit requires),
`off` leaves it. Both checked against this Frame's rtkit.

The log's 5 s "xr: latency:" line has, per window: cushion, underruns split late / after silence, lost, dropped
and trimmed ms, the longest gap between packets, and how long the receive thread waited for a CPU
(schedstat run delay) with its CPU share: late underruns with long gaps and ~0 wait = network; long waits =
scheduling.

## IMPORTANT: SteamOS spatializes every output stream (found on this device)

`/etc/wireplumber/scripts/spatial-node-config.lua` + `60-spatial-audio.conf` (setting
`node.stream.enable-spatial-audio` = true by default): while the built-in speakers are the default sink,
WirePlumber loads a `spatialize_filter_chain` (Valve's `vraudiocompositor` LADSPA plugin, keyed on the stream's PID)
for **every** Stream/Output/Audio node (mono, stereo, quad, 5.1, 7.1 all supported) and re-routes the stream into it. For us that means
(a) our own binaural render would be spatialized a second time, (b) +~30 ms (stream latency reported 44-48 ms through
it vs 14 ms without), and (c) it also hijacks `target.object`: a stream aimed at another sink is moved to the
chain, so my first test runs were audible on the speakers instead of going to a null sink (see "Honest notes").

Exempt nodes (from the script): node name containing both `virtual` and `sink`, or `filter`+`chain`, or `.spatialize_filter_chain`,
or prefix `alsa_loopback_`, or `pw-audio-namespace`. So the renderer names its node (property `node.name`, which
PipeWire-Pulse does not take from the stream name by itself) **`Moonlight XR virtual sink`** by default = bypass, and the click
`Moonlight XR UI virtual sink`. `MlxrOpusOutConfig.systemSpatializer = true` (tool: `--system-spatializer`) gives the plain
name so the system spatializes it, `streamName` overrides both. That is a workaround keyed on a Valve script's name filters and could
change with a SteamOS update; verified here by `pw-top` (no `*.spatialize_filter_chain` node, quantum 256, latency 14 ms) and by
a null-sink recording matching the transmitted level to 0.02 dB.

**Decision for the owner by ear**: bypass (our HRTFs, screen-anchored by head pose) vs `--system-spatializer`
(Valve's, anchored to the app/window in the SteamVR scene by PID: possibly nicer for stereo, wrong for our 5.1/7.1 path because it would
double-process). Default is bypass for surround; for plain stereo streams the orchestrator may prefer `systemSpatializer = true`
if A/B listening favours it. Also relevant to the video lane: with the system spatializer the audio source follows the PID's window, which
for a native OpenXR client is unknown territory.

## Port notes (what differs from upstream)
- Upstream puts the speaker layout, rear treatment, head yaw, KEMAR equalisation/resampling in Java and only the convolution in
  `xr_audio.c`; all of it is ported here to C: azimuths 5.1 `-30 30 0 0 -135 135`, 7.1 `-30 30 0 0 -150 150 -90 90`, LFE to both
  ears at 0.707, non-front channels at -3 dB, rear pair low-passed at 5 kHz and -1.5 dB (rear delay 0 as shipped, parameter kept),
  0.25 degree crossfade threshold, FFT kernel with direct fallback, 32-sample EQ pre-ring. Numerics match upstream's design; the FFT and direct kernels agree
  to 3e-7 in the test.
- Samples are float (normalised +-1) end to end instead of int16; the PulseAudio stream is float32.
- Yaw convention is upstream's: positive = head turned left of the screen, azimuth is clockwise, speaker azimuth + yaw.
- Added: runtime enable/disable (one-block crossfade between HRTF and plain downmix, convolution history kept running while off),
  stereo streams are never touched, a plain downmix path when the HRTFs are missing.
- Not ported: Android audio effects session, `AudioTrack` buffering, underrun polling (Pulse reports underflows by callback instead).
- Opus mapping: the decoder is created with the mapping from the stream config exactly like moonlight-qt; `opus_layouts.h` documents the
  mappings a Sunshine host sends (5.1 `0 1 4 5 2 3`, 7.1 `0 1 6 7 2 3 4 5`) and the tests use them.
- Stream session behaviour on a dead PulseAudio server: no reconnect; `sinkDead` goes to 1 and frames are discarded. Restart the stream to retry.

## Tests (`ctest --test-dir build-audio -R audio`, all pass; also clean under ASan+UBSan, TSan on surround/queue)

| ctest | checks | what |
|---|---|---|
| `audio_hrtf` | 199 | 37 WAVs load; 128/160/175 taps at 44.1/48 kHz; ring mirrored (az 360-a = ears swapped); EQ flattens the average spectrum 11.4 -> 2.3 dB spread; azimuth wrap; failure message |
| `audio_surround` | 120 | impulse per virtual speaker (5.1 and 7.1): FL left ear +9.4 dB and 12 samples (0.25 ms) early, FR mirrored exactly, FC 0.0 dB / lag 0, LFE identical in both ears (0.707), BL/BR 5-9 dB with 13-18 samples, SL/SR +13.8 dB / 35 samples (0.73 ms); head turned +-90 moves FC to the right/left ear by 13.8 dB and 35 samples, 180 degrees is balanced, FL with head turned 30 degrees is centred ("held to the screen"), 360 degrees is the identity; crossfade is exactly `was*old + (1-was)*new` over the block (6e-8) and the sub-0.25 degree move does not fade; enable toggle is an exact fade between downmix and HRTF; FFT vs direct 3e-7; pose set from another thread while rendering; NaN pose ignored; bad dir/channels |
| `audio_jitter` | 151 | cushion policy (grow on late data, not on silence, cap, calm shrink, refill), trim, crossfaded skip, Wi-Fi stall simulation old vs new, thread priority parse/off and schedstat |
| `audio_queue` | 140 | FIFO order across wrap, drop-oldest keeps the newest, exact drop counts, oversized push, set_capacity, fade-in after splice/underrun, producer/consumer threads (no loss/dup/reorder; popped + dropped == pushed) |
| `audio_opus` | 1854 | libopus encoder -> renderer callbacks, stereo/5.1/7.1: each Moonlight channel soloed lands on its own decoded channel within 1 dB, all others > 50 dB down (silent channels decode to exact 0); simultaneous distinct tones per channel (coupled-stream mixing check); downmix routing (left/right/center/LFE and -3 dB levels); HRTF render left-right balance sign per speaker; 10 ms packets; loss concealment; garbage packet counted; mute drops packets and unmute resumes; stats; init failures; missing HRTF dir falls back; no PulseAudio server -> silent stream |
| `audio_pulse` | 51 | against the real PulseAudio API into a temporary null sink (loaded and unloaded by the test, recorded from its monitor, default sink verified unchanged; skipped with code 77 without a server): tone level within 0.02 dB (stereo), 0.8 dB (HRTF), latency <= 40 ms, no underruns/drops, mute gap 603 ms for 600 ms muted, 400 ms burst capped at 10 ms queue, click level -20.8 dBFS / 28 ms and non-blocking, 0 volume silent |

`xrcore_audio` (22628 checks) still passes. Manual: `build-audio/audio/mlxr-audio-test --tones stereo|51|71 [--surround]` (see its header: `--sink`, `--level`, `--seconds`, `--yaw`, `--system-spatializer`, `--stream-name`, `--dry`; level is clamped to <= -6 dBFS, tone <= 1 s per channel).

## Confirmed on this device vs reasoned
Confirmed: everything above in the Tests table; pulse latency numbers (null sink and real speakers); playback of the 7.1 tone sequence
through the real speaker path without decode errors (heard by nobody, so channel placement by ear is **not** confirmed); the spatializer behaviour
(config read + pw-top + recordings); ldd-audit ok (libopus, libpulse and their SteamOS dependencies only).
Reasoned only: that KEMAR filters through the headset speakers sound right (diffuse-field EQ is upstream's answer to that);
that screen-anchored yaw is the right sign in the real frame loop (matches upstream's Java contract: positive = head turned left of the screen);
whether bypass or the system spatializer sounds better; behaviour when the headset route changes during a stream (we stay on the default sink we connected to).

## Honest notes
Before I found the spatializer behaviour, my first test_pulse run (about 12 s, tones at -6 dBFS up to 1.5 s at a time, plus the click) and three debug
runs of `mlxr-audio-test` (-20 dBFS, up to 2 s each, one at -70 dBFS) were redirected by SteamOS to the headset speakers instead of the null sink, so
they were audible; then one deliberate 7.1 run on the speakers (-20 dBFS, 0.3 s per channel) as the allowed proof run. No volume or default sink was changed
(checked before/after each run) and the temporary null sinks were removed. The tests now use the bypass node name so they cannot be heard.
