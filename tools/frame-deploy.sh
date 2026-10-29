#!/bin/bash
# Build, install, launch and collect logs on the Steam Frame from one place. GPLv3.
#
# Runs on a PC (everything goes through ssh) or on the Frame itself (everything runs
# locally); it tells which by the host name, FRAME_LOCAL=1/0 overrides.
#
#   tools/frame-deploy.sh status
#   tools/frame-deploy.sh build   [--branch B] [--no-pull] [--ctest]
#   tools/frame-deploy.sh install
#   tools/frame-deploy.sh artifact <release.tar.gz | CI run id> [--artifact-name S]
#   tools/frame-deploy.sh shortcut
#   tools/frame-deploy.sh launch  [--app NAME] [--cef] [--force]
#   tools/frame-deploy.sh logs    <outdir>
#   tools/frame-deploy.sh shot    <outdir>
#
#   status    pgrep of the VR client and launcher, SteamVR, uptime, battery, the VERSION of the
#             installed app and the dev app, the source tree's branch and commit.
#   build     in the source tree on the Frame: refuse if it has uncommitted changes, git fetch,
#             switch to --branch (default: the current branch) and fast-forward it, ./build.sh
#             (which updates the submodules), then with --ctest the test suite without audio_pulse
#             (CTEST_ARGS adds ctest options, e.g. "-R ui_").
#   install   install the Frame-built tree (packaging/install.sh from the source tree) into DEST
#             with NO_SHORTCUT=1: no tarball round trip. Never touches the Steam shortcut.
#   artifact  a CI release tarball (a file, or a GitHub Actions run id that is downloaded with
#             tools-local/ci.py first): copied to the Frame, unpacked into a fresh directory, its
#             own install.sh run into DEST (NO_SHORTCUT=1), then the ldd audit, and a smoke run
#             of the new binaries that needs no headset session (usage output).
#   shortcut  add the Steam library shortcut "Moonlight XR (dev)" for DEST's launcher (the
#             installer cannot name a second shortcut), with the 1080p window and the SteamVR
#             per-app settings of the main one. Needs Steam up; does nothing if one exists.
#   launch    refuse if the VR client or launcher already runs, find the shortcut for DEST in
#             shortcuts.vdf, gameid = (appid << 32) | 0x02000000, then
#             `steam steam://rungameid/<gameid>` (--cef: SteamClient.Apps.RunGame through
#             packaging/steam_cef.py instead). --app NAME is appended to the URL as launch
#             arguments (`//--app NAME`); that is for a shortcut whose target is moonlight-xr.sh
#             (VR_ONLY=1) and is not verified. The headset must be worn for a session to run.
#   logs      into <outdir>: moonlight.log, moonlight-console.log, moonlight-launcher.log and
#             vrtiming.log (with their .1/.previous), SteamVR's vrcompositor.txt (+ .previous),
#             the masked bug-report zip, and status.txt. The plain logs hold addresses and host
#             names; only the zip is masked.
#   shot      SteamVR screenshot (tools/vrshot.py: x_preview.png and x_vr.png) into <outdir>.
#
# Options (any subcommand): --dry-run prints each step instead of doing it (read-only lookups
# such as the gameid still run), --host H, --dest D, -h/--help.
# Environment: FRAME_HOST (steamos@<name or address>; default: first line of ~/.config/frame-deploy/host), DEST (default
# ~/Games/MoonlightXRNative/app-dev), BRANCH, FRAME_SRC (default ~/Games/MoonlightXRNative/src),
# FRAME_LOCAL, CI_PY (path of ci.py), CTEST_ARGS, FRAME_ALLOW_MAIN=1 (allow DEST to be the main
# install ~/Games/MoonlightXRNative/app; refused otherwise).
# Never run it as root, and never install into the main app unless you mean to: the owner uses it.
# Exit status: 0 ok, 1 a step failed, 2 usage, 3 installed but the binaries cannot start.
set -uo pipefail

# The Frame's address is not kept in the repository: FRAME_HOST, or the first line of
# ~/.config/frame-deploy/host (steamos@<name or address>). Not needed on the Frame itself.
FRAME_HOST="${FRAME_HOST:-$(head -n 1 "$HOME/.config/frame-deploy/host" 2>/dev/null || true)}"
DEST="${DEST:-~/Games/MoonlightXRNative/app-dev}"
SRC="${FRAME_SRC:-~/Games/MoonlightXRNative/src}"
BRANCH="${BRANCH:-}"
DRY=0
MAIN_APP='~/Games/MoonlightXRNative/app'
HERE="$(cd "$(dirname "$0")" && pwd)"

