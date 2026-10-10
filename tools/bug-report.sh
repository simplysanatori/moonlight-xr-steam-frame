#!/bin/bash
# Collects what is needed to look at a problem into one zip. GPLv3.
#   bug-report.sh
# Writes ~/Games/MoonlightXRNative/reports/mlxr-report-<date>.zip containing:
#   moonlight.log, moonlight.previous.log   (client key material masked)
#   config.ini                              (settings; secret-looking values masked)
#   hosts.ini                               (names and addresses; no certificate or MAC lines)
#   docs/probe/SUMMARY.md                   (platform probe, when found)
#   system.txt                              (version, uname -a, SteamOS build id, free space)
#   kernel.txt                              (journalctl -k, last 2 hours, decoder/GPU lines only)
# It never includes client.key, client.pem, uniqueid or servercerts/. Look
# inside the zip before sharing it: logs contain host names and addresses.
# Environment: MLXR_LOG_DIR (logs, default ~/Games/MoonlightXRNative),
# MLXR_CONFIG_DIR (default ~/.config/moonlight-xr), REPORT_DIR.
set -uo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
LOGDIR="${MLXR_LOG_DIR:-$HOME/Games/MoonlightXRNative}"
CFG="${MLXR_CONFIG_DIR:-$HOME/.config/moonlight-xr}"
OUTDIR="${REPORT_DIR:-$HOME/Games/MoonlightXRNative/reports}"
STAMP="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$OUTDIR"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
R="$WORK/mlxr-report-$STAMP"; mkdir -p "$R/docs/probe"

# Mask hex strings that follow key-like names (AES key, riKey, PIN) in logs.
redact() { sed -E 's/((rikey|aes[ _-]?key|remoteInputAesKey|secret|token|password)[A-Za-z]*[=: ]+)[0-9A-Fa-f]{8,}/\1<masked>/Ig'; }
for f in moonlight.log moonlight.previous.log; do
    [ -f "$LOGDIR/$f" ] && redact < "$LOGDIR/$f" > "$R/$f"
done
if [ -f "$CFG/config.ini" ]; then
    sed -E -e 's/^([[:space:]]*[^#;=]*(secret|token|passw|private|cert|pem|(^|[_. -])key[_. -])[^=]*=).*/\1<masked>/I' \
        -e 's/^([[:space:]]*[^#;=]*(^|[_. -])key[[:space:]]*=).*/\1<masked>/I' "$CFG/config.ini" > "$R/config.ini"
fi
[ -f "$CFG/hosts.ini" ] && grep -viE '^(cert|mac)[[:space:]]*=' "$CFG/hosts.ini" > "$R/hosts.ini"
for s in "$HERE/docs/probe/SUMMARY.md" "$HERE/../docs/probe/SUMMARY.md"; do
    [ -f "$s" ] && { cp "$s" "$R/docs/probe/SUMMARY.md"; break; }
done
rmdir "$R/docs/probe" "$R/docs" 2>/dev/null

{
    echo "report: $(date -Is)"
    for v in "$HERE/VERSION" "$HERE/../VERSION"; do [ -f "$v" ] && { echo "moonlight-xr-native: $(cat "$v")"; break; }; done
    [ -d "$HERE/../.git" ] || [ -f "$HERE/../.git" ] && echo "git: $(git -C "$HERE/.." describe --tags --always --dirty 2>/dev/null)"
    echo "uname: $(uname -a)"
    . /etc/os-release 2>/dev/null && echo "SteamOS: $PRETTY_NAME $VERSION_ID, build id ${BUILD_ID:-unknown}, variant ${VARIANT_ID:-none}"
    echo "binary: $(ls -l "$HERE/moonlight-xr" "$HERE/../build/moonlight-xr" 2>/dev/null | head -n 1)"
    echo "log files:"; ls -l "$LOGDIR"/moonlight*.log 2>/dev/null
    echo "disk:"; df -h "$HOME" 2>/dev/null | tail -n 1
    echo "xr runtime: ${XR_RUNTIME_JSON:-unset}; $(readlink -f "$HOME/.config/openxr/1/active_runtime.json" 2>/dev/null)"
} > "$R/system.txt"

{
    if command -v journalctl >/dev/null; then
        journalctl -k --since "-2h" --no-pager 2>&1 | grep -iE 'iris|msm|vidc|smmu|hangcheck|gpu fault'
        echo "(journalctl exit: ${PIPESTATUS[0]}, matching lines: $(journalctl -k --since '-2h' --no-pager 2>/dev/null | grep -ciE 'iris|msm|vidc|smmu|hangcheck|gpu fault'))"
    else
        echo "journalctl not available"
    fi
} > "$R/kernel.txt" 2>&1

ZIP="$OUTDIR/mlxr-report-$STAMP.zip"
rm -f "$ZIP"
if command -v zip >/dev/null; then
    ( cd "$WORK" && zip -qr "$ZIP" "mlxr-report-$STAMP" )
else
    python3 -I - "$WORK" "mlxr-report-$STAMP" "$ZIP" <<'PY'
import os, sys, zipfile
work, top, out = sys.argv[1:4]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for d, _, fs in os.walk(os.path.join(work, top)):
        for f in fs:
            p = os.path.join(d, f)
            z.write(p, os.path.relpath(p, work))
PY
fi
[ -f "$ZIP" ] || { echo "could not write $ZIP"; exit 1; }
echo "wrote $ZIP"
python3 -I -c 'import sys,zipfile; [print("  ",i.filename,i.file_size) for i in zipfile.ZipFile(sys.argv[1]).infolist()]' "$ZIP"
echo "Check the contents before you share it: logs contain host names and addresses."
