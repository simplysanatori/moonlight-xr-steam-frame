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
