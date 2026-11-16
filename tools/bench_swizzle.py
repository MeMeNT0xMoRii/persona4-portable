#!/usr/bin/env python3
"""Time the port's native texture decode and GE swizzle kernels on a host CPU."""

import re
import statistics
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "swizzle.h"

enum { GIF_HEADER = 0x50, TRXREG_RRW = 0x20 };
typedef struct { int width, height, depth, stride; unsigned char *originalPixels; } Raster;

COPY_LEVEL

static uint64_t cpu_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static unsigned char blob[GIF_HEADER + 512 * 256 * 4] __attribute__((aligned(64)));
static unsigned char linear[512 * 256 * 4], swizzled[sizeof linear];

static void measure(int w, int h, int depth)
{
    enum { REPEATS = 128, SAMPLES = 9 };
    int upload_width = w / 2;
    memcpy(blob + TRXREG_RRW, &upload_width, sizeof upload_width);
    Raster r = { w, h, depth, w * depth / 8, linear };
    for (int run = -1; run < SAMPLES; run++) {
        uint64_t start = cpu_ns();
        for (int i = 0; i < REPEATS; i++) {
            copy_level(&r, blob);
            __asm__ volatile("" ::: "memory");
        }
        uint64_t decode = cpu_ns() - start;
        start = cpu_ns();
        for (int i = 0; i < REPEATS; i++) {
            ge_swizzle(swizzled, linear, r.stride, h, r.stride, h, 1);
            __asm__ volatile("" ::: "memory");
        }
        uint64_t swizzle = cpu_ns() - start;
        if (run >= 0)
            printf("%d %d %d %.3f %.3f\n", w, h, depth, decode / (1000.0 * REPEATS), swizzle / (1000.0 * REPEATS));
    }
}

int main(void)
{
    for (unsigned i = GIF_HEADER; i < sizeof blob; i++)
        blob[i] = (unsigned char)(7 * i + (i >> 7));
    measure(128, 128, 4);
    measure(256, 256, 8);
    measure(512, 256, 8);
    measure(512, 256, 32);
}
'''


def main():
    renderer = (ROOT / "port/rwsky.c").read_text()
    copy_level = re.search(r"^static void copy_level\([^\n]*\)\n\{.*?^\}", renderer, re.M | re.S)[0]
    with tempfile.TemporaryDirectory(prefix="p4-swizzle-") as directory:
        path = Path(directory)
        source, binary = path / "bench.c", path / "bench"
        source.write_text(FIXTURE.replace("COPY_LEVEL", copy_level))
        subprocess.run(["cc", "-O2", "-Wall", "-I", ROOT / "port", source, "-o", binary], check=True)
        samples = subprocess.check_output([binary], text=True)
    rows = {}
    for row in samples.splitlines():
        width, height, depth, decode, swizzle = row.split()
        decodes, swizzles = rows.setdefault((width, height, depth), ([], []))
        decodes.append(float(decode))
        swizzles.append(float(swizzle))
    print("raster             decode us (median, min-max)   swizzle us (median, min-max)")
    for (width, height, depth), (decodes, swizzles) in rows.items():
        print(f"{width:>3}x{height:<3} {depth:>2} bpp       "
              f"{statistics.median(decodes):7.2f} ({min(decodes):.2f}-{max(decodes):.2f})         "
              f"{statistics.median(swizzles):7.2f} ({min(swizzles):.2f}-{max(swizzles):.2f})")


if __name__ == "__main__":
    main()