usage() { sed -n '2,/^# Exit status/p' "$0" | sed 's/^# \{0,1\}//'; }
fail() { echo "frame-deploy: $*" >&2; exit 1; }
bad_usage() { echo "frame-deploy: $*" >&2; echo "try: $0 --help" >&2; exit 2; }

if [ -z "${FRAME_LOCAL:-}" ]; then
    case "$(hostname -s 2>/dev/null | tr 'A-Z' 'a-z')" in frame) FRAME_LOCAL=1 ;; *) FRAME_LOCAL=0 ;; esac
fi
[ "$FRAME_LOCAL" = 1 ] || [ -n "$FRAME_HOST" ] || [ "${1:-}" = -h ] || [ "${1:-}" = --help ] || [ $# -eq 0 ]     || fail "no Frame address: set FRAME_HOST=steamos@<name or address> or put it in ~/.config/frame-deploy/host"
SSH_OPTS=(-o BatchMode=yes -o ConnectTimeout=10 -o ServerAliveInterval=30)

# rx <script> [args...]: run a bash script on the Frame (or here), stdin and stdout passed through.
# The script is sent as base64 in the command line, so quoting never matters and stdin stays free.
PRE='
unset LD_LIBRARY_PATH
DRY="${DRY:-0}"
die() { echo "frame-deploy: $*" >&2; exit 1; }
expand() { case "$1" in "~") echo "$HOME" ;; "~/"*) echo "$HOME/${1#\~/}" ;; *) echo "$1" ;; esac; }
run() { if [ "$DRY" = 1 ]; then echo "[dry-run] $*"; else "$@"; fi; }
# appid of the Steam shortcut whose Exe is DEST/moonlight-xr-launcher.sh or DEST/moonlight-xr.sh
shortcut_appid() {
    local v id
    for v in "$HOME"/.local/share/Steam/userdata/*/config/shortcuts.vdf; do
        [ -f "$v" ] || continue
        id=$(python3 -I -c '"'"'
import re, struct, sys
d = open(sys.argv[1], "rb").read()
dest = sys.argv[2]
for m in re.finditer(rb"\x02appid\x00(....)\x01AppName\x00[^\x00]*\x00\x01Exe\x00([^\x00]*)\x00", d, re.S):
    exe = m.group(2).decode(errors="replace").strip(chr(34))
    if exe in (dest + "/moonlight-xr-launcher.sh", dest + "/moonlight-xr.sh"):
        print(struct.unpack("<I", m.group(1))[0]); break
'"'"' "$v" "$1") && [ -n "$id" ] && { echo "$id"; return 0; }
    done
    return 1
}
# the VR client or the flat launcher running (comm is cut at 15 characters; -x never matches this script)
running_pids() { pgrep -x moonlight-xr; pgrep -x moonlight-xr-la; pgrep -x moonlight-xr.sh; return 0; }
'
rx() {
    local script="$1" b64 q="" a; shift
    if [ "$FRAME_LOCAL" = 1 ]; then
        DRY="$DRY" bash -c "$PRE$script" frame-deploy "$@"
    else
        b64=$(printf '%s%s' "$PRE" "$script" | base64 | tr -d '\n')
        for a in "$@"; do q="$q $(printf '%q' "$a")"; done
        ssh "${SSH_OPTS[@]}" "$FRAME_HOST" "DRY=$DRY bash -c \"\$(echo $b64 | base64 -d)\" frame-deploy$q"
    fi
}

# ---------------------------------------------------------------------------- remote bodies

R_STATUS='
APP=$(expand "$1"); DEV=$(expand "$2"); SRC=$(expand "$3")
echo "host:       $(hostname)"
echo "uptime:     $(uptime -p) (since $(uptime -s))"
bat=""; for d in /sys/class/power_supply/*/; do
    [ -f "$d/capacity" ] && [ "$(cat "$d/type" 2>/dev/null)" = Battery ] && bat="$bat$(basename "$d") $(cat "$d/capacity")% ($(cat "$d/status" 2>/dev/null)) "
done
echo "battery:    ${bat:-unknown}"
for n in moonlight-xr moonlight-xr-la moonlight-xr.sh vrserver vrcompositor steam claude; do
    p=$(pgrep -x "$n" | tr "\n" " ")
    case "$n" in moonlight-xr-la) l=launcher ;; *) l=$n ;; esac
    printf "%-11s %s\n" "$l:" "${p:-not running}"
