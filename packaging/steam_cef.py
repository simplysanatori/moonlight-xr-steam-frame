#!/usr/bin/env python3
"""Evaluate JavaScript in the Steam client's SharedJSContext over the CEF
remote-debugging port (127.0.0.1:8080), using only the standard library.

    steam_cef.py 'expression'          print the awaited result as JSON
    steam_cef.py -                     the same, with the expression read from stdin

    steam_cef.py --probe               does the port answer, is SharedJSContext there, is the call the
                                       flat launcher brings its window back with (ShowDashboard) present?
                                       JSON; exit 0 ok, 3 unreachable, 4 the call missing

    steam_cef.py --show APPID [SECONDS]
                                       is the app's flat window really shown in the headset? If not, and the
                                       dashboard is open, Steam is asked for it again and its frame docked again,
                                       three times at the most. A closed dashboard is left closed. SECONDS (6):
                                       how long SteamVR may take to have a frame for it. One line a step, the
                                       time first. Exit 0 shown, 10 SteamVR has no frame for it after the last
                                       try, 11 not shown after the last try, 12 cannot tell (no vrcmd, no
                                       SteamVR, nothing that says whether the dashboard is open), 13 the
                                       dashboard is closed

    steam_cef.py --look APPID          the same question answered once, nothing asked or moved: exit 0 shown,
                                       10 no frame for it, 11 not shown, 12 cannot tell

STEAM_CEF_PORT changes the port, MLXR_VRCMD the vrcmd of --show and --look. The installer (packaging/install.sh), the flat launcher's return from
a stream and tools/frame-deploy.sh drive Steam through this; tools/probe/doctor.py runs --probe.

Moonlight XR Native. GPLv3.
"""
import base64, json, os, re, socket, struct, subprocess, sys, time, urllib.request

DEFAULT_PORT = 8080
# The call the flat launcher brings its window back with after a stream; --probe reports it as the JS typeof
API_CALLS = {
    "ShowDashboard": "SteamClient.OpenVR.VROverlay.ShowDashboard",
}
API_REQUIRED = ("ShowDashboard",)

EXIT_FAILED, EXIT_USAGE, EXIT_UNREACHABLE, EXIT_NO_API = 1, 2, 3, 4
EXIT_NO_FRAME, EXIT_NOT_SHOWN, EXIT_CANNOT_TELL, EXIT_CLOSED = 10, 11, 12, 13


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


# ---- the preflight (strings only: built here, unit-tested) ------------------------------------------

def js_api_check():
    """typeof of every call in API_CALLS ("missing" when the path itself does not exist), and the client's
    user agent."""
    parts = ", ".join(f"{json.dumps(k)}: t(function () {{ return {path}; }})" for k, path in API_CALLS.items())
    return ("(function () { function t(f) { try { return typeof f(); } catch (e) { return \"missing\"; } }"
            " return {" + parts + ", userAgent: navigator.userAgent}; })()")


def probe_hint(result):
    """One line for the user from probe()'s result: what is wrong and what to try."""
    p = result.get("port", DEFAULT_PORT)
    if not result.get("reachable"):
        flag = result.get("flagFile")
        extra = (" The flag file ~/.steam/steam/.cef-enable-remote-debugging is missing: create it and restart Steam."
                 if flag is False else "")
        return (f"Steam's debugging port {p} does not answer, so the flat window cannot be brought back after a "
                f"stream and the installer cannot add its Steam shortcut. SteamOS can move or close it; set "
                f"STEAM_CEF_PORT if it moved.{extra}")
    if not result.get("context"):
        return "Steam answers on the debugging port but has no SharedJSContext yet: wait for Steam to finish starting."
    missing = [k for k in API_REQUIRED if result.get("api", {}).get(k) != "function"]
    if missing:
        return ("This Steam build lacks " + ", ".join(missing) + ": the flat window may not come back after a "
                "stream. Please report the probe output.")
    return "Steam's debugging port is fine."


# ---- the flat window in the headset ------------------------------------------------------------------
# gamescope gives SteamVR the app's window as the overlay valve.steam.desktopgame.APPID. ShowDashboard answers null
# whatever came of it: on 2026-11-18 the dashboard opened on the app's frame with nothing in it. Likely (nothing read
# SteamVR's side then): the stream, which fades the window out, took the overlay away, and the frame came up without
# the new one's picture. vrcmd --overlays says what SteamVR has. The way to read it and to dock a frame again is from
# Azahar for Frame, by the same owner. The overlay's size says nothing: on the Frame (2026-11-25) it reads 0x0 with the
# window on show, "visible" and one of its layers visible. So that is what shown means here.

