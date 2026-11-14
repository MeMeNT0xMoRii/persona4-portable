#!/usr/bin/env python
"""Syntax-check every decomp .c file with psp-gcc and report what blocks a PSP build.

Mirrors src/ and include/ into build/census with the three mechanical shims from
docs/port/host_build_census.md (no __int128, no mode(TI), asm function bodies dropped),
so the decomp checkout itself is never edited.

  python tools/psp_census.py            # syntax only, writes build/census/report.txt
  python tools/psp_census.py --compile  # real -O2 objects, plus repair passes for per-TU prototype conflicts
"""
import os, re, shutil, subprocess, sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from psp_make_data import K
from psp_vu0 import translate as translate_vu0

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / "ext" / "Persona4-Decompilation"
OUT = ROOT / "build" / "census"
OBJ = ROOT / "build" / "obj"
GCC = Path(os.environ["PSPDEV"]) / "bin" / "psp-gcc.exe"

ASM_FN = re.compile(r"^[ \t]*(?:static[ \t]+)?asm[ \t]+([^;{()\n]+\([^)]*\))\s*\{", re.M)
MODE_TI = re.compile(r"__attribute__\s*\(\(\s*(?:__)?mode(?:__)?\s*\(\s*(?:__)?TI(?:__)?\s*\)\s*\)\)")
ERROR = re.compile(r"^(.+?):(\d+):(?:\d+: )?(?:fatal )?[Ee]rror: (.*)$", re.M)
MODE_TI_TYPEDEF = re.compile(r"typedef\s+[\w ]+?\s+(\w+)\s*__attribute__\s*\(\(\s*(?:__)?mode(?:__)?\s*\(\s*(?:__)?TI(?:__)?\s*\)\s*\)\)\s*;")
INT128 = re.compile(r"(?:(?:un)?signed\s+)?__int128(?:_t)?\b")
SECTION_ATTR = re.compile(r"\s*__attribute__\s*\(\(\s*section\s*\(\s*\"\.sdata\"\s*\)\s*\)\)")
# Local prototypes that fight each other inside one function: the header's or an implicit call stands in.
# ponytail: no s64 or float argument may depend on one of these; check each when its caller is first run.
DROP_LOCAL = {
    "src/Battle/btlAICommand.c": ["btlAICommandSetActionCode"],
    "src/Battle/btlCamera_inferred_001c0e40.c": ["btlUnitGetSphereWorldCenter"],
    "src/Battle/btlEffect_inferred_001e8d80.c": ["scrGetParamInt", "scrSetReturnInt"],
    "src/Battle/btlTarget_inferred_001f1030.c": ["btlConditionMessageId"],
    "src/promoted/code1_003c.c": ["func_003e1220", "func_003cb820"],
    "src/promoted/code1_003d.c": ["func_003df590"],
    "src/rw/skyblit.c": ["func_003ec330"],
}
FIXES = {
    # Retail reads this word with lwr/lwl (0x0019cf54): offset 0x4e is not 4-aligned. A plain lw raised an address
    # error on the PSP when the first battle's units refreshed their placement.
    "src/Battle/btlUnit_inferred_0019c0d0.c": [
        ("*(u32 *)&((BtlUnit *)entry)->unk_4e);",
         "({ u32 tag_; __builtin_memcpy(&tag_, ((BtlUnit *)entry)->unk_4e, 4); tag_; }));"),
    ],
    # C equivalents of PCSX2's SLUS-21782 widescreen patch (pavachan / nemesis2000).
    "src/Event/Fcl/fclBankManager_inferred_00103510.c": [
        ("((FclBankScale *)((u8 *)D_00796728 + offset))->bits = 0x3f800000;",
         "((FclBankScale *)((u8 *)D_00796728 + offset))->bits = 0x3f400000;"),
    ],
    "src/Scene/mt_sceneFunc.c": [
        ("K_View_SetFov(scene, *(f32 *)&((SceneCameraUpdate *)resource)->fov);",
         "K_View_SetFov(scene, *(f32 *)&((SceneCameraUpdate *)resource)->fov / 1.2578125f);"),
    ],
    "src/frFontRaster.c": [
        ("void frFontRasterUploadGlyph(int entryAddress, u8 *bitmap, int palette)\n{", "__attribute__((noipa)) void frFontRasterUploadGlyph(int entryAddress, u8 *bitmap, int palette)\n{"),
        ("originX = (f32)x / 16.0f;", "originX = (f32)x / 21.0f;"),
        ("extentX = (f32)(u32)width;", "extentX = (f32)((width * 12) >> 4);"),
    ],
    "src/nmCmdList_inferred_00353b50.c": [
        ("width = 256.0f * ((DatScreenWipe *)wipe)->scaleX.f;", "width = 192.0f * ((DatScreenWipe *)wipe)->scaleX.f;"),
    ],
    "src/unplaced/unplaced_inferred_00222210.c": [
        ("256.0f * scale[0],  512.0f * scale[1]", "192.0f * scale[0],  512.0f * scale[1]"),
    ],
    "src/datCalendar_inferred_001228c0.c": [
        ("((SdkSpriteSample *)sprite)->signedScaleX = 0xFF6;", "((SdkSpriteSample *)sprite)->signedScaleX = 0xBF6;"),
    ],
    "src/unplaced/unplaced_inferred_00260510.c": [
        ("    struct CalendarColor { u8 r, g, b, a; } depthValue;",
         "    extern void p4_calendar_selected_date(int);\n"
         "    struct CalendarColor { u8 r, g, b, a; } depthValue;"),
        ("    drawDepth = 1.0f + depth;\n",
         "    drawDepth = 1.0f + depth;\n    p4_calendar_selected_date(enabled);\n"),
        ("color, alpha, number, 2, 31, 0, calDrawDayNumberGlyph, (u8 *)&glyphContext);",
         "color, alpha, number, 2, 31, 0, calDrawDayNumberGlyph, (u8 *)&glyphContext);\n"
         "    p4_calendar_selected_date(0);"),
        ("void calDrawDayStripCallback(s32 queue, u8 *task)\n{",
         "void calDrawDayStripCallback(s32 queue, u8 *task)\n{\n    extern void p4_calendar_depth_pass(int);"),
        ("    work = kwlnTaskGetWork((void*)(task));\n    state = ((CalendarDayChange *)(work))->state;\n    switch (state) {",
         "    p4_calendar_depth_pass(1);\n    work = kwlnTaskGetWork((void*)(task));\n"
         "    state = ((CalendarDayChange *)(work))->state;\n    switch (state) {"),
        ("\n}\n#pragma pop\n\n#pragma push\n#pragma optimization_level 2\n// Queues a day-strip draw task",
         "\n    p4_calendar_depth_pass(0);\n}\n#pragma pop\n\n#pragma push\n#pragma optimization_level 2\n// Queues a day-strip draw task"),
    ],
    "src/shdPersona.c": [
        ("middleX = 256.0f + x;", "middleX = 192.0f + x;"),
        ("rightX = 256.0f + middleX;", "rightX = 192.0f + middleX;"),
        ("MEMBER(f32, work, ShdPersonaModelWork, current.x), ((ShdPersonaModelWork *)work)->current.y,",
         "MEMBER(f32, work, ShdPersonaModelWork, current.x) / 0.5078125f, ((ShdPersonaModelWork *)work)->current.y,"),
        ("shdPersonaDrawSmoothBar(work->model);", "/* Widescreen persona art uses the textured strip only. */"),
    ],
    "src/Camp/cmpPersona.c": [
        ("shdPersonaDrawSmoothBar(portraitTask);", "/* Widescreen persona art uses the textured strip only. */"),
    ],
    "src/Kosaka/Field/k_fldFrame_inferred_0016b8a0.c": [
        # At a path endpoint both selected keys can be the same; keep that key's rotation.
        ("interpolationFraction = (seededTime - lowTime) / span;", "interpolationFraction = span == 0.0f ? 0.0f : (seededTime - lowTime) / span;"),
    ],
    # Battle effects share the retail battle state; a private zero pointer becomes a GCC null trap.
    "src/Battle/btlEPL.c": [("static u8 *D_0072449C;", "extern u8 *D_0072449C;")],
    "src/promoted/code1_0042.c": [
        ("func_00421820(D_0070C684[0]);", "((void (*)())func_00421820)(D_0070C684[0]);"),
        ("u32 segment = 0x00710000;", f"u32 segment = {0x710000 + K:#x};"),
    ],
    # These promoted routines form absolute data addresses as a segment plus a signed offset.
    "src/promoted/code1_004c.c": [
        ("u32 segment = 0x00710000;", f"u32 segment = {0x710000 + K:#x};"),
        ("u32 segment = 0x00720000;", f"u32 segment = {0x720000 + K:#x};"),
    ],
    "src/Graphics/Effect/effObjectParticle.c": [("*(f32 (*)[1])", "*(f32 *)")],
    "src/frFont.c": [
        ("extern u8 iGpffffa748;", "extern s8 iGpffffa748;"),
        ("x = ((FrFontTextRun *)node)->x;", "x = (((FrFontTextRun *)node)->x * 21) / 16;"),
        # Run anchors use sixteenths of a pixel. Glyph advances use twenty-firsts in the PSP rasterizer.
        ("((FrFontTextRun *)tail)->width * 0x10;", "((FrFontTextRun *)tail)->width * 256 / 21;"),
    ],
    "src/Kernel/sdkUttmx.c": [
        ("    return image;\n}\n\n\n/* Read the TMX header",
         "    if (image->width > 512 || image->height > 512) {\n"
         "        extern unsigned p4_tmx_texture_size(unsigned size);\n"
         "        extern void p4_tmx_reduce(void *, unsigned, const void *, unsigned, unsigned, unsigned, int);\n"
         "        /* RwImage stores one byte per index even for a four-bit palette. */\n"
         "        p4_tmx_reduce(image->cpPixels, image->stride, image->cpPixels, image->stride,\n"
         "                      image->width, image->height, image->depth == 4 ? 8 : image->depth);\n"
         "        image->width = p4_tmx_texture_size(image->width);\n"
         "        image->height = p4_tmx_texture_size(image->height);\n"
         "    }\n"
         "    return image;\n}\n\n\n/* Read the TMX header"),
        ("textureHandle = func_003ec590(width, height, bitsPerPixel, resourceValue);",
         "{ extern unsigned p4_tmx_texture_size(unsigned size);\n"
         "    textureHandle = func_003ec590(p4_tmx_texture_size(width), p4_tmx_texture_size(height), bitsPerPixel, resourceValue); }"),
        ("  switch (bitsPerPixel)\n  {",
         "  if (width > 512 || height > 512) {\n"
         "    extern void p4_tmx_copy_limited(void *, const void *, unsigned, unsigned, int);\n"
         "    p4_tmx_copy_limited((void *)textureBuffer, (const void *)sourceAddressOrBits, width, height, bitsPerPixel);\n"
         "  } else switch (bitsPerPixel)\n  {"),
    ],
    "src/Kernel/sdkSpr.c": [
        # The Allegrex point transform reads all three components, even for the screen-plane rotation.
        ("    /* Retail leaves source[].z unwritten, yet both point-transform providers read Z, so these calls\n"
         "     * read uninitialized input. */\n",
         "    for (copyIndex = 0; copyIndex < 4; copyIndex++) {\n"
         "        source[copyIndex].z = 0.0f;\n"
         "    }\n"),
    ],
    "src/unplaced/unplaced_inferred_0046a770.c": [
        ("        /* Retail leaves the third component unwritten before rotation. */",
         "        d->z = 0.0f;"),
    ],
    "src/Event/Fcl/fclCombineMisc.c": [("typedef struct p4_q s128;", "typedef long long s128;")],  # holds a small integer
}
# FldFrame_RaycastFieldWorlds gets NULL from MT_Scene_GetRes (b112-b130, dungeon corridors, near treasure chests): the
# key is not a resource id but the low half of a stack address (fc20, f030, e440 ... one frame size apart). The PS2
# read kernel RAM at 0x228 and went on; the PSP faults. b131: the camera's cell from its frame's cached LTM. b118/b130
# took it through RwFrameGetLTM, whose frame sync raycasts again: recursion until the stack overflowed (thread killed,
# StackFree 0). b119's return 0 spun the main thread.
# The real corridor freeze (b112-b131): FldFrame_RaycastFieldObjects walks D_007E8020.objects[k] until NULL, but
# the table declares objects[1]. GCC took k > 0 as undefined, so the loop body ran once and the function ended in a
# jal RpClumpForAllAtomics with no return: it fell through into FldFrame_RaycastFieldWorlds with stale registers
# (a stack address as the key), which tail-calls RaycastFieldObjects again, until the 1 MB stack overflowed. A
# flexible array has no bound to assume.
FIXES["src/Kosaka/Field/k_fldFrame.c"] = [("    u8 *objects[1];\n", "    u8 *objects[];\n"), ('        field = (u8*)MT_Scene_GetRes(fieldId);\n        fieldFlags = fieldId & 0xffff;\n        if (fieldFlags == 0xffff)\n',
    '        field = (u8*)MT_Scene_GetRes(fieldId);\n        fieldFlags = fieldId & 0xffff;\n        if (field == NULL && fieldFlags != 0xffff)\n        {\n            extern void p4_log(const char *format, ...);\n            static s32 p4MissingLogged;\n            if (p4MissingLogged < 16)\n            {\n                u32 ids[8] = { 0 };\n                s32 n = 0;\n                u8 *listed = (u8*)MT_Scene_GetResListHead((fieldId >> 10) & 0x3f);\n                while (listed != NULL && n < 8)\n                {\n                    ids[n++] = *(u16*)listed;\n                    listed = (u8 *)((ResrcRecord *)(listed))->next;\n                }\n                p4MissingLogged++;\n                p4_log("TRACE field res %04x missing, type %d has %d: %04x %04x %04x %04x %04x %04x %04x %04x\\n", fieldFlags,\n                       (fieldId >> 10) & 0x3f, n, ids[0], ids[1], ids[2], ids[3], ids[4], ids[5], ids[6], ids[7]);\n            }\n            /* the camera\'s cell from the LTM its frame already holds (RwFrame: object, dirty link, modelling, ltm at\n               0x50). RwFrameGetLTM would sync the frame, and its callbacks raycast again: b130 recursed here until the\n               1 MB stack overflowed. */\n            object = (u8 *)((RwObject *)((u8*)((u8*)(uintptr_t)kwlnGetCamera())))->parent + 0x50;\n            fieldX = (s32)((((RwMatrixTag *)(object))->pos.x + 600.0f) / 1200.0f);\n            fieldY = (s32)((((RwMatrixTag *)(object))->pos.z + 600.0f) / 1200.0f);\n        }\n        else if (fieldFlags == 0xffff)\n')]
