#!/usr/bin/env python
"""Ask a running PPSSPP over its WebSocket debugger: screenshot and CPU state.

  python tools/psp_debug.py shot [file]   # the frame on screen, as PNG
  python tools/psp_debug.py regs          # registers of the running thread
"""
import base64, json, os, socket, struct, sys, time
from pathlib import Path
from PIL import Image
from psp_data_census import ROOT

PORT = 12345


def screenshot_image(reply):
    mode = {"B8G8R8A8_UNORM": "BGRA", "R8G8B8A8_UNORM": "RGBA"}[reply["format"]]
    image = Image.frombytes("RGBA", (reply["width"], reply["height"]), base64.b64decode(reply["base64"]), "raw", mode)
    if reply.get("flipped"):
        image = image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    return image.convert("RGB")


class Debugger:
    def __init__(self, port=None):
        self.sock = socket.create_connection(("127.0.0.1", PORT if port is None else port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET /debugger HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Protocol: debugger.ppsspp.org\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                self.sock.close()
                raise EOFError("PPSSPP closed the WebSocket handshake")
            head += chunk
        self.extra = head.split(b"\r\n\r\n", 1)[1]

    def read(self, n):
        while len(self.extra) < n:
            chunk = self.sock.recv(1 << 20)
            if not chunk:
                raise EOFError("PPSSPP closed the debugger connection")
            self.extra += chunk
        out, self.extra = self.extra[:n], self.extra[n:]
        return out

    def frame(self):
        text = b""
        while True:
            head = self.read(2)
            size = head[1] & 127
            if size == 126:
                size = struct.unpack(">H", self.read(2))[0]
            elif size == 127:
                size = struct.unpack(">Q", self.read(8))[0]
            text += self.read(size)
            if head[0] & 0x80:
                return text

    def ask(self, event, **args):
        data = json.dumps({"event": event, "ticket": event, **args}).encode()
        mask = os.urandom(4)
        head = bytes([0x81, 0xFE]) + struct.pack(">H", len(data)) if len(data) >= 126 else bytes([0x81, 0x80 | len(data)])
        self.sock.sendall(head + mask + bytes(c ^ mask[i & 3] for i, c in enumerate(data)))
        while True:
            reply = json.loads(self.frame())
            if reply.get("ticket") == event or reply.get("event") in (event, "error"):
                return reply


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else "shot"
    debug = Debugger()
    if what == "watch":  # watch address: stop the CPU on the next write there
        print(debug.ask("memory.breakpoint.add", address=int(sys.argv[2], 16), size=4, enabled=True, log=True, read=False, write=True, change=False))
        return
    if what == "press":  # press button [frames]: leaves the game running
        print(debug.ask("input.buttons.press", button=sys.argv[2], duration=int(sys.argv[3]) if len(sys.argv) > 3 else 4))
        return
    if what == "poke":  # poke address value: one word, the game keeps running
        print(debug.ask("memory.write_u32", address=int(sys.argv[2], 16), value=int(sys.argv[3], 0)))
        return
    debug.ask("cpu.stepping")
    time.sleep(0.3)
    if what == "mem":  # mem address count
        address, count = int(sys.argv[2], 16), int(sys.argv[3]) if len(sys.argv) > 3 else 16
        print(debug.ask('memory.read_u32', address=address)); print(' '.join(f"{debug.ask('memory.read_u32', address=address + i * 4).get('uint', debug.ask('memory.read_u32', address=address + i * 4).get('value', 0)):08x}" for i in range(count)))
        return
    if what == "dump":  # dump address size file: raw bytes, the CPU stays stopped (a frozen frame to parse)
        import base64
        address, size = int(sys.argv[2], 16), int(sys.argv[3], 0)
        data = b"".join(base64.b64decode(debug.ask("memory.read", address=a, size=min(65536, address + size - a))["base64"]) for a in range(address, address + size, 65536))
        Path(sys.argv[4]).write_bytes(data)
        print("dumped", len(data))
        return
    if what == "threads":
        for t in debug.ask("hle.thread.list").get("threads", []):
            print(t.get("name"), hex(t.get("pc", 0)), t.get("status"), t.get("waitType"), t.get("priority"))
        return
    if what == "shot":
        reply = debug.ask("gpu.buffer.screenshot", type="base64")
        if not reply.get("width"):  # Vulkan gives no display copy; the render target holds the same frame
            reply = debug.ask("gpu.buffer.renderColor", type="base64")
        if "base64" not in reply:
            sys.exit(str(reply)[:300])
        image = screenshot_image(reply)
        image.crop((60, 660, 760, 800)).save(ROOT / "build" / "crop.png")
        image.thumbnail((720, 480))
        image.save(sys.argv[2] if len(sys.argv) > 2 else ROOT / "build" / "shot.png")
        print("saved", reply.get("width"), reply.get("height"))
        debug.ask("cpu.resume")  # a shot mid-run must not leave the game paused
    else:
        reply = debug.ask("cpu.getAllRegs")
        for category in reply.get("categories", [reply])[:1]:
            print(" ".join(f"{n}={v}" for n, v in zip(category.get("registerNames", []), category.get("uintValues", []))))


if __name__ == "__main__":
    main()
