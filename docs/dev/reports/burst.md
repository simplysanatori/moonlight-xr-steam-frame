# Burst loss (lane wave1-burst, PLAN 1.15)

## What was seen

Packet loss between the PC and the Frame comes in bursts: a few hundred milliseconds to a few seconds
of heavy loss, then a clean link again. During a burst the client logged storms of common-c's
`Unrecoverable frame`, `Waiting for RFI frame` and RFI request lines and asked for IDR frames over and
over. Each IDR frame is large and lands in the same congested window, so it is lost or late and the
stall gets longer (video falls to a few fps, audio lags). The client cannot fix the radio. It can stop
making a burst worse, and say what a burst was in one line.

## What the client does now

All the logic is pure and lives in `stream/rfi_watch.c` (tested in `tests/stream/test_rfi_watch.c`
with synthetic common-c line sequences); `stream/session.c` feeds it the lines, the clock and the
round trip.

**Adaptive RFI wait.** The watchdog (lane perf) asked for an IDR frame once an RFI wait was 100 ms old
and 3 complete frames had been dropped in it, then every 300 ms. Now:

    base = max(MLXR_VIDEO_RFI_IDR_MS (100), 3 x round trip)
    due  = max(wait start, last network loss) + base, at most wait start + MLXR_VIDEO_RFI_IDR_MAX_MS (1000)

and the repeats use the same rule from the last ask (300 ms apart at least). A clean LAN behaves as
before (100 ms). A long round trip waits three of them (a recovery P-frame needs a round trip and an
encode). A burst that is still losing frames (`Unrecoverable frame`, `Network dropped`, `Sending ... RFI
request`) pushes the ask on until the link has been clean for `base`, up to the ceiling. The round trip
is common-c's estimate, refreshed about once a second (stream_hud) and at every 5 s line.

**IDR request cap, `[stream] rfi_idr_ms` (default 500).** Every IDR request of the client's own goes
through one cap (`MlxrIdrCap`): the watchdog's, and hw_video's (decoder errors and damaged pictures,
whose `requestIdr` callback is now the session's capped one). At most one per `rfi_idr_ms`; a held ask
is counted, not sent. common-c's own requests (its IDR wait, `DR_NEED_IDR` through
`requestDecoderRefresh`) cannot be held from here and are counted: the burst line separates `IDR
requests sent` (every `IDR frame request sent` line, anyone's), `asked here`, `held back` and `for the
decoder`. The value is read at every ask (`xrStreamPrefsRfiIdrMs`, set by `xrApplySettings`), so a
stream that starts before the XR session applied the settings still gets the right value at its first
ask.

Why 500 ms: in the 23:46 run the recoveries that came back as an IDR frame took 40 ms to 12 s, p90
0.6 s; the damaged-picture path in hw_video already held its requests 500 ms apart, and the watchdog
repeated every 300 ms. A healthy link answers an IDR request within about 100 ms (a round trip, an
encode, the frame's transfer), so a second request inside 500 ms only lands while the first IDR frame
is still on its way, which in a burst is exactly the congested window. 500 ms still re-asks before the
p90 of the long waits, so a lost IDR frame does not leave the picture frozen much longer. Choices in
the launcher: No limit, 250, 500, 1000, 2000 ms; config.ini takes 0 to 5000.

**Burst summary line.** A burst runs from the first lost or dropped frame (network loss, an RFI request,
a frame dropped waiting for RFI or IDR, a frame our decoder refused) to the last one, and closes after
1 s without another (checked on every line, every decoded frame and every 5 s line). Its duration is
first to last lost or dropped frame. One line when it closes:

    video: burst at 14:03:21.512 for 450 ms: frames lost on the network 3 (unrecoverable 2), dropped waiting
    for RFI 3 / IDR 0, RFI waits 2, IDR requests sent 1 (asked here 1, held back 1 by rfi_idr_ms 500, for the
    decoder 0), recovered by IDR; 4 per-frame lines folded

`recovered by` is what restored the picture after the burst's last loss: `RFI` (a post-invalidation
P-frame), `IDR` (a post-invalidation I-frame, or an IDR frame reaching the decoder after an IDR wait),
`neither`, or `not recovered (still waiting when it closed)` (the link went silent). While a burst is
open each per-frame line kind (`Waiting for RFI frame`, `Invalidate reference frame request sent`,
`Network dropped`, `IDR frame request sent`, ...) is written the first time and then only counted
(`per-frame lines folded`). `Unrecoverable frame` lines keep their own one-a-second throttle
(log_throttle.h). The parsers (loss counters, watchdog, throttle) still see every line.

**5 s line.** New parts: `IDR asked by the RFI watchdog N (wait W ms now, N put off by a burst), held
back by rfi_idr_ms 500: N; bursts N (longest N ms); rx drop misc +N (...)`.

**`rx drop misc`** (backend/netdiag.c, tested on synthetic proc text in `tests/backend/test_netdiag.c`):
the 5 s change of receive drops the line's `udp rcvbuf errors` / `in errors` part does not show. The
stream interface (the one the PC's address is reached through, found at stream start) from
`/proc/net/dev`: rx `drop`, `err` and `fifo` (the driver or the stack threw the packet away before any
socket); Udp `MemErrors` and `InCsumErrors` from `/proc/net/snmp`. The total `+N` is the sum of those.
`own sockets +N of M` sums the `drops` column of `/proc/net/udp` and `udp6` for this process's UDP
sockets (inodes from `/proc/self/fd`): it tells whether the system-wide rcvbuf errors were ours, so it
is not added to the total. A part that could not be read twice shows `?`.

## Reading a lossy run

- A burst storm that the cap tamed: burst lines with `held back N` above 0 and `IDR requests sent`
  close to `duration / rfi_idr_ms` or less; the 5 s line's `IDR requests` per window low.
- The adaptive wait at work: `put off by a burst` above 0, `wait W ms now` above 100 on a long round
  trip.
- The radio, not the headset: `frames lost on the network` large, `rx drop misc +0`, `udp rcvbuf
  errors +0`. Drops in `rx drop misc` or `own sockets` instead point at the headset (interface queue,
  socket buffer, CPU).

## Not done

common-c's internal IDR requests cannot be capped without changing the vendored library. The burst
gap (1 s) and the ceiling of the wait (1 s, `MLXR_VIDEO_RFI_IDR_MAX_MS`) are reasoned, not measured on
the Frame's 6 GHz link; the first lossy run's burst lines are the data to tune them.
