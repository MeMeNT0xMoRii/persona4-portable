"""Keep one observable USB host alive; local host commands include verbose/exit.

The PSP must be disconnected when replacing a host. This wrapper owns stdin so
the host can later exit normally instead of being terminated during a transfer.
"""
import argparse
import subprocess
import time
from pathlib import Path

from psp_data_census import ROOT


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--host', required=True, type=Path)
    ap.add_argument('--root', required=True, type=Path)
    args = ap.parse_args()
    session = ROOT / 'build/psplink_session'
    queue = session / 'host_commands.txt'
    queue.write_text('', encoding='utf-8')
    with (session / 'usbhost_stdout.log').open('ab', buffering=0) as output, \
         (session / 'usbhost_stderr.log').open('ab', buffering=0) as error:
        proc = subprocess.Popen([str(args.host), '-vv'], cwd=args.root, stdin=subprocess.PIPE,
                                stdout=output, stderr=error, creationflags=subprocess.CREATE_NO_WINDOW)
        (session / 'host.pid').write_text(str(proc.pid))
        print(f'USB host {proc.pid}, root {args.root}', flush=True)
        position = 0
        while proc.poll() is None:
            with queue.open(encoding='utf-8') as commands:
                commands.seek(position)
                pending = commands.read()
                position = commands.tell()
            if pending:
                proc.stdin.write(pending.encode())
                proc.stdin.flush()
            time.sleep(0.1)
        print(f'USB host exited {proc.returncode}', flush=True)


if __name__ == '__main__':
    main()
