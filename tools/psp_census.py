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
        ('    s32 blueShifted;\n', '    s32 blueShifted;\n    extern int p4_font_skip, p4_font_skip_prepare(void);\n    int p4_font_skip_ = p4_font_skip && p4_font_skip_prepare();\n'),
        ('    originX = (f32)x / 16.0f;', '    if (p4_font_skip_) goto p4_font_cache_;\n    originX = (f32)x / 16.0f;'),
        ('    if (MEMBER(u8 *, glyph, FrFontGlyphData, raster) == NULL) {', 'p4_font_cache_:\n    if (MEMBER(u8 *, glyph, FrFontGlyphData, raster) == NULL) {'),
        ('    ((FrFontRasterEntry *)raster)->flags |= 1;\n    if (opacity == 0)', '    ((FrFontRasterEntry *)raster)->flags |= 1;\n    if (p4_font_skip_) {\n        if ((rgba & 0xFF) == 0) return 0;\n        goto p4_font_bind_;\n    }\n    if (opacity == 0)'),
        ('    D_00887300[0](1, ((FrFontRasterEntry *)raster)->texture);', 'p4_font_bind_:\n    D_00887300[0](1, ((FrFontRasterEntry *)raster)->texture);'),
        ('    D_00887310[0](4, vertices, 4);', '    if (!p4_font_skip_) D_00887310[0](4, vertices, 4);'),
        ("originX = (f32)x / 16.0f;", "originX = (f32)x / 21.0f;"),
        ("extentX = (f32)(u32)width;", "extentX = (f32)((width * 12) >> 4);"),
    ],
    "src/nmCmdList_inferred_00353b50.c": [
        ("width = 256.0f * ((DatScreenWipe *)wipe)->scaleX.f;", "width = 192.0f * ((DatScreenWipe *)wipe)->scaleX.f;"),
    ],
    "src/unplaced/unplaced_inferred_00222210.c": [
        ("256.0f * scale[0],  512.0f * scale[1]", "192.0f * scale[0],  512.0f * scale[1]"),
    ],
    # The CRI splash: a 640x448 active picture in a 1024x512 atlas, drawn 4:3 and centred (persona4-portable, home25).
    "src/promoted/code1_0012.c": [
        ("        sflDataDrawTexturedQuad(0, 0, 0.0f, 0x400, 0x200, 0xFFFFFF, 0xFF, 1, 0, 0, 0, fullImage, 0);",
         "        { extern void p4_draw_cri_logo(s32 texture); p4_draw_cri_logo(fullImage); }"),
    ],
    # The fusion list's 16x16 indicators keep their 4:3 shape around the original column centres (home25).
    "src/Event/y_fclCmbBall.c": [
        ("    position.x += packetIndex * 19;",
         "    const f32 halfWidth = 8.0f * (4.0f * 272.0f / (3.0f * 480.0f)); position.x += packetIndex * 19;"),
        ("->x = position.x;", "->x = position.x + 8.0f - halfWidth;"),
        ("->x = 16.0f + position.x;", "->x = position.x + 8.0f + halfWidth;"),
    ],
    # Retail sign-extends the room's low halfword; on an s32 roomId the 48-bit shift is undefined
    # (secondary room fix from MeMeNT0xMoRii's fork; the primary compare has the same shape).
    # The digit fade reads RGBA at sprite + 0x75. GCC ignores packed on the typedef name, leaving an unaligned lw
    # that raises an address error on the PSP when a fusion's Persona rows retract (fix from MeMeNT0xMoRii's fork,
    # P4-057: PSP Go crash after the second Persona; PPSSPP tolerates the load).
    "src/nmCmdList_inferred_002ba080.c": [
        ("    typedef struct {\n        u32 word;\n    } DigitStoredWord __attribute__((packed));",
         "    typedef struct __attribute__((packed, may_alias)) {\n"
         "        u32 word;\n"
         "    } DigitStoredWord;\n"
         "    _Static_assert(_Alignof(DigitStoredWord) == 1, \"digit color must be byte-aligned\");"),
    ],
    # Social Stats (P4-052, MeMeNT0xMoRii's fork): radarDrawSegment's last argument is a full register.
    "src/Kernel/h_malloc_inferred_001400f0.c": [
        ("extern void radarDrawSegment(s64 work, s64 value, s32 context, f32 depth, u8 option);",
         "extern void radarDrawSegment(s64 work, s64 value, s32 context, f32 depth, s32 option);"),
    ],
    "src/Kernel/h_malloc_inferred_001441e0.c": [
        ("->primaryRoom != ((s64)(roomId << 0x30) >> 0x30)", "->primaryRoom != (s16)roomId"),
        ("->secondaryRoom != ((s64)(roomId << 0x30) >> 0x30)", "->secondaryRoom != (s16)roomId"),
    ],
    "src/datCalendar_inferred_001228c0.c": [
        ("void calendarDrawBackdrop(void *unusedNode, void *work)\n{", f'#include "{(ROOT / "port/calendar_skip.h").as_posix()}"\nvoid calendarDrawBackdrop(void *unusedNode, void *work)\n{{\n    extern int p4_calendar_skip, p4_calendar_skip_prepare(void);\n    int p4skip;\n    unsigned p4initial;'),
        ("    calendar = (CalendarState *)work;", "    p4skip = p4_calendar_skip && p4_calendar_skip_prepare();\n    calendar = (CalendarState *)work;\n    p4initial = calendar->field_0C;"),
        ("        startFrame = calendar->records[1].field_28;", "        if (p4skip) goto p4_calendar_end_;\n        startFrame = calendar->records[1].field_28;"),
        ("    if ((calendar->field_0C & 2) != 0)", "    if (!p4skip && (calendar->field_0C & 2) != 0)"),
        ("    if ((calendar->field_0C & 4) != 0)", "    if (!p4skip && (calendar->field_0C & 4) != 0)"),
        ("    states[0](rwRENDERSTATEFOGENABLE, (void *)savedFog);", "p4_calendar_end_:\n    if (p4skip) p4_calendar_restore_state(calendar, p4initial);\n    states[0](rwRENDERSTATEFOGENABLE, (void *)savedFog);"),
        ("((SdkSpriteSample *)sprite)->signedScaleX = 0xFF6;", "((SdkSpriteSample *)sprite)->signedScaleX = 0xBF6;"),
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
        # Run anchors use sixteenths of a pixel, glyph advances twenty-firsts in the PSP rasterizer (fix from
        # MeMeNT0xMoRii's fork: gaps around inserted names and in stat messages).
        ("x = ((FrFontTextRun *)node)->x;", "x = (((FrFontTextRun *)node)->x * 21) / 16;"),
        ("((FrFontTextRun *)tail)->width * 0x10;", "((FrFontTextRun *)tail)->width * 256 / 21;"),
    ],
    "src/Event/Fcl/fclCombineMisc.c": [("typedef struct p4_q s128;", "typedef long long s128;")],  # holds a small integer
    # The point transform reads all three components, even for the screen-plane rotation; retail leaves z unwritten
    # (fix from MeMeNT0xMoRii's fork).
    # b311 TMX images over 512 a side reduced for the GE (fix from MeMeNT0xMoRii's fork, P4-001: blank CRI logo).
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
    # b312 silhouette tint and TV rings (from MeMeNT0xMoRii's fork).
    "src/shdMisc.c": [
        ("    ShdQuadVertex verts[4];\n",
         "    extern int p4_texture_tint;\n"
         "    extern void p4_draw_texture_tint(u32, f32, f32, f32, f32, f32, f32, void *, int, int);\n"
         "    if (p4_texture_tint && texture && (blendMode == 0 || blendMode == 1)) {\n"
         "        p4_draw_texture_tint(color, x, y, maskX, maskY, width, height, *(void **)texture,\n"
         "                             drawMask && (color & 255) != 255, blendMode);\n"
         "        return;\n"
         "    }\n"
         "    ShdQuadVertex verts[4];\n"),
    ],
    "src/Scene/mt_scene_inferred_00266a40.c": [
        ("    if (shape == 9) {\n        specialCount = counts.value[shape * 2];",
         "    {\n"
         "        extern int p4_draw_tv_ring(const void *, int, const void *, int, float, float,\n"
         "                                  int, int, float, float, float, unsigned, unsigned);\n"
         "        if ((unsigned)shape < 9 &&\n"
         "            p4_draw_tv_ring(sources.value[shape * 2], counts.value[shape * 2],\n"
         "                            sources.value[shape * 2 + 1], counts.value[shape * 2 + 1],\n"
         "                            x, y, originX, originY, rotation, scaleX, scaleY, rgb, alpha))\n"
         "            return;\n"
         "    }\n"
         "    if (shape == 9) {\n        specialCount = counts.value[shape * 2];"),
    ],
    # b323 Shuffle Time card scopes for the back-face culling in im3d_draw (MeMeNT0xMoRii's fork, P4-032).
    "src/Battle/btlShuffleCard.c": [
        ("    pscModelDraw(card);",
         "    extern void p4_shuffle_card_begin(int);\n"
         "    p4_shuffle_card_begin(1);\n"
         "    pscModelDraw(card);\n"
         "    p4_shuffle_card_begin(0);"),
    ],
    "src/Battle/btlShuffleDraw.c": [
        ("extern s32 func_004106a0(BtlShufflePrimitive primitive);",
         "extern s32 p4_shuffle_render_primitive(BtlShufflePrimitive primitive);"),
        ("func_004106a0(4);", "p4_shuffle_render_primitive(4);"),
    ],
    "src/Kernel/sdkSpr.c": [
        ("    /* Retail leaves source[].z unwritten, yet both point-transform providers read Z, so these calls\n"
         "     * read uninitialized input. */\n",
         "    for (copyIndex = 0; copyIndex < 4; copyIndex++) {\n"
         "        source[copyIndex].z = 0.0f;\n"
         "    }\n"),
    ],
    "src/unplaced/unplaced_inferred_0046a770.c": [
        ("        /* Retail leaves the third component unwritten before rotation. */", "        d->z = 0.0f;"),
    ],
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
# mdlEffectResolveChildOffset loads values[] as a full quadword: the fourth word was a stray stack slot.
FIXES["src/Graphics/Model/mdlEffect_inferred_00486780.c"] = [("u32 values[3];", "u32 values[4] = {0};")]
FIXES["src/Graphics/Model/mdlManager.c"] = [
    # The local matrices' flags were or-ed into an unset stack word (fix from MeMeNT0xMoRii's fork).
    ("      identityMatrix.flags = identityMatrix.flags | 0x20003;", "      identityMatrix.flags = 0x20003;"),
    ("      baseMatrix.flags |= 0x20003;", "      baseMatrix.flags = 0x20003;"),
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
     '        { extern void p4_sec(int, int); p4_sec(0, 1); }\n        { extern int p4_tick_lite(int); if (!p4_tick_lite(1)) func_003e9cb0(frame, &matrix, 0); }\n        { extern void p4_sec(int, int); p4_sec(0, 0); }\n'),
    ('        mdlSetAttachedModelMatrices((Model*)model, &matrix);\n',
     '        { extern void p4_sec(int, int); p4_sec(1, 1); }\n        { extern int p4_tick_lite(int); if (!p4_tick_lite(1)) mdlSetAttachedModelMatrices((Model*)model, &matrix); }\n        { extern void p4_sec(int, int); p4_sec(1, 0); }\n'),
    ('                previousLayer = mdlAdvanceLayer(model + animationLayer * 0xA4 + 0xEC,\n                                             previousLayer, tick);\n',
     '                { extern void p4_sec(int, int); p4_sec(2, 1); }\n'
     '                { MdlLayerState *p4l = (MdlLayerState *)(model + animationLayer * 0xA4 + 0xEC); extern void p4_anim_lazy_scope(void *, void *);\n'
     '                  p4_anim_lazy_scope(p4l->hierarchy ? (void *)((RpHAnimHierarchy *)p4l->hierarchy)->currentAnim : 0, (void *)p4l->rebaseInput); }\n'
     '                { extern int p4_anim_freeze; if (!(p4_anim_freeze & 1))\n'
     '                previousLayer = mdlAdvanceLayer(model + animationLayer * 0xA4 + 0xEC,\n                                             previousLayer, tick); }\n'
     '                { extern void p4_anim_lazy_scope(void *, void *); p4_anim_lazy_scope(0, 0); }\n'
     '                { extern void p4_sec(int, int); p4_sec(2, 0); }\n'),
    ('        mdlAdvanceAnimControl(model + 0x23C, model + 0xEC);\n',
     '        { extern void p4_sec(int, int); p4_sec(3, 1); }\n        { extern void p4_anim_lazy_materials(int); p4_anim_lazy_materials(1); }\n'
     '        { extern int p4_anim_freeze; if (!(p4_anim_freeze & 4)) mdlAdvanceAnimControl(model + 0x23C, model + 0xEC); }\n'
     '        { extern void p4_anim_lazy_materials(int); p4_anim_lazy_materials(0); }\n        { extern void p4_sec(int, int); p4_sec(3, 0); }\n'),
    ('                mdlEffectListBuildTransforms(*(u8** *)&((Model *)model)->effects, model);\n',
     '                { extern void p4_sec(int, int); p4_sec(4, 1); }\n                { extern int p4_tick_lite(int); if (!p4_tick_lite(2)) mdlEffectListBuildTransforms(*(u8** *)&((Model *)model)->effects, model); }\n                { extern void p4_sec(int, int); p4_sec(4, 0); }\n'),
    ('        mdlTickMotionState(model + 0x2D0);\n',
     '        { extern void p4_sec(int, int); p4_sec(5, 1); }\n        mdlTickMotionState(model + 0x2D0);\n        { extern void p4_sec(int, int); p4_sec(5, 0); }\n'),
]
# b279: section 19 (aadv): mdlAdvanceModel as mdlQueueModelForDraw calls it, inclusive; qmodel minus aadv is the queue itself
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('        mdlAdvanceModel(model, (flags & 1) == 0);\n',
     '        { extern void p4_sec(int, int); p4_sec(19, 1); mdlAdvanceModel(model, (flags & 1) == 0); p4_sec(19, 0); }\n'),
    # b283: one statement, it is the body of `if ((flags & 4) == 0)`. b279-b282 had three: the advance ran for every
    # model, flag 4 too (models the game advances elsewhere), and their animation ran twice as fast.
]
# b278 p4_tick_lite (port/rwsky.c): in a tick frameskip leaves undrawn, mdlAdvanceModel skips what the next drawn tick
# sets again from scratch: bit 1 the root frame and attached model matrices, 2 the effect transforms, 4 the child models'
# indexed matrix copies. Clocks, layers, motion and attachment delays still run every tick.
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('                        mdlEffectListBuildTransforms((u8**)attachments->primaryDraw, model);\n',
     '                        { extern int p4_tick_lite(int); if (!p4_tick_lite(2)) mdlEffectListBuildTransforms((u8**)attachments->primaryDraw, model); }\n'),
    ('                            mdlCopyIndexedMatrix(model, childFrame, childMatrix);\n',
     '                            { extern int p4_tick_lite(int); if (!p4_tick_lite(4)) mdlCopyIndexedMatrix(model, childFrame, childMatrix); }\n'),
]
# Profiler (b182): mdlUpdateDrawState is the character shadow pass (OT node 28, ~4 Mcycles a frame of its own in a
# battle): time its parts, and check whether the flattened pose is the normal one times the shear (port/rwsky.c
# p4_shadow_check), which would let the GE draw the shadow from the body's bones. b183 (p4_shadow_skip): in a frame
# frameskip leaves undrawn, take the game's own no-shadow path (alpha 0: one pose update, no shadow pose, draw or
# restore), then the effect transforms and attachment delays as every frame; b182 battle: 1.45 Mcycles of skipped frames.
# b186 (p4_shadow_shear): without look-at, pose once for the body and draw the shadow sheared from it. b187: blend
# tracks too (b186 excluded them, and every battle character has one; b185 matched with the track between the poses).
# b192 (p4_shadow_lookat): look-at models too. The game's look-at pose blends a stored rotation toward its target on
# every call and ran twice a tick (shadow, body); the one body call now takes blend' = 1 - (1 - blend)^2, two steps.
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('void mdlUpdateDrawState(MdlDrawState* owner)\n{\n',
     'extern void p4_sec(int, int); extern void p4_shadow_check(void *, const void *, int); static void p4_mdlUpdateDrawState(MdlDrawState* owner); void mdlUpdateDrawState(MdlDrawState* owner) { p4_sec(9, 1); p4_mdlUpdateDrawState(owner); p4_sec(9, 0); } static void p4_mdlUpdateDrawState(MdlDrawState* owner)\n{ extern int p4_shadow_skip(void); extern int p4_shadow_shear_on(void); extern int p4_shadow_lookat_on(void); extern void p4_shadow_shear_set(const void *); extern void p4_shadow_shear_set_color(const void *, unsigned); int p4skip = p4_shadow_skip(), p4shear = 0;\n'),
    ('    if (color.alpha == 0) {\n', '    if (color.alpha == 0 || p4skip) { p4_shadow_check(owner->hierarchy, 0, 3);\n'),
    # b183: ... then the effect transforms and attachment delays, which tick every frame, and returns
    ('            j++;\n        }\n        }\n        return;\n    }\n', '            j++;\n        }\n        }\n        if (p4skip) goto p4_fx; return;\n    }\n'),
    ('    RwMatrixMultiply(&matrix0, &owner->identityMatrix, owner);\n    RwMatrixMultiply(&matrix1, &matrix0, &identity);\n    func_003e9cb0(frame, &matrix1, 0);\n',
     '    p4_sec(15, 1); p4shear = (!(owner->flags140 & 0x4000) || p4_shadow_lookat_on()) && p4_shadow_shear_on(); if (!p4shear) { RwMatrixMultiply(&matrix0, &owner->identityMatrix, owner);\n    RwMatrixMultiply(&matrix1, &matrix0, &identity);\n    func_003e9cb0(frame, &matrix1, 0); } p4_sec(15, 0);\n'),
    ('    if ((owner->flags140 & 0x4000) != 0) {\n        mdlUpdateHierarchyPose(owner->hierarchy,\n                      (u8*)&owner->animFlags, (u8*)&owner->flags140,\n                      &identity);\n    } else {\n        func_00397c40(owner->hierarchy);\n    }\n',
     '    p4_shadow_check(owner->hierarchy, &identity, 0); p4_sec(10, 1); if (p4shear) { f32 *p4blend = &((MdlLookState *)&owner->flags140)->blend, p4keep = *p4blend; if ((owner->flags140 & 0x4000) && p4keep > 0.0f && p4keep < 1.0f) *p4blend = 1.0f - (1.0f - p4keep) * (1.0f - p4keep); mdlUpdateModelMatrix((Model*)owner); if ((owner->animFlags & 0x10) != 0) mdlInstallHierarchyScaleHook(owner->hierarchy, (u8*)&owner->animFlags); else if ((owner->flags140 & 0x81E0) != 0) mdlUpdateHierarchyPose(owner->hierarchy, (u8*)&owner->animFlags, (u8*)&owner->flags140, 0); else func_00397c40(owner->hierarchy); *p4blend = p4keep; } else if ((owner->flags140 & 0x4000) != 0) {\n        mdlUpdateHierarchyPose(owner->hierarchy,\n                      (u8*)&owner->animFlags, (u8*)&owner->flags140,\n                      &identity);\n    } else {\n        func_00397c40(owner->hierarchy);\n    } p4_sec(10, 0); p4_shadow_check(owner->hierarchy, &identity, (owner->flags140 & 0x4000) ? 11 : 1);\n'),
    ('    mdlClumpApplyMatColorScale(owner->clump, (u32*)&color,\n                        (u16)(s64)((owner->flags & 8) != 0));\n',
     '    p4_sec(12, 1); mdlClumpApplyMatColorScale(owner->clump, (u32*)&color,\n                        (u16)(s64)((owner->flags & 8) != 0)); p4_sec(12, 0);\n'),
    ('    mdlDrawVisibleAtomics(owner->clump);\n    mdlUpdateModelMatrix((Model*)owner);\n',
     '    p4_sec(13, 1); if (p4shear) p4_shadow_shear_set_color(&identity, color.alpha); mdlDrawVisibleAtomics(owner->clump); p4_shadow_shear_set(0); p4_sec(13, 0);\n    p4_sec(11, 1); if (!p4shear) { mdlUpdateModelMatrix((Model*)owner);\n'),
    ('    {\n    u8* effect;\n    effect = owner->effect2CC;\n',
     '    } p4_sec(11, 0); p4_shadow_check(owner->hierarchy, &identity, 2); p4_fx: p4_sec(14, 1); {\n    u8* effect;\n    effect = owner->effect2CC;\n'),
    ('    {\n    struct { u16 index; u8* slot; MdlDrawState* model; } attached;\n',
     '    p4_sec(14, 0); if (p4skip) return; {\n    struct { u16 index; u8* slot; MdlDrawState* model; } attached;\n'),
]
# b198: the three material-group passes have an existing record==NULL no-draw path.
# Use it only in undrawn ticks. The OT start/end callbacks, including blend-track and
# optional model callbacks, still run. Tint is reconstructed from per3modelMatColor on
# every drawn tick, not accumulated from the previous material colour.
FIXES["src/Kosaka/k_clump/k_clump_inferred_00461560.c"] = [
    ('    record = (u8 *)((OtPrimitiveNode *)bucket)->primitive;\n',
     '    record = (u8 *)((OtPrimitiveNode *)bucket)->primitive;\n    { extern int p4_skip_visual_prep(int); if (p4_skip_visual_prep(1)) record = NULL; }\n'),
]
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('    RpClumpForAllAtomics(clump, mdlMatColorScaleCallback, &colorScaleContext);\n',
     '    { extern int p4_skip_visual_prep(int); if (!p4_skip_visual_prep(2))\n        RpClumpForAllAtomics(clump, mdlMatColorScaleCallback, &colorScaleContext); }\n'),
]
# b218 p4_head_trace: probes inside the head look-at (node 0x1389) of mdlUpdateHierarchyPose; rwsky.c prints them when
# the call leaves the look rotation non-finite (b215: one call turns a finite rotation into NaN, inputs finite).
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('                 RwMatrixRotate((struct RwMatrixTag*)rotationMatrix, (const RwV3d*)((u8 *)&axisMatrix), yaw + baseYaw, rwCOMBINEPOSTCONCAT);\n',
     '                 { extern void p4_look_probe(int, const float *, int); float p4v[10] = { pitch, basePitch, yaw, baseYaw, look[0], look[1], look[2], workingVector.x, workingVector.y, workingVector.z }; p4_look_probe(0, p4v, 10); }\n'
     '                 RwMatrixRotate((struct RwMatrixTag*)rotationMatrix, (const RwV3d*)((u8 *)&axisMatrix), yaw + baseYaw, rwCOMBINEPOSTCONCAT);\n'),
    ('              RtQuatConvertFromMatrix((struct RtQuat *)(&rotation.value[0]),(const RwMatrix *)(outputMatrix));\n            } else {\n              if ((controllerFlags & 0x80) != 0) {\n',
     '              RtQuatConvertFromMatrix((struct RtQuat *)(&rotation.value[0]),(const RwMatrix *)(outputMatrix));\n'
     '              { extern void p4_look_probe(int, const float *, int); p4_look_probe(1, rotation.value, 4); p4_look_probe(2, outputMatrix, 12); }\n'
     '            } else {\n              if ((controllerFlags & 0x80) != 0) {\n'),
    ('            /* Snapshot the complete starting controller quaternion. */\n',
     '            { extern void p4_look_probe(int, const float *, int); p4_look_probe(3, rotation.value, 4); }\n            /* Snapshot the complete starting controller quaternion. */\n'),
    ('            func_003dcc70((struct RtQuat *)(&startRotation.value[0]),(struct RtQuat *)(&rotation.value[0]),(struct RtQuatSlerpCache *)(&interpolation.from[0]));\n',
     '            func_003dcc70((struct RtQuat *)(&startRotation.value[0]),(struct RtQuat *)(&rotation.value[0]),(struct RtQuatSlerpCache *)(&interpolation.from[0]));\n'
     '            { extern void p4_look_probe(int, const float *, int); p4_look_probe(4, interpolation.from, 10); }\n'),
    ('            if (((*controller & 0x2000) != 0) && (limitRotation != 0)) {\n',
     '            { extern void p4_look_probe(int, const float *, int); float p4v[5] = { blendedRotation.value[0], blendedRotation.value[1], blendedRotation.value[2], blendedRotation.value[3], (float)limitRotation }; p4_look_probe(5, p4v, 5); }\n'
     '            if (((*controller & 0x2000) != 0) && (limitRotation != 0)) {\n'),
    ('              if (angleOrTime * 2.0f <= fGpffff8068 * ((MdlLookState *)controller)->yawLimit) {\n',
     '              { extern void p4_look_probe(int, const float *, int); float p4v[5] = { angleOrTime, fallbackRotation.value[0], fallbackRotation.value[1], fallbackRotation.value[2], fallbackRotation.value[3] }; p4_look_probe(6, p4v, 5);\n'
     '                p4_look_probe(7, &localMatrix.right.x, 12); }\n'
     '              if (angleOrTime * 2.0f <= fGpffff8068 * ((MdlLookState *)controller)->yawLimit) {\n'),
]
# b219: the headless party members (b155-b218). effMiscQuatAngle called __sqrt(x, 0.0f) with no declaration: gnu89 made
# it int __sqrt(double, double), so the length went out as a double in a0-a3 and came back from v0 through cvt.s.w,
# whatever v0 held. Divided by that, the head look-at's dot was NaN whenever v0 was 0, its acos NaN, and the look
# rotation NaN for good (mdlUpdateHierarchyPose keeps it). The port's __sqrt is float __sqrt(float), the EE's sqrt.s.
FIXES["src/Graphics/Effect/effMisc_inferred_004bd4a0.c"] = [
    ('    firstLength = __sqrt(firstLength, 0.0f);\n',
     '    { extern float __sqrt(float); firstLength = __sqrt(firstLength); }\n'),
    ('    secondLength = __sqrt(secondLength, 0.0f);\n',
     '    { extern float __sqrt(float); secondLength = __sqrt(secondLength); }\n'),
]
# b225: the battle panel frame (btlPanelDrawFrame FUN_0020B1A0, btlPanelDrawBackdropGrid FUN_0020B3A0). Once the panel
# has grown (scaleFrame >= 11, or <= 0) neither sets its scale: retail passes whatever its caller left in $f20. On the
# PSP that was NaN: the frame outline, its fill and the backdrop disc went to the GE with NaN corners and drew as long
# stripes across the attack target select. PCSX2 (ss_aim.p2s, Margaret's target select) shows no frame there; on the
# PSP any scale above 0 draws a rounded box over the target ring. p4_panel_scale (0) holds the value.
FIXES["src/Battle/btlEffect_inferred_00202c60.c"] = [
    ('    f32 progress;\n    f32 scale;\n',
     '    f32 progress;\n    extern float p4_panel_scale; f32 scale = p4_panel_scale;\n'),
    ('    s16 growthFrame;\n    f32 scale;\n\n    glyphs',
     '    s16 growthFrame;\n    extern float p4_panel_scale; f32 scale = p4_panel_scale;\n\n    glyphs'),
]
# b202 p4_anim_lazy (rwsky.c): the readers of an interpolator's frames. The layer blend and the rebase may be recorded
# instead of run (the port replays them through the p4_ bodies); look-at materialises and runs its own RtAnim calls
# without deferral.
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('u32 mdlBlendAnimInterpolators(RtAnimInterpolator* destination, RtAnimInterpolator* first,\n                  RtAnimInterpolator* second, f32 blend)\n{\n',
     'u32 p4_mdlBlendAnimInterpolators(RtAnimInterpolator* destination, RtAnimInterpolator* first, RtAnimInterpolator* second, f32 blend);\n'
     'u32 mdlBlendAnimInterpolators(RtAnimInterpolator* destination, RtAnimInterpolator* first, RtAnimInterpolator* second, f32 blend)\n'
     '{ extern int p4_anim_lazy_blend(void *, void *, void *, f32); if (p4_anim_lazy_blend(destination, first, second, blend)) return 1;\n'
     '  return p4_mdlBlendAnimInterpolators(destination, first, second, blend); }\n'
     'u32 p4_mdlBlendAnimInterpolators(RtAnimInterpolator* destination, RtAnimInterpolator* first,\n                  RtAnimInterpolator* second, f32 blend)\n{\n'),
    ('void mdlRebaseLayerPose(u8 *layer)\n{\n',
     'void p4_mdlRebaseLayerPose(u8 *layer);\n'
     'void mdlRebaseLayerPose(u8 *layer)\n'
     '{ extern int p4_anim_lazy_rebase(void *, void *, void *, void *, int, f32); MdlLayerState *p4l = (MdlLayerState *)layer;\n'
     '  if (p4_anim_lazy_rebase(layer, p4l->hierarchy ? (void *)((RpHAnimHierarchy *)p4l->hierarchy)->currentAnim : 0, (void *)p4l->rebaseInput,\n'
     '                          (void *)p4l->rebaseRotation, p4l->rebaseInput != 0 && (*(u16 *)layer & 0x80) != 0, iGpffff8040)) return;\n'
     '  p4_mdlRebaseLayerPose(layer); }\n'
     'void p4_mdlRebaseLayerPose(u8 *layer)\n{\n'),
    ('s32 mdlUpdateHierarchyPose(u8 *hierarchyData, u8 *layerState, u8 *controllerState, void *parentTransform)\n\n{\n    RwMatrix *parentMatrix;\n',
     'static s32 p4_mdlUpdateHierarchyPose(u8 *hierarchyData, u8 *layerState, u8 *controllerState, void *parentTransform);\n'
     's32 mdlUpdateHierarchyPose(u8 *hierarchyData, u8 *layerState, u8 *controllerState, void *parentTransform)\n'
     '{ extern void p4_anim_lazy_pause(void *, int); s32 p4r; void *p4cur = ((RpHAnimHierarchy *)hierarchyData)->currentAnim;\n'
     '  extern int p4_anim_freeze; if (p4_anim_freeze & 2) return 1;\n'
     '  extern void p4_look_check(void *, void *, void *, int);\n'
     '  p4_anim_lazy_pause(p4cur, 1); p4_look_check(hierarchyData, controllerState, parentTransform, 0);\n'
     '  p4r = p4_mdlUpdateHierarchyPose(hierarchyData, layerState, controllerState, parentTransform);\n'
     '  p4_look_check(hierarchyData, controllerState, parentTransform, 1); p4_anim_lazy_pause(p4cur, 0); return p4r; }\n'
     'static s32 p4_mdlUpdateHierarchyPose(u8 *hierarchyData, u8 *layerState, u8 *controllerState, void *parentTransform)\n\n{\n    RwMatrix *parentMatrix;\n'),
]
# b269: the parts of mdlAdvanceModel no section timed yet (Heaven b258: ~2.5 M cycles of qmodel outside every
# section): 16 ascale (mdlHasActiveMode + mdlCopyScale), 17 ahandle (the attachment handle switch), 18 achild (the
# child models: weapons through mdlCopyIndexedMatrix and a recursive mdlAdvanceModel; inclusive).
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('        if (mdlHasActiveMode(model) != 0) {\n            mdlCopyScale(model, &((Model*)model)->scale);\n        }\n',
     '        { extern void p4_sec(int, int); p4_sec(16, 1); }\n'
     '        if (mdlHasActiveMode(model) != 0) {\n            mdlCopyScale(model, &((Model*)model)->scale);\n        }\n'
     '        { extern void p4_sec(int, int); p4_sec(16, 0); }\n'),
    ('        for (switchLayer = 0; switchLayer < 2; switchLayer++) {\n',
     '        { extern void p4_sec(int, int); p4_sec(17, 1); }\n        for (switchLayer = 0; switchLayer < 2; switchLayer++) {\n'),
    ('        if (tick != 0) {\n            draw = ((Model *)model)->effects;\n',
     '        { extern void p4_sec(int, int); p4_sec(17, 0); }\n        if (tick != 0) {\n            draw = ((Model *)model)->effects;\n'),
    ('        for (child = 0; child < 5; child++) {\n',
     '        { extern void p4_sec(int, int); p4_sec(18, 1); }\n        for (child = 0; child < 5; child++) {\n'),
    ('                        mdlAdvanceModel(*updateSlot, tick);\n                    }\n                }\n            }\n        }\n    }\n}\n',
     '                        mdlAdvanceModel(*updateSlot, tick);\n                    }\n                }\n            }\n        }\n'
     '        { extern void p4_sec(int, int); p4_sec(18, 0); }\n    }\n}\n'),
]
# b287 (Shuffle Time, docs/shuffle_time_perf.md): the effect tree walks call each child's kind callbacks, +0x08 advance
# (mdlEffectCommitTreeFrame) and +0x0C draw (mdlEffectFlushTreeFrame). Timed into the hwot table keyed by callback
# address | 1 (advance) or | 2 (draw); inclusive, nested trees count again. p4_fx_skip_draw (port/rwsky.c) may leave
# the draw callback out in ticks frameskip does not draw.
_FX_CALL = "(((MdlEffectPrimitiveRecord *)primitive)->instance.handle);\n"
FIXES["src/Graphics/Model/mdlEffect_inferred_00484bb0.c"] = [
    ('        (*(void (**)(s32))(D_00713480 + (((MdlEffectPrimitiveRecord *)primitive)->kind << 6) + 0x08))' + _FX_CALL,
     '        { extern unsigned p4_ot_begin(void); extern void p4_ot_end(unsigned, unsigned);\n'
     '          void (*p4fn)(s32) = *(void (**)(s32))(D_00713480 + (((MdlEffectPrimitiveRecord *)primitive)->kind << 6) + 0x08);\n'
     '          unsigned p4t = p4_ot_begin(); p4fn' + _FX_CALL.rstrip('\n') + ' p4_ot_end(p4t, (unsigned)p4fn | 1); }\n'),
    ('        (*(void (**)(s32))(D_00713480 + (((MdlEffectPrimitiveRecord *)primitive)->kind << 6) + 0x0C))' + _FX_CALL,
     '        { extern unsigned p4_ot_begin(void); extern void p4_ot_end(unsigned, unsigned); extern int p4_fx_draw_skipped(void);\n'
     '          void (*p4fn)(s32) = *(void (**)(s32))(D_00713480 + (((MdlEffectPrimitiveRecord *)primitive)->kind << 6) + 0x0C);\n'
     '          if (!p4_fx_draw_skipped()) { unsigned p4t = p4_ot_begin(); p4fn' + _FX_CALL.rstrip('\n') + ' p4_ot_end(p4t, (unsigned)p4fn | 2); } }\n'),
]
# p4_fx_direct (port/fx_direct.h, 2026-11-15): effect atomics are marked where effGeometry.c makes them and unmarked when
# RenderWare destroys an atomic, so the port can draw them on their own path.
_FX_MARK = '{ extern void p4_fx_mark(void *); p4_fx_mark((void *)(%s)); }\n'
FIXES["src/Graphics/Effect/effGeometry.c"] = [
    ('    func_003c0210((RpAtomic *)atomic, (RpGeometry *)geometry, 0);\n',
     '    func_003c0210((RpAtomic *)atomic, (RpGeometry *)geometry, 0);\n    ' + _FX_MARK % 'atomic'),
    ('        func_003c0210(atomic, geometry, 0);\n',
     '        func_003c0210(atomic, geometry, 0);\n        ' + _FX_MARK % 'atomic'),
    ('    ((EffGeometryBlock *)block)->atomicData.signedValue = func_003a2340(resourceId & 0xFFFF, flags, 2);\n',
     '    ((EffGeometryBlock *)block)->atomicData.signedValue = func_003a2340(resourceId & 0xFFFF, flags, 2);\n    '
     + _FX_MARK % '((EffGeometryBlock *)block)->atomicData.signedValue'),
    ('    s32 resource = ((EffGeometryBlock *)block)->atomicData.signedValue;\n    if (resource != 0) {\n',
     '    s32 resource = ((EffGeometryBlock *)block)->atomicData.signedValue;\n'
     '    { extern void p4_fx_unmark(void *); p4_fx_unmark((void *)resource); }\n    if (resource != 0) {\n'),
]
# p4_shuffle_force_bonus (2026-11-15, port/rwsky.c): 1 = the Arcana Chance roll always passes once the game's own
# flags allow it, so the slow Arcana Chance screen of Shuffle Time can be measured on demand. 0: the retail roll.
FIXES["src/Battle/btlShuffleResult.c"] = [
    ('    if ((u8)chance < (u8)thresh) {\n',
     '    if ((u8)chance < (u8)thresh || ({ extern int p4_shuffle_force_bonus; p4_shuffle_force_bonus; })) {\n'),
]
FIXES["src/promoted/code1_003c.c"] = [
    ('    self = arg0;\n    func_003e3c20(D_0070AF70, self);\n',
     '    self = arg0;\n    { extern void p4_fx_unmark(void *); p4_fx_unmark(self); }\n    func_003e3c20(D_0070AF70, self);\n'),
]
# b344: this pair's framebuffer alpha is scratch for its immediately following noise draw.
# Build the reader UVs before drawing, so unsupported cases still use the untouched two-pass path.
FIXES["src/Graphics/Effect/effDistortParticle.c"] = [
    ('        u32 primitiveTable;\n',
     '        u32 primitiveTable;\n        DistortVertex p4_mask[4];\n'
     '        extern int p4_cloud_draw(const void *, const void *, void *, unsigned);\n'
     '        memcpy(p4_mask, quad, sizeof p4_mask);\n'),
    ('        ((s32 (**)(s32, DistortVertex *, s32))primitiveTable)[0](4, quad, 4);\n'
     '        mdlEffectEmitTagPair();\n'
     '        ((void (**)(u32, u32))setStateTable)[0](1, *(u32 *)instance->texture);\n'
     '        RpSkyRenderStateSet(2, savedTextureState | 0x10);\n', ''),
    ('        quad[3].v = D_0071458C[uv].coordinate;\n',
     '        quad[3].v = D_0071458C[uv].coordinate;\n'
     '        if (p4_cloud_draw(p4_mask, quad, *(void **)instance->texture, savedTextureState | 0x10)) {\n'
     '            mdlEffectEmitTagPair();\n'
     '            ((void (**)(u32, u32))setStateTable)[0](1, *(u32 *)instance->texture);\n'
     '            RpSkyRenderStateSet(2, savedTextureState | 0x10);\n'
     '        } else {\n'
     '            ((s32 (**)(s32, DistortVertex *, s32))primitiveTable)[0](4, p4_mask, 4);\n'
     '            mdlEffectEmitTagPair();\n'
     '            ((void (**)(u32, u32))setStateTable)[0](1, *(u32 *)instance->texture);\n'
     '            RpSkyRenderStateSet(2, savedTextureState | 0x10);\n'),
    ('        mdlEffectEmitSlotPacket();\n        break;\n', '        }\n        mdlEffectEmitSlotPacket();\n        break;\n'),
]
RETAIL_LITERAL = re.compile(r"(\*\)\s*(?:\([^()\n]*\)\s*\)\s*)?\(?)(0x0{0,2}[5-9][0-9A-Fa-f]{5})\b")  # data and callback pointer casts move with the image
TIMER_CONTROL = re.compile(r"\*\s*\(\s*(?:volatile\s+)?u32\s*\*\s*\)\s*0x100000[123]0")
TIMER0 = re.compile(r"\*\s*\(\s*(?:volatile\s+)?(?:u32|unsigned int|s32|int)\s*\*\s*\)\s*0x10000000")
ASM_STMT = re.compile(r"\b(?:__asm__|asm)\s*(?:volatile|__volatile__)?\s*\(")
EE_ONLY = re.compile(r"\$?\bvf\d|\$?\bvi\d|\$ACC|\$Q\b|\b(?:q?mtc2|q?mfc2|c[tf]c2|lqc2|sqc2|lq|sq|vwaitq|vu\w+|v[a-z]+\d?\.[xyzw]+|p[a-z]{3,6}\d?w?|ei|di|dsll32|dsrl32|prot3w|mt(?:sa|hi1|lo1)|mf(?:hi1|lo1)|sync\.[lp])\b")
COMPILE = "--compile" in sys.argv
PASSES = 20
FLAGS = ["-std=gnu89", "-w", "-ffreestanding", "-fno-builtin", "-fms-extensions", "-fmax-errors=0",
         "-Iinclude", "-Isrc", "-I.", f"-I{ROOT / 'port'}", "-DINCLUDE_ASM(F,N)=", "-DINCLUDE_RODATA(F,N)=", "-include", str(ROOT / "port" / "compat.h")]


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
        # Some setters do their only store in the return expression (fix from MeMeNT0xMoRii's fork).
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
                 "sflDataDrawByteValuePanel", "sflDataDrawNumberValue", "sflDataDrawTitledIcon", "sflDataDrawLevelBadge",
                 # Item cursor and Persona layout pass packed screen coordinates by value (MeMeNT0xMoRii's fork).
                 "radarDrawSpritePair", "shdPersonaDrawNamePlate", "shdPersonaDrawFramePair",
                 "shdPersonaDrawExpRow", "shdPersonaDrawArcanaBadge", "shdPersonaDrawLevelDigits",
                 "shdPersonaDrawPanelSprites", "radarDrawPalettePair", "radarDrawPaletteSet", "radarDrawItemCard",
                 # Social Stats draw calls too (P4-052, MeMeNT0xMoRii's fork).
                 "radarDrawNumber", "radarDrawSegment", "datGridEffectDrawStrip"}


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
        # RingUpdateLineSegment (0x00499a30): the same pack with colourStack at sp + 0x50.
        ("sw %1, 0x54($sp)", "sw %1, 4(%3)"),
        ("addiu $2, $sp, 0x54", "addiu $2, %3, 4"),
        ("sw $3, 0x50($sp)", "sw $3, 0(%3)"),
        ('"r"(baseColor), "r"(sampledColor), "f"(scale)', '"r"(baseColor), "r"(sampledColor), "f"(scale), "r"(colourStack)'),
    ],
    # mdlEffectResolveChildOffset (0x00486840): $f0 only carries a zero or the Q bits; the Q length goes to values[],
    # retail's sp + 64. The .word is vsqrt Q, vf2x.
    "src/Graphics/Model/mdlEffect_inferred_00486780.c": [
        ("mtc1 $zero, $f0", "nop"),
        ("mfc1 %0, $f0", "or %0, $zero, $zero"),
        ("mfc1 $2, $f0", "or $2, $zero, $zero"),
        (".word 0x4a0203bd", "vsqrt $Q, $vf2x"),
        ("mtc1 $2, $f0", "nop"),
        ("sw $2, 64($sp)", "sw $2, 0(%0)"),
        ("sw $2, 68($sp)", "sw $2, 4(%0)"),
        ("sw $2, 72($sp)", "sw $2, 8(%0)"),
        ('            :\n            : "$2", "$f0", "$vf2", "$vf10", "Q", "memory");',
         '            : "r"(values)\n            : "$2", "$f0", "$vf2", "$vf10", "Q", "memory");'),
    ],
    # mdlEffectMultiplyPaletteColorVU (0x004ae2f0): the 1/255 scale is the small-data float at gp - 0x7e0c.
    "src/Graphics/Model/mdlEffect_inferred_004ae2f0.c": [
        ("lwc1 $f0, -0x7e0c($gp)", "nop"),
        ("mfc1 $2, $f0", "lw $2, fGpffff81f4"),
        ("mfc1 $3, $f0", "lw $3, fGpffff81f4"),
        ("lw $3, -0x7e0c($gp)", "lw $3, fGpffff81f4"),
    ],
    # mdlEffectCatmullRomVU: work[] is the frame at sp (tangents at 0x50/0x60), points arrives in a0.
    # enclosing_arguments cannot name `u8 (*points)[16]`, so both become operands.
    "src/Graphics/Model/mdlEffect.c": [
        ('0x0($a1)            \\n"\n      ".set reorder"\n      :\n      :',
         '0x0($a1)            \\n"\n      ".set reorder"\n      :\n      : "r"(work), "r"(points)'),
        ('$vf12        \\n"\n      ".set reorder"\n      :\n      :',
         '$vf12        \\n"\n      ".set reorder"\n      :\n      : "r"(work), "r"(points)'),
        ("0x0($a2)", "0x60(%0)"),
        ("0x0($a1)", "0x50(%0)"),
        ("($a0)", "(%1)"),
        ("$sp", "%0"),
    ],
}


