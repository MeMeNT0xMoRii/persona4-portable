#!/usr/bin/env python3
"""Build synthetic PSP fixtures for effects and renderer A/B checks."""

import argparse
import os
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def extract(source, name):
    return re.search(rf"^static (?:void|int) {name}\([^\n]*\)\n\{{.*?^\}}", source, re.M | re.S)


def build(pspdev, output, renderer, depth_only=False):
    source = renderer.read_text()
    names = ("blend_factor", "set_blend", "camera_projection", "mask_diagonal_side", "effect_mask_quad", "effect_triangle_alpha",
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
    if extract(source, "draw_ptank_split"):
        functions.append("#define P4_PROBE_PTANK_SPLIT 1")
        functions.append(re.search(r"^enum \{ PTANK_ALPHA_REF = .*?^typedef struct .* PtankDrawRun;$",
                                   source, re.M | re.S)[0])
        functions.extend(extract(source, name)[0] for name in ("ptank_depth_mode", "draw_ptank_split"))
    (output / "effect_helpers.h").write_text("\n\n".join(functions) + "\n")
    defines = ("P4_PROBE_DEPTH_ONLY=1",) if depth_only else ()
    build_program(pspdev, output, ROOT / "tools/psp_effect_probe.c", "P4 Effect Probe", defines)


def build_program(pspdev, output, probe, title, defines=(), *, cflags=(), libraries=()):
    sdk = pspdev / "psp/sdk"

    def run(program, *args):
        subprocess.run([pspdev / "bin" / program, *args], cwd=output, check=True)

    run("psp-gcc", "-G0", "-O2", "-Wall", "-Wextra", *(f"-D{name}" for name in defines),
        f"-I{sdk / 'include'}", f"-L{sdk / 'lib'}",
        f"-I{output}", f"-specs={sdk / 'lib/prxspecs'}", f"-Wl,-q,-T{sdk / 'lib/linkfile.prx'}",
        *cflags,
        probe, sdk / "lib/prxexports.o", "-lpspgu", "-lpspge", "-lpspdisplay", "-lpsppower",
        *libraries, "-o", "probe.elf")
    run("psp-fixup-imports", "probe.elf")
    run("psp-prxgen", "probe.elf", "probe.prx")
    run("mksfoex", title, "param.sfo")
    run("pack-pbp", "EBOOT.PBP", "param.sfo", "NULL", "NULL", "NULL", "NULL", "NULL", "probe.prx", "NULL")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--renderer", type=Path, default=ROOT / "port/rwsky.c",
                        help="renderer source to extract (defaults to the current checkout)")
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--depth-only", action="store_true", help="read back the depth fixture in the first display list")
    modes.add_argument("--renderer-perf", action="store_true",
                       help="build the renderer command, framebuffer and CPU submission probe")
    args = parser.parse_args()
    pspdev = os.environ.get("PSPDEV")
    if not pspdev:
        parser.error("set PSPDEV to the native PSPSDK toolchain")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sdk = Path(pspdev).resolve()
    if args.renderer_perf:
        build_program(sdk, output, ROOT / "tools/psp_renderer_probe.c", "P4 Renderer Probe",
                      (f'P4_PROBE_RENDERER="{args.renderer.resolve().as_posix()}"',),
                      cflags=("-ffunction-sections", "-fdata-sections", "-D_PSP_FW_VERSION=600",
                              "-Wl,--gc-sections,--wrap=sceGuGetMemory,--wrap=sceGeListUpdateStallAddr"),
                      libraries=("-lpspctrl", "-lpspgum", "-lm"))
    else:
        build(sdk, output, args.renderer, args.depth_only)


if __name__ == "__main__":
    main()
