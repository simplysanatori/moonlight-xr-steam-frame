# Backend lane report (lane/backend)

C11 port of moonlight-qt's `app/backend`: HTTP/HTTPS to Sunshine/Apollo/GFE, the full
pairing handshake, client identity, serverinfo/applist/launch/resume/cancel, a persistent
host store, an importer for Moonlight Qt's config, and `mlxr-cli`.
Everything links only SteamOS host libraries: libcurl 8.7.1, OpenSSL 3.2.1, libxml2 2.12.6.

## Files

| Path | Role |
|---|---|
| `backend/backend.h` | The whole public API (this is what the orchestrator includes) |
| `backend/nvhttp.c` | `mlxr_server`: requests, TLS pinning, serverinfo/applist/launch/resume/quit/unpair |
| `backend/pairing.c` | `mlxr_pair` (5-stage handshake), PIN generator |
| `backend/identity.c` | client RSA-2048 cert/key/unique id, `mlxr_config_dir` |
| `backend/computer.c` | `mlxr_computer_parse` (serverinfo -> struct) |
| `backend/store.c` | `hosts.ini` + `servercerts/<uuid>.pem` |
| `backend/xml.c`, `crypto.c`, `util.c`, `internal.h`, `server.h` | helpers (libxml2 tree access, OpenSSL primitives) |
| `backend/backend_limelight.h`, `limelight_glue.c` | fills moonlight-common-c `SERVER_INFORMATION` / `STREAM_CONFIGURATION` (built only if the `moonlight-common-c` target exists, as it does at top level) |
| `backend/cli.c` | `mlxr-cli` |
| `backend/seed-from-moonlight-qt.sh` + `seed_moonlight_qt.py` | importer (stdlib Python, run with `python3 -I`) |
| `tests/backend/` | 7 ctest tests, fixtures, a fake Sunshine host (`fake_host.c`) |

CMake targets: `mlxr_backend` (static lib, PUBLIC include dir = `backend/`, links curl/openssl/libxml2/pthread),
`mlxr_backend_ll` (glue + moonlight-common-c), `mlxr-cli`, tests prefixed `backend_`.
Nothing outside `backend/` and `tests/backend/` was touched. Requests for the orchestrator: none required;
see "Open questions".

## API summary (`backend/backend.h`)