done
r=$(running_pids | sort -u | tr "\n" " ")
[ -n "$r" ] && echo "APP RUNNING: $r (launch and install into the running DEST are refused)" || echo "app:        not running"
for d in "$APP" "$DEV"; do
    printf "version:    %-45s %s\n" "$d" "$(head -n 1 "$d/VERSION" 2>/dev/null || echo "not installed")"
done
echo "shortcuts:  main=$(shortcut_appid "$APP" || echo none) dev=$(shortcut_appid "$DEV" || echo none)"
if [ -d "$SRC/.git" ] || [ -f "$SRC/.git" ]; then
    echo "source:     $(git -C "$SRC" rev-parse --abbrev-ref HEAD) $(git -C "$SRC" rev-parse --short HEAD)$([ -n "$(git -C "$SRC" status --porcelain --untracked-files=no --ignore-submodules=all)" ] && echo " (uncommitted changes)")"
    echo "build:      $(ls -l --time-style=+%F\ %T "$SRC/build/moonlight-xr" 2>/dev/null | awk "{print \$6, \$7}" || true)"
fi
'

R_BUILD='
SRC=$(expand "$1"); BRANCH=$2; NOPULL=$3; CTEST=$4
{ [ -d "$SRC/.git" ] || [ -f "$SRC/.git" ]; } || die "no git checkout at $SRC"
cd "$SRC" || die "cannot cd to $SRC"
if pgrep -x ninja >/dev/null || pgrep -x cmake >/dev/null; then die "a build (ninja/cmake) is already running here; not starting another"; fi
cur=$(git rev-parse --abbrev-ref HEAD)
if [ "$NOPULL" != 1 ]; then
    [ -z "$(git status --porcelain --untracked-files=no --ignore-submodules=all)" ] || die "uncommitted changes in $SRC (commit or stash them, or use --no-pull)"
    want=${BRANCH:-$cur}
    [ "$want" != HEAD ] || die "the source tree is on a detached HEAD: name a branch with --branch"
    run git fetch origin "$want" || die "git fetch origin $want failed"
    if [ "$want" != "$cur" ]; then
        if git show-ref --verify --quiet "refs/heads/$want"; then run git checkout "$want" || die "git checkout $want failed"
        else run git checkout -b "$want" --track "origin/$want" || die "git checkout -b $want failed"; fi
    fi
    run git merge --ff-only "origin/$want" || die "cannot fast-forward $want to origin/$want (local commits or a rewritten branch)"
fi
echo "source: $(git rev-parse --abbrev-ref HEAD) $(git rev-parse --short HEAD)"
run env -u LD_LIBRARY_PATH ./build.sh || die "build.sh failed"
if [ "$CTEST" = 1 ]; then
    run env -u LD_LIBRARY_PATH ctest --test-dir build -E audio_pulse --output-on-failure $CTEST_ARGS || die "ctest failed"
fi
[ "$DRY" = 1 ] || echo "build ok: $(git rev-parse --short HEAD)"
'

