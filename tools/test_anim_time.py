"""Check that deferred animation time cannot leak across SetCurrentAnim."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class AnimationTime(unittest.TestCase):
    def test_set_current_anim_discards_only_the_matching_pending_time(self):
        cc = shutil.which("gcc")
        if not cc:
            self.skipTest("host gcc is unavailable")
        renderer = (Path(__file__).resolve().parents[1] / "port/rwsky.c").read_text()
        functions = [
            re.search(rf"^[^\n]*\b{name}\([^\n]*\)\s*(?:/\*[^\n]*\*/)?\n\{{.*?^\}}",
                      renderer, re.M | re.S)[0]
            for name in ("anim_by_interp", "func_003d5840")
        ]
        fixture = r"""
#include <assert.h>
"""
        state, = re.findall(r"^typedef struct .* AnimState;$", renderer, re.M)
        fixture += state + r"""
enum { ANIM_STATES = 4 };
static AnimState anim_state[ANIM_STATES];
static const void *anim_interp_of[ANIM_STATES];
static int invalidation, retail_calls;
static void *last_animation;

static void lazy_obsolete_now(void *interpolator)
{
    assert(interpolator);
    invalidation++;
}

static int p4r_003d5840(void *interpolator, void *animation)
{
    assert(invalidation == retail_calls + 1);
    if (interpolator == anim_state[1].interp)
        assert(anim_state[1].pending == 0);
    last_animation = animation;
    return ++retail_calls;
}
"""
        fixture += "\n".join(functions) + r"""
int main(void)
{
    int hierarchy, other_hierarchy, interpolator, unrelated, new_clip, old_clip;
    anim_state[0] = (AnimState){.h = &other_hierarchy, .interp = &unrelated, .pending = 0.75f};
    anim_interp_of[0] = &interpolator; /* stale reverse mapping */
    anim_state[1] = (AnimState){.h = &hierarchy, .interp = &interpolator, .pending = 0.25f};
    anim_interp_of[1] = &interpolator;

    last_animation = &old_clip;
    assert(func_003d5840(&interpolator, &new_clip) == 1);
    assert(last_animation == &new_clip && anim_state[1].pending == 0);
    assert(anim_state[0].pending == 0.75f);

    anim_state[1].pending = 0.125f;
    assert(func_003d5840(&unrelated, &old_clip) == 2);
    assert(anim_state[1].pending == 0.125f);

    last_animation = &new_clip;
    assert(func_003d5840(&interpolator, &new_clip) == 3); /* restart the same clip */
    assert(last_animation == &new_clip && anim_state[1].pending == 0);
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "check.c"
            executable = source.with_suffix("")
            source.write_text(fixture)
            subprocess.run([cc, "-O2", str(source), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
