# Network path quality (lane netpath-quality)

Request: Automatic should not only pick the faster link. It should prefer the lower latency and the fewer
drops. Before this lane the choice leaned on link quality (signal, tx rate) and "Wi-Fi unless it is weak".

## What changed

### Probing

- 15 probes per candidate instead of 7 (`MLXR_NET_AUTO_SAMPLES`), still concurrent per candidate. The first
  asks serverinfo (is it the PC?), the others time the TCP handshake.
- Gap between probes 10 ms (was 15). A bare handshake that takes over 250 ms counts as failed (was 400 ms); the
  serverinfo probe keeps 400 ms. A lost SYN is resent after a second, so a slow handshake is a drop.
- Grace after the first path finished raised from 300 ms to 700 ms, so the slower path can send its 15 probes.
  The whole probe still ends at the 1.5 s budget. A dry run on the Frame took 1.1 s.
- Per candidate: median rtt, 90th percentile rtt (nearest rank), jitter (mean deviation from the median, as
  before), failed probes out of sent.

### Score

Lower is better:

    score = median + jitter + (p90 - median) + 50 ms * failed + history penalty

- History penalty: 0 up to 0.5 percent of frames lost last time, then 20 ms per percent, capped at 150 ms.
  No history, too old (14 days), or fewer than 500 frames: no penalty.
- Choice: the lowest score. Scores within 3 ms are a tie. A tie goes to the better link quality when the two
  differ by more than 5 points, else to Wi-Fi. Two addresses on one path: the lower score, no margin.
- Hard rules unchanged: a path with no answer, another PC, or (the adapter) no station is never chosen. The
  forced settings (home, adapter) work as before, including the retry window and the fallback toast.
- Removed: the "weak" rule (signal under -70 dBm, rate under 4x the bitrate, half the probes failed) as a
  reason to leave Wi-Fi. Failures now cost 50 ms each, so half the probes failing costs about 400 ms. The
  `poor` flag is still computed and the link line is still logged.
- `mlxr_net_better()` now compares scores as above (it used to rank failures, then poor links, then round
  trips). Public: `mlxr_net_score()`, `mlxr_net_hist_penalty()`, `mlxr_net_plan_apply_history()`.

### Reason text

Examples (made-up numbers):

    chose adapter: score 6.0 ms against Wi-Fi 49.0 ms (lower is better); decided by the last stream's losses (Wi-Fi: 2.3% lost last time; adapter: no history)
    chose adapter: score 8.0 ms against Wi-Fi 53.0 ms (lower is better); decided by failed probes (Wi-Fi 1 of 15, adapter 0 of 15)
    chose adapter: score 2.0 ms against Wi-Fi 12.0 ms (lower is better); decided by latency (rtt 2.0 ms against 12.0 ms, jitter 0.0 ms against 0.0 ms)
    chose Wi-Fi: scores within 3 ms (Wi-Fi 4.2, adapter 2.3), the better link breaks the tie (quality 97 against 65)
    chose Wi-Fi: scores within 3 ms (Wi-Fi 4.0, adapter 2.0), Wi-Fi preferred on a tie

"Decided by" names the part of the score where the winner is furthest ahead. The per-candidate log line now
has p90 and the score. `plan->reason` grew from 200 to 360 bytes.

### History

`netpath-history.ini` in the config directory (`mlxr_config_dir()`: `MLXR_CONFIG_DIR`, else
`~/.config/moonlight-xr`), mode 0600, written through a temporary file and a rename:

    [home]
    frames=17510
    lost=0
    jitter_ms=1.25
    rtt_ms=3.00
    time=1790000000

Keywords are `home` (Wi-Fi) and `adapter`. No addresses, names or network names. A missing, cut-short or
damaged entry is dropped (every key must be there and be a plain number); the rest of the file still counts.
Loss rate = lost / (received + lost). Code: `backend/nethistory.c`.

The log line before the candidates, only when a file entry exists:

    network path: history: Wi-Fi lost 0.0% of 17510 frames last time (jitter 1.2 ms, rtt 3 ms, 2 h ago); adapter lost 2.3% of 4300 frames last time (jitter ..., penalty 46 ms)

### Recording