# shared by install and artifact: DEST must not be the main app, and nothing may run from it
R_GUARD='
guard_dest() {
    local real main p exe
    real=$(readlink -f "$1"); main=$(readlink -f "$(expand "'"$MAIN_APP"'")")
    if [ "$real" = "$main" ] && [ "${FRAME_ALLOW_MAIN:-0}" != 1 ]; then
        die "DEST $1 is the main install ($main); use another DEST (default app-dev) or FRAME_ALLOW_MAIN=1"
    fi
    for p in $(running_pids | sort -u); do
        exe=$(readlink -f /proc/$p/exe 2>/dev/null)
        case "$exe" in "$real"/*) die "pid $p runs from $1: not installing over a running app" ;; esac
        exe=$(tr "\0" " " </proc/$p/cmdline 2>/dev/null)
        case "$exe" in *"$real"/*) die "pid $p runs from $1: not installing over a running app" ;; esac
    done
}
check_binaries() {   # check_binaries <DEST> <source tree for ldd-audit.sh or empty>
    local d=$1 audit=$2 miss rc=0 b
    for b in moonlight-xr mlxr-cli moonlight-xr-launcher; do
        [ -x "$d/$b" ] || continue
        miss=$(ldd "$d/$b" 2>&1 | grep "not found")
        if [ -n "$miss" ]; then echo "MISSING LIBRARIES for $b:"; echo "$miss"; rc=3; fi
    done
    if [ -x "$audit/tools/ldd-audit.sh" ] && command -v pacman >/dev/null; then
        for b in moonlight-xr mlxr-cli; do
            [ -x "$d/$b" ] || continue
            "$audit/tools/ldd-audit.sh" "$d/$b" || rc=3
        done
    else
        echo "ldd audit: skipped (no tools/ldd-audit.sh or no pacman here)"
    fi
    return $rc
}
'

R_INSTALL="$R_GUARD"'
SRC=$(expand "$1"); DEST=$(expand "$2")
guard_dest "$DEST"
[ -x "$SRC/build/moonlight-xr" ] && [ -x "$SRC/build/backend/mlxr-cli" ] || die "no build in $SRC/build: run: frame-deploy.sh build"
[ "$(git -C "$SRC" status --porcelain --untracked-files=no --ignore-submodules=all | wc -l)" = 0 ] || echo "note: $SRC has uncommitted changes; the install is of what was built"
run env NO_SHORTCUT=1 DEST="$DEST" "$SRC/packaging/install.sh" || die "install.sh failed"
[ "$DRY" = 1 ] && exit 0
echo "installed: $DEST $(head -n 1 "$DEST/VERSION")"
check_binaries "$DEST" "$SRC" || die "installed, but the binaries will not start (see above)"
'

R_ARTIFACT_PREP='
mkdir -p /tmp/frame-deploy || die "cannot make /tmp/frame-deploy"
'

R_ARTIFACT="$R_GUARD"'
TAR=$1; DEST=$(expand "$2"); SRC=$(expand "$3")
[ -f "$TAR" ] || die "no tarball $TAR on the Frame"
guard_dest "$DEST"
STAGE=/tmp/frame-deploy/rel
run rm -rf "$STAGE"; run mkdir -p "$STAGE"
run tar -xzf "$TAR" -C "$STAGE" || die "cannot unpack $TAR"
U="$STAGE/MoonlightXRNative"
[ "$DRY" = 1 ] || [ -x "$U/install.sh" ] || die "$TAR has no MoonlightXRNative/install.sh"
run env NO_SHORTCUT=1 DEST="$DEST" "$U/install.sh" || die "the tarball install.sh failed"
[ "$DRY" = 1 ] && exit 0
echo "installed: $DEST $(head -n 1 "$DEST/VERSION")"
rc=0
check_binaries "$DEST" "$SRC" || rc=3
echo "== smoke (no headset session): usage output of the new binaries"
for b in moonlight-xr mlxr-cli; do
    [ -x "$DEST/$b" ] || continue
    out=$(cd "$DEST" && timeout 20 env -u LD_LIBRARY_PATH ./$b --not-an-option 2>&1 </dev/null | head -n 3)
    echo "$b --not-an-option -> $(printf "%s" "$out" | head -n 1)"
done
[ $rc = 0 ] || { echo "frame-deploy: installed, but the binaries will not start (see above)" >&2; exit 3; }
'

R_SHORTCUT='
DEST=$(expand "$1"); SRC=$(expand "$2"); NAME=$3
id=$(shortcut_appid "$DEST") && { echo "shortcut for $DEST already exists: appid $id"; exit 0; }
if [ -x "$DEST/moonlight-xr-launcher" ] && [ -f "$DEST/moonlight-xr-launcher.sh" ]; then EXE="$DEST/moonlight-xr-launcher.sh"
elif [ -f "$DEST/moonlight-xr.sh" ]; then EXE="$DEST/moonlight-xr.sh"
else die "nothing installed in $DEST: run install first"; fi
CEFPY="$DEST/steam_cef.py"; [ -f "$CEFPY" ] || CEFPY="$SRC/packaging/steam_cef.py"
[ -f "$CEFPY" ] || die "no steam_cef.py"
pgrep -x steam >/dev/null || die "Steam is not running"
JS=$(python3 -I -c '"'"'
import json, sys
name, exe, d = sys.argv[1:4]
print("(async()=>{const name=%s, exe=%s, dir=%s; const id=await SteamClient.Apps.AddShortcut(name, exe, dir, \"\");"
      "SteamClient.Apps.SetShortcutName(id, name); SteamClient.Apps.SetShortcutExe(id, exe);"
      "SteamClient.Apps.SetShortcutStartDir(id, dir); SteamClient.Apps.SetShortcutLaunchOptions(id, \"\"); return id;})()"
      % (json.dumps(name), json.dumps("\"" + exe + "\""), json.dumps("\"" + d + "\"")))
'"'"' "$NAME" "$EXE" "$DEST")
if [ "$DRY" = 1 ]; then echo "[dry-run] python3 -I $CEFPY <<< $JS"; echo "[dry-run] then SetAppResolutionOverride(appid,1920x1080) and vrcmd per-app settings (90 Hz, smoothing off, 2160, sharpening off)"; exit 0; fi
APPID=$(python3 -I "$CEFPY" "$JS") || die "Steam did not add the shortcut (CEF port?)"
APPID=$(printf "%s" "$APPID" | tr -d "\"[:space:]")
echo "shortcut \"$NAME\": appid $APPID ($EXE)"
python3 -I "$CEFPY" "SteamClient.Apps.SetAppResolutionOverride($APPID,\"1920x1080\")" >/dev/null 2>&1 && echo "game resolution: 1920x1080" || echo "game resolution: not set"
VRCMD=/opt/steamvr/bin/linuxarm64/vrcmd
if [ -x "$VRCMD" ] && pgrep -x vrserver >/dev/null; then
    for kv in preferredRefreshRate:90 motionSmoothingOverride:0 resolutionOverride:2160 sharpeningOverride:0; do
        timeout 20 "$VRCMD" --set-settings-int "steam.app.$APPID.${kv%%:*}" "${kv##*:}" >/dev/null 2>&1 || echo "vrcmd: could not set ${kv%%:*}"
    done
    echo "SteamVR per-app settings: 90 Hz, smoothing off, resolution 2160, sharpening off"
else echo "SteamVR per-app settings: SteamVR is not running; set 90 Hz in its per-app video settings"; fi
'

R_LAUNCH='
DEST=$(expand "$1"); APPARG=$2; METHOD=$3; FORCE=$4; SRC=$(expand "$5")
r=$(running_pids | sort -u | tr "\n" " ")
if [ -n "$r" ] && [ "$FORCE" != 1 ]; then die "the app is already running (pid $r): not launching a second one (--force overrides)"; fi
if [ -n "${GAMEID:-}" ]; then gameid=$GAMEID
else
    appid=$(shortcut_appid "$DEST") || die "no Steam shortcut runs $DEST/moonlight-xr-launcher.sh (or moonlight-xr.sh): create one with: frame-deploy.sh shortcut (or set GAMEID)"
    gameid=$(printf "%u" $(( (appid << 32) | 0x02000000 )))   # unsigned: appids above 2^31 overflow a signed 64-bit
fi
echo "shortcut appid ${appid:-?}, gameid $gameid"
pgrep -x steam >/dev/null || die "Steam is not running"
url="steam://rungameid/$gameid"
[ -z "$APPARG" ] || url="$url//$(python3 -I -c '"'"'import sys,urllib.parse; print(urllib.parse.quote("--app " + sys.argv[1], safe=""))'"'"' "$APPARG")"
if [ "$METHOD" = cef ]; then
    CEFPY="$DEST/steam_cef.py"; [ -f "$CEFPY" ] || CEFPY="$SRC/packaging/steam_cef.py"
    run python3 -I "$CEFPY" "SteamClient.Apps.RunGame(\"$gameid\",\"\",-1,100)" || die "RunGame failed"
else
    if [ "$DRY" = 1 ]; then echo "[dry-run] steam $url"
    else setsid -f timeout 30 env -u LD_LIBRARY_PATH steam "$url" >/dev/null 2>&1 || die "steam $url failed to start"; fi
fi
[ "$DRY" = 1 ] && exit 0
for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
    sleep 1; r=$(running_pids | sort -u | tr "\n" " ")
    [ -n "$r" ] && { echo "launched: pid $r after ${i}s"; exit 0; }
done
echo "frame-deploy: asked Steam to launch, but no moonlight-xr process after 15 s (is the headset on and SteamVR up? see: logs)" >&2
exit 1
'

R_REPORT='
DEST=$(expand "$1"); APP=$(expand "$2"); SRC=$(expand "$3")
BR=
for d in "$DEST" "$APP"; do [ -x "$d/bug-report.sh" ] && { BR="$d/bug-report.sh"; break; }; done
[ -n "$BR" ] || { [ -x "$SRC/tools/bug-report.sh" ] && BR="$SRC/tools/bug-report.sh"; }
[ -n "$BR" ] || die "no bug-report.sh in $DEST, $APP or $SRC/tools"
RD=$(mktemp -d /tmp/frame-deploy-report.XXXXXX) || die "mktemp failed"
REPORT_DIR="$RD" "$BR" >"$RD.out" 2>&1 || { cat "$RD.out" >&2; die "bug-report.sh failed"; }
echo "REPORTDIR=$RD"
'

R_FETCH='
RD=$1
cd "$HOME/Games/MoonlightXRNative" || die "no ~/Games/MoonlightXRNative"
files=""; for f in moonlight.log moonlight.log.1 moonlight.previous.log moonlight-console.log moonlight-launcher.log moonlight-launcher.log.1 vrtiming.log vrtiming.log.1; do
    [ -f "$f" ] && files="$files $f"
done
SL=""; for d in "$HOME/.local/share/Steam/logs" "$HOME/.steam/steam/logs"; do [ -f "$d/vrcompositor.txt" ] && { SL=$d; break; }; done
[ -n "$SL" ] || echo "frame-deploy: no vrcompositor.txt found" >&2
sf=""; for f in vrcompositor.txt vrcompositor.previous.txt; do [ -n "$SL" ] && [ -f "$SL/$f" ] && sf="$sf $f"; done
args=($files)
[ -n "$sf" ] && args+=(-C "$SL" $sf)
args+=(-C "$RD" .)
tar -cf - "${args[@]}" 2>/dev/null
rm -rf "$RD" "$RD.out"
'

R_SHOT='
D=$(mktemp -d /tmp/frame-deploy-shot.XXXXXX) || die "mktemp failed"
pgrep -x vrserver >/dev/null || die "SteamVR is not running"
env -u LD_LIBRARY_PATH python3 -I - "$D/x" || die "vrshot.py failed"
ls "$D"/x_*.png >/dev/null 2>&1 || { ls -la "$D"; rm -rf "$D"; die "SteamVR wrote no screenshot (is a VR app drawing?)"; }
echo "SHOTDIR=$D"
'

# ---------------------------------------------------------------------------- local side

# fetch_stream <out dir>: stdin is a tar stream
unpack_into() { mkdir -p "$1" && tar -xf - -C "$1"; }

cmd_status() { rx "$R_STATUS" "$MAIN_APP" "$DEST" "$SRC"; }

cmd_build() {
    local nopull=0 ctest=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --branch) BRANCH="${2:?--branch needs a name}"; shift ;;
            --no-pull) nopull=1 ;;
            --ctest) ctest=1 ;;
            *) bad_usage "build: unknown option $1" ;;
        esac; shift
    done
    rx "CTEST_ARGS='${CTEST_ARGS:-}'; $R_BUILD" "$SRC" "$BRANCH" "$nopull" "$ctest"
}

cmd_install() { [ $# -eq 0 ] || bad_usage "install takes no arguments"; rx "$R_INSTALL" "$SRC" "$DEST"; }

cmd_artifact() {
    local arg="${1:-}" art="moonlight-xr-steam-frame-frame-arm64" tar remote dl
    [ -n "$arg" ] || bad_usage "artifact needs a tarball or a CI run id"
    shift
    while [ $# -gt 0 ]; do
        case "$1" in --artifact-name) art="${2:?}"; shift ;; *) bad_usage "artifact: unknown option $1" ;; esac; shift
    done
    if [[ "$arg" =~ ^[0-9]+$ ]]; then
        local ci="${CI_PY:-$HERE/../../../tools-local/ci.py}"
        [ -f "$ci" ] || fail "run id given but ci.py not found (set CI_PY)"
        dl="$(mktemp -d)" || fail "mktemp failed"
        trap 'rm -rf "$dl"' EXIT
        python "$ci" artifact "$arg" "$art" "$dl" >&2 || fail "ci.py could not fetch artefact '$art' of run $arg"
        tar="$(ls "$dl"/*.tar.gz 2>/dev/null | head -n 1)"
        [ -n "$tar" ] || fail "no .tar.gz in the artefact of run $arg ($dl)"
        echo "downloaded $tar"
    else
        tar="$arg"
    fi
    [ -f "$tar" ] || fail "no such file: $tar"
    echo "tarball: $tar ($(sha256sum "$tar" | cut -c1-16)...)"
    if [ "$FRAME_LOCAL" = 1 ]; then
        remote="$(cd "$(dirname "$tar")" && pwd)/$(basename "$tar")"
    else
        remote="/tmp/frame-deploy/$(basename "$tar")"
        rx "$R_ARTIFACT_PREP" || fail "cannot prepare /tmp/frame-deploy on the Frame"
        if [ "$DRY" = 1 ]; then echo "[dry-run] scp $tar $FRAME_HOST:$remote"
        else ( cd "$(dirname "$tar")" && scp -q "${SSH_OPTS[@]}" "$(basename "$tar")" "$FRAME_HOST:$remote" ) || fail "scp failed"; fi
    fi
    rx "$R_ARTIFACT" "$remote" "$DEST" "$SRC"
}

cmd_shortcut() { rx "$R_SHORTCUT" "$DEST" "$SRC" "Moonlight XR (dev)"; }

cmd_launch() {
    local app="" method=url force=0
    while [ $# -gt 0 ]; do
        case "$1" in
            --app) app="${2:?--app needs a name}"; shift ;;
            --cef) method=cef ;;
            --force) force=1 ;;
            *) bad_usage "launch: unknown option $1" ;;
        esac; shift
    done
    rx "$R_LAUNCH" "$DEST" "$app" "$method" "$force" "$SRC"
}

cmd_logs() {
    local out="${1:-}" line rd
    [ -n "$out" ] || bad_usage "logs needs an output directory"
    mkdir -p "$out" || fail "cannot create $out"
    cmd_status > "$out/status.txt" 2>&1 || fail "status failed (is the Frame reachable?)"
    if [ "$DRY" = 1 ]; then echo "[dry-run] bug-report.sh on the Frame, then fetch logs and vrcompositor.txt into $out"; return 0; fi
    line=$(rx "$R_REPORT" "$DEST" "$MAIN_APP" "$SRC" | grep '^REPORTDIR=') || fail "bug report failed"
    rd="${line#REPORTDIR=}"
    rx "$R_FETCH" "$rd" | unpack_into "$out" || fail "fetching the logs failed"
    ls "$out"/mlxr-report-*.zip >/dev/null 2>&1 || fail "no bug report zip arrived in $out"
    echo "logs in $out:"; ls -l "$out" | sed 1d
}

cmd_shot() {
    local out="${1:-}" line sd text
    [ -n "$out" ] || bad_usage "shot needs an output directory"
    [ -f "$HERE/vrshot.py" ] || fail "$HERE/vrshot.py missing"
    mkdir -p "$out" || fail "cannot create $out"
    if [ "$DRY" = 1 ]; then echo "[dry-run] run tools/vrshot.py on the Frame, fetch x_preview.png and x_vr.png into $out"; return 0; fi
    text=$(rx "$R_SHOT" < "$HERE/vrshot.py") || { echo "$text" >&2; fail "screenshot failed"; }
    echo "$text" | grep -v '^SHOTDIR='
    line=$(echo "$text" | grep '^SHOTDIR=') || fail "screenshot failed: no output directory reported"
    sd="${line#SHOTDIR=}"
    rx 'cd "$1" && tar -cf - x_*.png; rm -rf "$1"' "$sd" | unpack_into "$out" || fail "fetching the screenshots failed"
    ls -l "$out"/x_*.png
}

# ---------------------------------------------------------------------------- main

SUB=""; REST=()
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage; exit 0 ;;
        --dry-run) DRY=1 ;;
        --host) FRAME_HOST="${2:?--host needs a value}"; shift ;;
        --dest) DEST="${2:?--dest needs a value}"; shift ;;
        *) if [ -z "$SUB" ]; then SUB="$1"; else REST+=("$1"); fi ;;
    esac
    shift
done
[ -n "$SUB" ] || { usage >&2; exit 2; }
case "$SUB" in
    status|build|install|artifact|shortcut|launch|logs|shot) "cmd_$SUB" "${REST[@]+"${REST[@]}"}" ;;
    *) bad_usage "unknown subcommand: $SUB" ;;
esac