# Profiler (b137): sdkOtFlushBuckets runs every draw of the frame; time each node's start callback, its command (by
# type, or the atomic's render callback) and its end callback (port/rwsky.c p4_ot_begin/p4_ot_end, hwot in the log).
FIXES["src/Kosaka/k_clump/k_clump_inferred.c"] = [
    ('    void (*endCallback)(u8 *, s32, void *);\n    extern void RwCameraClear(s32, s8 *, s32);\n', '    void (*endCallback)(u8 *, s32, void *);\n    u32 p4OtStart;\n    extern void RwCameraClear(s32, s8 *, s32);\n'),
    ('            startCallback(bucket + 0x1C, MEMBER(s32, bucket, OtPrimitiveNode, callbackData), (void *)startCallback);\n', '            { extern u32 p4_ot_begin(void); extern void p4_ot_end(u32, u32); u32 p4t = p4_ot_begin();\n            startCallback(bucket + 0x1C, MEMBER(s32, bucket, OtPrimitiveNode, callbackData), (void *)startCallback);\n            p4_ot_end(p4t, (u32)startCallback); }\n'),
    ('        type = ((OtPrimitiveNode *)bucket)->command;\n', '        type = ((OtPrimitiveNode *)bucket)->command;\n        { extern u32 p4_ot_begin(void); p4OtStart = p4_ot_begin(); }\n'),
    ('        endCallback = (void (*)(u8 *, s32, void *))(u32)((OtPrimitiveNode *)bucket)->endCallback;\n', '        { extern void p4_ot_end(u32, u32); p4_ot_end(p4OtStart, type == 4 ? (u32)((struct RpAtomic *)((OtPrimitiveNode *)bucket)->primitive)->renderCallBack\n                                                   : type == 9 ? 0x109u : type); }\n        endCallback = (void (*)(u8 *, s32, void *))(u32)((OtPrimitiveNode *)bucket)->endCallback;\n'),
    ('            endCallback(bucket + 0x1C, MEMBER(s32, bucket, OtPrimitiveNode, endCallbackData), (void *)endCallback);\n', '            { extern u32 p4_ot_begin(void); extern void p4_ot_end(u32, u32); u32 p4t = p4_ot_begin();\n            endCallback(bucket + 0x1C, MEMBER(s32, bucket, OtPrimitiveNode, endCallbackData), (void *)endCallback);\n            p4_ot_end(p4t, (u32)endCallback); }\n'),
]
# Profiler (b139): mdlAdvanceModel is ~1 ms a character in Heaven (mdlQueueModelForDraw 3.0 of draw scene's 3.1 Mcycles);
# time its steps: 0 frame matrix, 1 attached matrices, 2 animation layers, 3 anim control, 4 effect transforms, 5 motion.
FIXES["src/Graphics/Model/mdlManager.c"] = [
    # b154: log the per-node scale the hierarchy scale hook applies (Margaret's head drawn as a line)
    ('        *(f32*)D_00922BB4_abs = hierarchyScale->aspect;\n',
     '        *(f32*)D_00922BB4_abs = hierarchyScale->aspect;\n        { extern void p4_scale_hook_note(void *layer, int node, const float *scale, float model_scale, float aspect, int speed_zero);\n          p4_scale_hook_note(layer, hierarchyScale->node, (const float *)(scaleFields + 4), hierarchyScale->modelScale, hierarchyScale->aspect,\n                             ((MdlLayerState *)layer)->speed == 0.0f); }\n'),
    ('                    mdlBlendAnimInterpolators(currentInterpolator,\n                        *(void * *)&((RpHAnimHierarchy *)((MdlLayerState *)previousLayer)->hierarchy)->currentAnim,\n                        currentInterpolator, 1.0f);\n',
     '                    { extern void p4_sec(int, int); p4_sec(6, 1); }\n                    mdlBlendAnimInterpolators(currentInterpolator,\n                        *(void * *)&((RpHAnimHierarchy *)((MdlLayerState *)previousLayer)->hierarchy)->currentAnim,\n                        currentInterpolator, 1.0f);\n                    { extern void p4_sec(int, int); p4_sec(6, 0); }\n'),
    ('                mdlRebaseLayerPose(layer);\n                if (updatePose != 0) {\n',
     '                { extern void p4_sec(int, int); p4_sec(7, 1); }\n                mdlRebaseLayerPose(layer);\n                { extern void p4_sec(int, int); p4_sec(7, 0); }\n                if (updatePose != 0) {\n'),
    ('                        mdlUpdateHierarchyPose(*(void * *)&((MdlLayerState *)layer)->hierarchy, layer, layer + 0x54, 0);\n',
     '                        { extern void p4_sec(int, int); p4_sec(8, 1); }\n                        { extern int p4_pose_plain; if (p4_pose_plain) func_00397c40(*(void * *)&((MdlLayerState *)layer)->hierarchy); else\n                        mdlUpdateHierarchyPose(*(void * *)&((MdlLayerState *)layer)->hierarchy, layer, layer + 0x54, 0); }\n                        { extern void p4_sec(int, int); p4_sec(8, 0); }\n'),
    ('        func_003e9cb0(frame, &matrix, 0);\n',
     '        { extern void p4_sec(int, int); p4_sec(0, 1); }\n        func_003e9cb0(frame, &matrix, 0);\n        { extern void p4_sec(int, int); p4_sec(0, 0); }\n'),
    ('        mdlSetAttachedModelMatrices((Model*)model, &matrix);\n',
     '        { extern void p4_sec(int, int); p4_sec(1, 1); }\n        mdlSetAttachedModelMatrices((Model*)model, &matrix);\n        { extern void p4_sec(int, int); p4_sec(1, 0); }\n'),
    ('                previousLayer = mdlAdvanceLayer(model + animationLayer * 0xA4 + 0xEC,\n                                             previousLayer, tick);\n',
     '                { extern void p4_sec(int, int); p4_sec(2, 1); }\n                previousLayer = mdlAdvanceLayer(model + animationLayer * 0xA4 + 0xEC,\n                                             previousLayer, tick);\n                { extern void p4_sec(int, int); p4_sec(2, 0); }\n'),
    ('        mdlAdvanceAnimControl(model + 0x23C, model + 0xEC);\n',
     '        { extern void p4_sec(int, int); p4_sec(3, 1); }\n        mdlAdvanceAnimControl(model + 0x23C, model + 0xEC);\n        { extern void p4_sec(int, int); p4_sec(3, 0); }\n'),
    ('                mdlEffectListBuildTransforms(*(u8** *)&((Model *)model)->effects, model);\n',
     '                { extern void p4_sec(int, int); p4_sec(4, 1); }\n                mdlEffectListBuildTransforms(*(u8** *)&((Model *)model)->effects, model);\n                { extern void p4_sec(int, int); p4_sec(4, 0); }\n'),
    ('        mdlTickMotionState(model + 0x2D0);\n',
     '        { extern void p4_sec(int, int); p4_sec(5, 1); }\n        mdlTickMotionState(model + 0x2D0);\n        { extern void p4_sec(int, int); p4_sec(5, 0); }\n'),
]
RETAIL_LITERAL = re.compile(r"(\*\)\s*(?:\([^()\n]*\)\s*\)\s*)?\(?)(0x0{0,2}[5-9][0-9A-Fa-f]{5})\b")  # data and callback pointer casts move with the image
TIMER_CONTROL = re.compile(r"\*\s*\(\s*(?:volatile\s+)?u32\s*\*\s*\)\s*0x100000[123]0")
TIMER0 = re.compile(r"\*\s*\(\s*(?:volatile\s+)?(?:u32|unsigned int|s32|int)\s*\*\s*\)\s*0x10000000")
ASM_STMT = re.compile(r"\b(?:__asm__|asm)\s*(?:volatile|__volatile__)?\s*\(")
EE_ONLY = re.compile(r"\$?\bvf\d|\$?\bvi\d|\$ACC|\$Q\b|\b(?:q?mtc2|q?mfc2|c[tf]c2|lqc2|sqc2|lq|sq|vwaitq|vu\w+|v[a-z]+\d?\.[xyzw]+|p[a-z]{3,6}\d?w?|ei|di|dsll32|dsrl32|prot3w|mt(?:sa|hi1|lo1)|mf(?:hi1|lo1)|sync\.[lp])\b")
COMPILE = "--compile" in sys.argv
PASSES = 20
FLAGS = ["-std=gnu89", "-w", "-ffreestanding", "-fno-builtin", "-fms-extensions", "-fmax-errors=0",
         "-Iinclude", "-Isrc", "-I.", "-DINCLUDE_ASM(F,N)=", "-DINCLUDE_RODATA(F,N)=", "-include", str(ROOT / "port" / "compat.h")]


