#!/bin/bash
# Maintaining launcher/patches/*.patch (the changes to moonlight-qt) without ever editing the
# submodule. GPLv3.
#   launcher/patches.sh workdir   build-launcher/work = pristine submodule tree + launcher/src
#                                 + the patches applied as commits (git am --3way)
#   launcher/patches.sh export    write build-launcher/work's commits back to launcher/patches/
#
# After a submodule update that makes launcher/build.sh stop at "patch does not apply":
#   launcher/patches.sh workdir     (resolve conflicts with git am --continue in build-launcher/work)
#   launcher/patches.sh export
# Edit the new hand-off code in launcher/src/ (copied over the work tree by workdir/export).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SUB="$ROOT/third_party/moonlight-qt"
WORK="$ROOT/build-launcher/work"
GIT=(git -c user.name="Moonlight XR Native" -c user.email="noreply@example.invalid")
case "${1:-}" in
workdir)
    [ -e "$SUB/moonlight-qt.pro" ] || git -C "$ROOT" submodule update --init --recursive third_party/moonlight-qt
    rm -rf "$WORK"; mkdir -p "$WORK"
    rsync -a --exclude='.git*' --exclude=/libs "$SUB/" "$WORK/"
    cd "$WORK"
    "${GIT[@]}" init -q && "${GIT[@]}" add -A >/dev/null && "${GIT[@]}" commit -qm "pristine $(git -C "$SUB" describe --tags --always)"
    cp "$ROOT"/launcher/src/* app/streaming/
    cp "$ROOT"/launcher/qml/*.qml app/gui/
    "${GIT[@]}" add -A >/dev/null && "${GIT[@]}" commit -qm "launcher sources (not exported as a patch)"
    git branch -f base HEAD
    ls "$ROOT"/launcher/patches/*.patch >/dev/null 2>&1 && "${GIT[@]}" am --3way "$ROOT"/launcher/patches/*.patch
    echo "work tree ready: $WORK (commits after 'base' are the patches)"
    ;;
export)
    cd "$WORK"
    rm -f "$ROOT"/launcher/patches/*.patch
    # base = the commit with the launcher sources; everything after it is a patch
    git format-patch --zero-commit --no-signature -N -o "$ROOT/launcher/patches" base..HEAD
    ;;
*) sed -n 2,13p "$0"; exit 2 ;;
esac
