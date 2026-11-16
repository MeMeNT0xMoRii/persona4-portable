#!/usr/bin/env python3
"""Build a PSP fixture for particle depth and the existing effect alpha mask."""

import argparse
import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    return re.search(rf"^static (?:void|int) {name}\([^\n]*\)\n\{{.*?^\}}", source, re.M | re.S)


def build(pspdev, output, renderer):
    source = renderer.read_text()
    names = ("blend_factor", "set_blend", "camera_projection", "mask_diagonal_side", "effect_mask_quad",
             "draw_effect_mask")
    functions = [match[0] for name in names if (match := extract(source, name))]
    submit = extract(source, "draw_ptank_quads")
    if submit:
        functions.append(submit[0])
    else:
        # Home19 submitted the PTank vertices directly in draw_ptank_body.
        body = extract(source, "draw_ptank_body")[0]
        draw = re.search(r"^    sceGuDrawArray\(GU_TRIANGLES, .*emitted, 0, vertices\);$", body, re.M)[0]
        functions.append("static void draw_ptank_quads(const char *ps2, int emitted, const Vertex3D *vertices)\n"
                         "{\n    (void)ps2;\n" + draw + "\n}")
    (output / "effect_helpers.h").write_text("\n\n".join(functions) + "\n")
    sdk = pspdev / "psp/sdk"

    def run(program, *args):
        subprocess.run([pspdev / "bin" / program, *args], cwd=output, check=True)

    run("psp-gcc", "-G0", "-O2", "-Wall", "-Wextra", f"-I{sdk / 'include'}", f"-L{sdk / 'lib'}",
        f"-I{output}", f"-specs={sdk / 'lib/prxspecs'}", f"-Wl,-q,-T{sdk / 'lib/linkfile.prx'}",
        ROOT / "tools/psp_effect_probe.c", sdk / "lib/prxexports.o", "-lpspgu", "-lpspge", "-lpspdisplay",
        "-o", "probe.elf")
    run("psp-fixup-imports", "probe.elf")
    run("psp-prxgen", "probe.elf", "probe.prx")
    run("mksfoex", "P4 Effect Probe", "param.sfo")
    run("pack-pbp", "EBOOT.PBP", "param.sfo", "NULL", "NULL", "NULL", "NULL", "NULL", "probe.prx", "NULL")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--renderer", type=Path, default=ROOT / "port/rwsky.c",
                        help="renderer source to extract (defaults to the current checkout)")
    args = parser.parse_args()
    pspdev = os.environ.get("PSPDEV")
    if not pspdev:
        parser.error("set PSPDEV to the native PSPSDK toolchain")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build(Path(pspdev).resolve(), output, args.renderer)


if __name__ == "__main__":
    main()
