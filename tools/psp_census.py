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
        ("x = ((FrFontTextRun *)node)->x;", "x = (((FrFontTextRun *)node)->x >> 4) * 21;"),
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
# mdlEffectResolveChildOffset loads values[] as a full quadword: the fourth word was a stray stack slot.
FIXES["src/Graphics/Model/mdlEffect_inferred_00486780.c"] = [("u32 values[3];", "u32 values[4] = {0};")]
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
     '                { extern void p4_sec(int, int); p4_sec(2, 1); }\n'
     '                { MdlLayerState *p4l = (MdlLayerState *)(model + animationLayer * 0xA4 + 0xEC); extern void p4_anim_lazy_scope(void *, void *);\n'
     '                  p4_anim_lazy_scope(p4l->hierarchy ? (void *)((RpHAnimHierarchy *)p4l->hierarchy)->currentAnim : 0, (void *)p4l->rebaseInput); }\n'
     '                previousLayer = mdlAdvanceLayer(model + animationLayer * 0xA4 + 0xEC,\n                                             previousLayer, tick);\n'
     '                { extern void p4_anim_lazy_scope(void *, void *); p4_anim_lazy_scope(0, 0); }\n'
     '                { extern void p4_sec(int, int); p4_sec(2, 0); }\n'),
    ('        mdlAdvanceAnimControl(model + 0x23C, model + 0xEC);\n',
     '        { extern void p4_sec(int, int); p4_sec(3, 1); }\n        { extern void p4_anim_lazy_materials(int); p4_anim_lazy_materials(1); }\n'
     '        mdlAdvanceAnimControl(model + 0x23C, model + 0xEC);\n'
     '        { extern void p4_anim_lazy_materials(int); p4_anim_lazy_materials(0); }\n        { extern void p4_sec(int, int); p4_sec(3, 0); }\n'),
    ('                mdlEffectListBuildTransforms(*(u8** *)&((Model *)model)->effects, model);\n',
     '                { extern void p4_sec(int, int); p4_sec(4, 1); }\n                mdlEffectListBuildTransforms(*(u8** *)&((Model *)model)->effects, model);\n                { extern void p4_sec(int, int); p4_sec(4, 0); }\n'),
    ('        mdlTickMotionState(model + 0x2D0);\n',
     '        { extern void p4_sec(int, int); p4_sec(5, 1); }\n        mdlTickMotionState(model + 0x2D0);\n        { extern void p4_sec(int, int); p4_sec(5, 0); }\n'),
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
     '  extern void p4_look_check(void *, void *, void *, int);\n'
     '  p4_anim_lazy_pause(p4cur, 1); p4_look_check(hierarchyData, controllerState, parentTransform, 0);\n'
     '  p4r = p4_mdlUpdateHierarchyPose(hierarchyData, layerState, controllerState, parentTransform);\n'
     '  p4_look_check(hierarchyData, controllerState, parentTransform, 1); p4_anim_lazy_pause(p4cur, 0); return p4r; }\n'
     'static s32 p4_mdlUpdateHierarchyPose(u8 *hierarchyData, u8 *layerState, u8 *controllerState, void *parentTransform)\n\n{\n    RwMatrix *parentMatrix;\n'),
]
# b237: consume ordinary-node runs in a small native loop. The original head case and
# directional-blend tail still run exactly once, with the same parent-stack pointers.
FIXES["src/Graphics/Model/mdlManager.c"] += [
    ('    for (nodeIndex = 0; nodeIndex < hierarchy->numNodes; nodeIndex = nodeIndex + 1) {\n      if (func_003954b0 == applyKeyFrame) {\n',
     '    for (nodeIndex = 0; nodeIndex < hierarchy->numNodes; nodeIndex = nodeIndex + 1) {\n'
     '      { extern int p4_pose_runs; extern int p4_pose_run(void *, int, void *, void *, void *, void *, void *, void *, void (*)(void *, void *));\n'
     '        if (p4_pose_runs) {\n'
     '          int p4n = p4_pose_run(hierarchy, nodeIndex, (void *)keyFrameAddress, outputMatrix, (void *)nodeInfoAddress, &nodeParentMatrix, &parentStackTop, &baseMatrix, applyKeyFrame);\n'
     '          nodeIndex += p4n; keyFrameAddress += p4n * frameOrStride; outputMatrix += p4n * 16; nodeInfoAddress += p4n * 16;\n'
     '          if (nodeIndex == hierarchy->numNodes) break;\n'
     '        } }\n'
     '      if (func_003954b0 == applyKeyFrame) {\n'),
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
    text = QUAD_SETTERS.sub(r"void\1", text)
    out, pos = [], 0
    for m in QUAD_SETTER_BODY.finditer(text):
        depth, i = 1, m.end()
        while depth:
            depth += {"{": 1, "}": -1}.get(text[i], 0)
            i += 1
        out.append(text[pos:m.end()] + re.sub(r"\breturn\s+[^;]+;", "return;", text[m.end():i]))
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