- `stream/session.c`: `stream_network_figures()` returns frames received, frames lost on the network, arrival
  jitter and the last rtt for the whole stream. The latency counters reset at every stats line, so each window
  is folded into a stream total first (`foldNetWindow`). Jitter is the square root of the window variances
  weighted by their interval counts (the cadence code's own figure, so clock drift between windows does not
  count). The totals, `g_lat` and `g_cadence` are cleared in `stream_start`.
- `app/main.c`: `stopStream()` calls `mlxr_nethist_record_stream()` before `stream_stop()`. It places the
  stream's address on a path with the interface table (as the choice does) and writes the entry. Streams under
  500 frames are not recorded, so a failed start does not overwrite good history. `connectChosen()` sets
  `plan->history_dir`.
- `mlxr-cli probe-paths` reads the same file and prints p90, score and history per candidate. Its default
  sample count follows `MLXR_NET_AUTO_SAMPLES`.

## Tests

`tests/backend/test_netpath.c`: score parts (median, jitter, tail, failures, history), the penalty curve and
cap, tie margin (exactly 3 ms is a tie), latency over link rate, failures over a few ms, history applied,
ignored when old or short, not charged when absent; file round trip, missing and corrupt files, file mode and
contents; the full `mlxr_net_select` with a fake interface table, the fake host on 127.0.0.1 (Wi-Fi) and a small
server on 127.0.0.2 (adapter), with no history, a lossy Wi-Fi, a lossy adapter and a damaged file.

## Open risks

- The history is the last stream only. One bad stream (a walk to another room) sends the next stream to the
  other path, and a good stream on that path clears it only for that path. The 14-day age and the cap limit it.
- The adapter link is only chosen if it answers the probe. A path that was avoided is not re-measured by a
  stream, so its history stays until it ages out or the other path is bad.
- Idle probes do not show losses under load; the history is what covers that, and it exists only after a
  first stream on each path.
- Jitter is the cadence figure (arrival interval spread), not the probe jitter. Only the probe jitter is in
  the score; the stored jitter is for the log.
- The loss counter comes from common-c's "Network dropped N frames" messages, as the stats line does.

## A start bitrate for each path (lane wave4b-abr, PLAN 4b.8 "ABR-lite")

Apollo and Sunshine cannot change the bitrate of a running stream, so the only lever is the bitrate at the start,
and by then the path is known (Automatic has decided). Four settings, `[stream] bitrate_wifi`, `bitrate_adapter`,
`bitrate_wired`, `bitrate_usb` (kbps, 0 = the launcher's bitrate, else 500..500000), each replace the launcher's
bitrate for a stream that goes over that path.

- Decision: `xr/core/xr_abr.c`, `mlxr_abr_pick(kind, launcherKbps, overrides[4])`. A kind outside 0..3 or an override
  of 0 gives the launcher's figure; an override and a launcher figure above 0 are held to 500 kbps .. 500 Mbps.
  Test: `tests/xrcore/test_abr.c`.
- Path kind: `mlxr_net_bitrate_kind(path, link)` (`backend/netpath.c`). The home path is Wi-Fi unless the interface it
  went through is not a Wi-Fi one (the test behind the "Wired network" description), then wired. The adapter and the
  cable are their own kinds. `mlxr_net_select` notes the kind for the chosen address (last 8 addresses), and
  `mlxr_net_kind_for_address()` finds it at stream start, when the plan is gone. A forced adapter that fell back to
  Wi-Fi is Wi-Fi: the path that carries the stream decides, not the setting. No choice made (a failed probe, the
  sequential fallback) gives -1 and the launcher's bitrate.
- Applying: `xrStreamBitrate(settings, kind, launcherKbps)` (`xr/xr_prefs.c`) logs
  `bitrate for <path>: N kbps (override|launcher)` and returns the bitrate. The one call is in `app/main.c` where
  `g_job.cfg.bitrate` is set (see the lane report for the line).
- Shown: `stream_start` keeps the bitrate it was given; the HUD style's rate line reads "54 of 98 Mb/s" (arriving of
  started with), the full stats' stream line begins "bitrate set N kbps", and the perf CSV's bitrate column follows it.
- The flat launcher mirrors the four keys as stepped rows under Network connection ("Same as above" at 0). The launcher
  needs no new argument: the client reads config.ini when the stream starts.
- Not done: raising or lowering the bitrate while a stream runs (not possible on the host), and learning a good
  bitrate from the history. The history already penalises a lossy path in Automatic's score; a lossy path now also
  has a knob.

## Failover to another path on persistent burst loss (lane wave1-failover)

