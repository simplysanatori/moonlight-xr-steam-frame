# USB cable path (lane wave4c-usb)

The Steam Frame's USB-C port can carry a network to the PC. This lane adds it to the network path choice as a third path, "USB cable", next to Wi-Fi and the Steam Frame Wireless Adapter. The code is in `backend/netpath.c`, `backend/netpath_sys.c` and `app/main.c`. The tests are in `tests/backend/test_netpath.c`. Nothing here has been run with a cable on a real headset and PC yet. The headset test is at the end.

## What the Frame provides

Measured on the Frame (SteamOS VR 0.5.6, kernel 6.18, probe of 2026-10-29, no PC attached). All of this is Valve's own setup, not something this project installs.

- The USB device controller is `a600000.usb`. A USB gadget called `g1` is already built and bound. It has two functions: FunctionFS for adb, and an NCM network function whose interface is `usb0`. The kernel has NCM built in. RNDIS and ECM gadget functions are not built.
- It exists because the Frame is in developer mode. The service that builds it is conditioned on the developer-mode flag file `/etc/systemd/system/adbd.service.d/steamos-devkit-enabled`. Without developer mode there is no `usb0`; nothing in this lane enables it.
- `usb0` is configured with a small private network, a /29, with the Frame on the first usable address. A dnsmasq instance (`usb-ncm-dnsmasq@usb0`) hands the PC the next address, matched by the NCM host MAC, plus a small pool above it. It offers no default gateway and no DNS server.
- With no PC attached, `usb0` is `DOWN` with `NO-CARRIER`, still holding its address. Its sysfs path is `/sys/devices/platform/soc@0/a600000.usb/gadget.0/net/usb0`.
- The Type-C port was in device role with a charger on it during the probe. The probe did not show whether a PC enumerates the adapter, how fast it runs, or whether the PC charges the headset.

## How the client sees it

- **Which interface is USB.** `mlxr_netifs_read` marks an interface `usb` when its name starts with `usb`, or its uevent says `DEVTYPE=gadget`, or its sysfs path runs through a gadget device. The name rule alone matches the Frame. The other two rules are there in case Valve renames the interface. Wi-Fi interfaces are never USB. A USB Ethernet dongle on the Frame in host mode is named `eth0` or `enx...` and stays a plain "Wired network" (that route needs no code, see PLAN 4c.5).
- **Up means carrier.** An interface counts as up only with `IFF_UP`, `IFF_RUNNING` and a carrier file that does not say 0. A USB interface that is down is ignored: it is not a candidate and no address is classified onto it.
- **The PC's address is never written down.** `mlxr_net_subnet_peers(addr, mask)` returns the other usable addresses of the interface's own subnet: not the network address, not the broadcast address, not the Frame's own. It starts at the address after the Frame's own and wraps to the bottom. A /29 gives up to five, a /30 one, a /31 one (RFC 3021). Each interface gets at most three guesses (`MLXR_NET_USB_MAX_PEERS`), so a /29 with the Frame on the first usable address gives the PC's lease first, then the next two. A subnet larger than a /24, a /32 and a mask that is not a prefix give none: a link that big is a LAN, and the ARP table and mDNS can find the PC there.
- **Discovery is unicast.** Nothing on the cable answers mDNS for the PC, and the PC does not know the Frame's name, so each guess is probed directly: a serverinfo request (is it the PC? same uuid) and TCP connects to the same port for the round trip. This is the same probe as for the other paths.
- **Scoring is the same as for the others** (median plus jitter plus tail plus 50 ms per failed probe plus the history penalty). The cable has no radio, so it normally wins on round trip. In a tie (scores within 3 ms) the better link quality decides (a cable counts as a wired link, quality 100), then Wi-Fi, then the cable, then the adapter. The history file `netpath-history.ini` has a `[usb]` section like the other two.
- **Classifying an address as "USB cable"** needs the address to be on the USB interface's own subnet. A default route that happens to leave through a USB tether does not make every off-link address a cable address.
- **The learned-addresses list is not used for the cable.** `hosts.ini` keeps the addresses a PC answered on (`extra_addresses`). The cable's addresses are left out: they are worked out from the interface each time, and a stale one would only cost a timeout when the cable is out.

## The setting

| Where | Value |
| --- | --- |
| `config.ini`, section `[stream]` | `network_path = usb` (also `auto`, `home`, `adapter`) |
| Command line | `--network usb` |
| Environment | `MLXR_NETWORK=usb` |
| Flat launcher | Settings, Connection, Network connection, "USB cable". It stores `networkPath=usb` and passes `--network usb` |

The stored value `usb` is stable; the label shown to the user is "USB cable". Precedence is unchanged: `MLXR_NETWORK`, then the flag, then `config.ini`, then `auto`.

Choosing `usb` when no cable is up uses the other path and says why. The notice in the headset reads, for a cable that is not there:

`USB cable not reachable (no USB cable found: usb0 down (no cable, or the PC end is not up)). Using Wi-Fi.`

and when the cable is in but the PC does not answer on it:

`USB cable not reachable (<address>: connect timed out). Using Wi-Fi.`

With `usb` asked for and a cable that is up, the client retries it for four seconds (the same window as for the adapter) before it falls back.

## Hot-plug