def drop_asm_bodies(text):
    """asm <type> name(args) { ... }  ->  <type> name(args);  (the census only needs the prototype)"""
    out, pos = [], 0
    for m in iter(lambda: ASM_FN.search(text, pos), None):
        out.append(text[pos:m.start()] + m.group(1) + ";")
        depth, i = 1, m.end()
        while depth and i < len(text):
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        pos = i
    return "".join(out) + text[pos:]


VU0_DONE = [0, 0]  # asm statements translated, left as traps
FUNCTION_HEAD = re.compile(r"^[A-Za-z_][^\n;(]*\(([^;{}()]*)\)\s*\{", re.M)


def enclosing_arguments(text, position):
    """Names of the integer arguments of the function around position, in register order."""
    head = None
    for m in FUNCTION_HEAD.finditer(text, max(0, position - 60000), position):
        head = m
    if head is None:
        return ()
    names = []
    for parameter in head[1].split(","):
        parameter = parameter.strip()
        if not parameter or parameter == "void":
            continue
        if re.match(r"(?:const\s+)?(?:f32|float|u_long128)\b(?!\s*\*)", parameter):
            continue  # floats go in FPU registers
        names.append(re.findall(r"\w+", parameter)[-1])
    return names


def trap_ee_asm(text):
    """Inline asm the Allegrex cannot run becomes C (psp_vu0.py) or else a runtime trap naming file and line."""
    out, pos = [], 0
    for m in ASM_STMT.finditer(text):
        if m.start() < pos:
            continue
        depth, i, quoted = 1, m.end(), False
        while depth and i < len(text):
            c = text[i]
            if c == '"' and text[i - 1] != "\\":
                quoted = not quoted
            elif not quoted:
                depth += {"(": 1, ")": -1}.get(c, 0)
            i += 1
        if EE_ONLY.search(text[m.end():i]):
            newlines = text.count("\n", m.start(), i)  # keep line numbers: errors map back to the decomp
            inner = text[m.end():i - 1].replace("\\\n", "\n")
            # A following volatile reload names the local behind MWCC's fixed stack output slot.
            following = re.match(r";\s*(?:/\*.*?\*/\s*)*([^;]*);", text[i:i + 1200], re.S)
            # b165: also without volatile (FallingFunnelWindPackColors, the Flash*PackColors: Megidolaon's colours)
            output = re.search(r"\*\s*\(\s*(?:volatile\s+)?(?:s32|u32)\s*\*\s*\)\s*&\s*(\w+)", following[1]) if following else None
            if output and len(re.findall(r"\$(?:sp|29)\b", inner)) == 1:
                inner = re.sub(r"(\bsw\s+\$\w+,\s*)(?:0x[0-9a-fA-F]+|\d+)\(\$(?:sp|29)\)", lambda match: match[1] + output[1], inner)
            translated = translate_vu0(inner, enclosing_arguments(text, m.start()))
            if translated:
                VU0_DONE[0] += 1
                out.append(text[pos:m.start()] + translated + ("\\\n" if "\\\n" in text[m.start():i] else "\n") * newlines)
                pos = i
                continue
            VU0_DONE[1] += 1
            in_macro = "\\\n" in text[m.start():i]
            file_scope = m.start() == 0 or text[m.start() - 1] == "\n"
            trap = "" if file_scope else "P4_EE_ASM()"
            out.append(text[pos:m.start()] + trap + ("\\\n" if in_macro else "\n") * newlines)
            pos = i
            if file_scope and text[pos:pos + 1] == ";":
                pos += 1
    return "".join(out) + text[pos:]