All calls are blocking, return `mlxr_status` (0 = `MLXR_OK`) and fill an optional `mlxr_error{code,status,msg}`.
Status codes: `E_ARG E_NOMEM E_IO E_CRYPTO E_NET E_TIMEOUT E_TLS E_HTTP E_XML E_PIN_WRONG E_PAIR_BUSY E_PAIR_FAILED E_MITM E_NOT_PAIRED E_BUSY_OTHER`.
`E_HTTP`: the host answered but `<root status_code>` != 200; `err.status` holds it (401 = client not paired, 418 GFE audio, 400 launch refused).
`E_TLS`: handshake failed or the server cert is not the pinned one (moonlight-qt's synthetic 401).

```c
/* config + identity */
char *mlxr_config_dir(err);                       // $MLXR_CONFIG_DIR or ~/.config/moonlight-xr, 0700
mlxr_identity_load_or_create(dir, &id, &created, err);   // client.pem/client.key(0600)/uniqueid
/* one host */
mlxr_server *s = mlxr_server_new(&id, "192.168.1.10", 47989);
mlxr_server_set_pinned_cert(s, pem, err);  mlxr_server_set_https_port(s, 47984);  mlxr_server_set_gfe(s, is_nvidia);
mlxr_server_get_info(s, fast_fail, &computer, &raw_xml, err);   // HTTPS-first when pinned, HTTP fallback on TLS error
mlxr_server_get_apps(s, &apps, &n, err);          // sorted, mlxr_apps_free()
/* launch */
mlxr_launch_params p; mlxr_launch_params_init_ll(&p);   // random rikey/rikeyid, corever from common-c
p.width=..; p.height=..; p.fps=..; p.hdr=..; p.audio_configuration=MLXR_AUDIO_STEREO|51|71; p.is_gfe=computer.is_nvidia;
mlxr_server_launch_or_resume(s, &computer, appid, &p, &rtsp_url, err);   // resume iff computer.current_game != 0
STREAM_CONFIGURATION cfg; mlxr_fill_stream_config(&cfg, &p);   // width/height/fps/audio + remoteInputAesKey/Iv
SERVER_INFORMATION si;   mlxr_fill_server_info(&si, address, &computer, rtsp_url);
/* ... caller sets bitrate, packetSize, supportedVideoFormats, color*, encryptionFlags, then LiStartConnection ... */
mlxr_server_quit_app(s, err);                     // cancel + verify; E_BUSY_OTHER (599) if another client owns the game
/* pairing */
mlxr_pair_generate_pin(pin); mlxr_pair(s, computer.app_version, pin, timeout_ms, &server_cert_pem, err);
/* persistence */
mlxr_hosts_load/save, mlxr_hosts_find(key = address|uuid|name), mlxr_hosts_upsert, mlxr_host_cert_load/save, mlxr_host_record_from_computer
```
`mlxr_computer` carries: name, uuid, mac(+has_mac), local/external ip, http/https/external port, is_nvidia,
paired, via_https, current_game, state, app_version, gfe_version, gpu, `server_codec_mode_support` (SCM_*, 1 if absent),
`max_luma_pixels_hevc`, display modes (sorted like moonlight-qt).

Behaviour ported 1:1 from moonlight-qt (with the reasons kept in code comments): fresh connection per request, HTTP/1.1,
no proxy, `devicename=roth`, URL `uniqueid=&uuid=<random>` prefix, GFE placeholder uid `0123456789ABCDEF`,
fps>60 -> 0 for GFE only, `status_code` parsed as unsigned then cast (GFE 0xFFFFFFFF), "Invalid" -> 418, currentgame only
trusted when `state` ends `_SERVER_BUSY`, MJOLNIR => GFE, loopback `LocalIP` dropped, 00:..:00 MAC ignored,
`<AppTitle/>` accepted, quit verifies via serverinfo, the pairing hash is SHA-256 for server gen >= 7 else SHA-1,
all timeouts (2/5/120/30/30 s; the PIN stage waits 5 min instead of forever).

**TLS pinning.** HTTPS presents the client cert and accepts the handshake only if the server's leaf certificate is
byte-for-byte the one stored at pairing time (`SSL_CTX_set_cert_verify_callback`, installed through
`CURLOPT_SSL_CTX_FUNCTION`), plus a curl `PINNEDPUBLICKEY` (SPKI sha256) as a second layer. Verification is not off:
it is replaced by an exact-match check, like moonlight-qt (which ignores SSL errors whose certificate equals the stored
one). Consequences, both tested: an expired or oddly-formed self-signed host cert that equals the pin works (no
dependence on the Frame's clock), and any other certificate is refused with `E_TLS`. HTTPS without a pinned cert is refused (`E_ARG`).

## CLI

```
mlxr-cli identity
mlxr-cli serverinfo <host> [--xml]
mlxr-cli pair <host>              # prints the PIN, waits up to 5 min, then saves the host
mlxr-cli apps <host>
mlxr-cli launch <host> <appid> <WxHxFPS> [--hdr]     # resume if a game is already running
mlxr-cli quit <host>
```
`<host>` = `addr[:httpport]`, `[v6]:port`, or a saved host's name/uuid. An address that is not in `hosts.ini` is probed over
HTTP and matched to a saved host by uuid, so `apps 192.168.1.10` works even if the import recorded a VPN address.
Exit codes: 0 ok, 1 usage, 2 error, 3 network/timeout, 4 not paired. `MLXR_VERBOSE=1` logs requests (never secrets).
`launch` prints the rtsp URL and rikeyid only, never the key. `identity` creates `client.pem client.key uniqueid`
(`hosts.ini` appears with the first saved host).

Storage (`~/.config/moonlight-xr/` or `$MLXR_CONFIG_DIR`, dir 0700): `client.pem` 0644, `client.key` 0600, `uniqueid` 0600,
`hosts.ini` 0600 (sections per uuid: name, address, extra_addresses, mac, http_port, https_port, cert),
`servercerts/<uuid>.pem`. Writes are atomic (tmp + rename).

## What was tested live vs by fixture

### Live: NOT possible, the gaming PC is off
Commands run (read-only) against `192.168.1.10` from the Frame (192.168.1.20):
```
$ mlxr-cli serverinfo 192.168.1.10            (MLXR_CONFIG_DIR = temp dir seeded from Moonlight Qt, see below)
error: serverinfo: Failed to connect to 192.168.1.10 port 47989 after 3091 ms: Couldn't connect to server (network-error, 7)
$ ping -c1 192.168.1.10      -> 100% packet loss      $ ip neigh show 192.168.1.10 -> FAILED / INCOMPLETE
$ bash /dev/tcp probes of 47989 47984 47990 48010 -> No route to host (no ARP answer)
$ avahi-browse -rtp _nvstream._tcp -> nothing advertised
```
The MAC `02:f0:2c:5d:a5:10` is not in the Frame's neighbour table either. So `serverinfo` (HTTP and HTTPS), `apps`
against the real Sunshine/Apollo have not run. I did not send Wake-on-LAN (not asked).
The imported record's `address` is `100.64.0.5`, a VPN address (see below), which needs the VPN up.
Retry when the PC is on: `MLXR_CONFIG_DIR=<seeded dir> mlxr-cli serverinfo 192.168.1.10` then `... apps 192.168.1.10`.

### Live-adjacent: the real Moonlight Qt config, imported into a temp dir (read-only on the source)
```
$ MLXR_CONFIG_DIR=<tmp> backend/seed-from-moonlight-qt.sh
source: ~/.var/app/com.moonlight_stream.Moonlight/config/Moonlight Game Streaming Project/Moonlight.conf
identity: Moonlight stored no unique id; one is created on first use
identity: imported client.pem (sha256 <fingerprint>) and client.key (1704 bytes, mode 0600)
host #1: gaming-pc uuid=6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B address=100.64.0.5 extra=203.0.113.50 mac=02:f0:2c:5d:a5:10 paired(cert)=yes sha256=<fingerprint> apps=91
hosts: 1 imported into <tmp>/hosts.ini
$ mlxr-cli identity     -> loads without regenerating, cert sha256 <fingerprint> (matches), saved hosts: 1
```
Findings about the real file: one host `gaming-pc`, paired (has `srvcert`), 91 cached apps, no `uniqueid` stored
(Moonlight Qt here never persisted one, so a new random one is created; Sunshine identifies the client by certificate,
so the existing pairing carries over). Qt's stored addresses were rewritten for a VPN setup:
`manualaddress=127.0.0.1` (dropped as loopback), `localaddress=100.64.0.5` (a VPN address), `remoteaddress=203.0.113.50`.
The LAN address 192.168.1.10 is not stored anywhere, hence the uuid-matching in the CLI. The temp dir (which holds a copy of the
client key) was deleted after the run. To import for real: `backend/seed-from-moonlight-qt.sh` (no env), `--force` to replace an
existing identity. Note: while exploring I printed the first 150 characters of two config lines, which included the standard
PKCS#8 header and the first bytes of the RSA modulus (public); no private exponent material and no full key/cert was printed.


### By fixture / fake host (ctest, all pass, also clean under ASan+UBSan+LSan)
`env -u LD_LIBRARY_PATH ctest --test-dir build-backend -R backend`:
```
1 backend_test_parse     XML (entities, XXE not expanded), serverinfo Sunshine/busy/GFE(MJOLNIR)/old-GFE, errors 401/418/400,
                         applist (entities, <AppTitle/>, case-insensitive sort, broken app), pair/launch fixtures,
                         launch query byte-for-byte (rikeyid sign, surroundAudioInfo 196610/4128774, hdr, GFE fps, corever), mode parser
2 backend_test_crypto    key derivation vectors (python hashlib) SHA-256 / SHA-1 / leading-zero PIN, AES-128-ECB FIPS-197 vector,
                         sign/verify, tamper cases, an independent client+server implementation of the whole hash/encrypt exchange
                         for gen 6 and 7 including wrong PIN, PIN generator
3 backend_test_identity  create, reload byte-identical, perms 0600/0700, cert properties (v3, CN, RSA-2048, ~20y, self-signed),
                         regenerate on garbage / mismatched pair, uniqueid recreation, MLXR_CONFIG_DIR/HOME; hosts.ini round trip,
                         lookup by address/extra/name/uuid, newline injection, '..' cert path refusal
4 backend_test_flow      fake Sunshine (HTTP+HTTPS, client-cert aware): serverinfo, FULL pairing (gen 7 SHA-256, gen 7 GFE-style, gen 6 SHA-1),
                         pinned HTTPS serverinfo/applist, launch (query == mlxr_launch_query), resume, cancel, unpair, unpaired stranger gets 401,
                         wrong PIN (E_PIN_WRONG + /unpair sent + pin restored), busy pairing, MITM (bad secret signature), other server's cert
                         refused (E_TLS) with HTTP fallback in serverinfo, expired-but-pinned cert accepted, quit by non-owner (E_BUSY_OTHER 599), unreachable
5 backend_test_glue      AUDIO macros equal Limelight.h, STREAM_CONFIGURATION key/iv == rikey/rikeyid, SERVER_INFORMATION fields
6 backend_test_seed      synthetic Qt Moonlight.conf (QSettings escaping, @ByteArray, \x escape followed by a hex digit, uuid-less host,
                         loopback manual address, unpaired host), source not modified, files/modes, idempotent re-run, exit codes 3/4
7 backend_test_cli       mlxr-cli against the fake host as a user would: identity, serverinfo, apps refused before pairing (exit 4),
                         pair (reads the printed PIN, "types" it into the host), apps, launch, resume, quit, uuid-matched alias address, exit 3
```
What the fixtures cannot prove: that real Sunshine/Apollo accept exactly these bytes. The request/response shapes follow
moonlight-qt line by line and Sunshine's documented format, but only a live run proves it (milestone M1).

## ldd audit (`~/Games/MoonlightXRNative/src/tools/ldd-audit.sh build-backend/backend/mlxr-cli`)
```
NEEDED: libcurl.so.4 libssl.so.3 libxml2.so.2 libcrypto.so.3 libc.so.6
steamos libcurl.so.4 (curl)   libssl.so.3 libcrypto.so.3 (openssl)   libxml2.so.2 (libxml2)   libc.so.6 libm.so.6 libresolv.so.2 (glibc)
steamos libnghttp3 libnghttp2 libidn2 libssh2 libpsl libgssapi_krb5 libkrb5 libk5crypto libkrb5support libcom_err libkeyutils
        libzstd libbrotlidec libbrotlicommon libz liblzma libicuuc libicudata libunistring libstdc++ libgcc_s   (all transitive, all SteamOS packages)
ldd audit ok: build-backend/backend/mlxr-cli
```
(29 lines, every library resolved to a SteamOS package, no FOREIGN/MISSING.)

## Open questions / notes for the orchestrator

1. **Host address**: Qt's saved addresses are VPN ones; the LAN address is not stored. The CLI copes (uuid match);
   the app UI needs "add host by address" and should store the working address in `hosts.ini` via `mlxr_host_record_from_computer`.
2. **PIN stage UX**: `mlxr_pair` blocks (up to `timeout_ms`, default 5 min) in the caller's thread; run it on a worker thread and show the PIN from
   `mlxr_pair_generate_pin`. Sunshine/Apollo web UI is `https://<host>:<http_port+1>/pin` (47990).
3. **Apollo extras** (virtual display / `phone mode`, per-client permissions) are not implemented; plain Sunshine protocol only. Say so if wanted.
4. **`sessionUrl0` host**: Sunshine builds it from the address the request arrived on. If the Frame reaches the PC over a VPN or NAT and the URL
   host is not routable, pass the address you connected to as `SERVER_INFORMATION.address` (done by `mlxr_fill_server_info`) and check what common-c uses.
5. **Unique id**: new random 16-hex id when none imported. If Sunshine ever keys sessions on it, `mlxr-cli quit` after a launch from a different client
   would hit 599; unchanged identity => no problem.
6. **Host with the same cert but new Sunshine install** (regenerated server cert): HTTPS fails with `E_TLS`; `mlxr_server_get_info` then falls back
   to HTTP and reports `paired=false`, the UI should offer re-pairing (same as moonlight-qt).
7. **HDR/10-bit, AV1, YUV444 gating** (session.cpp's codec negotiation) is the video lane's/orchestrator's job; the backend only delivers
   `server_codec_mode_support`, `max_luma_pixels_hevc`, display modes and `p.hdr` (adds `hdrMode=1...`).
8. No libxml2 vs hand-rolled parser dilemma: libxml2 2.12.6 is a SteamOS package (`pacman -Qo` confirms), so it is used, with network access and entity
   expansion off and recover mode on.
9. `mlxr-cli launch` against the owner's PC would start a session; it was never run live (nor `pair`, `resume`, `quit`), per the rules.

## Build and run
```
cmake -S . -B build-backend -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
nice -n 5 ninja -C build-backend -j2 mlxr-cli
env -u LD_LIBRARY_PATH ctest --test-dir build-backend -R backend
```