The path is chosen once, at stream start; Apollo and Sunshine send one UDP flow to one client address, so there is
no multipath. A path can still go bad mid-stream (the 6 GHz link in one corner, loss in bursts of seconds, see
`burst.md`). `[stream] failover = off|on` (default off: unmeasured on the Frame) and `failover_burst_ms` (500..9000,
default 3000) let the client leave it. Automatic only: a path set by hand (setting, `--network`, `MLXR_NETWORK`) is
never left.

- **Policy** (`backend/failover.c`, pure; table-driven test `tests/backend/test_failover.c`).
  `mlxr_failover_decide(in)` gives "stay" with a reason or "switch to <path>". All must hold: the setting is on and
  the path setting Automatic; the stream is at least 20 s old; the last switch at least 5 min ago (no flapping);
  the loss bursts of the last 10 s add up to `failover_burst_ms`; another path is `present` (it answered the probe;
  an adapter without a station and a cable that was not up are never candidates, a path the stream already left
  is not) with a measurement no older than 2 h; and its score is at least 15 ms below the current path's score
  plus what the live loss costs it. The loss cost is the history penalty's own: 20 ms per percent of frames lost
  over the last 10 s once over 0.5 percent, at most 150 ms. So the adapter at +16 ms against a good link loses
  to it until the link really drops frames, and then wins; a path never wins by name. Unknown loss costs nothing;
  no frame at all in the window counts as 100 percent.
- **History**: `mlxr_failover_history` (closed bursts, ring of 32, plus the open one) sums the time inside the
  window; `mlxr_failover_samples` keeps frame counts about once a second for the percent. The session
  (`stream/session.c`) adds each closed burst in `logBurst`, and reads the open one through
  `mlxr_burst_open` (new, read only, `rfi_watch.c`). Accessors: `stream_connected_ms`, `stream_loss_window`.
- **Measurements** (`backend/netpath.c`): `mlxr_net_select` leaves the best answering candidate's score and time per
  path in `mlxr_net_quality` (`mlxr_net_quality_get`). An unforced choice (stream start, reconnect after a drop)
  rebuilds it; a forced one only updates the paths it probed, so the cable measured at the start survives a move to
  the adapter. `mlxr_net_quality_abandon(path)` drops the path the stream left until the next unforced choice,
  which is the "re-probe the abandoned path only at the next stream start" rule.
- **Forcing**: `mlxr_net_force_path(path)` makes the next `mlxr_net_select` (within 60 s, one shot) go for that
  path as if it were set by hand, with `retry_ms` 0, so a path that does not answer costs the probe and nothing
  else, and the other paths are tried at once (the usual fell-back toast, suppressed on a reconnect). The history
  of the path being left is recorded by the reconnect's teardown as for any drop, so the next Automatic choice
  charges it.
- **Loop** (`stream/failover.c`, once a second from the worker; test `tests/stream/test_failover.c` against a fake
  session): samples, asks the policy, and on "switch" logs
  `network path: switching from X to Y after N ms of loss (...)`, writes the open burst line with
  `network path switched from X to Y` at its end (`stream_note_path_switch`; the tracker restarts so it is written
  once), forces and abandons, schedules the reconnect (`mlxr_reconnect_switch`: a fresh series, first try after
  500 ms, same app, scene kept) and shows a notice that names the new path. `failover_session.c` binds it to the
  real session, netpath and settings (`xrStreamPrefsFailover`, set by `xrApplySettings`; read every second).
- **Hook the app must call** (`app/main.c`, not edited by this lane): in the worker loop, after the existing
  `stream_failed()` and reconnect branches, `else if (state == LAUNCH_STREAMING && atomic_load(&g_streaming) &&
  !atomic_load(&g_linkDown) && !g_args.file && n->streamPc >= 0 && n->streamApp > 0 &&
  mlxr_failover_poll(&g_fo, &g_rc, monoMs(), xr_app_notice)) teardownForRetry(n);` with `static MlxrFailover g_fo;`
  (all zero is its start state) and `#include "../stream/failover.h"`. Without it the setting does nothing.
- **Not measured**: the thresholds are reasoned, not measured on the Frame. The 15 ms margin, 20 s settle, 5 min
  gap and the 2 h age limit are constants in `backend/failover.h`. The first run with the 6 GHz link in the bad spot
  and the adapter or 5 GHz as the other candidate is the data (`HEADSET_TEST` list in the lane report). Other
  candidates are not re-probed mid-stream; a stream older than 2 h will not switch.