def native_ui(text, rel):
    """Retain the stock body as fallback; compact variants share its P4 calculations."""
    names = {"src/frFontRaster.c": "frFontRasterDrawGlyph", "src/Kernel/sdkSpr.c": "sdkSpriteDraw"}
    if rel not in names:
        return text
    name = names[rel]
    match = re.search(rf"^(?:s32|void) {name}\([^;{{}}]*\)\s*\{{", text, re.M)
    assert match, (rel, name)
    end = text.index('\n}', match.end()) + 2
    body = text[match.start():end]
    clone = 'static ' + body.replace(name + '(', 'p4_native_' + name + '(', 1)
    if name == 'frFontRasterDrawGlyph':
        a, b = clone.index('    originX ='), clone.index('p4_font_cache_:')
        geometry = clone[a:b].replace('    opacity = rgba & 0xFF;\n', '')
        geometry = geometry.replace('    reciprocal = 1.0f / ((RwCamera *)(u32)kwlnGetCamera())->nearPlane;\n', '')
        clone = clone[:a] + '    opacity = rgba & 0xFF;\n' + clone[b:]
        clone = clone.replace('    if (p4_font_skip_) goto p4_font_cache_;\n', '')
        clone = clone.replace('f32 vertices[64];', 'P4UiVertex vertices[4];')
        clone = clone.replace('    memset(vertices, 0, 0x100);', geometry)
        a, b = clone.index('    while (vertexIndex < 4)'), clone.index('p4_font_bind_:')
        clone = clone[:a] + '''    while (vertexIndex < 4) {
        P4UiVertex *v = &vertices[vertexIndex];
        v->u.els.screen.x = corners[vertexIndex * 2];
        v->u.els.screen.y = corners[vertexIndex * 2 + 1];
        v->u.els.screen.z = depthZ;
        v->u.els.u = texture.coordinates[vertexIndex * 2];
        v->u.els.v = texture.coordinates[vertexIndex * 2 + 1];
        p4_ui_colour(v, redShifted, greenShifted, blueShifted, opacity);
        vertexIndex++;
    }
''' + clone[b:]
        clone = clone.replace('D_00887310[0](4, vertices, 4);', 'p4_ui_quad_draw(vertices);')
        dispatch = ('\n    if (p4_ui_font) return p4_native_frFontRasterDrawGlyph('
                    'x, y, depth, glyph, palette, width, height, rgba);\n')
    else:
        initial = body[body.index('    if (setStates != 0)'):body.index('    rasterIndex =')]
        sky = body[body.index('    if (setStates != 0)', body.index('    rasterIndex =')):body.index('    /* With a null initial raster')]
        a = body.index('    if ((((SdkSpriteRecord *)sdkSpriteTableRecord(sample))->flags & 8) == 0)')
        bind = body[a:body.index('    render =', a)]
        state_only = '''void p4_calendar_sprite_state(void *pack, unsigned record)
{
    SdkSpriteSample temporary = {0};
    u8 *sample = (u8 *)&temporary;
    s32 setStates = 1, rasterIndex;
    s32 (**states)(RwRenderState, void *);
    temporary.packAddress = (u32)pack;
    temporary.recordIndex = record;
''' + initial + '    rasterIndex = ((SdkSpriteRecord *)sdkSpriteIndexedRecord(sample))->rasterIndex;\n' + sky + bind + '}\n\n'
        clone = clone.replace('SdkSpriteVertex vertices[4];', 'P4UiVertex vertices[4];')
        clone = clone.replace('SdkSpriteVertex *vertex =', 'P4UiVertex *vertex =')
        clone = clone.replace('    reciprocalZ = 1.0f / ((RwCamera *)(u32)kwlnGetCamera())->nearPlane;\n', '')
        clone = clone.replace('            vertex->u.els.reciprocalZ = reciprocalZ;\n', '')
        clone = clone.replace('sdkSpriteVertexSetColor(', 'p4_ui_colour(')
        clone = clone.replace('sdkSpritePositionQuad(', 'p4_ui_position(')
        clone = clone.replace('render[0](4, vertices, 4);', 'p4_ui_quad_draw(vertices);')
        dispatch = ("\n    if (p4_ui_sprite && SDK_SPRITE_RASTERS(sample)["
                    "((SdkSpriteRecord *)sdkSpriteIndexedRecord(sample))->rasterIndex]) {\n"
                    "        p4_native_sdkSpriteDraw(sample, setStates); return;\n    }\n")
    header = f'#include "{(ROOT / "port/ui_quad.h").as_posix()}"\n'
    original = body[:match.end() - match.start()] + dispatch + body[match.end() - match.start():]
    return header + text[:match.start()] + (state_only if name == 'sdkSpriteDraw' else '') + clone + '\n\n' + original + text[end:]


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
    return native_ui(text, rel)


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