# b113: the Get...Vec accessors too (mdlEffectGetPositionVec): every caller declares them void, so with the hidden
# struct return the output pointer arrived in a2 and the PSP faulted storing to 0x6B (an event effect after a bustup).
QUAD_SETTERS = re.compile(r"\b(?:u_long128|s128|u128)(\s+\w*(?:Set|Store|Copy|InitParticleState|Get\w*Vec)\w*\s*\()")
QUAD_SETTER_BODY = re.compile(r"\bvoid\s+\w*(?:Set|Store|Copy|InitParticleState|Get\w*Vec)\w*\s*\([^;{)]*\)\s*\{")
QUAD_SETTER_RETURN = re.compile(r"\breturn\s+([^;]+);")
QUAD_ASSIGNMENT = re.compile(r"(?<![=!<>])=(?!=)")


# The quad loaders are defined to return a 128-bit value (a struct here: hidden first argument) and most callers say so,
# but effDistortParticle.c and effPolygonTrack.c declare them void: those calls passed the output where the callee
# reads the instance. Give them the real return type (b114).
QUAD_LOADERS_VOID = re.compile(r"\bextern\s+void(\s+mdlEffectLoadQuad[AB]\s*\()")


def quad_loaders_return(text):
    return QUAD_LOADERS_VOID.sub(r"extern struct p4_q\1", text)


