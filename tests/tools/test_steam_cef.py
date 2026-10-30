#!/usr/bin/env python3
"""Tests of packaging/steam_cef.py against a fake CEF endpoint (tests/tools/fake_cef.py): the JavaScript
strings of the PC VR hand-off, the device and app list parsing, the preflight, and the command line.

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


# ---- the JavaScript strings --------------------------------------------------------------------------
check(sc.js_stream_game(123456, "76561198000000001") == 'SteamClient.Apps.StreamGame(123456, "76561198000000001", 0)',
      "StreamGame(appId, clientId, 0): the client id is a string, the app id a number")
check(sc.js_stream_game("42", " 7 ", 2) == 'SteamClient.Apps.StreamGame(42, "7", 2)', "ids are trimmed, param2 is passed")
check(sc.js_stream_game(4294967295, "1") == 'SteamClient.Apps.StreamGame(4294967295, "1", 0)', "a non-Steam shortcut's 32-bit id is allowed")
for bad in (0, -5, 2 ** 32, "abc", "1; evil()", "", None, "1.5"):
    check(raises(ValueError, sc.js_stream_game, bad, "1"), f"app id {bad!r} is refused")
for bad in ("", "abc", '1"); evil(); ("', "1 2", "-1", "1" * 21, None):
    check(raises(ValueError, sc.js_stream_game, 5, bad), f"client id {bad!r} is refused")
check(sc.js_run_game(77) == 'SteamClient.Apps.RunGame("77", "", -1, 100)', "RunGame as tools/frame-deploy.sh calls it")
check(sc.js_run_game(77, '--x "y"') == 'SteamClient.Apps.RunGame("77", "--x \\"y\\"", -1, 100)', "RunGame launch options are quoted")
check(sc.js_show_dashboard(3000000001) == "SteamClient.OpenVR.VROverlay.ShowDashboard('valve.steam.desktopgame.3000000001')",
      "ShowDashboard names the shortcut")
check(sc.js_library() == "SteamClient.URL.ExecuteSteamURL('steam://nav/games')", "the library page")
js = sc.js_devices(4)
check("RegisterForDevicesChanges" in js and "}, 4000);" in js and "unregister" in js, "devices: subscribes for 4 s and lets go")
check("}, 15000);" in sc.js_devices(600) and "}, 1000);" in sc.js_devices(0), "devices: the time is clamped to 1..15 s")
check("allApps" in sc.js_apps() and "per_client_data" in sc.js_apps(), "apps: reads the library overviews")
api = sc.js_api_check()
check(all(path in api for path in sc.API_CALLS.values()) and "userAgent" in api, "the probe asks for every call it needs")

# ---- parsing -----------------------------------------------------------------------------------------
raw = [
    {"clientId": "222", "clientName": "Zed PC", "unStreamingSessionID": 0},
    {"clientId": "111", "clientName": "attic", "unStreamingSessionID": 9},
    {"clientId": "111", "clientName": "attic again"},
    {"clientId": "0", "clientName": "nobody"},
    {"clientName": "no id"},
    "junk",
    {"clientId": 333},
]
d = sc.parse_devices(raw)
check([x["clientId"] for x in d] == ["333", "111", "222"], "devices: dropped without id, deduplicated, sorted by name (333, attic, Zed)")
check(d[1]["clientName"] == "attic" and d[1]["streaming"] is True and d[2]["streaming"] is False, "devices: name and streaming flag")
check(d[0]["clientName"] == "333", "devices: no name falls back to the id")
check(sc.parse_devices({"devices": raw[:1]})[0]["clientId"] == "222", "devices: {devices: [...]} is accepted")
check(sc.parse_devices(None) == [] and sc.parse_devices({}) == [] and sc.parse_devices("x") == [], "devices: nothing is nothing")
check(sc.parse_devices([{"client_id": "5", "client_name": "snake"}])[0]["clientName"] == "snake", "devices: snake_case keys")

apps_raw = {"apps": [
    {"appid": 10, "name": "Beta", "categories": [53], "perClient": [{"clientid": "111", "installed": True}]},
    {"appid": 11, "name": "alpha", "categories": [1, 2], "perClient": [{"clientid": "111", "installed": True}]},
    {"appid": 12, "name": "Gamma VR", "categories": [54], "perClient": [{"clientid": "111", "installed": True}, {"clientid": "222", "installed": True}]},
    {"appid": 13, "name": "Not installed", "categories": [53], "perClient": [{"clientid": "111", "installed": False}]},
    {"appid": 14, "name": "Other PC", "categories": [53], "perClient": [{"clientid": "222", "installed": True}]},
    {"appid": "bad", "name": "x", "perClient": []},
]}
a = sc.parse_apps(apps_raw, "111")
check([x["appid"] for x in a] == [10, 12, 11], "apps: this PC's installed apps, VR first then by name")
check(a[0]["vr"] is True and a[2]["vr"] is False, "apps: vr from the store categories")
check([x["appid"] for x in sc.parse_apps(apps_raw)] == [10, 12, 14, 11], "apps: no client id = installed on any PC")
nocat = sc.parse_apps({"apps": [{"appid": 5, "name": "B", "perClient": [{"clientid": "1"}]}, {"appid": 6, "name": "A", "perClient": [{"clientid": "1"}]}]}, "1")
check([x["appid"] for x in nocat] == [6, 5] and all(x["vr"] is None for x in nocat), "apps: no categories anywhere = unfiltered (vr null), by name")
check(sc.parse_apps(None) == [] and sc.parse_apps({"error": "x"}) == [], "apps: an error answer is no apps")

# ---- the preflight against a fake endpoint -------------------------------------------------------------
good_api = {"StreamGame": "function", "RunGame": "function", "RegisterForDevicesChanges": "function",
            "ExecuteSteamURL": "function", "ShowDashboard": "function", "userAgent": "Steam/1 Chrome/2"}
with FakeCef([("typeof (", good_api)]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    os.environ["STEAM_CEF_FLAG_FILE"] = os.path.join(HERE, "no-such-flag")
    r = sc.probe()
    check(r["ok"] and r["reachable"] and r["context"] and r["userAgent"] == "Steam/1 Chrome/2", "probe: everything present")
    check(r["hint"] == "Steam's debugging port is fine." and r["port"] == cef.port, "probe: the hint says fine")
    c, out, err = cli(cef.port, "--probe")
    check(c == 0 and json.loads(out)["ok"] is True, "--probe exits 0")

half = dict(good_api, StreamGame="undefined")
with FakeCef([("typeof (", half)]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    r = sc.probe()
    check(not r["ok"] and r["reachable"] and "StreamGame" in r["hint"], "probe: a missing call is named")
    c, out, err = cli(cef.port, "--probe")
    check(c == 4 and json.loads(out)["api"]["StreamGame"] == "undefined", "--probe exits 4 when a call is missing")

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

# ---- devices, apps and the hand-off ------------------------------------------------------------------
devs = {"devices": [{"clientId": "9001", "clientName": "gaming-pc", "unStreamingSessionID": 0}]}
with FakeCef([("RegisterForDevicesChanges", devs), ("appStore", apps_raw), ("StreamGame", True), ("ExecuteSteamURL", True)]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    check(sc.devices(1) == [{"clientId": "9001", "clientName": "gaming-pc", "streaming": False}], "devices() parses the collected list")
    c, out, err = cli(cef.port, "--devices", "--seconds", "1")
    check(c == 0 and json.loads(out)[0]["clientName"] == "gaming-pc", "--devices prints the list")
    check([x["appid"] for x in sc.apps("111")] == [10, 12, 11], "apps() for a client")
    c, out, err = cli(cef.port, "--apps", "--client", "222")
    check(c == 0 and [x["appid"] for x in json.loads(out)] == [12, 14], "--apps --client")

    cef.expressions.clear()
    res = sc.pcvr(12, "9001")
    check(cef.expressions == ['SteamClient.Apps.StreamGame(12, "9001", 0)'] and res["action"] == "stream",
          "pcvr with app and client sends exactly StreamGame(app, client, 0)")
    cef.expressions.clear()
    res = sc.pcvr(12)
    check(len(cef.expressions) == 2 and "RegisterForDevicesChanges" in cef.expressions[0]
          and cef.expressions[1] == 'SteamClient.Apps.StreamGame(12, "9001", 0)' and res["client"] == "9001",
          "pcvr without a client asks for the paired PCs and uses the only one")
    cef.expressions.clear()
    res = sc.pcvr()
    check(cef.expressions == ["SteamClient.URL.ExecuteSteamURL('steam://nav/games')"] and res["action"] == "library",
          "pcvr without an app opens the library page")
    c, out, err = cli(cef.port, "--pcvr", "--app", "5", "--client", "77")
    check(c == 0 and json.loads(out)["js"] == 'SteamClient.Apps.StreamGame(5, "77", 0)', "--pcvr on the command line")
    c, out, err = cli(cef.port, "--pcvr", "--app", "5; x", "--client", "77")
    check(c == 2 and "not an app id" in err, "--pcvr with a bad app id exits 2")
    c, out, err = cli(cef.port, "--bogus")
    check(c == 2, "an unknown option exits 2")
    c, out, err = cli(cef.port, "--pcvr", "--app")
    check(c == 2 and "needs a value" in err, "an option without its value exits 2")

two = {"devices": devs["devices"] + [{"clientId": "9002", "clientName": "laptop"}]}
with FakeCef([("RegisterForDevicesChanges", two)]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    check(raises(sc.CefError, sc.pcvr, 5), "pcvr with two PCs paired and none chosen is an error")
    c, out, err = cli(cef.port, "--pcvr", "--app", "5")
    check(c == 1 and "gaming-pc" in err and "laptop" in err, "...which names both, exit 1")
with FakeCef([("RegisterForDevicesChanges", {"devices": []})]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    c, out, err = cli(cef.port, "--pcvr", "--app", "5")
    check(c == 1 and "no paired Remote Play PC" in err, "no paired PC is an error")
with FakeCef([("RegisterForDevicesChanges", {"devices": [], "error": "TypeError: undefined is not a function"})]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    c, out, err = cli(cef.port, "--devices", "--seconds", "1")
    check(c == 1 and "RegisterForDevicesChanges" in err, "a failing subscription is reported")

with FakeCef([("StreamGame", RuntimeError("boom"))]) as cef:
    os.environ["STEAM_CEF_PORT"] = str(cef.port)
    c, out, err = cli(cef.port, "--pcvr", "--app", "5", "--client", "7")
    check(c == 1 and "JS exception" in err, "a JS exception comes back as exit 1")

# the plain expression mode the deploy script uses is unchanged
with FakeCef([("1+1", 2)]) as cef:
    c, out, err = cli(cef.port, "1+1")
    check(c == 0 and out.strip() == "2", "steam_cef.py 'expression' prints the result as JSON")
    p = subprocess.run([sys.executable, "-I", SRC, "-"], input="1+1", capture_output=True, text=True,
                       env=dict(os.environ, STEAM_CEF_PORT=str(cef.port)), timeout=60)
    check(p.returncode == 0 and p.stdout.strip() == "2", "steam_cef.py - reads the expression from stdin")

print("failures:", fails)
sys.exit(1 if fails else 0)
