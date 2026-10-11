#!/bin/bash
# usage: test_seed.sh <seed-from-moonlight-qt.sh> <unused> <mlxr-cli>
# Builds a synthetic Qt Moonlight.conf from fresh identities, imports it into a temp
# MLXR_CONFIG_DIR, and checks identity, hosts.ini, pinned cert files and idempotence.
set -euo pipefail
seed="$1"; cli="$3"; here="$(cd "$(dirname "$0")" && pwd)"
t="$(mktemp -d)"; trap 'rm -rf "$t"' EXIT
unset LD_LIBRARY_PATH
fail() { echo "FAIL: $*" >&2; exit 1; }

# two identities: one plays the Moonlight client, one plays the server cert
MLXR_CONFIG_DIR="$t/qtclient" "$cli" identity >/dev/null 2>&1
MLXR_CONFIG_DIR="$t/fakesrv" "$cli" identity >/dev/null 2>&1
python3 -I "$here/make_qt_conf.py" "$t/Moonlight.conf" "$t/qtclient/client.pem" "$t/qtclient/client.key" "$t/fakesrv/client.pem"
cp "$t/Moonlight.conf" "$t/Moonlight.conf.orig"

dest="$t/dest"
out="$(MLXR_CONFIG_DIR="$dest" "$seed" --conf "$t/Moonlight.conf")" || fail "seed exited non-zero: $out"
echo "$out"
cmp -s "$t/Moonlight.conf" "$t/Moonlight.conf.orig" || fail "source config was modified"

cmp "$t/qtclient/client.pem" "$dest/client.pem" || fail "client.pem differs"
cmp "$t/qtclient/client.key" "$dest/client.key" || fail "client.key differs"
[ "$(stat -c %a "$dest/client.key")" = 600 ] || fail "client.key mode"
[ "$(stat -c %a "$dest")" = 700 ] || fail "dest dir mode"
[ "$(stat -c %a "$dest/hosts.ini")" = 600 ] || fail "hosts.ini mode"
cmp "$t/fakesrv/client.pem" "$dest/servercerts/6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B.pem" || fail "server cert differs"

# the imported identity loads unchanged through the C code (no regeneration)
before="$(MLXR_CONFIG_DIR="$t/qtclient" "$cli" identity 2>/dev/null | grep 'cert sha256')"
after="$(MLXR_CONFIG_DIR="$dest" "$cli" identity 2>&1)"
echo "$after" | grep -q "created a new client identity" && fail "identity was regenerated: $after"
echo "$after" | grep -q "${before##* }" || fail "fingerprint mismatch"
echo "$after" | grep -q "saved hosts:  2" || fail "mlxr-cli did not load 2 hosts: $after"

h="$dest/hosts.ini"
grep -q '^\[6E1F0C2A-3B4D-4E5F-8A9B-0C1D2E3F4A5B\]' "$h" || fail "host 1 missing"
grep -q '^name = gaming-pc' "$h" || fail name
grep -q '^address = 100.64.0.5' "$h" || fail "address (first non-loopback)"
grep -q '^extra_addresses = 203.0.113.7' "$h" || fail "extra addresses"
grep -q '^mac = 02:f0:2c:5d:a5:10' "$h" || fail "mac 1 (binary bytes)"
grep -q '^cert = servercerts/6E1F0C2A' "$h" || fail "cert path"
grep -q '^\[LAPTOP-UUID-0002\]' "$h" || fail "host 2 missing"
grep -q '^mac = 1b:61:00:7f:ff:01' "$h" || fail "mac 2 (\\x escape followed by hex digit)"
grep -q '^http_port = 47990' "$h" || fail "port of chosen address"
grep -q 'ghost' "$h" && fail "uuid-less host must be skipped"
grep -A8 'LAPTOP-UUID-0002' "$h" | grep -q '^cert =' && fail "unpaired host must have no cert"

# second run: the existing identity is kept without --force; hosts merge by uuid
cp "$dest/client.key" "$t/key.before"
out2="$(MLXR_CONFIG_DIR="$dest" "$seed" --conf "$t/Moonlight.conf")"
echo "$out2" | grep -q "kept" || fail "second run should keep identity: $out2"
cmp "$t/key.before" "$dest/client.key" || fail "key changed on re-run"
[ "$(grep -c '^\[' "$h")" = 2 ] || fail "hosts duplicated on re-run"

# missing config -> exit 3, nothing created
if MLXR_CONFIG_DIR="$t/none" "$seed" --conf "$t/nonexistent" >/dev/null 2>&1; then fail "missing conf must fail"; fi

# garbage config: exit 4 for missing identity
printf '[General]\nfps=60\n' > "$t/empty.conf"
set +e; MLXR_CONFIG_DIR="$t/none2" "$seed" --conf "$t/empty.conf" >/dev/null 2>&1; rc=$?; set -e
[ "$rc" = 4 ] || fail "empty conf should exit 4, got $rc"
echo ok
