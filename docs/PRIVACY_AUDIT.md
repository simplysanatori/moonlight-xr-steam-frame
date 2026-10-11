# Privacy audit for 0.6.0-alpha.1

Done on 2026-10-11, before the repository was made public. The goal: nothing personal about the owner in the repository, in the release tarball, or in what the app writes for a tester to share.

This file names categories and counts, never the values themselves.

## What was searched

Every tracked file outside `third_party/` (524 files): source, tests and fixtures, launcher patches, packaging, tools, the CI workflow, `docs/probe/*` dumps, the developer notes and reports, and the asset READMEs. Then the unpacked release tarball.

| Category | Looked for |
| --- | --- |
| Names and accounts | Fragments of the previous owner identity and account names, e-mail addresses |
| Network | The owner's PC host name, the client host name fixture, home LAN addresses, Frame hotspot (adapter link) addresses, the Tailscale address, the public IP, MAC addresses, the Frame's access point name, Wi-Fi names and bands in log excerpts |
| Ids and secrets | The host UUID, the Moonlight client unique id, the Steam user id, serial numbers, device and driver UUIDs in probe dumps, session cookies, access tokens (`ghp_`, `github_pat_`, `sk-` and similar), API keys, private keys and certificates, certificate fingerprints, the SteamGridDB key path |
| Paths and devices | Home directories other than the default `/home/steamos`, personal folder names, device models (keyboard, router) |
| Git history | Tokens and keys in `git log -p` |

## What was found and changed

| Category | Found | Change |
| --- | --- | --- |
| Previous owner identity | 0 in the tree | none |
| PC host name | 60 lines, 18 files | `gaming-pc` in tests; "the owner's PC" in notes |
| Client host name fixture | 3 lines, 1 file | `GAMING-PC` |
| Home LAN addresses | 53 lines, 15 files | 192.168.1.x placeholders |
| Hotspot addresses | 24 lines, 4 files | 10.35.50.x placeholders |
| Tailscale address | 13 lines, 8 files | 100.64.0.5 |
| Public IP | 6 lines, 2 files | 203.0.113.50 (documentation range) |
| Host UUID | 17 lines, 10 files | a made-up UUID |
| PC MAC address | 9 lines, 8 files | a locally administered placeholder MAC |
| Frame access point name | 1 line, 1 file | `frame_1A2B3C4D` |
| Client unique id, Steam user id | 0 | none |
| Certificate fingerprints | 3 lines, 1 file | `<fingerprint>` |
| SteamGridDB key path | 1 line, 1 file | removed |
| Probe dump ids (device, driver and pipeline cache UUIDs, audio cookies, host name) | 16 lines, 6 files | `<uuid>`, `<cookie>`, `<hostname>` |
| Keyboard model | 4 lines, 4 files | "a Bluetooth keyboard" |
| Personal folder and tool paths | 6 lines, 3 files | removed or made generic |
| Tokens, API keys, private keys | 0 | none (`BEGIN PRIVATE KEY` appears only in code that handles keys) |
| E-mail addresses | only `noreply` and `example.com` addresses and licence texts of bundled code | none |

Placeholders come from documentation ranges (RFC 5737) or obviously made-up values. The Steam shortcut appid 2761093165 stays: it is derived from the install path and the shortcut name.

Byte-level fixtures in the backend tests (an mDNS answer, a binary MAC in a Qt settings file) were rewritten to the placeholders, and the tests pass.

## Git history

`git log -p` holds no access tokens, API keys or private keys. One commit adds a code comment that mentions a PEM header.

Older commits still contain the values listed above, before this scrub: the PC host name, addresses, host UUID, MAC and access point name, and one old test fixture string with a fragment of the previous owner name. The history was not rewritten. Publishing the repository publishes that history. To avoid it, publish a single squashed commit of this tree instead, or rewrite the history before making the repository public.

## The release tarball

`tools/release.sh` now:

- copies only tracked files from `assets/`, so nothing generated locally gets in;
- leaves out the converted Steam Frame controller models (Valve's), which `install.sh` makes on each Frame;
- stops if the staged tree has `config.ini`, `hosts.ini`, `uniqueid`, keys, certificates, `servercerts`, logs, reports, `.conf` files, backup files or the LiteRT build;
- runs `tools/privacy-check.sh` on the finished tarball and stops on any finding.

`.gitignore` keeps build directories and the converted controller models out of git. Configuration, pairing and logs live outside the repository (`~/.config/moonlight-xr`, `~/Games/MoonlightXRNative`).

## What the app writes

The live logs (`moonlight.log`, `moonlight-launcher.log`, `moonlight-console.log`, `vrtiming.log`) keep host names and addresses, so users can debug their own network. The README says not to post them as they are.

`bug-report.sh` is the thing to share. It now masks, in every file it bundles:

- IPv4 and IPv6 addresses (each gets a stable token such as `<ip1:lan>` or `<ip2:cgnat>`, so lines about one address still match up);
- MAC and Bluetooth addresses;
- host names: the saved PCs, the PCs in the flat window's list, the headset's own host name unless it is a SteamOS default, `*.local` names, and names in "host ..." log lines;
- UUIDs, Moonlight unique ids, serial numbers, the Steam user id in paths, home directories, e-mail addresses;
- Wi-Fi network names (from `iw dev`, NetworkManager and the logs), keeping the band;
- PEM blocks, certificate fingerprints, long hex and base64 strings.

Timings, counters, rates, codec lines and version numbers stay. It never includes `client.key`, `client.pem`, `uniqueid`, `servercerts/` or the flat window's settings file (which holds the client key). `hosts.ini` goes in without its certificate and MAC lines, and masked. `system.txt` uses `uname` without the host name.

The in-headset "Report a problem" sheet said it would send the note and the log. It never sent anything. It now says that it saves the note on the Frame, and its button reads **Save**. `bug-report.sh` includes the newest note, masked.

## Verification

`tools/privacy-check.sh` checks for tokens, keys, certificates, e-mail addresses, MAC addresses, IPv4 addresses outside the placeholder ranges, Steam user ids and home directories. The owner's own values are not written anywhere in the repository, not even as search patterns. They are in a local list outside it (`~/.config/moonlight-xr-privacy-patterns.txt`, one regular expression per line), which the script also reads.

```sh
tools/privacy-check.sh
tools/privacy-check.sh ~/Games/MoonlightXRNative/releases/moonlight-xr-native-0.6.0-alpha.1-linux-arm64.tar.gz
tests/tools/test_bug_report.sh
env -u LD_LIBRARY_PATH ctest --test-dir build -R tools_
```

Results on the release commit: the tree check found 0, with the local list loaded. The tarball check found 0. The bug report test passed: every planted address, name, id, SSID, serial, e-mail and certificate was masked, and the planted timings and counters were kept.
