"""A texture animation's turn gives the transforms its angle's own sine and cosine give, with them remembered."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["repeated", "crowd", "transforms"]

# a fault in the memo, and the case that must catch it
NEGATIVE_CONTROLS = {
    "ignores-the-angle": (("if (entry->angle != angle)", "if (!entry->angle)"), "repeated"),
    "keeps-a-stale-sine": (("entry->sine_value = sine(radians);", "if (!entry->sine_value) entry->sine_value = sine(radians);"),
                           "crowd"),
}


def under_test(fault=None):
    shaders = read("source/shaders/shaders.c")
    memo = shaders[shaders.index("/* port: the sine and cosine of the angles"):shaders.index("void shader_texture_animation_evaluate(")]
    real_math = read("source/math/real_math.h")
    text = re.search(r"#define DEGREES_TO_RADIANS\(.*", real_math).group(0) + "\n"
    text += "#ifndef M_PI\n#define M_PI 3.14159265358979323846\n#endif\n"
    text += (mutated(memo, *fault) if fault else memo) + "\n"
    text += function(shaders, "shader_texture_animation_evaluate") + "\n"
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("texture_rotation", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("texture_rotation", under_test(fault)), case)
    assert status == CHECK_FAILED, f"the memo with a fault ({control}) passed '{case}': the test cannot see it"
