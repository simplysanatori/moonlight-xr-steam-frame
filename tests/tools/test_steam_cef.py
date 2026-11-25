#!/usr/bin/env python3
"""Tests of packaging/steam_cef.py against a fake CEF endpoint (tests/tools/fake_cef.py): the preflight
(--probe) and the command line.

    test_steam_cef.py <path to steam_cef.py>

No Steam, no network beyond 127.0.0.1. Moonlight XR Native. GPLv3.
"""
import importlib.util, json, os, socket, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "packaging", "steam_cef.py")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


sc = load("steam_cef", SRC)
fake_cef = load("fake_cef", os.path.join(HERE, "fake_cef.py"))
FakeCef = fake_cef.FakeCef

fails = 0


def check(cond, what):
    global fails
    if cond:
        print("ok:", what)
    else:
        fails += 1
        print("FAIL:", what, file=sys.stderr)


def raises(exc, fn, *a):
    try:
        fn(*a)
    except exc:
        return True
    except Exception:
        return False
    return False


def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def cli(port, *args, env_extra=None):
    env = dict(os.environ, STEAM_CEF_PORT=str(port), STEAM_CEF_FLAG_FILE=os.path.join(HERE, "no-such-flag"))
    env.update(env_extra or {})
    p = subprocess.run([sys.executable, "-I", SRC, *args], capture_output=True, text=True, env=env, timeout=60)
    return p.returncode, p.stdout, p.stderr


# ---- --show: is the flat window shown in the headset? -----------------------------------------------------
import stat, tempfile

KEY = "valve.steam.desktopgame.1234567890"
# the other dashboard overlays of an open and of a closed dashboard (the app ids of one start as the other's do)
OPEN = ("'valve.steam.desktopgame.12345678900' -- 'another', size=0x0 not_visible VROverlayType_Dashboard_Main flags=0x8000\n"
        "'a.dashboard.main' -- 'Steam', size=64x64 visible VROverlayType_Dashboard_Main flags=0x29e28000 dashboard, smooth_scroll\n"
        "'a.hud' -- 'x', size=64x64 visible VROverlayType_World flags=0x0\n")
CLOSED = OPEN.replace("size=64x64 visible VROverlayType_Dashboard_Main", "size=64x64 not_visible VROverlayType_Dashboard_Main")


def line(size="0x0", flags="0x29e28000", vis="visible", layers=1):
    """The window's own line as the Frame's SteamVR writes it (2026-11-25), and its layers' (made up)"""
    return (f"'{KEY}' -- 'Moonlight XR for Frame', size={size} {vis} VROverlayType_Dashboard_Main flags={flags} dashboard, "
            f"smooth_scroll, is_premult overlayhandle=0x000000e600000027\n"
            + "".join(f"'{KEY}.layer{n}' -- 'x', size=0x0 {vis} VROverlayType_Subview flags=0x0\n" for n in range(layers)))


HIDDEN = line(vis="not_visible")
own, layers = sc.overlay_lines(OPEN + line() + f"'{KEY}.layer7' -- 'x', not_visible VROverlayType_Subview flags=0x0\n", KEY)
check(own == line(layers=0).strip() and layers == 1, "--show: the window's own line and its visible layers")
check(sc.overlay_lines(OPEN, KEY) == (None, 0), "--show: another app's overlay is not it")
check(sc.overlay_state(line()) == (True, True), "overlay: in the dashboard, visible")
check(sc.overlay_state(line(flags="0x10", vis="not_visible")) == (False, False), "overlay: nothing of it")
check(sc.overlay_state(f"'{KEY}' x size=64x64 flags=0x8000 visible VROverlayType") == (True, True), "overlay: the fields in another order")
check(sc.overlay_state(f"'{KEY}' -- 'x', size=64x64 shown") == (False, None), "overlay: a line that does not say is not guessed at")
check(sc.dashboard_open(OPEN + HIDDEN, KEY)[0] is True, "dashboard: another of its overlays visible: open")
check(sc.dashboard_open(CLOSED + HIDDEN, KEY)[0] is False, "dashboard: none of the others visible: closed")
check(sc.dashboard_open(line() + f"'a.hud' -- 'x', visible VROverlayType_World flags=0x0\n", KEY) == (None, []), "dashboard: the window's own lines and others say nothing of it")
check(sc.dashboard_open(OPEN, KEY)[1][0] == "a.dashboard.main visible", "dashboard: what it is told by is named")

