#!/usr/bin/env python3
"""Tests of tools/probe/doctor.py (the update doctor): the probe diff and the Steam port line.

    test_doctor.py <tools/probe/doctor.py> <docs/probe/openxr-probe.txt> <packaging/steam_cef.py>

Moonlight XR Native. GPLv3.
"""
import importlib.util, os, socket, stat, subprocess, sys, tempfile

DOCTOR, PROBE, CEF = sys.argv[1:4]
HERE = os.path.dirname(os.path.abspath(__file__))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


doctor = load("doctor", DOCTOR)
fake_cef = load("fake_cef", os.path.join(HERE, "fake_cef.py"))
fails = 0


def check(cond, what):
    global fails
    if cond:
        print("ok:", what)
    else:
        fails += 1
        print("FAIL:", what, file=sys.stderr)


def run(*args, env_extra=None):
    env = dict(os.environ, HOME=tmp, USERPROFILE=tmp)
    env.update(env_extra or {})
    p = subprocess.run([sys.executable, "-I", DOCTOR, *args], capture_output=True, text=True, env=env, timeout=120)
    return p.returncode, p.stdout, p.stderr


base = open(PROBE, encoding="utf-8").read()
tmp = tempfile.mkdtemp()

# the same probe, other refresh rates: nothing changed
same = base.replace("# display refresh rates: 90.00", "# display refresh rates: 72.00 90.00").replace(
    "# current refresh rate: 90.00", "# current refresh rate: 72.00")
check(same != base, "the test changes the refresh rate lines")
added, removed = doctor.compare(base, same)
check(not added and not removed, "refresh rates are not a difference")

# an extension gone, one new, a path that stopped being accepted, a new format
cur = base.replace("XR_EXT_palm_pose v3\n", "").replace("XR_EXT_uuid v1\n", "XR_EXT_uuid v1\nXR_FB_new_thing v1\n")
cur = cur.replace("  left   input/trackpad                   XR_ERROR_PATH_UNSUPPORTED",
                  "  left   input/trackpad                   ok")
cur = cur.replace("130 D32_SFLOAT_S8_UINT\n", "130 D32_SFLOAT_S8_UINT\n999 NEW_FORMAT\n")
added, removed = doctor.compare(base, cur)
check(("# instance extensions (49)", "XR_FB_new_thing v1") in added, "a new extension is listed under the extensions")
check(("# instance extensions (49)", "XR_EXT_palm_pose v3") in removed, "a gone extension is listed")
check(any(l == "left   input/trackpad                   ok" and s == "profile /interaction_profiles/valve/frame_controller" for s, l in added),
      "a binding path that changed is listed under its profile")
check(any("NEW_FORMAT" in l for s, l in added), "a new swapchain format is listed")
text = doctor.report(added, removed)
check("  + XR_FB_new_thing v1" in text and "  - XR_EXT_palm_pose v3" in text, "the report shows + and -")

cur_file = os.path.join(tmp, "cur.txt")
open(cur_file, "w", encoding="utf-8").write(cur)
same_file = os.path.join(tmp, "same.txt")
open(same_file, "w", encoding="utf-8").write(same)

c, out, err = run("--current", same_file, "--baseline", PROBE, "--no-cef")
check(c == 0 and "matches" in out, "exit 0 when the probe matches")
c, out, err = run("--current", cur_file, "--baseline", PROBE, "--no-cef")
check(c == 1 and "+ XR_FB_new_thing v1" in out and "- XR_EXT_palm_pose v3" in out, "exit 1 and the lines when it differs")
c, out, err = run("--current", os.path.join(tmp, "missing.txt"), "--baseline", PROBE, "--no-cef")
check(c == 2, "exit 2 when the probe cannot be read")
c, out, err = run("--bogus")
check(c == 2, "exit 2 on a bad option")

# --save, then it matches itself; with no baseline at all it only says so
saved = os.path.join(tmp, "sub", "baseline.txt")
c, out, err = run("--current", cur_file, "--baseline", saved, "--save", "--no-cef")
check(c == 0 and os.path.exists(saved), "--save writes the baseline (and its folder)")
c, out, err = run("--current", cur_file, "--baseline", saved, "--no-cef")
check(c == 0 and "matches" in out, "the saved baseline matches")
c, out, err = run("--current", cur_file, "--baseline", os.path.join(tmp, "none.txt"), "--no-cef")
check(c == 0 and "no baseline" in out, "no baseline is not a difference")

# mlxr-hello itself (a stand-in script, POSIX only)
if os.name == "posix":
    hello = os.path.join(tmp, "mlxr-hello")
    with open(hello, "w") as f:
        f.write('#!/bin/sh\n[ "$1" = --probe ] && [ "$2" = --probe-out ] || exit 9\ncp "%s" "$3"\n' % cur_file)
    os.chmod(hello, os.stat(hello).st_mode | stat.S_IXUSR)
    c, out, err = run("--hello", hello, "--baseline", PROBE, "--no-cef")
    check(c == 1 and "+ XR_FB_new_thing v1" in out, "--hello runs the probe and diffs it")
    bad = os.path.join(tmp, "bad-hello")
    with open(bad, "w") as f:
        f.write("#!/bin/sh\necho no headset >&2\nexit 1\n")
    os.chmod(bad, os.stat(bad).st_mode | stat.S_IXUSR)
    c, out, err = run("--hello", bad, "--baseline", PROBE, "--no-cef")
    check(c == 2 and "no headset" in err, "a probe that fails is exit 2 with its message")

# the Steam port line
good_api = {"StreamGame": "function", "RunGame": "function", "RegisterForDevicesChanges": "function",
            "ExecuteSteamURL": "function", "ShowDashboard": "function", "userAgent": "x"}
with fake_cef.FakeCef([("typeof (", good_api)]) as cef:
    c, out, err = run("--current", same_file, "--baseline", PROBE, "--cef-script", CEF,
                      env_extra={"STEAM_CEF_PORT": str(cef.port), "STEAM_CEF_FLAG_FILE": os.path.join(tmp, "x")})
    check(c == 0 and "Steam CEF port: Steam's debugging port is fine." in out, "the port line says fine")
s = socket.socket()
s.bind(("127.0.0.1", 0))
dead = s.getsockname()[1]
s.close()
c, out, err = run("--current", same_file, "--baseline", PROBE, "--cef-script", CEF, env_extra={"STEAM_CEF_PORT": str(dead)})
check(c == 1 and "does not answer" in out, "a dead port is a finding")

print("failures:", fails)
sys.exit(1 if fails else 0)