VRCMD = "/opt/steamvr/bin/linuxarm64/vrcmd"
OVERLAY_IN_DASHBOARD = 0x8000        # OpenVR's VROverlayFlags_VisibleInDashboard
# The docks before each ShowDashboard. Only into the dashboard, which is where the frame is meant to be: a try cut
# short (a stream starts) leaves it nowhere else.
SHOW_TRIES = ((), ("dashboard",), ("dashboard",))
SHOW_DEADLINE_S = 35                 # no new step after this, whatever is left of the tries


def vrcmd_path():
    return os.environ.get("MLXR_VRCMD") or VRCMD


def vrcmd(*args):
    """vrcmd's output, or None when it failed. A call takes about 2 s (it connects to SteamVR first)."""
    env = {k: v for k, v in os.environ.items() if k not in ("LD_PRELOAD", "LD_LIBRARY_PATH")}
    try:
        p = subprocess.run([vrcmd_path(), *args], env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                           stderr=subprocess.DEVNULL, timeout=8)
    except (OSError, subprocess.TimeoutExpired):
        return None
    return p.stdout.decode(errors="replace") if p.returncode == 0 else None


def overlay_lines(listing, key):
    """The overlay's own line of a vrcmd --overlays listing (None: not listed), and how many of its layers
    (KEY.layer1 ...) are visible."""
    own, layers = None, 0
    for line in listing.splitlines():
        if line.startswith(f"'{key}' "):
            own = line.strip()
        elif line.startswith(f"'{key}.") and re.search(r" visible VROverlayType", line):
            layers += 1
    return own, layers


def overlay_state(line):
    """(in the dashboard, visible) of an overlay's line; visible is None when the line does not say."""
    flags = re.search(r"\bflags=0x([0-9a-fA-F]+)", line)
    vis = re.search(r" (visible|not_visible) VROverlayType", line)
    return (bool(flags) and int(flags.group(1), 16) & OVERLAY_IN_DASHBOARD != 0,
            None if vis is None else vis.group(1) == "visible")


def dashboard_open(listing, key):
    """(open, its lines): whether the dashboard is open, from the dashboard overlays that are not the app's own: one
    of them visible is open, all of them not visible is closed, none of them listed is None. (vrcmd has no word for
    it. Not seen on a headset yet, so what it was told by is said.)"""
    said = []
    for line in listing.splitlines():
        m = re.match(r"'([^']+)' ", line)
        vis = re.search(r" (visible|not_visible) VROverlayType_Dashboard", line)
        if m and vis and m.group(1) != key and not m.group(1).startswith(key + "."):
            said.append((vis.group(1) == "visible", f"{m.group(1)} {vis.group(1)}"))
    said.sort(key=lambda x: not x[0])
    return (any(v for v, _ in said) if said else None), [t for _, t in said]


