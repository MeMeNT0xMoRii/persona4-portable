"""Run the P4 release EBOOT in the local portable PPSSPP and drive it through PPSSPP's JSON debugger (WebSocket).

  python tools/ppsspp_p4.py start                 # PPSSPP (software GE, hidden window, debugger on 127.0.0.1:24892)
  python tools/ppsspp_p4.py press start           # a press: button names joined by '+', e.g. ltrigger+rtrigger+start
  python tools/ppsspp_p4.py press down --hold 0.2 --times 3
  python tools/ppsspp_p4.py shot                  # the shown framebuffer -> C:/temp/p3f-ppsspp/shot.png
  python tools/ppsspp_p4.py scene day 4 12 0      # a scene request (port/scene_menu.c p4_scene_req), as psp_scene.py
  python tools/ppsspp_p4.py log                   # new lines of ms0:/PSP/GAME/P4/p4.log since the last call
  python tools/ppsspp_p4.py close

The game folder is PPSSPP's memstick/PSP/GAME/P4 (game data from build/psp_dist, the release files, scenes.txt,
days.txt, saves). Symbols come from p4_b<N>.elf next to the EBOOT (the release ELF, loaded at 0x08804000).
Debugger client after the nsmbpsp project's tools/ppsspp_check.py (PPSSPP v1.20.4 WebSocket API).
"""
import argparse
import base64
import hashlib
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(os.environ.get('P4_PPSSPP') or Path(os.environ.get('PPSSPP_DIR', '.')) / 'PPSSPPWindows64.exe')
GAME = EXE.parent / 'memstick/PSP/GAME/P4'
STATE = Path(os.environ.get('P4_PPSSPP_STATE', 'C:/temp/p3f-ppsspp'))  # local: PPSSPP ignored a config on the share
PORT = 24892
BASE = 0x08804000
KINDS = {'field': 1, 'floor': 2, 'event': 3, 'battle': 4, 'menu': 5, 'day': 6}
MONTH_DAYS = (30, 31, 30, 31, 31, 30, 31, 30, 31, 31, 28, 31)


class Debugger:
    def __init__(self):
        self.socket = socket.create_connection(('127.0.0.1', PORT), timeout=30)
        key = base64.b64encode(os.urandom(16)).decode()
        self.socket.sendall((f'GET /debugger HTTP/1.1\r\nHost: 127.0.0.1:{PORT}\r\nUpgrade: websocket\r\n'
                             f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n'
                             'Sec-WebSocket-Protocol: debugger.ppsspp.org\r\n\r\n').encode())
        response = b''
        while not response.endswith(b'\r\n\r\n'):
            response += self.read(1)
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        if b' 101 ' not in response or accept not in response:
            raise RuntimeError(f'websocket handshake failed: {response!r}')
        self.ticket = 0

    def read(self, size):
        data = bytearray()
        while len(data) < size:
            chunk = self.socket.recv(size - len(data))
            if not chunk:
                raise EOFError('PPSSPP closed the debugger connection')
            data.extend(chunk)
        return bytes(data)

    def send(self, data, opcode=1):
        mask = os.urandom(4)
        size = len(data)
        header = bytes((0x80 | opcode, 0x80 | (size if size < 126 else 126 if size < 65536 else 127)))
        if size >= 126:
            header += struct.pack('!H' if size < 65536 else '!Q', size)
        self.socket.sendall(header + mask + bytes(v ^ mask[i % 4] for i, v in enumerate(data)))

    def receive(self):
        message = bytearray()
        while True:
            flags, length = self.read(2)
            size = length & 127
            if size in (126, 127):
                size = struct.unpack('!H' if size == 126 else '!Q', self.read(2 if size == 126 else 8))[0]
            mask = self.read(4) if length & 128 else None
            data = self.read(size)
            if mask:
                data = bytes(v ^ mask[i % 4] for i, v in enumerate(data))
            opcode = flags & 15
            if opcode == 8:
                raise EOFError('PPSSPP closed the debugger')
            if opcode == 9:
                self.send(data, 10)
                continue
            if opcode in (0, 1):
                message.extend(data)
                if flags & 128:
                    return json.loads(message)

    def request(self, event, **params):
        self.ticket += 1
        self.send(json.dumps(dict(event=event, ticket=self.ticket, **params)).encode())
        while True:
            response = self.receive()
            if response.get('ticket') == self.ticket:
                if response.get('event') == 'error':
                    raise RuntimeError(response)
                return response

    def close(self):
        try:
            self.socket.settimeout(2)
            self.send(struct.pack('!H', 1000), 8)
            while True:
                self.receive()
        except (OSError, EOFError):
            pass
        finally:
            self.socket.close()


def symbols():
    elves = sorted(GAME.glob('p4_b*.elf'), key=lambda p: int(re.search(r'b(\d+)', p.name)[1]))
    nm = Path(os.environ['PSPDEV']) / 'bin/psp-nm.exe'
    out = subprocess.check_output([str(nm), str(elves[-1])], text=True)
    return {p[2]: int(p[0], 16) + BASE for p in (line.split() for line in out.splitlines()) if len(p) == 3}


