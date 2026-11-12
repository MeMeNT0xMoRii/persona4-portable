"""Shared XMB metadata and artwork for both PSP packaging paths."""
from pathlib import Path


TITLE = 'Persona4 PORTABLE'
ARTWORK = ('assets/xmb/ICON0.png', 'assets/xmb/PIC0.png', 'assets/xmb/PIC1.png')


def pack_eboot(root, out, binpath, run, executable, *, title=TITLE):
    """Embed the original PNG bytes in their named PBP sections."""
    icon, info, background = (Path(root) / name for name in ARTWORK)
    for path in (icon, info, background):
        if not path.is_file():
            raise FileNotFoundError(f'Missing XMB artwork: {path}')
    run(binpath / 'mksfoex.exe', '-d', 'MEMSIZE=1', title, out / 'PARAM.SFO')
    # PARAM.SFO, ICON0, ICON1, PIC0, PIC1, SND0, DATA.PSP, DATA.PSAR.
    run(binpath / 'pack-pbp.exe', out / 'EBOOT.PBP', out / 'PARAM.SFO',
        icon, 'NULL', info, background, 'NULL', executable, 'NULL')
