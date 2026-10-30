#!/usr/bin/env python3
"""Evaluate JavaScript in the Steam client's SharedJSContext over the CEF
remote-debugging port (127.0.0.1:8080), using only the standard library.

    steam_cef.py 'expression'          print the awaited result as JSON
    steam_cef.py -                     the same, with the expression read from stdin

PC VR (Valve's Steam Link stream, see docs/USER_GUIDE.md "PC VR"):

    steam_cef.py --probe               preflight: does the port answer, is SharedJSContext there,
                                       are the calls below present? JSON; exit 0 ok, 3 unreachable, 4 a call missing
    steam_cef.py --devices [--seconds N]
                                       the paired Remote Play PCs (RemotePlay.RegisterForDevicesChanges,
                                       collected for N seconds, default 4): JSON [{clientId, clientName, ...}]
    steam_cef.py --apps [--client ID]  the library's apps installed on that PC (all PCs when no id): JSON
    steam_cef.py --pcvr [--app ID|steamvr] [--client ID] [--confirm N] [--dry-run]
                                       the hand-off: Apps.StreamGame(app, client, 0) streams the app from
                                       the PC to this headset. Without --app the app is SteamVR itself
                                       (250820: SteamVR Home on the PC, any game from there). Without
                                       --client the only paired PC is used. Steam refusing the call opens
                                       the app's page instead (press Stream there); several paired PCs and
                                       none chosen open that page too; no paired PC at all opens the
                                       library. The JSON says which path was taken ("path") and why.
                                       --confirm N then watches the PC's Remote Play state for N seconds
                                       ("streaming": true once Steam reports a session). --dry-run asks
                                       only for the paired PCs (none with --client) and prints the call
                                       it would make, without making it.

STEAM_CEF_PORT changes the port. The calls are the community-documented names of Steam's own client
API (decky-frontend-lib typings); their exact signatures on the Frame's Steam build are what
`--probe`, `--devices` and `--apps` are there to confirm.

Moonlight XR Native. GPLv3.
"""
import base64, json, os, socket, struct, sys, urllib.request

DEFAULT_PORT = 8080
# The calls the PC VR hand-off needs; --probe reports each as the JS typeof
API_CALLS = {
    "StreamGame": "SteamClient.Apps.StreamGame",
    "RunGame": "SteamClient.Apps.RunGame",
    "RegisterForDevicesChanges": "SteamClient.RemotePlay.RegisterForDevicesChanges",
    "ExecuteSteamURL": "SteamClient.URL.ExecuteSteamURL",
    "ShowDashboard": "SteamClient.OpenVR.VROverlay.ShowDashboard",
}
# What the hand-off cannot work without (RunGame and ShowDashboard are fallbacks)
API_REQUIRED = ("StreamGame", "RegisterForDevicesChanges")
# Steam store category ids for VR Supported / VR Only (unverified on the Frame's build: --apps
# shows what the client really reports; no app carries one => the list is not filtered)
VR_CATEGORIES = (53, 54)
MAX_SECONDS = 15   # the websocket read times out at 20 s
# SteamVR's own Steam app id (a Tool, so the library's app list may leave it out): what PC VR streams when
# no game was chosen, so the headset lands in SteamVR Home on the PC and any game starts from there
STEAMVR_APPID = 250820
STEAMVR_NAME = "SteamVR"

EXIT_FAILED, EXIT_USAGE, EXIT_UNREACHABLE, EXIT_NO_API = 1, 2, 3, 4


class CefError(Exception):
    """Steam answered, but not with what was asked for (no SharedJSContext, a JS exception, a bad result)."""


class CefUnreachable(CefError):
    """Nothing answers on the CEF port."""


def port():
    return int(os.environ.get("STEAM_CEF_PORT", str(DEFAULT_PORT)))


def target_ws():
    p = port()
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{p}/json", timeout=5) as r:
            targets = json.load(r)
    except (OSError, ValueError) as e:   # URLError, ConnectionError, socket errors, bad JSON
        raise CefUnreachable(f"cannot reach Steam's CEF port {p}: {e}")
    for t in targets:
        if t.get("title") == "SharedJSContext":
            return t["webSocketDebuggerUrl"]
    raise CefError("SharedJSContext not found: is Steam running with CEF remote debugging on?")