- Every choice of path (when a stream starts) reads the interfaces afresh, so a cable plugged in at any time before that is found.
- The client's worker thread rescans every 10 s (`MLXR_USB_WATCH_PERIOD_S`) in the PC list, the app list and during a stream. It logs the changes: `USB cable link up: usb0 up, carrier on, 3 peer candidates` and `USB cable link lost: usb0 down (no cable, or the PC end is not up)`. During a stream over the cable the lost line reads `USB cable link lost while streaming over it: ...`.
- The PC list's online check (`connectHost`) also tries the nearest guess last, when a cable is up, so a headset with only the cable plugged in sees the PC as online.
- The flat launcher rescans `/sys/class/net` every 10 s as well and shows in the Network connection row whether a cable is detected (`MLXR_LAUNCHER_USB: cable on usb0` / `no cable` in the launcher log).
- Pulling the cable mid-stream ends the stream the way any lost connection does: the client returns to the launcher. There is no automatic reconnect yet (PLAN 3.5). The only change is the log line.

## Log lines to expect

At every choice (`moonlight.log`):

```
network path: USB cable: usb0 up, carrier on, 3 peer candidates
network path: setting auto from default; N candidate addresses: ... , <address> (usb, USB cable via usb0), ...
network path: USB cable <address> via usb0: 15/15 ok, rtt median ... ms, p90 ..., jitter ... (serverinfo ... ms); score ... ms; link: wired
network path: chose USB cable: score ... ms against Wi-Fi ... ms (lower is better); decided by latency (...)
network path: using USB cable <address> via usb0 (setting auto from default)
```

The two guesses nobody answers on show as `network path: usb <address> via usb0: no answer (...)`. With no cable: `network path: USB cable: usb0 down (no cable, or the PC end is not up)`. `mlxr-cli probe-paths` prints the same facts and a `usb cable:` line, and starts no stream.

## Host side (PLAN 4c.3)

What the PC needs. None of this is automated by the project.

1. **A driver for the NCM adapter.** Windows 11 has an in-box USB NCM class driver, so the PC should show a new "USB Ethernet" style adapter when the cable is plugged into a USB data port. The Frame only offers NCM (no RNDIS), so an older Windows without the NCM driver would need one. Not verified on this PC.
2. **Address.** The PC takes a lease from the Frame's dnsmasq: the address next to the Frame's own. The lease is matched by the NCM host MAC, so it is the same each time. There is no gateway and no DNS on that link, so Windows marks it "No internet" and keeps its default route elsewhere. That is intended: only the PC's own traffic to the Frame uses it.
3. **Network profile: Private.** Windows puts a new adapter on "Public" and its firewall then drops unsolicited inbound connections. Set the adapter's network profile to Private (Settings, Network and Internet, the Ethernet adapter, Network profile type).
4. **Firewall.** Allow Apollo (or Sunshine) through the Windows firewall on that profile. Apollo's installer may already have added rules for the Private profile, in which case step 3 is enough; not checked.
5. **Apollo binds all interfaces** by default, so nothing needs changing in Apollo. If the address-binding option has been set to one interface in its web UI, put it back to all.
6. **Discovery over the /29 is unicast.** The PC's mDNS does not listen on that adapter and the cable is not a broadcast domain the Frame browses, so the headset does not "find" the PC there by name. It probes the guesses above directly. This is why the first candidate, the PC's lease, matters, and why the headset never needs a PC name for this path.

If the PC does not answer on the cable, check in this order: the adapter is up with an address on the headset's subnet (`ipconfig`), `ping` the headset's address from the PC, the adapter's profile is Private, Apollo is running, and `mlxr-cli probe-paths --network usb` on the headset prints which guess failed and how.

## Not verified without hardware

- That the PC enumerates the NCM adapter and takes the lease.
- That the cable's round trip is below the others. A third-party note on the Frame's wired mode reports a ping of about 0.9 ms; it has not been measured here.
- Throughput. A USB 2.0 link should carry the stream's bitrates (real throughput about 280 to 350 Mbit/s), but it is not measured.
- Power. In device mode the Frame draws from the PC's port. Whether the PC charges the headset, holds it, or lets it drain during a stream is the open item PLAN 4c.4. A hub with USB-PD pass-through may fix it; not verified.
- Whether the gadget exists outside developer mode. The probe says no, by the service condition.

## Headset test

Plug a USB data cable between the Frame's USB-C port and a USB port on the PC (a charge-only cable has no data lines and gives no `usb0` carrier).

1. On the PC: a new network adapter appears with an address next to the headset's. Set it to Private. `ping` the headset's `usb0` address.
2. In the headset's flat launcher, Settings, Connection, Network connection: the "USB cable" entry says "Cable detected" within 10 s of plugging in. Choose it, or leave Automatic.
3. Start a stream. `moonlight.log` has the lines under "Log lines to expect". With Automatic it should say `chose USB cable` when its round trip is lower; with USB it says `the setting is usb`. A toast in the headset reads "Connected over USB cable." (Automatic) when the stream comes up.
4. Pull the cable during the stream. The stream ends and the launcher returns. The log has `USB cable link lost while streaming over it`.
5. Choose "USB cable" with the cable out, start a stream: it uses Wi-Fi and the headset shows the "USB cable not reachable (no USB cable found: ...). Using Wi-Fi." notice.
