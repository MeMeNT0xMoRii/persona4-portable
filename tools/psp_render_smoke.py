#!/usr/bin/env python3
"""Compile the port's 2D draw helper into a PSP test and check its pixels in PPSSPP."""

import argparse
import base64
import os
import socket
import subprocess
import tempfile
import time
from pathlib import Path

from psp_debug import Debugger


ROOT = Path(__file__).resolve().parent.parent
RED, GREEN, BLUE = (255, 0, 0), (0, 255, 0), (0, 0, 255)
PIXELS = {
    (120, 70): GREEN,  # The baseline paints the command panel's green.
    (350, 70): RED,    # The depth-only draws preserve the existing red.
    (310, 180): RED,
    (410, 180): RED,
    (100, 92): GREEN,
    (350, 92): RED,    # The blue stripe lies behind the depth-only draw.
    (255, 207): BLUE,  # The stripe remains visible outside the masks.
    (310, 207): RED,
    (410, 207): RED,
}


def build_probe(pspdev, work):
    renderer = (ROOT / "port" / "rwsky.c").read_text(encoding="utf-8")
    start = renderer.index("static const int gs_alpha_method")
    end = renderer.index("\n}\n", renderer.index("static void draw_2d_tested", start)) + len("\n}\n")
    (work / "p4_draw_2d_tested.h").write_text(renderer[start:end], encoding="utf-8")
    sdk = pspdev / "psp" / "sdk"

    def run(program, *args):
        executable = pspdev / "bin" / program
        subprocess.run([executable, *args], cwd=work, check=True, stdout=subprocess.PIPE)

    run("psp-gcc", "-G0", "-O2", f"-I{sdk / 'include'}", f"-L{sdk / 'lib'}",
        f"-I{work}", f"-specs={sdk / 'lib' / 'prxspecs'}",
        f"-Wl,-q,-T{sdk / 'lib' / 'linkfile.prx'}", ROOT / "tools" / "psp_render_smoke.c",
        sdk / "lib" / "prxexports.o", "-lpspgu", "-lpspge", "-lpspdisplay", "-o", "test.elf")
    run("psp-fixup-imports", "test.elf")
    run("psp-prxgen", "test.elf", "test.prx")
    run("mksfoex", "P4 depth mask test", "param.sfo")
    run("pack-pbp", "EBOOT.PBP", "param.sfo", "NULL", "NULL", "NULL", "NULL", "NULL",
        "test.prx", "NULL")


def read_pixels(debugger):
    values = {}
    for x, y in PIXELS:
        address = 0x04000000 + (512 * y + x) * 4
        reply = debugger.ask("memory.read", address=address, size=4)
        if "base64" in reply:
            values[x, y] = tuple(base64.b64decode(reply["base64"])[:3])
    return values


def check_probe(ppsspp, work):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        port = listener.getsockname()[1]
    config = work / "config"
    system = config / "ppsspp" / "PSP" / "SYSTEM"
    system.mkdir(parents=True)
    (system / "ppsspp.ini").write_text(
        "[General]\nRemoteDebuggerOnStartup = True\nRemoteDebuggerLocal = True\n"
        f"RemoteISOPort = {port}\n", encoding="utf-8")
    env = os.environ | {"XDG_CONFIG_HOME": str(config), "SDL_VIDEODRIVER": "offscreen",
                        "SDL_AUDIODRIVER": "dummy"}
    with (work / "ppsspp.log").open("w") as log:
        emulator = subprocess.Popen([ppsspp, "--graphics=opengl", "--software-rendering",
                                     work / "EBOOT.PBP"],
                                    cwd=work, env=env, stdout=log, stderr=subprocess.STDOUT)
        debugger = None
        values = {}
        try:
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline and emulator.poll() is None:
                try:
                    if debugger is None:
                        debugger = Debugger(port=port)
                    values = read_pixels(debugger)
                    if values == PIXELS:
                        print("PPSSPP depth masks passed: green/black/white leave RGB intact and clip by depth.")
                        return
                except (OSError, EOFError):
                    if debugger is not None:
                        debugger.sock.close()
                        debugger = None
                time.sleep(0.2)
            log_tail = (work / "ppsspp.log").read_text(errors="replace").splitlines()[-8:]
            raise RuntimeError(f"Expected {PIXELS}, read {values}.\n" + "\n".join(log_tail))
        finally:
            if debugger is not None:
                debugger.sock.close()
            if emulator.poll() is None:
                emulator.terminate()
                try:
                    emulator.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    emulator.kill()
                    emulator.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ppsspp", type=Path, help="path to the Linux PPSSPP executable or AppRun")
    args = parser.parse_args()
    pspdev = os.environ.get("PSPDEV")
    if not pspdev:
        parser.error("set PSPDEV to the native Linux PSPSDK toolchain")
    with tempfile.TemporaryDirectory(prefix="p4-render-smoke-") as temp:
        work = Path(temp)
        build_probe(Path(pspdev).resolve(), work)
        check_probe(args.ppsspp.resolve(), work)


if __name__ == "__main__":
    main()
