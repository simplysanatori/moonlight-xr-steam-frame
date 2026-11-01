#!/bin/bash
# Fails unless an app/ directory loads nothing from the system but the libraries on
# tools/host-libs.allow, on every SteamOS build in docs/probe/host-sonames/. GPLv3.
#   tools/check-bundle.sh <app dir>
# For every ELF file under the app dir (binaries, app/lib, Qt plugins, QML plugins):
#   - each library it needs (NEEDED) is in app/lib, or on the allowlist and named in every
#     docs/probe/host-sonames/*.txt (so it exists, under that soname, on the older and the newer OS);
#   - it needs no glibc symbol newer than GLIBC_2.39 (the older SteamOS) and no libstdc++ symbol newer
#     than the older SteamOS's (GLIBCXX_3.4.34, CXXABI_1.3.15);
#   - it has no RPATH, and every RUNPATH entry is relative ($ORIGIN): never a build machine's path;
#     the top-level binaries' RUNPATH is exactly $ORIGIN/lib;
#   - resolved here (ldd, LD_LIBRARY_PATH unset), each library it needs comes from the app dir, or is
#     allowlisted.
# And app/lib holds nothing on the allowlist (Mesa, GL, Vulkan and the rest always come from the system).
# ALLOW_MISSING_HOST=1: an allowlisted library this machine lacks is a warning, not a failure (CI's
# runner is not SteamOS); a library that is not allowlisted must still resolve inside the app dir.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$(cd "${1:?usage: check-bundle.sh <app dir>}" && pwd)"
ALLOW="$ROOT/tools/host-libs.allow"
LISTS=("$ROOT"/docs/probe/host-sonames/*.txt)
GLIBC_MAX=2.39 GLIBCXX_MAX=3.4.34 CXXABI_MAX=1.3.15
for t in objdump readelf ldd; do command -v "$t" >/dev/null || { echo "check-bundle: missing tool $t"; exit 1; }; done
[ -f "${LISTS[0]}" ] || { echo "check-bundle: no docs/probe/host-sonames/*.txt"; exit 1; }
mapfile -t PATS < <(sed -e 's/#.*//' -e 's/[[:space:]]//g' "$ALLOW" | grep .)
allowed() { local p; for p in "${PATS[@]}"; do [[ "$1" == $p ]] && return 0; done; return 1; }
newer() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$1" ]; }   # newer A B: A > B
bad=0; warn=0; n=0
fail() { echo "  FAIL $*"; bad=$((bad+1)); }
declare -A EXT=()
L="$(readlink -f "$APP/lib" 2>/dev/null || echo "$APP/lib")"
if [ -d "$APP/lib" ]; then
    for f in "$APP"/lib/*; do allowed "${f##*/}" && fail "app/lib/${f##*/} is on the allowlist: it must come from the system"; done
fi
while IFS= read -r -d '' f; do
    [ "$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')" = 7f454c46 ] || continue
    n=$((n+1)); rel="${f#"$APP"/}"
    dyn="$(readelf -d "$f" 2>/dev/null)"
    grep -q '(RPATH)' <<<"$dyn" && fail "$rel has an RPATH"
    rp="$(sed -n 's/.*(RUNPATH).*\[\(.*\)\]/\1/p' <<<"$dyn")"
    if [[ "$rel" != */* ]] && [ "$rp" != '$ORIGIN/lib' ]; then fail "$rel: RUNPATH '$rp', not \$ORIGIN/lib"; fi
    IFS=: read -ra rps <<<"$rp"
    for e in "${rps[@]}"; do [[ "$e" == '$ORIGIN'* ]] || fail "$rel: RUNPATH entry '$e' is not relative to \$ORIGIN"; done
    # symbol versions it needs from glibc and libstdc++
    vers="$(objdump -p "$f" 2>/dev/null | sed -n '/^Version References:/,/^$/p')"
    for k in GLIBC:$GLIBC_MAX GLIBCXX:$GLIBCXX_MAX CXXABI:$CXXABI_MAX; do
        top="$(grep -oE "${k%%:*}_[0-9][0-9.]*" <<<"$vers" | sed "s/^${k%%:*}_//" | sort -uV | tail -n 1)"
        [ -n "$top" ] && newer "$top" "${k#*:}" && fail "$rel needs ${k%%:*}_$top (the older SteamOS has ${k%%:*}_${k#*:})"
    done
    mapfile -t need < <(objdump -p "$f" 2>/dev/null | awk '$1 == "NEEDED" { print $2 }')
    [ "${#need[@]}" -gt 0 ] || continue
    for s in "${need[@]}"; do
        [ -f "$APP/lib/$s" ] && continue
        if ! allowed "$s"; then fail "$rel needs $s: not in app/lib and not on tools/host-libs.allow"; continue; fi
        EXT[$s]=1
        for l in "${LISTS[@]}"; do grep -qxF "$s" "$l" || fail "$rel needs $s, which SteamOS ${l##*/} does not have"; done
    done
    # resolved here, only the libraries it needs itself (the system's libraries' own needs are the system's)
    out="$(env -u LD_LIBRARY_PATH ldd "$f" 2>&1)"
    for s in "${need[@]}"; do
        line="$(grep -E "^[[:space:]]*$(sed 's/[.+]/\\&/g' <<<"$s") => " <<<"$out" | head -n 1)"
        [ -n "$line" ] || continue                     # ld-linux and the like: no "=>" line
        p="$(awk '{ print $3 }' <<<"$line")"
        if [ "$p" = not ]; then
            if allowed "$s" && [ "${ALLOW_MISSING_HOST:-0}" = 1 ]; then warn=$((warn+1))
            else fail "$rel: $s not found"; fi
            continue
        fi
        real="$(readlink -f "$p")"
        [[ "$real" == "$APP/"* || "$real" == "$L/"* ]] && continue
        if [ -f "$APP/lib/$s" ]; then fail "$rel loads $s from $real, not its own app/lib copy"
        elif ! allowed "$s"; then fail "$rel loads $s from $real, outside the app"; fi
    done
done < <(find "$APP" -type f -print0 | sort -z)
echo "check-bundle: $n ELF files; from the system: $(printf '%s\n' "${!EXT[@]}" | sort | tr '\n' ' ')"
[ "$warn" = 0 ] || echo "check-bundle: $warn allowlisted libraries are not installed on this machine (ALLOW_MISSING_HOST=1)"
[ "$bad" = 0 ] && { echo "check-bundle ok: $APP"; exit 0; }
echo "check-bundle FAILED: $bad problems in $APP"; exit 1
