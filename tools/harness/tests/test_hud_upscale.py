"""The high-res HUD's upscale of a map's sheet gives, texel for texel, what the upscale written texel by texel gives.

    python -m pytest -q tools/harness/tests/test_hud_upscale.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["sheets", "random", "refused"]

# the code's two paths: with SSE2 (the desktop builds), and without (Android's and the web's)
PATHS = {"sse2": None, "scalar": ("#ifdef __SSE2__\n\t\t{", "#if 0\n\t\t{")}

# a fault, the path it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "texels-not-swapped": (("to[0] = texel[2];", "to[0] = texel[0];"), "sse2", "sheets"),
    "edge-not-repeated": (("px >= (long)width ? width - 1 : px", "px >= (long)width ? 0 : px"), "sse2", "sheets"),
    "row-not-clamped": (("if (py >= (long)height) py = height - 1;\n\t\t\tfrom",
                         "if (py >= (long)height) py = 0;\n\t\t\tfrom"), "sse2", "sheets"),
    "wrong-phase-weights": (("_mm_mul_ps(right_taps, weights[2][column])", "_mm_mul_ps(right_taps, weights[1][column])"),
                            "sse2", "sheets"),
    "not-rounded": (("half = _mm_set1_ps(0.5f)", "half = _mm_set1_ps(0.0f)"), "sse2", "random"),
    "scalar-wrong-taps": (("(x & 2 ? 1 : 0)", "(x & 1 ? 1 : 0)"), "scalar", "sheets"),
    "scalar-not-clamped": (("value[channel] > 255 ? 255 :", "value[channel] > 255 ? 0 :"), "scalar", "sheets"),
}


def under_test(path, fault=None):
    source = read("port/linux/src/hud_hires.c")
    text = function(source, "upscale_weights") + "\n\n" + function(source, "hud_hires_upscale_pixels") + "\n"
    if fault:
        text = mutated(text, *fault)
    if PATHS[path]:
        text = mutated(text, *PATHS[path])
    return (("under_test.inc", text),)


@pytest.mark.parametrize("path", PATHS)
@pytest.mark.parametrize("case", CASES)
def test_case(case, path):
    status, output = run(build("hud_upscale", under_test(path)), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, path, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("hud_upscale", under_test(path, fault)), case)
    assert status == CHECK_FAILED, f"the upscale with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