def void_quad_setters(text):
    """A struct return takes a hidden first argument here, but retail's quad setters go through tables of plain calls
    and return the quad only in passing: they become void."""
    if not QUAD_SETTERS.search(text):
        return text

    def drop_value(match):
        value = match[1]
        # Some setters perform their only store in the return expression.
        if QUAD_ASSIGNMENT.search(value):
            return f"{{ {value}; return; }}"
        return "return;"

    text = QUAD_SETTERS.sub(r"void\1", text)
    out, pos = [], 0
    for m in QUAD_SETTER_BODY.finditer(text):
        depth, i = 1, m.end()
        while depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out.append(text[pos:m.end()] + QUAD_SETTER_RETURN.sub(drop_value, text[m.end():i]))
        pos = i
    return "".join(out) + text[pos:]


# An EE register holds 64 bits, so decompiled code calls many int arguments s64. Here an s64 takes a register pair and
# shifts every argument after it, against both the real definition and reassembled retail code: they become 32-bit.
PROTOTYPE = re.compile(r"^([ \t]*(?:(?:extern|static)[ \t]+)?[A-Za-z_][\w \t*]*?[ \t*](\w+)[ \t]*\()([^;{}()]*)(\)[ \t]*(?:;|\{|$))", re.M)
WIDE_PARAMETER = re.compile(r"\b([su])64\b(?![ \t]*\*)")
REALLY_64_BIT = {"gsPackGifTag", "mdlEffectEmitValuePacket", "dpsub", "RunningStatAdd", "RFMAI_CnvS64toU32plusU32", "sprintf",
                 # b165: an s64 that carries two floats (x low, y high; callers pass *(s64 *)&vec2). Narrowed, y was
                 # lost: the minimap's player arrow drew at y ~0, above the map.
                 "mapSymbolMarkerDraw", "taskRecordSetPosition", "H_Dbprt_FmtAt", "sflDataDrawPersonaPanel",
                 "sflDataDrawByteValuePanel", "sflDataDrawNumberValue", "sflDataDrawTitledIcon", "sflDataDrawLevelBadge"}


