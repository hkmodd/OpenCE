"""The scissor of a shadow's projection holds every pixel the projection changes, against a model of the projection.

    python tools/harness/tests/test_shadow_scissor.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["exact", "cuts", "inside", "fade", "states"]

# a fault in the scissor, and the case that must catch it
NEGATIVE_CONTROLS = {
    "rows-from-the-bottom": (("(1.0f - y) * 0.5f", "(y + 1.0f) * 0.5f"), "exact"),
    "short-slab": (("radius * 4.2f", "radius * 3.5f"), "exact"),
    "slab-below": (("radius * -0.525f", "radius * -0.4f"), "exact"),
    "narrow-prism": (("radius * 1.05f", "radius * 0.95f"), "exact"),
    "ignores-the-fade": (("!shadow_projection_fade_saturated()", "FALSE"), "fade"),
    "ignores-the-border": (("D3D__TextureState[0][D3DTSS_BORDERCOLOR] != 0 ||", ""), "states"),
    "cuts-in-front-only": (("if (!(w > 1e-3f))\n\t\t\treturn;", "if (!(w > 1e-3f))\n\t\t\tcontinue;"), "inside"),
}


def under_test(fault=None):
    gl = read("port/linux/src/d3d8_gl.c")
    gl = gl[gl.index("/* a draw's volume (halo_scissor_volume)"):gl.index("static void apply_raster_state(BOOL has_depth)")]
    projection = read("port/linux/game/shadow_projection.c")
    projection = projection[projection.rindex("#include"):].split("\n", 1)[1]
    text = gl + "\n" + projection + "\n"
    return (("under_test.inc", mutated(text, *fault) if fault else text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("shadow_scissor", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("shadow_scissor", under_test(fault)), case)
    assert status == CHECK_FAILED, f"the scissor with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