class Overlays:
    """What SteamVR has of the app's window, one vrcmd --overlays a look; says what it read when that changed."""

    def __init__(self, appid):
        self.key = f"valve.steam.desktopgame.{int(appid)}"
        self.start = time.monotonic()
        self.said = {}
        self.known = self.shown = self.framed = self.listed = False
        self.dashboard = None

    def elapsed(self):
        return time.monotonic() - self.start

    def say(self, text, about=None):
        if about is None or self.said.get(about) != text:
            print(f"{self.elapsed():.1f} s: {text}", flush=True)
        self.said[about] = text

    def ready(self):
        """Whether there is a vrcmd and a SteamVR to ask"""
        if not os.access(vrcmd_path(), os.X_OK):
            self.say(f"cannot tell, no vrcmd at {vrcmd_path()}")
            return False
        if os.environ.get("MLXR_VRCMD"):
            return True
        # vrcmd is for a SteamVR that runs
        try:
            up = subprocess.run(["pgrep", "-x", "vrserver"], stdout=subprocess.DEVNULL, timeout=5).returncode == 0
        except (OSError, subprocess.TimeoutExpired):
            up = False
        if not up:
            self.say("cannot tell, SteamVR is not running")
        return up

    def look(self):
        """known: the listing says; listed, framed: the window's overlay is there, and is the dashboard's; shown:
        it is visible and so is a layer of it; dashboard: open, closed or None (dashboard_open)"""
        self.known = self.shown = self.framed = self.listed = False
        self.dashboard = None
        listing = vrcmd("--overlays")
        if listing is None or not re.search(r"^'", listing, re.M):
            return
        self.dashboard, by = dashboard_open(listing, self.key)
        line, layers = overlay_lines(listing, self.key)
        if line is None:
            self.known = True
            self.say(f"SteamVR has no overlay {self.key}", "window")
        else:
            self.listed = True
            self.framed, visible = overlay_state(line)
            # (a SteamVR that writes the line another way is not guessed at, nor one visible without a layer)
            self.known = visible is not None and (not visible or layers > 0)
            self.shown = bool(visible) and layers > 0
            self.say(f"{'in' if self.framed else 'not in'} the dashboard, {'visible' if visible else 'not visible'}, "
                     f"{layers} layers visible [{line[:300]}]", "window")
        self.say(f"the dashboard is {({True: 'open', False: 'closed', None: 'not known to be open or closed'})[self.dashboard]}"
                 f" [{len(by)} other dashboard overlays{': ' if by else ''}{', '.join(by)[:300]}]", "dashboard")


def look(appid):
    o = Overlays(appid)
    if not o.ready():
        return EXIT_CANNOT_TELL
    o.look()
    if not o.known:
        o.say("cannot tell from vrcmd --overlays")
        return EXIT_CANNOT_TELL
    return 0 if o.shown else EXIT_NOT_SHOWN if o.framed else EXIT_NO_FRAME


def show(appid, seconds):
    o = Overlays(appid)
    if not o.ready():
        return EXIT_CANNOT_TELL
    o.look()
    unknown = 0 if o.known else 1
    while not o.shown and not o.framed and unknown < 2 and o.elapsed() < seconds:
        time.sleep(0.25)
        o.look()
        unknown = 0 if o.known else unknown + 1
    if not o.known:
        o.say("cannot tell from vrcmd --overlays")
        return EXIT_CANNOT_TELL
    if o.shown:
        return 0
    time.sleep(0.75)     # the dashboard makes the frame a moment after the overlay can show
    for n, docks in enumerate(SHOW_TRIES, 1):
        # Nothing here opens a dashboard that is closed: the player closed it, or the headset is off
        if o.dashboard is None:
            o.say("cannot tell whether the dashboard is open, left alone")
            return EXIT_CANNOT_TELL
        if not o.dashboard:
            o.say("left alone, the dashboard is closed")
            return EXIT_CLOSED
        if o.elapsed() > SHOW_DEADLINE_S:
            o.say("out of time")
            break
        # Without the flag the dashboard has no frame for the window, and a dock request moves whichever frame is
        # active (Steam's library): Steam is only asked then
        if not o.framed:
            docks = ()
        o.say(f"not shown, try {n} of {len(SHOW_TRIES)}: "
              + (f"docking it again ({', then '.join(docks)}) and " if docks else "") + "asking Steam for it")
        for where in docks:
            if vrcmd("--dock-overlay", where, o.key) is None:
                o.say(f"vrcmd --dock-overlay {where} failed")
        try:
            evaluate(f"SteamClient.OpenVR.VROverlay.ShowDashboard({json.dumps(o.key)})")
        except (CefError, OSError) as e:
            o.say(f"ShowDashboard failed: {e}")
        time.sleep(0.5)
        o.look()
        if o.shown:
            return 0
        if not o.known:
            o.say("cannot tell from vrcmd --overlays")
            return EXIT_CANNOT_TELL
    return EXIT_NOT_SHOWN if o.framed else EXIT_NO_FRAME


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
        if cmd == "--show":
            if len(argv) not in (3, 4) or not re.fullmatch(r"[0-9]{1,10}", argv[2]):
                sys.stderr.write(__doc__)
                return EXIT_USAGE
            return show(argv[2], float(argv[3]) if len(argv) == 4 else 6.0)
        if cmd == "--look":
            if len(argv) != 3 or not re.fullmatch(r"[0-9]{1,10}", argv[2]):
                sys.stderr.write(__doc__)
                return EXIT_USAGE
            return look(argv[2])
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
