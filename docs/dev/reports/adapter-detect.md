# Adapter detection (lane adapter-detect)

Question: is there a Steam Frame Wireless Adapter at all? Before this lane the plan took the adapter address
from the stored list and probed it, or skipped it by preference. It did not know whether the adapter link
exists. Now it looks first, on the Frame side, without root.

## What is checked

The adapter link is the Frame's own access point interface, `wlanap`. Its name comes from the candidate
classified as adapter (its interface), else from the interface table's AP, else `wlanap`.

1. Exists and up: `/sys/class/net/<if>` exists; `operstate` is `up` (or `unknown` with a carrier that is not 0);
   `carrier` is not `0`. Else `<if> missing` or `<if> down`.
2. Stations (first source that works wins):
   - `iw dev <if> station dump`, run as the user with `LD_LIBRARY_PATH` and `LD_PRELOAD` removed, 1.5 s limit.
     Counts `Station` lines, takes the strongest `signal:`.
   - nl80211 station dump over generic netlink (what `mlxr_net_link_read` already uses). Used when `iw` is not
     installed or fails.
   - the neighbour table (`/proc/net/arp`): complete entries on the interface. Only used when both of the above
     are unavailable. A complete entry is one with flag 0x2 and a real MAC.
   - If none can say, the adapter is assumed present ("station list unavailable (assumed present)"). This keeps
     the old behaviour on a machine where detection does not work.
3. The PC: the candidates classified as adapter are looked up in the neighbour table on that interface
   (`pc_known`, `pc_in_table`). This does not decide anything: the probe still does. A station with the PC not in
   the table gives "PC address not in the neighbour table" in the reason text, because the entry appears only after
   the first packet.

A stale neighbour entry never makes a station: when `iw` or nl80211 say zero stations the result is link only.

## Result

`mlxr_adapter_info` (in `backend/netpath.h`), filled into `plan->adapter`:

| `state` | meaning | text in the log |
|---|---|---|
| `MLXR_ADAPTER_NONE` | interface missing or down | `no adapter found (wlanap down)`, `(wlanap missing)` |
| `MLXR_ADAPTER_LINK_ONLY` | up, no station | `no adapter found (wlanap up, no station)` |
| `MLXR_ADAPTER_STATION` | a station is associated, or unknown | `present (wlanap up, 1 station (-48 dBm), PC address in the neighbour table)` |

Other fields: `exists`, `up`, `stations`, `stations_known`, `stations_from` ("iw", "nl80211", "neighbours"),
`signal_dbm`, `pc_known`, `pc_in_table`, `reason`, `ifname`, `checked`.

## Behaviour

- Auto: with no station (NONE or LINK_ONLY) the adapter candidates are marked `skipped`: never probed, never chosen.
  The log has `network path: Steam Frame Wireless Adapter: no adapter found (wlanap up, no station)` and the
  reason line says `chose Wi-Fi: no Steam Frame Wireless Adapter found (...)`. With a station nothing changes.
- Adapter forced, no station: no probe and no 4 s retry window. Wi-Fi is used. `plan->fell_back` and
  `plan->adapter_absent_fallback` are set; the toast is the one the client already shows for a path change:
  `Steam Frame Wireless Adapter not reachable (no adapter found: wlanap up, no station). Using Wi-Fi.`
  `app/main.c` also writes it as a warning line (`network path: ...`) and the existing `g_netToast` shows it in the
  headset.
- Wi-Fi forced: unchanged. The adapter is not probed as the fallback when it is absent (nothing to find there).
- The detection runs in `mlxr_net_select` after classifying and reading the links.

## Files

| Path | Change |
|---|---|
| `backend/netpath.h` | `mlxr_adapter_state`, `mlxr_adapter_info`, `mlxr_adapter_inputs`, `mlxr_net_adapter_*`, `mlxr_net_plan_detect_adapter`; plan fields `adapter`, `adapter_inputs`, `adapter_absent_fallback`; candidate field `skipped` |
| `backend/netpath.c` | the pure part: parsing (`iw` text, `/proc/net/arp` text), `mlxr_net_adapter_assess`, text, skip logic, forced-adapter fallback in `mlxr_net_choose`, log lines |
| `backend/netpath_sys.c` | reading the system: sysfs, `/proc/net/arp`, `iw` via `posix_spawn`, nl80211 station count |
| `backend/cli.c` | `mlxr-cli adapter [<interface>]`, read-only, probes nothing; exit 0 present, 3 not |
| `app/main.c` | one warning line after `mlxr_net_select` when the adapter fallback happened |
| `tests/backend/test_netpath.c` | tests below |

## For the flat launcher lane

Call, with no plan and no probe:

```c
mlxr_adapter_info a;
mlxr_net_adapter_detect(NULL /* "wlanap" */, NULL, 0, &a);   /* pass the PC's addresses to fill pc_in_table */
bool grey = a.state != MLXR_ADAPTER_STATION;                 /* or !mlxr_net_adapter_usable(&a) */
char text[192];
mlxr_net_adapter_text(&a, text, sizeof text);                /* tooltip: "no adapter found (wlanap up, no station)" */
```

It takes under 100 ms when `iw` is there (it starts one process) and never blocks longer than 1.5 s. It needs
`backend/netpath.c` and `netpath_sys.c` linked. The launcher is a Qt binary: if it does not link the backend, run
`mlxr-cli adapter` and read the exit code (0 present, 3 not) and the first output line (`adapter: none|link-only|station`).
Poll it again while the page is open: the PC may join later.

## Tests (`tests/backend/test_netpath.c`)

Facts are fed through `mlxr_adapter_inputs` as text (a plan takes them through `plan->adapter_inputs`), so no test
reads the real system except one that asks for an interface that does not exist. All addresses and MACs are made up.

- `test_adapter_assess`: wlanap missing; down (operstate, carrier, lowerlayerdown); "unknown" operstate; up with no station
  (and a stale neighbour entry); one station with the PC in the table; one station with the PC not in the table
  (entry on another interface, incomplete entry); no PC given; two stations and the stronger signal; `iw` absent with
  nl80211 count, with neighbours, with nothing (assumed present); another interface name; the real reader on a missing interface.
- `test_adapter_plan`: auto with no station (skipped, Wi-Fi chosen, log line), auto with wlanap down, auto with a station
  (probed as before), adapter forced with no station (no probe, no retry wait, exact toast), forced with wlanap down,
  Wi-Fi forced, adapter forced with a station (old "not reachable" text), the pure `mlxr_net_choose` with the adapter marked
  absent, and an unchecked adapter keeping the old text.
- The existing select tests now say "a station is present" explicitly, so they do not depend on the machine.

## Open risks

- The result is a snapshot when the stream starts. The PC can join later; the flat launcher should poll.
- A station is any device associated to `wlanap`, not necessarily the PC. If another device joins, the adapter is
  probed, and the probe (uuid check) then decides, as before.
- `iw` may be absent or not allowed on another image: nl80211 then answers. If netlink is blocked too, the
  neighbour table is used, and if that is empty the adapter is assumed present (no regression).
- The neighbour table fallback can count a station that left a moment ago (entries last for minutes).
- `operstate` of an AP interface can be `unknown` on some drivers; handled with the carrier file.
- The interface name `wlanap` is the Steam image's. Another name is found only through the interface table (an AP-mode
  interface with an IPv4 address).
