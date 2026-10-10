#!/bin/bash
# Fails unless every shared library a binary needs resolves, with
# LD_LIBRARY_PATH unset, to a SteamOS package file in /usr/lib or to the
# binary's own ./lib directory. GPLv3.
set -uo pipefail
[ -f "${1:-}" ] || { echo "ldd audit FAILED: no such file: ${1:-}"; exit 1; }
bin="$1"; dir="$(cd "$(dirname "$bin")" && pwd)"; bad=0; n=0
while read -r name _ path _; do
    case "$name" in linux-vdso.so.*|"") continue ;; esac
    if [ "$path" = "not" ]; then echo "  MISSING  $name"; bad=1; continue; fi
    real="$(readlink -f "$path")"
    if [[ "$real" == "$dir/lib/"* ]]; then echo "  bundled  $name"; continue; fi
    if pkg=$(pacman -Qqo "$real" 2>/dev/null); then [ -n "${VERBOSE:-}" ] && echo "  steamos  $name ($pkg)"; n=$((n+1)); else echo "  FOREIGN  $name -> $real"; bad=1; fi
done < <(env -u LD_LIBRARY_PATH ldd "$bin" | sed 's/^\s*//' | grep -v '^/lib/ld-linux')
[ $bad = 0 ] && echo "ldd audit ok: $bin ($n SteamOS libraries; VERBOSE=1 lists them)" || { echo "ldd audit FAILED: $bin"; exit 1; }