FAKE_VRCMD = """#!/bin/bash
# --overlays prints $OVL/state (and from the Nth call on, $OVL/state.N); a dock to W makes $OVL/dock.W the state
echo "$*" >> "$OVL/log"
case "$1" in
  --overlays) n=$(grep -c -- --overlays "$OVL/log"); [ -e "$OVL/state.$n" ] && cp "$OVL/state.$n" "$OVL/state"
              cat "$OVL/state" 2>/dev/null; exit "${VRCMD_EXIT:-0}";;
  --dock-overlay) [ -e "$OVL/dock.$2" ] && cp "$OVL/dock.$2" "$OVL/state"; exit 0;;
esac
"""


def run_show(files, seconds="1", rules=(("ShowDashboard", None),), env=None, path=None, app="1234567890", cmd="--show"):
    """(exit code, output, vrcmd's calls, the JavaScript Steam got)"""
    with tempfile.TemporaryDirectory() as d, FakeCef(list(rules)) as cef:
        exe = os.path.join(d, "vrcmd")
        open(exe, "w").write(FAKE_VRCMD)
        os.chmod(exe, os.stat(exe).st_mode | stat.S_IXUSR)
        for n, text in files.items():
            open(os.path.join(d, n), "w").write(text)
        c, out, err = cli(cef.port, cmd, app, *([seconds] if seconds else []), env_extra=dict({"MLXR_VRCMD": path or exe, "OVL": d}, **(env or {})))
        log = os.path.join(d, "log")
        calls = open(log).read().split("\n")[:-1] if os.path.exists(log) else []
        return c, out + err, calls, list(cef.expressions)


def docks(calls):
    return [x for x in calls if x.startswith("--dock-overlay")]


# a window on show has no size to SteamVR: visible with a layer is shown
for size in ("0x0", "1920x1080"):
    c, out, calls, js = run_show({"state": OPEN + line(size)})
    check(c == 0 and calls == ["--overlays"] and js == [], f"--show: shown at {size}: one look, nothing asked or moved")
check("in the dashboard, visible, 1 layers visible" in out and f"['{KEY}' -- " in out, "--show: SteamVR's own line is said")
check("the dashboard is open [2 other dashboard overlays: a.dashboard.main visible" in out, "--show: and what says the dashboard is open")
check(run_show({"state": CLOSED + line()})[0] == 0, "--show: shown, whatever the rest says")
# the overlay comes late (gamescope makes it again), and the dashboard shows it then
c, out, calls, js = run_show({"state": OPEN, "state.2": OPEN + line(flags="0x0", vis="not_visible"), "state.3": OPEN + line()}, "5")
check(c == 0 and docks(calls) == [] and js == [], "--show: waits for the overlay, nothing moved")
check("SteamVR has no overlay" in out and "not in the dashboard, not visible" in out, "--show: each state is said")
# not shown with the dashboard open: asked for first
c, out, calls, js = run_show({"state": OPEN + HIDDEN, "state.2": OPEN + line()})
check(c == 0 and len(js) == 1 and "ShowDashboard" in js[0] and KEY in js[0] and docks(calls) == [], "--show: asked Steam for it first")
# then docked again
c, out, calls, js = run_show({"state": OPEN + HIDDEN, "dock.dashboard": OPEN + line()})
check(c == 0 and len(js) == 2 and docks(calls) == [f"--dock-overlay dashboard {KEY}"], "--show: docked into the dashboard again")
c, out, calls, js = run_show({"state": OPEN + HIDDEN, "state.4": OPEN + line()})
check(c == 0 and docks(calls) == [f"--dock-overlay dashboard {KEY}"] * 2, "--show: and once more, never anywhere but the dashboard")
# bounded
c, out, calls, js = run_show({"state": OPEN + HIDDEN})
check(c == sc.EXIT_NOT_SHOWN and len(js) == 3 and len(docks(calls)) == 2 and calls.count("--overlays") == 4, "--show: three tries, then it says so")
# no frame of it in the dashboard: a dock would move whichever frame is active, so Steam is asked and nothing moved
for what, st in (("no flag", OPEN + line(flags="0x10", vis="not_visible")), ("no overlay", OPEN)):
    c, out, calls, js = run_show({"state": st})
    check(c == sc.EXIT_NO_FRAME and len(js) == 3 and docks(calls) == [], f"--show: {what}: asked for, nothing moved")