## VPN addresses only away from home (lane vpn-fallback)

Request: a PC that is also reachable through a VPN should be streamed over the VPN only when the headset is not
on the PC's network. Before this lane a VPN address was one more candidate on the Wi-Fi path. It answered at home
too (an overlay VPN routes its peers' addresses through the tunnel even on the same LAN) and could score lower
than the LAN address, so streams at home went through the tunnel, with dropped audio and a low frame rate.

- Classification (`mlxr_net_addr_is_vpn`, pure; `mlxr_net_plan_classify` fills `cand.vpn` and `cand.vpn_why`):
  VPN is an IPv4 address in 100.64.0.0/10 (the shared CGNAT range overlay VPNs use), IPv6 in fd7a:115c:a1e0::/48,
  loopback (a local port forward into a user-space VPN), or an address reached through an interface named
  `tailscale*`, `wg*`, `tun*`, `tap*`, `zt*`, `utun*`. "Reached through" is the interface table's subnet or
  default-route interface, and when `mlxr_net_select` reads the system's table itself (`plan.route_lookup`) also
  the kernel's own route lookup (`mlxr_net_route_ifname`: a UDP socket connected to the address, nothing sent, its
  source address matched to an interface), which sees policy routing and /32 tunnel addresses. Everything else is
  direct. The path is unchanged (a VPN peer is normally on the Wi-Fi path); `vpn` is a flag beside it.
- Choice (`[stream] vpn_only_away`, default true, `plan.vpn_only_away`): all candidates are probed as before, but
  while any direct candidate answered, VPN candidates are set aside in every rule (best per path, a path set by
  hand, the fallback to other paths). Probing: Automatic's 700 ms grace after the first answer starts only with a
  direct answer, or once every direct candidate has finished without one, so away from home the choice waits for
  the full budget (1.5 s) before it takes the VPN; a path set by hand (first answer in address order wins) lets a
  VPN candidate win only after every direct one has finished. False restores best-score-wins.
- One log line (`plan.vpn_note`, after the reason): `the PC answered on the home network (Wi-Fi 192.168.1.10);
  its VPN address 100.64.0.5 is not used`, `the PC did not answer on the home network; using its VPN address
  100.64.0.5 (score 38.2 ms)`, and with the setting off `... scored better than the VPN address ...` or
  `using the VPN address ... on its score`. The candidate list marks each VPN address (`VPN: 100.64.0.0/10`,
  `VPN: wg0`); the stats summary adds `, VPN`.
- Failover: `mlxr_net_quality` carries `chosen_vpn`, `vpn_only_away` and per path `vpn` (the path's figure is its
  best direct candidate when one answered, and the chosen path's figure is the chosen candidate's, which it was
  not always before). `mlxr_failover_decide` never moves a stream from a direct address to a path whose figure is
  VPN; from VPN to direct it may.
- Learning (`mlxr_net_learn_addresses`, replacing `rememberAddresses`' loop in `app/main.c`): every direct
  candidate that answered as the PC (above all the ones ARP and mDNS found) and serverinfo's LocalIP (only for a
  direct choice: over a VPN it is the PC's VPN side) are appended to the record's `extra_addresses`; VPN, loopback
  and USB cable addresses are not; nothing is removed. Next time at home the LAN address is a candidate from the
  start (and mDNS no longer has to find it; its 400 ms wait is skipped once every up interface has a candidate).
- History: `mlxr_nethist_record_stream` leaves a stream over a VPN address out; its losses say nothing about the
  link it would be filed under (Wi-Fi).
- Tests (`tests/backend/test_netpath.c`): the classification table (ranges by number, interface names, IPv6,
  zones, a VPN default route), the choice as a table (both answer, direct wins with a higher score; only the VPN
  answers; the setting off; a path set by hand; the fallback; nothing answers; no VPN address), the failover table
  (never direct to VPN, VPN to direct, the setting off), the learning (what is kept, nothing removed, a full list,
  the next plan has it), the probe ordering with two real loopback servers, and the failover table after a real
  select. The launcher test clicks the row and reads `vpn_only_away` back through the client's parser.
- Not covered: a VPN that uses none of these ranges or names (a company VPN handing out 10.x addresses on an
  interface with another name) is direct; the setting cannot tell it from the LAN.