def narrow_parameters(text):
    def narrow(m):
        return m[0] if m[2] in REALLY_64_BIT else m[1] + WIDE_PARAMETER.sub(r"\g<1>32", m[3]) + m[4]
    return PROTOTYPE.sub(narrow, text)


# Edits to inline asm itself: applied before trap_ee_asm translates it (FIXES run after).
ASM_FIXES = {
    # b165: the ring colour pack keeps temporaries in the retail frame, colourStack at sp + 0x40: name the array.
    "src/Graphics/Effect/effPolygonRing.c": [
        ('"sw          %1, 0x44($sp)       \\n"', '"sw          %1, 4(%3)           \\n"'),
        ('"addiu       $2, $sp, 0x44       \\n"', '"addiu       $2, %3, 4           \\n"'),
        ('"sw          $3, 0x40($sp)       \\n"', '"sw          $3, 0(%3)           \\n"'),
        (': "r"(baseColor), "r"((u32)value), "f"(scale)\n        : "$2", "$3", "memory");',
         ': "r"(baseColor), "r"((u32)value), "f"(scale), "r"(colourStack)\n        : "$2", "$3", "memory");'),
    ],
}


def shim(text, rel=""):
    for old, new in ASM_FIXES.get(rel, ()):
        assert old in text, (rel, old)
        text = text.replace(old, new)
    # scr_internal.h defines a 32-bit result; stale s64 prototypes shift arguments of unprototyped calls.
    text = re.sub(r"\bs64(\s+scrGetParamInt\s*\()", r"s32\1", text)
    text = MODE_TI_TYPEDEF.sub(r"typedef struct p4_q \1;", trap_ee_asm(drop_asm_bodies(void_quad_setters(quad_loaders_return(narrow_parameters(text))))))
    text = INT128.sub("struct p4_q", MODE_TI.sub("", text))
    text = SECTION_ATTR.sub("", text)
    text = RETAIL_LITERAL.sub(lambda m: f"{m[1]}{int(m[2], 16) + K:#x}", text)
    text = TIMER_CONTROL.sub("P4_T0_CONTROL", text)
    text = TIMER0.sub("P4_T0_COUNT", text)
    for name in DROP_LOCAL.get(rel, ()):
        text = re.sub(rf"^[ \t]+extern[^;{{]*\b{name}\s*\([^;]*;.*$", "", text, flags=re.M)
    for old, new in FIXES.get(rel, ()):
        assert old in text, (rel, old)
        text = text.replace(old, new)
    return text