def start():
    with socket.socket() as probe:
        if probe.connect_ex(('127.0.0.1', PORT)) == 0:
            raise SystemExit('the debugger port is in use: PPSSPP already runs (close it first)')
    STATE.mkdir(parents=True, exist_ok=True)
    config, controls = STATE / 'ppsspp.ini', STATE / 'controls.ini'
    controls.write_text('[ControlMapping]\n', encoding='utf-8')  # only debugger buttons reach the game
    config.write_text(f"""[General]
FirstRun = False
RemoteDebuggerOnStartup = True
RemoteDebuggerLocal = True
RemoteISOPort = {PORT}
PauseOnLostFocus = False
PauseWhenMinimized = False
CheckForNewVersion = False
[CPU]
CPUCore = 1
[Graphics]
SoftwareRenderer = True
RenderingMode = 1
InternalResolution = 1
FrameSkip = 0
[SystemParam]
PSPModel = 1
[Sound]
Enable = False
""", encoding='utf-8')
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    process = subprocess.Popen([str(EXE), '--windowed', '--graphics=software', f'--config={config}',
                                f'--controlconfig={controls}', f'--log={STATE / "emulator.log"}', str(GAME / 'EBOOT.PBP')],
                               cwd=EXE.parent, startupinfo=startup)
    (STATE / 'process.json').write_text(json.dumps(dict(pid=process.pid)), encoding='utf-8')
    deadline = time.monotonic() + 40
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SystemExit(f'PPSSPP exited: {process.returncode}')
        with socket.socket() as probe:
            if probe.connect_ex(('127.0.0.1', PORT)) == 0:
                print(f'PPSSPP started, pid {process.pid}, debugger 127.0.0.1:{PORT}')
                return
        time.sleep(0.2)
    raise SystemExit('the debugger did not start: see C:/temp/p3f-ppsspp/emulator.log')


def shot(debugger, target):
    from PIL import Image
    # the port's two 8888 buffers, VRAM 0 and 512*272*4 (rwsky.c), one above the other: one is the shown frame
    raw = base64.b64decode(debugger.request('memory.read', address=0x04000000, size=512 * 272 * 4 * 2)['base64'])
    frames = [Image.frombytes('RGBA', (512, 272), raw[i * 512 * 272 * 4:(i + 1) * 512 * 272 * 4]).crop((0, 0, 480, 272))
              for i in range(2)]
    sheet = Image.new('RGB', (480, 544))
    for i, frame in enumerate(frames):
        sheet.paste(frame.convert('RGB'), (0, 272 * i))
    sheet.save(target)
    return target


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('action', choices=('start', 'press', 'shot', 'scene', 'log', 'close', 'status'))
    ap.add_argument('args', nargs='*')
    ap.add_argument('--hold', type=float, default=0.15)
    ap.add_argument('--gap', type=float, default=0.35)
    ap.add_argument('--times', type=int, default=1)
    ap.add_argument('--out', type=Path, default=STATE / 'shot.png')
    args = ap.parse_args()
    if args.action == 'start':
        return start()
    if args.action == 'log':
        log = GAME / 'p4.log'
        mark = STATE / 'log.offset'
        offset = int(mark.read_text()) if mark.is_file() else 0
        data = log.read_bytes() if log.is_file() else b''
        if offset > len(data):
            offset = 0
        sys.stdout.write(data[offset:].decode('utf-8', 'replace'))
        mark.write_text(str(len(data)))
        return
    if args.action == 'close':
        import ctypes
        pid = json.loads((STATE / 'process.json').read_text())['pid']
        callback = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

        def close_window(hwnd, _):
            owner = ctypes.c_ulong()
            ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            if owner.value == pid:
                ctypes.windll.user32.PostMessageW(hwnd, 0x10, 0, 0)
            return True
        ctypes.windll.user32.EnumWindows(callback(close_window), 0)
        print(f'sent WM_CLOSE to PPSSPP {pid}')
        return
    debugger = Debugger()
    try:
        if args.action == 'status':
            for event in ('version', 'game.status', 'cpu.status'):
                print(json.dumps(debugger.request(event)))
        elif args.action == 'press':
            for _ in range(args.times):
                for combo in args.args:
                    names = combo.split('+')
                    debugger.request('input.buttons.send', buttons={n: True for n in names})
                    time.sleep(args.hold)
                    debugger.request('input.buttons.send', buttons={n: False for n in names})
                    time.sleep(args.gap)
        elif args.action == 'shot':
            print(shot(debugger, args.out))
        elif args.action == 'scene':
            kind, numbers = args.args[0], [int(v, 0) for v in args.args[1:]]
            if kind == 'day':
                month, day = numbers[0], numbers[1]
                order = month - 4 if month >= 4 else month + 8
                numbers = [sum(MONTH_DAYS[:order]) + day - 1, numbers[2] if len(numbers) > 2 else 0]
            if kind == 'floor' and len(numbers) == 1:
                numbers.append(0xFFFF)
            numbers += [0] * (4 - len(numbers))
            req = symbols()['p4_scene_req']
            payload = struct.pack('<4i', *numbers)
            debugger.request('memory.write', address=req + 4, base64=base64.b64encode(payload).decode())
            debugger.request('memory.write', address=req, base64=base64.b64encode(struct.pack('<i', KINDS[kind])).decode())
            print(f'sent {kind} {numbers}')
    finally:
        debugger.close()


if __name__ == '__main__':
    main()