# a closed dashboard is never opened, nor one that closes meanwhile, nor one that cannot be told
c, out, calls, js = run_show({"state": CLOSED + HIDDEN})
check(c == sc.EXIT_CLOSED and js == [] and docks(calls) == [] and "left alone, the dashboard is closed" in out, "--show: a closed dashboard is left closed")
check(run_show({"state": CLOSED})[0] == sc.EXIT_CLOSED, "--show: closed, and no overlay: left alone")
c, out, calls, js = run_show({"state": OPEN + HIDDEN, "state.2": CLOSED + HIDDEN})
check(c == sc.EXIT_CLOSED and len(js) == 1 and docks(calls) == [], "--show: the dashboard closed meanwhile: no more tries")
c, out, calls, js = run_show({"state": HIDDEN})
check(c == sc.EXIT_CANNOT_TELL and js == [] and docks(calls) == [] and "cannot tell whether the dashboard is open" in out, "--show: nothing says whether the dashboard is open: left alone")
# nothing to tell it by
for what, kw in (("vrcmd fails", {"files": {"state": OPEN + HIDDEN}, "env": {"VRCMD_EXIT": "1"}}), ("an empty listing", {"files": {}}),
                 ("another format", {"files": {"state": OPEN + f"'{KEY}' -- 'x', size=64x64 shown\n"}}),
                 ("visible without a layer", {"files": {"state": OPEN + line(layers=0)}}),
                 ("no vrcmd", {"files": {}, "path": "/nonexistent/vrcmd"})):
    c, out, calls, js = run_show(**kw)
    check(c == sc.EXIT_CANNOT_TELL and js == [] and docks(calls) == [] and "cannot tell" in out, f"--show: {what}: cannot tell, nothing moved")
# Steam does not answer: the docks still run
c, out, calls, js = run_show({"state": OPEN + HIDDEN, "dock.dashboard": OPEN + line()}, rules=(("ShowDashboard", RuntimeError("x")),))
check(c == 0 and "ShowDashboard failed" in out, "--show: goes on without Steam")
# --look: one reading, nothing asked or moved whatever it says
for what, st, want in (("shown", OPEN + line(), 0), ("not shown", OPEN + HIDDEN, sc.EXIT_NOT_SHOWN), ("no overlay", OPEN, sc.EXIT_NO_FRAME),
                       ("closed", CLOSED + HIDDEN, sc.EXIT_NOT_SHOWN), ("an empty listing", "", sc.EXIT_CANNOT_TELL)):
    c, out, calls, js = run_show({"state": st}, None, cmd="--look")
    check(c == want and calls == ["--overlays"] and js == [], f"--look: {what}: one look")
check(cli(free_port(), "--look")[0] == sc.EXIT_USAGE and cli(free_port(), "--look", "1", "2")[0] == sc.EXIT_USAGE, "usage: --look")
for bad in (("--show",), ("--show", "12a"), ("--show", "12345678901"), ("--show", "1", "2", "3")):
    check(cli(free_port(), *bad)[0] == sc.EXIT_USAGE, f"usage: {' '.join(bad)}")