def regime_flags():
    """Include paths and defines per compiler regime, from the decomp's own version_flags.txt."""
    flags = {}
    for line in (DECOMP / "config" / "version_flags.txt").read_text(encoding="utf-8").splitlines():
        words = line.split()
        if words and not line.startswith("#"):
            flags.setdefault(words[0], []).extend(w for w in words[1:] if w.startswith(("-I", "-D")))
    return flags


REGIMES = regime_flags()


def mirror():
    shutil.rmtree(OUT, ignore_errors=True)
    for tree in ("src", "include"):
        for p in (DECOMP / tree).rglob("*"):
            if p.suffix in (".c", ".h", ".inc", ".def", ".rpe"):
                dst = OUT / p.relative_to(DECOMP)
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_text(shim(p.read_text(encoding="utf-8", errors="replace"), p.relative_to(DECOMP).as_posix()), encoding="utf-8")


def group(rel):
    parts = rel.split("/")
    if len(parts) > 2:
        if parts[1] in ("rw", "renderware"):
            return "RW"
        if parts[1] in ("cri", "sce", "middleware"):
            return parts[1].upper()
    return "FIRST"


def unit_flags(rel):
    if rel.startswith("src/cri/re4/"):
        return REGIMES["eegcc296re4"]
    return {"RW": REGIMES["cw3.0.1b119"], "CRI": REGIMES["eegcc296"]}.get(group(rel), [])


def check(rel):
    obj = OBJ / (rel.replace("/", "__")[:-2] + ".o")
    mode = ["-c", "-O2", "-G0", "-fno-strict-aliasing", "-o", str(obj)] if COMPILE else ["-fsyntax-only"]
    r = subprocess.run([str(GCC), *mode, *FLAGS, *unit_flags(rel), rel],
                       cwd=OUT, capture_output=True, text=True, errors="replace")
    return rel, ERROR.findall(r.stderr), r.returncode


def statement_end(lines, start):
    """Index of the line that ends the declaration starting at lines[start], or None if it is a definition."""
    for i in range(start, min(start + 12, len(lines))):
        code = lines[i].split("//")[0]
        if "{" in code:
            return None
        if ";" in code:
            return i
    return None


def top_level_before(lines, line):
    """First line of the top-level item that holds lines[line]."""
    at = line
    while at and not (lines[at - 1].startswith("}") or (lines[at - 1][:1].strip() and lines[at - 1].rstrip().endswith(";"))):
        at -= 1
    return at


