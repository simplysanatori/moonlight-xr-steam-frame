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
