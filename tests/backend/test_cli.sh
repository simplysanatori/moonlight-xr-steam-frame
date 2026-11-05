#!/bin/bash
# End-to-end test of mlxr-cli against the fake host:
#   test_cli.sh <mlxr-cli> <fake_host_main>
set -uo pipefail
cli="$1"; fake="$2"
unset LD_LIBRARY_PATH
t="$(mktemp -d)"
fifo="$t/in"; mkfifo "$fifo"
fake_pid=""; pair_pid=""
cleanup() { [ -n "$pair_pid" ] && kill "$pair_pid" 2>/dev/null; exec 3>&- 2>/dev/null; [ -n "$fake_pid" ] && { kill "$fake_pid" 2>/dev/null; wait "$fake_pid" 2>/dev/null; }; rm -rf "$t"; }
trap cleanup EXIT
fail() { echo "FAIL: $*" >&2; [ -f "$t/pair.out" ] && sed 's/^/  pair: /' "$t/pair.out" >&2; exit 1; }

"$fake" <"$fifo" >"$t/fake.out" &
fake_pid=$!
exec 3>"$fifo"
for _ in $(seq 100); do grep -q '^PORTS' "$t/fake.out" 2>/dev/null && break; sleep 0.05; done
read -r _ http https < <(grep '^PORTS' "$t/fake.out")
[ -n "${http:-}" ] || fail "fake host did not start"
host="127.0.0.1:$http"
export MLXR_CONFIG_DIR="$t/cfg"

"$cli" identity | grep -q 'unique id' || fail "identity"
[ "$(stat -c %a "$MLXR_CONFIG_DIR")" = 700 ] || fail "config dir mode"

out="$("$cli" serverinfo "$host")" || fail "serverinfo: $out"
echo "$out" | grep -q 'hostname:        fake-host' || fail "hostname: $out"
echo "$out" | grep -q 'channel:         HTTP$' || fail "channel"
echo "$out" | grep -q 'mac:             aa:bb:cc:dd:ee:ff' || fail "mac"
"$cli" serverinfo "$host" --xml | grep -q '<hostname>fake-host</hostname>' || fail "--xml"

# not paired yet: apps/launch/quit must refuse with exit code 4
"$cli" apps "$host" >/dev/null 2>"$t/err"; [ $? = 4 ] || fail "apps before pairing should exit 4"
grep -q 'not paired' "$t/err" || fail "unpaired message"

# pair: CLI prints a PIN; play the user typing it into the host's web UI
"$cli" pair "$host" >"$t/pair.out" 2>&1 &
pair_pid=$!
pin=""
for _ in $(seq 100); do
  pin="$(grep -E '^ +[0-9]{4}$' "$t/pair.out" 2>/dev/null | tr -d ' ')"
  [ -n "$pin" ] && break; sleep 0.05
done
[ -n "$pin" ] || fail "no PIN printed"
echo "$pin" >&3
wait "$pair_pid"; rc=$?; pair_pid=""
[ $rc = 0 ] || fail "pair rc=$rc"
grep -q 'Paired. Host .fake-host. saved' "$t/pair.out" || fail "pair message"
grep -q '^cert = servercerts/FAKE-HOST-UUID-0001.pem' "$MLXR_CONFIG_DIR/hosts.ini" || fail "hosts.ini"
[ -s "$MLXR_CONFIG_DIR/servercerts/FAKE-HOST-UUID-0001.pem" ] || fail "server cert file"
! grep -q "$pin" "$MLXR_CONFIG_DIR/hosts.ini" || fail "PIN must not be stored"

out="$("$cli" serverinfo "$host")" || fail "paired serverinfo"
echo "$out" | grep -q 'channel:         HTTPS (pinned cert)' || fail "https channel: $out"
echo "$out" | grep -q 'paired:          yes' || fail "paired yes"

out="$("$cli" apps "$host")" || fail "apps: $out"
echo "$out" | grep -q $'881448767\tDesktop' || fail "apps list: $out"

out="$("$cli" launch "$host" 881448767 2560x1440x90)" || fail "launch: $out"
echo "$out" | grep -q 'rtsp url:     rtspenc://127.0.0.1:48010' || fail "rtsp url: $out"
echo "$out" | grep -qi 'rikey=' && fail "launch output must not print the key"
echo "$("$cli" serverinfo "$host")" | grep -q 'current game:    881448767' || fail "current game"
out="$("$cli" launch "$host" 881448767 1920x1080x60)" || fail "resume"
echo "$out" | grep -q '^resume ok' || fail "should resume: $out"
"$cli" launch "$host" 881448767 bogus >/dev/null 2>&1; [ $? = 1 ] || fail "bad mode should exit 1"
out="$("$cli" quit "$host")" || fail "quit: $out"
echo "$out" | grep -q 'quit ok' || fail "quit message"
echo "$("$cli" serverinfo "$host")" | grep -q 'current game:    0' || fail "game cleared"

# reached by another name: matched through the host uuid, still pinned
out="$("$cli" apps "localhost:$http" 2>&1)" || fail "apps via localhost: $out"
echo "$out" | grep -q 'matched by uuid' || fail "uuid match note: $out"

# probe-paths: read-only, the saved address answers; MLXR_NETWORK beats --network and the fall back is said
out="$("$cli" probe-paths fake-host --network home 2>&1)" || fail "probe-paths: $out"
echo "$out" | grep -q 'candidate 127.0.0.1' || fail "probe-paths candidate: $out"
echo "$out" | grep -q 'probe:   2/2 ok' || fail "probe-paths probe: $out"
echo "$out" | grep -q '^setting: home (from flag)' || fail "probe-paths flag: $out"
out="$(MLXR_NETWORK=adapter "$cli" probe-paths fake-host --network home 2>&1)" || fail "probe-paths env: $out"
echo "$out" | grep -q '^setting: adapter (from env)' || fail "probe-paths env wins: $out"
# the reason depends on the machine: the Frame has its wlanap (no address of the PC known), other hosts have none
echo "$out" | grep -Eq 'toast: Steam Frame Wireless Adapter not reachable \((no address of the PC known on this network|no adapter found: [^)]+)\)\. Using Wi-Fi\.' || fail "probe-paths fallback toast: $out"
"$cli" probe-paths --addr 127.0.0.2 >/dev/null 2>&1; [ $? != 0 ] || true
# unreachable host -> exit 3
"$cli" serverinfo 127.0.0.2:1 >/dev/null 2>&1; [ $? = 3 ] || fail "unreachable should exit 3"
echo ok