# ---- the preflight's JavaScript ----------------------------------------------------------------------
api = sc.js_api_check()
check(all(path in api for path in sc.API_CALLS.values()) and "userAgent" in api, "the probe asks for every call it needs")
check(list(sc.API_REQUIRED) == ["ShowDashboard"], "the probe needs only the flat screen's return call (PC VR was taken out)")
for gone in ("js_stream_game", "plan_pcvr", "pcvr", "devices", "apps"):
    check(not hasattr(sc, gone), f"no {gone} left (PC VR was taken out)")

# ---- the preflight against a fake endpoint -------------------------------------------------------------
good_api = {"ShowDashboard": "function", "userAgent": "Steam/1 Chrome/2"}
with FakeCef([("navigator.userAgent", good_api)]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    os.environ["STEAM_CEF_FLAG_FILE"] = os.path.join(HERE, "no-such-flag")
    r = sc.probe()
    check(r["ok"] and r["reachable"] and r["context"] and r["userAgent"] == "Steam/1 Chrome/2", "probe: everything present")
    check(r["hint"] == "Steam's debugging port is fine." and r["port"] == cef.port, "probe: the hint says fine")
    c, out, err = cli(cef.port, "--probe")
    check(c == 0 and json.loads(out)["ok"] is True, "--probe exits 0")

half = dict(good_api, ShowDashboard="undefined")
with FakeCef([("navigator.userAgent", half)]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    r = sc.probe()
    check(not r["ok"] and r["reachable"] and "ShowDashboard" in r["hint"], "probe: a missing call is named")
    c, out, err = cli(cef.port, "--probe")
    check(c == 4 and json.loads(out)["api"]["ShowDashboard"] == "undefined", "--probe exits 4 when a call is missing")

with FakeCef([], title="Steam Big Picture") as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    r = sc.probe()
    check(r["reachable"] and not r["context"] and not r["ok"] and "SharedJSContext" in r["hint"], "probe: port answers but no SharedJSContext")

dead = free_port()
os.environ["STEAM_CEF_PORT"] = str(dead)
r = sc.probe()
check(not r["reachable"] and not r["ok"] and str(dead) in r["hint"] and "STEAM_CEF_PORT" in r["hint"], "probe: a dead port is named with the way out")
check(r["flagFile"] is False and "cef-enable-remote-debugging" in r["hint"], "probe: the missing flag file is part of the hint")
flag = os.path.join(HERE, "fake-flag-file.tmp")
open(flag, "w").close()
try:
    os.environ["STEAM_CEF_FLAG_FILE"] = flag
    r = sc.probe()
    check(r["flagFile"] is True and "cef-enable-remote-debugging" not in r["hint"], "probe: with the flag file the hint does not blame it")
finally:
    os.remove(flag)
    os.environ["STEAM_CEF_FLAG_FILE"] = os.path.join(HERE, "no-such-flag")
c, out, err = cli(dead, "--probe")
check(c == 3 and json.loads(out)["reachable"] is False, "--probe exits 3 when nothing answers")
c, out, err = cli(dead, "1+1")
check(c == 3 and "cannot reach" in err, "an expression with nothing listening exits 3 with the reason")

for opt in ("--pcvr", "--devices", "--apps", "--bogus"):
    c, out, err = cli(dead, opt)
    check(c == 2, f"{opt} is an unknown option (exit 2)")

# the plain expression mode the deploy script uses is unchanged
with FakeCef([("1+1", 2)]) as cef:
    c, out, err = cli(cef.port, "1+1")
    check(c == 0 and out.strip() == "2", "steam_cef.py 'expression' prints the result as JSON")
    p = subprocess.run([sys.executable, "-I", SRC, "-"], input="1+1", capture_output=True, text=True,
                       env=dict(os.environ, STEAM_CEF_PORT=str(cef.port)), timeout=60)
    check(p.returncode == 0 and p.stdout.strip() == "2", "steam_cef.py - reads the expression from stdin")

print("failures:", fails)
sys.exit(1 if fails else 0)
