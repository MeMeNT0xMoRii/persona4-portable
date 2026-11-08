#!/usr/bin/env python
"""Persistent pspsh, following pspdbg/analysis/psplink_bridge.py.

Append commands to build/psplink_session/commands.txt; console.log captures
responses, stdout and exception reports. Keep this process running during play.
"""
import argparse
import os
import subprocess
import time
from pathlib import Path
from psp_data_census import ROOT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pspsh", required=True, type=Path)
    args = parser.parse_args()
    session = Path(os.environ.get("P4_SESSION", ROOT / "build" / "psplink_session"))  # local disk: the share stalls pspsh
    session.mkdir(parents=True, exist_ok=True)
    queue = session / "commands.txt"
    queue.write_text("", encoding="utf-8")
    with (session / "console.log").open("ab", buffering=0) as output:
        proc = subprocess.Popen([str(args.pspsh)], stdin=subprocess.PIPE, stdout=output,
                                stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
        (session / "shell.pid").write_text(str(proc.pid))
        print(f"PSPSH {proc.pid}: {session}", flush=True)
        pos = 0
        while proc.poll() is None:
            try:
                with queue.open(encoding="utf-8") as commands:
                    commands.seek(pos)
                    pending = commands.read()
                    pos = commands.tell()
            except PermissionError:
                time.sleep(0.1)
                continue
            for line in pending.splitlines(keepends=True):
                proc.stdin.write(line.encode("utf-8"))
                proc.stdin.flush()
            time.sleep(0.1)
        output.write(f"\n[pspsh exited {proc.returncode}]\n".encode())


if __name__ == "__main__":
    main()