def relabel(lines, line, name):
    """Keep a TU's own prototype: same symbol through an asm label, private C name where it is in scope.

    A block-scope prototype is renamed for its function only, as MWCC scopes it. Returns the first changed line."""
    end = statement_end(lines, line)
    if end is None:
        return None
    real = re.sub(r"__tu\d+$", "", name)
    if "__asm__" not in "".join(lines[line:end + 1]):
        code, semi, rest = lines[end].partition(";")
        lines[end] = f'{code} __asm__("{real}"){semi}{rest}'
    rename = [f"#undef {real}", f"#define {real} {real}__tu{line}"]
    if lines[line][:1].isspace():
        at = top_level_before(lines, line)
        close = next((i for i in range(end, len(lines)) if lines[i].startswith("}")), len(lines) - 1)
        lines[close + 1:close + 1] = [f"#undef {real}"]
        lines[at:at] = rename
        return at
    lines[line:line] = rename
    return line


def local_prototypes(lines, name):
    """Lines of every block-scope extern prototype of name."""
    real = re.sub(r"__tu\d+$", "", name)
    return [i for i, s in enumerate(lines) if s[:1].isspace() and re.search(rf"\bextern\b.*\b{real}\s*\(", s)]


def rename_definition(lines, line, name, static):
    """A definition that disagrees with an earlier prototype: compile it privately, publish the symbol with .set."""
    real = re.sub(r"__tu\d+$", "", name)
    close = next((i for i in range(line, len(lines)) if lines[i].startswith("}")), None)
    if close is None:
        return None
    private = f"{real}__def{line}"
    publish = "" if static else rf".globl {real}\n"
    lines[close + 1:close + 1] = [f"#undef {real}", rf'__asm__("{publish}.set {real}, {private}\n");']
    lines[line:line] = [f"#undef {real}", f"#define {real} {private}"] + (["__attribute__((used))"] if static else [])
    return line


def repair(results):
    """One pass over the mirror; returns the files changed. Bottom-up so earlier line numbers stay right."""
    changed = set()
    for rel, errors, rc in results:
        if not rc or not rel.endswith(".c"):
            continue
        path = OUT / rel
        lines = path.read_text(encoding="utf-8").split("\n")
        todo = set()
        for f, n, msg in errors:
            if f != rel:
                continue
            conflict = re.match(r"conflicting types for '([^']+)'", msg)
            implicit = re.match(r"incompatible implicit declaration of function '([^']+)'", msg)
            static = re.match(r"static declaration of '([^']+)' follows", msg)
            if conflict:
                todo.add((int(n) - 1, conflict[1]))
            elif static:
                todo.add((int(n) - 1, static[1]))
            elif implicit:
                todo.update((i, implicit[1]) for i in local_prototypes(lines, implicit[1]))
        floor = len(lines)
        for line, name in sorted(todo, reverse=True):
            if line < floor:  # a rename above moved everything below it: the rest waits for the next pass
                at = relabel(lines, line, name)
                if at is None:
                    at = rename_definition(lines, line, name, lines[line].lstrip().startswith("static"))
                if at is not None:
                    floor = at
                    changed.add(rel)
        if rel in changed:
            path.write_text("\n".join(lines), encoding="utf-8")
    return changed


def kind(msg):
    return re.sub(r"'[^']*'", "'X'", msg)[:70]


def main():
    mirror()
    OBJ.mkdir(exist_ok=True)
    files = sorted(p.relative_to(OUT).as_posix() for p in (OUT / "src").rglob("*.c"))
    with ThreadPoolExecutor(os.cpu_count()) as pool:
        results = list(pool.map(check, files))
        for _ in range(PASSES if COMPILE else 0):
            changed = repair(results)
            print(f"repair pass: {len(changed)} files")
            if not changed:
                break
            redo = dict(zip(sorted(changed), pool.map(check, sorted(changed))))
            results = [redo.get(r[0], r) for r in results]

    groups, kinds, bad = Counter(), Counter(), []
    for rel, errors, rc in results:
        groups[group(rel), "files"] += 1
        if rc:
            groups[group(rel), "failed"] += 1
            groups[group(rel), "errors"] += len(errors)
            bad.append((len(errors), rel))
            kinds.update(kind(e[2]) for e in errors)

    lines = [f"{g}: {groups[g, 'failed']}/{groups[g, 'files']} files fail, {groups[g, 'errors']} errors"
             for g in ("FIRST", "RW", "CRI", "SCE", "MIDDLEWARE")]
    lines += ["", "top kinds:"] + [f"{n:6} {k}" for k, n in kinds.most_common(25)]
    lines += ["", "worst files:"] + [f"{n:6} {r}" for n, r in sorted(bad, reverse=True)[:25]]
    report = "\n".join(lines)
    (OUT / ("report_compile.txt" if COMPILE else "report.txt")).write_text(report + "\n", encoding="utf-8")
    (OUT / ("failed_compile.txt" if COMPILE else "failed.txt")).write_text("\n".join(r for _, r in sorted(bad, key=lambda x: x[1])) + "\n", encoding="utf-8")
    (OUT / ("errors_compile.txt" if COMPILE else "errors.txt")).write_text(
        "".join(f"{e[0]}:{e[1]}: {e[2]}\n" for _, errors, rc in results if rc for e in errors), encoding="utf-8")
    print(report)
    assert sum(groups[g, "files"] for g in ("FIRST", "RW", "CRI", "SCE", "MIDDLEWARE")) == len(files)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
