#!/usr/bin/env python3
"""A fake Steam CEF debugging endpoint for the tests of packaging/steam_cef.py. Standard library only.

It serves what steam_cef.py asks: GET /json (one SharedJSContext target) and a websocket on that
target that answers Runtime.evaluate. It does not run JavaScript: a rule is a substring of the
expression and the value to answer; the first rule that matches wins, no match answers undefined.
Every expression received is kept in `.expressions`.

    with FakeCef([("StreamGame", True)]) as cef:
        os.environ["STEAM_CEF_PORT"] = str(cef.port)

    fake_cef.py PORT RULES.json [LOG]    standalone: serve until killed, append expressions to LOG

Moonlight XR Native. GPLv3.
"""
import base64, hashlib, json, socketserver, struct, sys, threading
from http.server import BaseHTTPRequestHandler, HTTPServer

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def _exact(rfile, n):
    buf = b""
    while len(buf) < n:
        chunk = rfile.read(n - len(buf))
        if not chunk:
            raise ConnectionError("closed")
        buf += chunk
    return buf


def read_frame(rfile):
    b0, b1 = _exact(rfile, 2)
    n = b1 & 0x7F
    if n == 126:
        n = struct.unpack(">H", _exact(rfile, 2))[0]
    elif n == 127:
        n = struct.unpack(">Q", _exact(rfile, 8))[0]
    mask = _exact(rfile, 4) if b1 & 0x80 else None
    data = _exact(rfile, n)
    if mask:
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    return b0 & 0x0F, data


def frame(text):
    data = text.encode()
    n = len(data)
    if n < 126:
        hdr = bytes([0x81, n])
    elif n < 65536:
        hdr = bytes([0x81, 126]) + struct.pack(">H", n)
    else:
        hdr = bytes([0x81, 127]) + struct.pack(">Q", n)
    return hdr + data


class FakeCef:
    def __init__(self, rules=(), title="SharedJSContext", port=0, log=None):
        self.rules = list(rules)          # (substring, value | callable(expression) -> value | Exception)
        self.expressions = []
        self.title = title
        self.log = log
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_GET(self):
                if self.path == "/json":
                    body = json.dumps([{
                        "title": owner.title, "type": "page",
                        "webSocketDebuggerUrl": f"ws://127.0.0.1:{owner.port}/devtools/page/SHARED",
                    }]).encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json")
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)
                    return
                if self.path.startswith("/devtools/page/") and self.headers.get("Upgrade", "").lower() == "websocket":
                    accept = base64.b64encode(hashlib.sha1((self.headers["Sec-WebSocket-Key"] + GUID).encode()).digest()).decode()
                    self.wfile.write(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                                      f"Connection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n").encode())
                    self.wfile.flush()
                    owner._serve_ws(self.rfile, self.wfile)
                    self.close_connection = True
                    return
                self.send_error(404)

        class Server(socketserver.ThreadingMixIn, HTTPServer):
            daemon_threads = True
            allow_reuse_address = True

        self.httpd = Server(("127.0.0.1", port), Handler)
        self.port = self.httpd.server_address[1]
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)

    def _answer(self, expression):
        for needle, value in self.rules:
            if needle in expression:
                return value(expression) if callable(value) else value
        return None

    def _serve_ws(self, rfile, wfile):
        try:
            while True:
                op, data = read_frame(rfile)
                if op == 8:
                    return
                if op != 1:
                    continue
                msg = json.loads(data.decode())
                expr = msg.get("params", {}).get("expression", "")
                self.expressions.append(expr)
                if self.log:
                    with open(self.log, "a") as f:
                        f.write(expr + "\n")
                value = self._answer(expr)
                if isinstance(value, Exception):
                    result = {"exceptionDetails": {"text": "Uncaught", "exception": {"description": str(value)}}}
                else:
                    result = {"result": {"type": "object", "value": value}}
                wfile.write(frame(json.dumps({"id": msg["id"], "result": result})))
                wfile.flush()
        except (ConnectionError, OSError, ValueError):
            return

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.httpd.shutdown()
        self.httpd.server_close()


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    cef = FakeCef([(k, v) for k, v in json.load(open(sys.argv[2]))], port=int(sys.argv[1]),
                  log=sys.argv[3] if len(sys.argv) > 3 else None)
    with cef:
        print(f"fake CEF on {cef.port}", flush=True)
        threading.Event().wait()
