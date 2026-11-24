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
OTHER = "'valve.steam.desktopgame.12345678900' -- 'another', size=64x64 visible VROverlayType_Dashboard flags=0x8000\n"


def line(size="1920x1080", flags="0x8000", vis="visible"):
    return f"'{KEY}' -- 'Moonlight', size={size} {vis} VROverlayType_Dashboard flags={flags}\n"


own, layers = sc.overlay_lines(OTHER + line() + f"'{KEY}.layer1' -- 'x', visible VROverlayType_Subview flags=0x0\n"
                               + f"'{KEY}.layer2' -- 'x', not_visible VROverlayType_Subview flags=0x0\n", KEY)
check(own == line().strip() and layers == 1, "--show: the window's own line and its visible layers")
check(sc.overlay_lines(OTHER, KEY) == (None, 0), "--show: another app's overlay is not it")
check(sc.overlay_state(line()) == (True, True, True), "overlay: picture, in the dashboard, visible")
check(sc.overlay_state(line("0x0", "0x10", "not_visible")) == (False, False, False), "overlay: nothing of it")
check(sc.overlay_state(f"'{KEY}' -- 'x', visible VROverlayType_Dashboard flags=0x8010") == (False, True, True), "overlay: no size is no picture")
check(sc.overlay_state(f"'{KEY}' x size=64x64 flags=0x8000 visible VROverlayType") == (True, True, True), "overlay: the fields in another order")
check(sc.overlay_state(f"'{KEY}' -- 'x', size=64x64 shown") == (True, False, None), "overlay: a line that does not say is not guessed at")

FAKE_VRCMD = """#!/bin/bash
# --overlays prints $OVL/state (and from the Nth call on, $OVL/state.N); a dock to W makes $OVL/dock.W the state
echo "$*" >> "$OVL/log"
case "$1" in
  --overlays) n=$(grep -c -- --overlays "$OVL/log"); [ -e "$OVL/state.$n" ] && cp "$OVL/state.$n" "$OVL/state"
              cat "$OVL/state" 2>/dev/null; exit "${VRCMD_EXIT:-0}";;
  --dock-overlay) [ -e "$OVL/dock.$2" ] && cp "$OVL/dock.$2" "$OVL/state"; exit 0;;
esac
"""


def run_show(files, seconds="1", rules=(("ShowDashboard", None),), env=None, path=None, app="1234567890"):
    """(exit code, output, vrcmd's calls, the JavaScript Steam got)"""
    with tempfile.TemporaryDirectory() as d, FakeCef(list(rules)) as cef:
        exe = os.path.join(d, "vrcmd")
        open(exe, "w").write(FAKE_VRCMD)
        os.chmod(exe, os.stat(exe).st_mode | stat.S_IXUSR)
        for n, text in files.items():
            open(os.path.join(d, n), "w").write(text)
        c, out, err = cli(cef.port, "--show", app, seconds, env_extra=dict({"MLXR_VRCMD": path or exe, "OVL": d}, **(env or {})))
        log = os.path.join(d, "log")
        calls = open(log).read().split("\n")[:-1] if os.path.exists(log) else []
        return c, out + err, calls, list(cef.expressions)


def docks(calls):
    return [x for x in calls if x.startswith("--dock-overlay")]


c, out, calls, js = run_show({"state": OTHER + line()})
check(c == 0 and calls == ["--overlays"] and js == [], "--show: shown: one look, nothing asked or moved")
check("a picture, in the dashboard, visible" in out and f"['{KEY}' -- " in out, "--show: SteamVR's own line is said")
# the picture comes late (gamescope makes the overlay again), and the dashboard shows it then
c, out, calls, js = run_show({"state": OTHER, "state.2": line("0x0", "0x0", "not_visible"), "state.3": line()}, "5")
check(c == 0 and docks(calls) == [] and js == [], "--show: waits for the picture, nothing moved")
check("SteamVR has no overlay" in out and "no picture, not in the dashboard, not visible" in out, "--show: each state is said")
# showable but not shown: asked for first
c, out, calls, js = run_show({"state": line(vis="not_visible"), "state.2": line()})
check(c == 0 and len(js) == 1 and "ShowDashboard" in js[0] and KEY in js[0] and docks(calls) == [], "--show: asked Steam for it first")
# then docked again
c, out, calls, js = run_show({"state": line(vis="not_visible"), "dock.dashboard": line()})
check(c == 0 and len(js) == 2 and docks(calls) == [f"--dock-overlay dashboard {KEY}"], "--show: docked into the dashboard again")
c, out, calls, js = run_show({"state": line(vis="not_visible"), "dock.world": line(vis="not_visible"), "state.4": line()})
check(c == 0 and docks(calls) == [f"--dock-overlay {w} {KEY}" for w in ("dashboard", "world", "dashboard")], "--show: through the world the last time")
# bounded
c, out, calls, js = run_show({"state": line(vis="not_visible")})
check(c == sc.EXIT_NOT_SHOWN and len(js) == 3 and len(docks(calls)) == 3 and calls.count("--overlays") == 4, "--show: three tries, then it says so")
# no picture: a dock would move whichever frame is active, so nothing is moved
for what, st in (("no picture", line("0x0")), ("no flag", line(flags="0x10", vis="not_visible")), ("no overlay", OTHER)):
    c, out, calls, js = run_show({"state": st})
    check(c == sc.EXIT_NO_PICTURE and js == [] and docks(calls) == [], f"--show: {what}: nothing asked, nothing moved")
# visible with no picture is the empty frame, not shown
check("no picture, in the dashboard, visible" in run_show({"state": line("0x0")})[1], "--show: an empty frame is not taken for the window")
# nothing to tell it by
for what, kw in (("vrcmd fails", {"files": {"state": line()}, "env": {"VRCMD_EXIT": "1"}}), ("an empty listing", {"files": {}}),
                 ("another format", {"files": {"state": f"'{KEY}' -- 'x', size=64x64 shown\n"}}),
                 ("no vrcmd", {"files": {}, "path": "/nonexistent/vrcmd"})):
    c, out, calls, js = run_show(**kw)
    check(c == sc.EXIT_CANNOT_TELL and js == [] and docks(calls) == [] and "cannot tell" in out, f"--show: {what}: cannot tell, nothing moved")
# Steam does not answer: the docks still run
c, out, calls, js = run_show({"state": line(vis="not_visible"), "dock.dashboard": line()}, rules=(("ShowDashboard", RuntimeError("x")),))
check(c == 0 and "ShowDashboard failed" in out, "--show: goes on without Steam")
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
