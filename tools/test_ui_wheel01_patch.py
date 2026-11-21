#!/usr/bin/env python3
"""Host-only guard for the opt-in UI-WHEEL-01 source overlay (no PSPDEV needed)."""
import ast
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = ROOT / "tools" / "psp_census.py"
ORIGINAL = ROOT / "ext" / "Persona4-Decompilation" / "src" / "Battle" / "btlEffect_inferred_00202c60.c"
SIGNATURE = "void btlSelectionDrawCommands(u8 *selection, s32 workAddress, u8 *drawState)\\n{"
OLD_LINES = (
    "circle.center.x = 96.0f + translationX;",
    "circle.center.y = 318.0f + offsetY;",
    "shdDrawRadialFan(position, 150.0f, color.word, color.word, outlineGrowth * 81.0f, 0.0f, 0x30, 1.0f, 1.0f, 1);",
    "shdDrawRadialFan(position, 150.0f, color.word, color.word, radius, 0.0f, 0x30, 1.0f, 1.0f, 1);",
    "shdDrawRadialFan(position, 150.0f, color.word, color.word, radius + 1.0f, 0.0f, 0x30, 1.0f, 1.0f, 1);",
)
NEW_LINES = (
    "circle.center.x = 95.3333333f + translationX;",
    "circle.center.y = 306.3529412f + offsetY;",
    "shdDrawRadialFan(position, 150.0f, color.word, color.word, outlineGrowth * 81.0f, 0.0f, 0x30, 0.94871795f, 1.17194570f, 1);",
    "shdDrawRadialFan(position, 150.0f, color.word, color.word, radius, 0.0f, 0x30, 0.94871795f, 1.17194570f, 1);",
    "shdDrawRadialFan(position, 150.0f, color.word, color.word, radius + 1.0f, 0.0f, 0x30, 0.94871795f, 1.17194570f, 1);",
)


def load_patch():
    tree = ast.parse(TOOL.read_text(encoding="utf-8"), filename=str(TOOL))
    funcs = [node for node in tree.body if isinstance(node, ast.FunctionDef)
             and node.name == "p4_wheel01_patch"]
    assert len(funcs) == 1, "Expected exactly one wheel patch helper"
    namespace = {}
    module = ast.Module(body=funcs, type_ignores=[])
    exec(compile(module, str(TOOL), "exec"), namespace)
    return namespace["p4_wheel01_patch"]


def test_fixture(patch):
    # The synthetic source has a second, unrelated region to verify scope.
    body = SIGNATURE + "\\n    " + "\\n    ".join(OLD_LINES) + "\\n}\\n"
    source = "/* before */\\n" + body + "\\n/* after */\\n"
    result = patch(source)
    expected = source
    for old, new in zip(OLD_LINES, NEW_LINES):
        assert expected.count(old) == 1, old
        expected = expected.replace(old, new, 1)
    assert result == expected, "Unexpected changes outside the five wheel expressions"
    assert patch(source) == result, "Patch not deterministic"
    for broken in (source.replace(SIGNATURE, "void wrong(void)\\n{", 1),
                   source + body,
                   source.replace(OLD_LINES[0], "", 1),
                   source.replace(OLD_LINES[0], OLD_LINES[0] + "\\n    " + OLD_LINES[0], 1)):
        try:
            patch(broken)
        except RuntimeError:
            pass
        else:
            raise AssertionError("A malformed or ambiguous source was accepted")
    print("HOST_FIXTURE_PASS: scoped expressions and 4 negative cases")


def test_original_if_available(patch):
    if not ORIGINAL.is_file():
        print("ORIGINAL_SOURCE_NOT_AVAILABLE: pinned decomp checkout absent, not a failure")
        return
    source = ORIGINAL.read_text(encoding="utf-8")
    result = patch(source)
    assert source.count(SIGNATURE) == 1
    assert result.count(SIGNATURE) == 1
    for old, new in zip(OLD_LINES, NEW_LINES):
        assert source.count(old) == 1, old
        assert result.count(new) == 1, new
        assert result.count(old) == 0, old
    # The five expression substitutions must account for the entire change.
    expected = source
    for old, new in zip(OLD_LINES, NEW_LINES):
        expected = expected.replace(old, new, 1)
    assert result == expected, "Source changed beyond intended call sites"
    print("ORIGINAL_SOURCE_EXACT_PASS: five scoped substitutions")


if __name__ == "__main__":
    patch = load_patch()
    test_fixture(patch)
    test_original_if_available(patch)
