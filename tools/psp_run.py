#!/usr/bin/env python
"""Deploy build/psp_dist to PPSSPP's memory stick, run it, print p4.log and the end of PPSSPP's own log.

  python tools/psp_run.py [seconds]
"""
import bisect, os, re, shutil, subprocess, sys, time
from pathlib import Path
from psp_data_census import ROOT

PSPDEV = Path(os.environ["PSPDEV"])

EMU = Path(os.environ["PPSSPP_DIR"])  # PPSSPP folder that holds memstick/
GAME = EMU / "memstick" / "PSP" / "GAME" / "P4"
DIST = ROOT / "build" / "psp_dist" / "PSP" / "GAME" / "P4"
EMU_LOG = ROOT / "build" / "ppsspp.log"
END = ("kwlnBoot returned", "too many missing")


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    GAME.mkdir(parents=True, exist_ok=True)
    for f in DIST.iterdir():
        target = GAME / f.name
        if f.is_dir():  # SOUND, VIDEO: copied once
            if not target.exists():
                shutil.copytree(f, target)
            continue
        if not target.exists() or target.stat().st_size != f.stat().st_size or target.stat().st_mtime < f.stat().st_mtime:
            shutil.copy2(f, GAME)
    route = GAME / "route.txt"  # P4_ROUTE=tools/routes/x.txt: the game presses its buttons by the PSP clock (pad.c)
    route.unlink(missing_ok=True)
    if os.environ.get("P4_ROUTE"):
        shutil.copy2(os.environ["P4_ROUTE"], route)
    log = GAME / "p4.log"
    log.unlink(missing_ok=True)
    EMU_LOG.unlink(missing_ok=True)
    emu = subprocess.Popen([str(EMU / "PPSSPPWindows64.exe"), f"--log={EMU_LOG}", str(GAME / "EBOOT.PBP")])
    start = time.time()
    presses = sorted((float(t), b) for t, b in (p.split(":") for p in os.environ.get("P4_PRESS", "").split()))  # "16:start 20:cross"
    while time.time() - start < limit and emu.poll() is None:
        time.sleep(0.5)
        while presses and time.time() - start >= presses[0][0]:
            when, button = presses.pop(0)
            if button == "shot":  # "70:shot" keeps build/shot_70.png
                subprocess.run([sys.executable, str(ROOT / "tools" / "psp_debug.py"), "shot"], timeout=15)
                shutil.copy2(ROOT / "build" / "shot.png", ROOT / "build" / f"shot_{when:g}.png")
                continue
            command = ["watch", button[1:]] if button.startswith("@") else ["press", button]  # "40:@8873fc" arms a write watch
            if "=" in button:  # "45:8b3a54c=0" writes a word
                command = ["poke", *button.split("=")]
            subprocess.run([sys.executable, str(ROOT / "tools" / "psp_debug.py"), *command], timeout=15)
        if log.exists() and any(e in log.read_text(errors="replace") for e in END):
            time.sleep(1)
            break
    if emu.poll() is None:
        subprocess.run([sys.executable, str(ROOT / "tools" / "psp_debug.py"), "shot"], timeout=15)
        subprocess.run([sys.executable, str(ROOT / "tools" / "psp_debug.py"), "threads"], timeout=15)
        subprocess.run([sys.executable, str(ROOT / "tools" / "psp_debug.py"), "regs"], timeout=15)
    if len(sys.argv) > 2:  # psp_run.py seconds address count: memory to dump before closing
        subprocess.run([sys.executable, str(ROOT / "tools" / "psp_debug.py"), "mem", *sys.argv[2:]], timeout=30)
    emu.kill()
    route.unlink(missing_ok=True)  # or a later manual run presses the route on top of the player
    print(f"--- p4.log after {time.time() - start:.0f}s")
    print(log.read_text(errors="replace") if log.exists() else "NO LOG")
    if EMU_LOG.exists():
        lines = EMU_LOG.read_text(errors="replace").splitlines()
        hot = [l[:160] for l in lines if any(w in l for w in ("Invalid", "invalid", "Bad", "Crash", "crash", "Exception", "Unimpl", "SIGSEGV", "z_un_", "Host:"))]
        print(f"--- ppsspp.log {len(lines)} lines, {len(hot)} flagged")
        print("\n".join(hot[:14]))
        code = sorted(set(re.findall(r"\b(?:RA|PC|pc:?) ?(0[89][0-9a-f]{6})\b", "\n".join(hot)) + re.findall(r"from (8[0-9a-f]{6})\b", log.read_text(errors="replace") if log.exists() else "")))
        if code:  # name the code addresses PPSSPP reported
            nm = subprocess.run([str(PSPDEV / "bin" / "psp-nm.exe"), "-n", str(ROOT / "build" / "psp" / "p4.elf")],
                                capture_output=True, text=True).stdout.splitlines()
            table = [(int(p[0], 16), p[2]) for p in (l.split() for l in nm) if len(p) == 3 and p[1] in "Tt"]
            for a in code:
                i = bisect.bisect_right(table, (int(a, 16), "~")) - 1
                print(f"{a} = {table[i][1]}+{int(a, 16) - table[i][0]:#x}")


if __name__ == "__main__":
    main()
