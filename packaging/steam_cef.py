#!/usr/bin/env python3
"""Evaluate JavaScript in the Steam client's SharedJSContext over the CEF
remote-debugging port (127.0.0.1:8080), using only the standard library.

    steam_cef.py 'expression'          print the awaited result as JSON
    steam_cef.py -                     the same, with the expression read from stdin

    steam_cef.py --probe               does the port answer, is SharedJSContext there, is the call the
                                       flat launcher brings its window back with (ShowDashboard) present?
                                       JSON; exit 0 ok, 3 unreachable, 4 the call missing

    steam_cef.py --show APPID [SECONDS]
                                       is the app's flat window really shown in the headset? If not, its frame
                                       is asked for and docked again, three times at the most. SECONDS (6): how
                                       long SteamVR may take to have a picture of it. One line a step, the time
                                       first. Exit 0 shown, 10 SteamVR has no picture of it (nothing was moved),
                                       11 not shown after the last try, 12 cannot tell (no vrcmd, no SteamVR)

STEAM_CEF_PORT changes the port, MLXR_VRCMD the vrcmd of --show. The installer (packaging/install.sh), the flat launcher's return from
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
EXIT_NO_PICTURE, EXIT_NOT_SHOWN, EXIT_CANNOT_TELL = 10, 11, 12


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
# Azahar for Frame, by the same owner.

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
    """(picture, in the dashboard, visible) of an overlay's line; visible is None when the line does not say."""
    size = re.search(r"\bsize=(\d+)x(\d+)", line)
    flags = re.search(r"\bflags=0x([0-9a-fA-F]+)", line)
    vis = re.search(r" (visible|not_visible) VROverlayType", line)
    return (bool(size) and int(size.group(1)) > 0 and int(size.group(2)) > 0,
            bool(flags) and int(flags.group(1), 16) & OVERLAY_IN_DASHBOARD != 0,
            None if vis is None else vis.group(1) == "visible")


def show(appid, seconds):
    key = f"valve.steam.desktopgame.{int(appid)}"
    start = time.monotonic()
    said = None

    def say(text):
        print(f"{time.monotonic() - start:.1f} s: {text}", flush=True)

    def read():
        """(known, showable, shown). Says the state, and SteamVR's own line for it, when it changed."""
        nonlocal said
        listing = vrcmd("--overlays")
        if listing is None or not re.search(r"^'", listing, re.M):
            return False, False, False
        line, layers = overlay_lines(listing, key)
        if line is None:
            text, res = f"SteamVR has no overlay {key}", (True, False, False)
        else:
            picture, dash, visible = overlay_state(line)
            text = (f"{'a picture' if picture else 'no picture'}, {'in' if dash else 'not in'} the dashboard, "
                    f"{'visible' if visible else 'not visible'}, {layers} layers visible [{line[:300]}]")
            # (a SteamVR that writes the line another way is not guessed at)
            res = (visible is not None, picture and dash, picture and bool(visible))
        if text != said:
            say(text)
            said = text
        return res

    if not os.access(vrcmd_path(), os.X_OK):
        say(f"cannot tell, no vrcmd at {vrcmd_path()}")
        return EXIT_CANNOT_TELL
    if not os.environ.get("MLXR_VRCMD"):
        # vrcmd is for a SteamVR that runs
        try:
            up = subprocess.run(["pgrep", "-x", "vrserver"], stdout=subprocess.DEVNULL, timeout=5).returncode == 0
        except (OSError, subprocess.TimeoutExpired):
            up = False
        if not up:
            say("cannot tell, SteamVR is not running")
            return EXIT_CANNOT_TELL

    known, showable, shown = read()
    unknown = 0 if known else 1
    while not shown and not showable and unknown < 2 and time.monotonic() - start < seconds:
        time.sleep(0.25)
        known, showable, shown = read()
        unknown = 0 if known else unknown + 1
    if not known:
        say("cannot tell, vrcmd --overlays gave nothing to read")
        return EXIT_CANNOT_TELL
    if shown:
        return 0
    # Without a picture and the flag the dashboard has no frame for the window, and a dock request moves whichever
    # frame is active (Steam's library): nothing is moved then
    if not showable:
        return EXIT_NO_PICTURE
    time.sleep(0.75)     # the dashboard makes the frame a moment after the overlay can show
    for n, docks in enumerate(SHOW_TRIES, 1):
        if time.monotonic() - start > SHOW_DEADLINE_S:
            say("out of time")
            break
        say(f"not shown, try {n} of {len(SHOW_TRIES)}: "
            + (f"docking it again ({', then '.join(docks)}) and " if docks else "") + "asking Steam for it")
        for where in docks:
            if vrcmd("--dock-overlay", where, key) is None:
                say(f"vrcmd --dock-overlay {where} failed")
        try:
            evaluate(f"SteamClient.OpenVR.VROverlay.ShowDashboard({json.dumps(key)})")
        except (CefError, OSError) as e:
            say(f"ShowDashboard failed: {e}")
        time.sleep(0.5)
        if read()[2]:
            return 0
    return EXIT_NOT_SHOWN


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
