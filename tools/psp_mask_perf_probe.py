#!/usr/bin/env python3
"""Run home20, home21 and current mask code in one PSP fixture."""
import argparse
import os
from pathlib import Path

from psp_effect_probe import ROOT, build_program, extract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--home20', type=Path, required=True, help='home20 port/rwsky.c')
    parser.add_argument('--home21', type=Path, required=True, help='home21 port/rwsky.c')
    args = parser.parse_args()
    pspdev = os.environ.get('PSPDEV')
    if not pspdev:
        parser.error('set PSPDEV to a native PSPSDK toolchain')
    functions = []
    renderers = (args.home20, args.home21, ROOT / 'port/rwsky.c')
    for suffix, path in zip(('20', '21', 'current'), renderers):
        source = path.read_text()
        names = ('mask_diagonal_side', 'effect_mask_quad', 'effect_triangle_alpha', 'draw_effect_mask')
        for name in names:
            match = extract(source, name)
            if match:
                code = match[0]
                for symbol in names:
                    code = code.replace(symbol + '(', symbol + '_' + suffix + '(')
                functions.append(code)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / 'mask_arms.h').write_text('\n\n'.join(functions) + '\n')
    build_program(Path(pspdev).resolve(), output, ROOT / 'tools/psp_mask_perf_probe.c', 'P4 Mask Timing')


if __name__ == '__main__':
    main()
