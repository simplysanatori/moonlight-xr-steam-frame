#!/usr/bin/env python3
"""Evaluate JavaScript in the Steam client's SharedJSContext over the CEF
remote-debugging port (127.0.0.1:8080), using only the standard library.

    steam_cef.py 'expression'          print the awaited result as JSON

Moonlight XR Native. GPLv3.
"""
import base64, json, os, socket, struct, sys, urllib.request

PORT = int(os.environ.get("STEAM_CEF_PORT", "8080"))


def target_ws():
    with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=5) as r:
        for t in json.load(r):
            if t.get("title") == "SharedJSContext":
                return t["webSocketDebuggerUrl"]
    sys.exit("SharedJSContext not found: is Steam running with CEF remote debugging on?")


class WS:
    def __init__(self, url):
        rest = url.split("://", 1)[1]
        hostport, path = rest.split("/", 1)
        host, port = hostport.split(":")
        self.s = socket.create_connection((host, int(port)), timeout=20)
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
    ws.send(json.dumps({"id": 1, "method": "Runtime.evaluate",
                        "params": {"expression": expr, "awaitPromise": True, "returnByValue": True}}))
    while True:
        m = json.loads(ws.recv())
        if m.get("id") == 1:
            res = m.get("result", {})
            if "exceptionDetails" in res:
                sys.exit("JS exception: " + json.dumps(res["exceptionDetails"])[:800])
            return res.get("result", {}).get("value")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    try:
        print(json.dumps(evaluate(sys.argv[1])))
    except OSError as e:   # URLError, ConnectionError and socket errors
        sys.exit(f"cannot reach Steam's CEF port {PORT}: {e}")
