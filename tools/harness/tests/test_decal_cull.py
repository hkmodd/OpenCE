"""The decals off the screen: the same states in the same order, and only decals that cover no pixel left out."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, read, run, structure

CASES = ["fight", "edges", "behind-camera", "invalid-projection"]
# (the reference: the same code drawing every decal's quads, on the screen or not)
EVERY_DECAL = ("if (vertex_data && decal_draw_on_screen[decal_order_index])", "if (vertex_data)")
# each control: (before, after), the case it is caught in, and whether the reference has the fault too (one that
# changes the order of the decals, which the reference must share for the comparison to be of the culling alone)
CONTROLS = {
    "no-margin": (("#define DECAL_ORDER_SCREEN_MARGIN (1.0f / 64.0f)", "#define DECAL_ORDER_SCREEN_MARGIN 0.0f"),
                  "edges", True),
    "behind-ignored": ((" || !(w > DECAL_ORDER_MINIMUM_W)", " || w == 0.0f"), "behind-camera", True),
    "projection-validity-ignored": (("!global_window_parameters.frustum.projection_valid || ", ""),
                                    "invalid-projection", True),
    "wrong-edge": (("bounds->x1 >= -1.0f", "bounds->x0 >= -1.0f"), "edges", False),
    "misaligned": (("decal_draw_on_screen[decal_order_index]", "decal_draw_on_screen[decal_order_index + 1]"),
                   "fight", False),
    "states-skipped": (("\t\tstruct decal_datum *decal = DECAL_GET(decal_index = decal_draw_order[decal_order_index]);",
                        "\t\tstruct decal_datum *decal;\n\t\tif (!decal_draw_on_screen[decal_order_index])\n\t\t\tcontinue;\n"
                        "\t\tdecal = DECAL_GET(decal_index = decal_draw_order[decal_order_index]);"), "fight", False),
    "none-left-out": (EVERY_DECAL, "fight", False),
}


def generated(*faults):
    decals = read("source/rasterizer/xbox/rasterizer_xbox_decals.c")
    math = read("source/math/real_math.h")
    types = "".join(re.search(r"^#define %s .*\n" % name, math, re.M).group(0) for name in ["REAL_MIN", "REAL_MAX"])
    types += re.search(r"^union real_rectangle2d\n\{.*?^\};\n", math, re.M | re.S).group(0)
    types += "typedef union real_rectangle2d real_rectangle2d;\n"
    types += structure(math, "real_matrix4x3") + "\ntypedef struct real_matrix4x3 real_matrix4x3;\n"
    for member in ["_decal_layer_alpha_tested", "_shader_framebuffer_blend_function_double_multiply", "MAXIMUM_DECALS_PER_MAP"]:
        types += enum_with(decals, member) + "\n"
    types += structure(decals, "decal_vertex") + "\n"
    start = decals.index("#define DECAL_BATCH_MAXIMUM_DECALS")
    end = decals.index("static void rasterizer_decals_draw_ordered(void);")
    code = decals[start:end] + "static void rasterizer_decals_draw_ordered(void);\n"
    for name in ["rasterizer_decal_blend_function_commutes", "rasterizer_decal_screen_bounds", "rasterizer_decal_on_screen",
                 "rasterizer_decal_rectangles_overlap", "rasterizer_decal_order_add", "rasterizer_decals_draw_ordered"]:
        code += "\n" + function(decals, name)
    for before, after in faults:
        code = mutated(code, before, after)
    return (("types.inc", types), ("under_test.inc", code))


def problems(output, reference, case):
    """What the drawing does other than the reference does with only decals that cover no pixel left out."""
    found = []
    off = {line.split()[1] for line in output.splitlines() if line.startswith("off ")}

    def segments(text):
        """The states set, and the decals drawn after each (and before the first)."""
        states, drawn = [], [[]]
        for line in text.splitlines():
            if line.startswith(("on ", "off ")):
                continue
            if line.startswith("decal "):
                drawn[-1].append(line)
            else:
                states.append(line)
                drawn.append([])
        return states, drawn

    states, drawn = segments(output)
    reference_states, reference_drawn = segments(reference)
    if states != reference_states:
        return ["the states set differ"]
    left_out = 0
    for decals, reference_decals in zip(drawn, reference_drawn):
        # (the decals drawn between two states are the reference's, in its order, with some left out)
        index = 0
        for line in reference_decals:
            if index < len(decals) and decals[index] == line:
                index += 1
                continue
            left_out += 1
            if line.split()[1] not in off:
                found.append(f"{line.split()[1]} left out although it may cover a pixel")
        if index < len(decals):
            found.append(f"{decals[index]} drawn out of the reference's order")
    if case in ("fight", "edges") and not left_out:
        found.append("no decal left out")
    return found[:10]


def outputs(case, *faults, reference_faults=()):
    status, output = run(build("decal_cull", generated(*faults)), case)
    assert status == 0, output
    status, reference = run(build("decal_cull", generated(*reference_faults, EVERY_DECAL)), case)
    assert status == 0, reference
    return output, reference


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    output, reference = outputs(case)
    assert problems(output, reference, case) == []


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    fault, case, shared = CONTROLS[control]
    output, reference = outputs(case, fault, reference_faults=(fault,) if shared else ())
    assert problems(output, reference, case), f"{control} escaped its check"