class WS:
    def __init__(self, url, timeout=20):
        rest = url.split("://", 1)[1]
        hostport, path = rest.split("/", 1)
        host, prt = hostport.split(":")
        self.s = socket.create_connection((host, int(prt)), timeout=timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f"GET /{path} HTTP/1.1\r\nHost: {hostport}\r\nUpgrade: websocket\r\n"
                        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                        "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.s.recv(1)
            if not chunk:
                raise ConnectionError("handshake closed")
            head += chunk
        if b" 101 " not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError(head.decode(errors="replace"))

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass

    def send(self, text):
        data = text.encode()
        hdr = bytearray([0x81])
        n = len(data)
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126); hdr += struct.pack(">H", n)
        else:
            hdr.append(0x80 | 127); hdr += struct.pack(">Q", n)
        mask = os.urandom(4)
        hdr += mask
        self.s.sendall(bytes(hdr) + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _exact(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.s.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("socket closed")
            buf += chunk
        return buf

    def recv(self):
        msg = b""
        while True:
            b0, b1 = self._exact(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._exact(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._exact(8))[0]
            payload = self._exact(n)
            op = b0 & 0x0F
            if op == 8:
                raise ConnectionError("closed by peer")
            if op in (0, 1, 2):
                msg += payload
                if b0 & 0x80:
                    return msg.decode()


def evaluate(expr):
    ws = WS(target_ws())
    try:
        ws.send(json.dumps({"id": 1, "method": "Runtime.evaluate",
                            "params": {"expression": expr, "awaitPromise": True, "returnByValue": True}}))
        while True:
            m = json.loads(ws.recv())
            if m.get("id") == 1:
                res = m.get("result", {})
                if "exceptionDetails" in res:
                    raise CefError("JS exception: " + json.dumps(res["exceptionDetails"])[:800])
                return res.get("result", {}).get("value")
    finally:
        ws.close()


# ---- the JavaScript the PC VR hand-off sends (strings only: built here, unit-tested) ----------------

def _app_id(v):
    """A Steam app id (an unsigned 32-bit number, or a non-Steam shortcut's 32-bit id) as a plain int.
    "steamvr" (any case) is SteamVR's."""
    if str(v).strip().lower() == "steamvr":
        return STEAMVR_APPID
    try:
        n = int(str(v).strip(), 10)
    except ValueError:
        raise ValueError(f"not an app id: {v!r}")
    if not 0 < n < 2 ** 32:
        raise ValueError(f"not an app id: {v!r}")
    return n


def _client_id(v):
    """A Remote Play client id: digits (Steam's are 64-bit numbers, passed around as strings)."""
    s = str(v).strip()
    if not s.isdigit() or len(s) > 20:
        raise ValueError(f"not a Remote Play client id: {v!r}")
    return s


def js_stream_game(app, client, param=0):
    """'Stream from your PC': Apps.StreamGame(appId, clientId, param2). The client id stays a string."""
    return f"SteamClient.Apps.StreamGame({_app_id(app)}, {json.dumps(_client_id(client))}, {int(param)})"


def js_run_game(app, launch_options="", param=-1, launch_source=100):
    """Apps.RunGame(appId, launchOptions, param2, launchSource): the local start (a third-party VR client shortcut)."""
    return (f"SteamClient.Apps.RunGame({json.dumps(str(_app_id(app)))}, {json.dumps(launch_options)}, "
            f"{int(param)}, {int(launch_source)})")


def js_show_dashboard(app):
    """The flat screen of a non-Steam shortcut in SteamVR's dashboard (the launcher's own return call)."""
    return f"SteamClient.OpenVR.VROverlay.ShowDashboard('valve.steam.desktopgame.{_app_id(app)}')"


def js_library():
    """Steam's library page: the last way out, when no paired PC was found to stream from."""
    return "SteamClient.URL.ExecuteSteamURL('steam://nav/games')"


def js_app_page(app):
    """The app's own library page, where its Stream button (and the PC to stream from) is one press away:
    the fallback when Steam refuses StreamGame or when several PCs are paired and none was chosen."""
    return f"SteamClient.URL.ExecuteSteamURL('steam://nav/games/details/{_app_id(app)}')"


def js_devices(seconds):
    """Subscribes to RemotePlay.RegisterForDevicesChanges, keeps the last entry of every client id it
    reports for `seconds`, then lets go of the subscription and returns {devices: [...]}."""
    ms = int(max(1, min(MAX_SECONDS, seconds)) * 1000)
    return ("new Promise(function (resolve) {"
            " var seen = {}, reg = null;"
            " function done(extra) {"
            "  try { if (reg && reg.unregister) reg.unregister(); } catch (e) {}"
            "  var out = { devices: Object.keys(seen).map(function (k) { return seen[k]; }) };"
            "  if (extra) out.error = extra;"
            "  resolve(out);"
            " }"
            " try {"
            "  reg = SteamClient.RemotePlay.RegisterForDevicesChanges(function (devs) {"
            "   (devs || []).forEach(function (d) { seen[String(d.clientId)] = d; });"
            "  });"
            " } catch (e) { done(String(e)); return; }"
            f" setTimeout(function () {{ done(null); }}, {ms});"
            "})")


def js_apps():
    """The library's overviews that exist on some Remote Play PC: id, name, store categories and the
    per-client rows (read-only)."""
    return ("(function () {"
            " try {"
            "  var all = (window.appStore && window.appStore.allApps) || [];"
            "  return { apps: all.filter(function (a) { return a.per_client_data && a.per_client_data.length; })"
            "   .map(function (a) { return { appid: a.appid, name: a.display_name,"
            "    categories: Array.prototype.slice.call(a.store_category || []),"
            "    perClient: a.per_client_data.map(function (c) { return { clientid: String(c.clientid),"
            "     installed: c.installed, name: c.client_name }; }) }; }) };"
            " } catch (e) { return { error: String(e) }; }"
            "})()")


def js_api_check():
    """typeof of every call the hand-off needs ("missing" when the path itself does not exist), and the
    client's user agent."""
    parts = ", ".join(f"{json.dumps(k)}: t(function () {{ return {path}; }})" for k, path in API_CALLS.items())
    return ("(function () { function t(f) { try { return typeof f(); } catch (e) { return \"missing\"; } }"
            " return {" + parts + ", userAgent: navigator.userAgent}; })()")


# ---- parsing what Steam answers (pure functions: unit-tested) ---------------------------------------

def parse_devices(raw):
    """Normalises RemotePlayDevice[] (or {devices: [...]}) to [{clientId, clientName, streaming}], sorted by
    name, without entries that have no client id and without duplicates."""
    if isinstance(raw, dict):
        raw = raw.get("devices", [])
    out, seen = [], set()
    for d in raw if isinstance(raw, list) else []:
        if not isinstance(d, dict):
            continue
        cid = d.get("clientId", d.get("clientid", d.get("client_id")))
        if cid is None or str(cid).strip() in ("", "0"):
            continue
        cid = str(cid).strip()
        if cid in seen:
            continue
        seen.add(cid)
        name = d.get("clientName", d.get("client_name", d.get("name")))
        session = d.get("unStreamingSessionID", d.get("streamingSessionID", 0))
        out.append({"clientId": cid, "clientName": str(name).strip() if name else cid,
                    "streaming": bool(session)})
    out.sort(key=lambda d: (d["clientName"].lower(), d["clientId"]))
    return out


def parse_apps(raw, client=None):
    """Normalises js_apps()'s answer to [{appid, name, vr}] for the apps installed on `client` (any PC when
    None). vr is True/False from the store categories, or None when no app in the whole answer carries any
    (then the caller cannot filter). VR apps first, then by name."""
    if isinstance(raw, dict):
        raw = raw.get("apps", [])
    rows = [a for a in raw if isinstance(a, dict)] if isinstance(raw, list) else []
    have_categories = any(a.get("categories") for a in rows)
    out = []
    for a in rows:
        try:
            appid = _app_id(a.get("appid"))
        except ValueError:
            continue
        per = a.get("perClient") or []
        if client is not None:
            if not any(str(c.get("clientid")) == str(client) and c.get("installed", True) for c in per):
                continue
        elif not any(c.get("installed", True) for c in per):
            continue
        vr = bool(set(a.get("categories") or []) & set(VR_CATEGORIES)) if have_categories else None
        out.append({"appid": appid, "name": str(a.get("name") or appid), "vr": vr})
    out.sort(key=lambda a: (a["vr"] is not True, a["name"].lower(), a["appid"]))
    return out


def probe_hint(result):
    """One line for the user from probe()'s result: what is wrong and what to try."""
    p = result.get("port", DEFAULT_PORT)
    if not result.get("reachable"):
        flag = result.get("flagFile")
        extra = (" The flag file ~/.steam/steam/.cef-enable-remote-debugging is missing: create it and restart Steam."
                 if flag is False else "")
        return (f"Steam's debugging port {p} does not answer, so the PC VR hand-off and the flat screen return "
                f"cannot work. SteamOS can move or close it; set STEAM_CEF_PORT if it moved.{extra}")
    if not result.get("context"):
        return "Steam answers on the debugging port but has no SharedJSContext yet: wait for Steam to finish starting."
    missing = [k for k in API_REQUIRED if result.get("api", {}).get(k) != "function"]
    if missing:
        return ("This Steam build lacks " + ", ".join(missing) + ": the PC VR hand-off can only open the "
                "library page. Please report the probe output.")
    return "Steam's debugging port is fine."


# ---- the commands ------------------------------------------------------------------------------------

def flag_file_present():
    """The file that turns Steam's CEF debugging on (Decky's convention), in either Steam root;
    STEAM_CEF_FLAG_FILE names another one (tests)."""
    override = os.environ.get("STEAM_CEF_FLAG_FILE")
    cands = [override] if override else [os.path.expanduser("~/.steam/steam/.cef-enable-remote-debugging"),
                                         os.path.expanduser("~/.local/share/Steam/.cef-enable-remote-debugging")]
    return any(os.path.exists(c) for c in cands)


def probe():
    result = {"port": port(), "reachable": False, "context": False, "api": {}, "userAgent": "",
              "flagFile": flag_file_present()}
    try:
        target_ws()
        result["reachable"] = result["context"] = True
        got = evaluate(js_api_check()) or {}
        result["userAgent"] = str(got.pop("userAgent", ""))
        result["api"] = {k: v for k, v in got.items() if k != "error"}
        if "error" in got:
            result["error"] = got["error"]
    except CefUnreachable as e:
        result["error"] = str(e)
    except CefError as e:
        result["reachable"] = True    # the port answered, there is no context
        result["error"] = str(e)
    result["hint"] = probe_hint(result)
    result["ok"] = (result["reachable"] and result["context"]
                    and all(result["api"].get(k) == "function" for k in API_REQUIRED))
    return result


def devices(seconds=4):
    got = evaluate(js_devices(seconds))
    if isinstance(got, dict) and got.get("error"):
        raise CefError("RemotePlay.RegisterForDevicesChanges: " + str(got["error"]))
    return parse_devices(got)


def apps(client=None):
    got = evaluate(js_apps())
    if isinstance(got, dict) and got.get("error"):
        raise CefError("the app store: " + str(got["error"]))
    return parse_apps(got, client)


def plan_pcvr(app=None, client=None, found=None):
    """The hand-off's decision (pure: unit-tested with made-up device lists). app None is SteamVR. found is
    the paired PCs (parse_devices' list; only looked at when client is None). Returns {path, target, app,
    appName, client, js, fallback, reason}: path "stream" calls StreamGame (fallback: the app's page if
    Steam refuses), "app-page" opens the app's page (several PCs, none chosen), "library" opens the library
    (no paired PC)."""
    app = STEAMVR_APPID if app is None or str(app).strip() == "" else _app_id(app)
    steamvr = app == STEAMVR_APPID
    out = {"path": None, "target": "steamvr" if steamvr else "game", "app": app,
           "appName": STEAMVR_NAME if steamvr else None, "client": None, "js": None, "fallback": None,
           "reason": ""}
    if client is not None and str(client).strip() != "":
        out["client"] = _client_id(client)
        out["reason"] = "the PC chosen in the launcher's Settings"
    else:
        found = [d for d in (found or []) if isinstance(d, dict) and d.get("clientId")]
        if len(found) == 1:
            out["client"] = _client_id(found[0]["clientId"])
            out["reason"] = "the only paired PC"
        elif len(found) > 1:
            out["path"] = "app-page"
            out["js"] = js_app_page(app)
            out["reason"] = ("several Remote Play PCs are paired (" + ", ".join(d.get("clientName", d["clientId"])
                             for d in found) + ") and none is chosen in the launcher's Settings: the app's page "
                             "opens, press Stream there")
            return out
        else:
            out["path"] = "library"
            out["js"] = js_library()
            out["reason"] = ("no paired Remote Play PC found: pair this headset with the PC in Steam "
                             "(Steam Link), then try again; the library opens")
            return out
    out["path"] = "stream"
    out["js"] = js_stream_game(app, out["client"])
    out["fallback"] = js_app_page(app)
    return out


def stream_state(found, client):
    """Whether Steam's device list says `client` is streaming: True, False, or None when it is not listed."""
    for d in found or []:
        if str(d.get("clientId")) == str(client):
            return bool(d.get("streaming"))
    return None


def pcvr(app=None, client=None, dry_run=False, confirm=0, ev=None, list_devices=None):
    """The hand-off. Asks Steam for what plan_pcvr() decides and returns the plan with what happened:
    {action (= path), path, target, app, appName, client, js (the call made), tried: [{js, error}], reason,
    returned, streaming (with confirm), dryRun}. ev and list_devices stand in for evaluate() and devices()
    in tests."""
    ev = ev or evaluate
    list_devices = list_devices or devices
    found, why_none = None, ""
    if client is None or str(client).strip() == "":
        try:
            found = list_devices()
        except CefUnreachable:
            raise
        except CefError as e:   # the list call is missing or threw: nothing to stream to
            found, why_none = [], str(e)
    plan = plan_pcvr(app, client, found)
    if why_none:
        plan["reason"] += " (" + why_none + ")"
    plan["tried"] = []
    if dry_run:
        plan["dryRun"] = True
        plan["action"] = plan["path"]
        return plan
    try:
        plan["returned"] = ev(plan["js"])
        plan["tried"].append({"js": plan["js"], "error": None})
    except CefUnreachable:
        raise
    except CefError as e:
        plan["tried"].append({"js": plan["js"], "error": str(e)[:300]})
        if not plan["fallback"]:
            raise
        # Steam refused StreamGame (the call or its form changed): the app's page, one press from Stream
        try:
            ev(plan["fallback"])
        except CefError as e2:
            raise CefError(f"StreamGame failed ({e}) and so did the app's page ({e2})")
        plan["tried"].append({"js": plan["fallback"], "error": None})
        plan["path"], plan["js"] = "app-page", plan["fallback"]
        plan["reason"] = "Steam refused StreamGame: the app's page opens, press Stream there"
    plan["action"] = plan["path"]
    if confirm and plan["path"] == "stream":
        # Steam's Remote Play device list carries a session id while a stream runs (parse_devices'
        # "streaming"); a failure here does not undo the call above
        try:
            plan["streaming"] = stream_state(list_devices(confirm), plan["client"])
        except (CefError, OSError) as e:
            plan["streaming"] = None
            plan["confirmError"] = str(e)[:300]
    return plan


def _opt(argv, name, default=None):
    if name in argv:
        i = argv.index(name)
        if i + 1 >= len(argv):
            raise ValueError(f"{name} needs a value")
        return argv[i + 1]
    return default


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    cmd = argv[1]
    try:
        if cmd == "--probe":
            res = probe()
            print(json.dumps(res))
            if not res["reachable"]:
                return EXIT_UNREACHABLE
            return 0 if res["ok"] else EXIT_NO_API
        if cmd == "--devices":
            print(json.dumps(devices(float(_opt(argv, "--seconds", 4)))))
            return 0
        if cmd == "--apps":
            print(json.dumps(apps(_opt(argv, "--client"))))
            return 0
        if cmd == "--pcvr":
            confirm = float(_opt(argv, "--confirm", 0))
            if not 0 <= confirm <= MAX_SECONDS:
                raise ValueError(f"--confirm takes 0 to {MAX_SECONDS} seconds")
            print(json.dumps(pcvr(_opt(argv, "--app"), _opt(argv, "--client"), "--dry-run" in argv, confirm)))
            return 0
        if cmd.startswith("--"):
            sys.stderr.write(__doc__)
            return EXIT_USAGE
        if len(argv) != 2:
            sys.exit(__doc__)
        print(json.dumps(evaluate(sys.stdin.read() if cmd == "-" else cmd)))
        return 0
    except ValueError as e:
        sys.stderr.write(f"{e}\n")
        return EXIT_USAGE
    except CefUnreachable as e:
        sys.stderr.write(f"{e}\n")
        return EXIT_UNREACHABLE
    except CefError as e:
        sys.stderr.write(f"{e}\n")
        return EXIT_FAILED
    except OSError as e:   # a connection that broke mid-way
        sys.stderr.write(f"cannot talk to Steam's CEF port {port()}: {e}\n")
        return EXIT_UNREACHABLE


if __name__ == "__main__":
    sys.exit(main(sys.argv))
