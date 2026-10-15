"""The decals' draw order: batches by blend function and bitmap, every overlapping pair in its order."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure

CASES = ["scattered-bullet-holes", "stacked-bullet-holes", "rounding", "multiply-then-add", "same-function-commutes",
         "alpha-tested", "behind-camera", "invalid-projection", "full", "bounded", "random"]
CONTROLS = {
    "individual-overlap-ignored": (("rasterizer_decal_rectangles_overlap(&decal_order_entries[entry_index].bounds,\n\t\t\t\t\t&decal_order_entries[other_index].bounds))",
                                    "FALSE)"), "stacked-bullet-holes"),
    "function-ignored": (("if (commutes && decal_order_batches[batch_index].framebuffer_blend_function == framebuffer_blend_function)",
                          "if (commutes)"), "multiply-then-add"),
    "alpha-tested-commutes": (("local_layer != _decal_layer_alpha_tested &&\n\t\t", ""), "alpha-tested"),
    "no-margin": (("#define DECAL_ORDER_SCREEN_MARGIN (1.0f / 64.0f)", "#define DECAL_ORDER_SCREEN_MARGIN 0.0f"), "rounding"),
    "behind-ignored": ((" || !(w > DECAL_ORDER_MINIMUM_W)", " || w == 0.0f"), "behind-camera"),
    "projection-validity-ignored": (("!global_window_parameters.frustum.projection_valid || ", ""), "invalid-projection"),
    "bitmap-ignored": (("decal_order_batches[batch_index].bitmap_index == bitmap_index", "TRUE"), "scattered-bullet-holes"),
    "unbounded": (("if (++tests > DECAL_ORDER_MAXIMUM_TESTS)\n", "if (FALSE && ++tests > DECAL_ORDER_MAXIMUM_TESTS)\n"), "bounded"),
    "flushed-early": (("if (decal_order_entry_count == DECAL_ORDER_MAXIMUM_DECALS)",
                       "if (decal_order_entry_count == DECAL_ORDER_MAXIMUM_DECALS / 2)"), "full"),
}


def generated(control=None):
    decals = read("source/rasterizer/xbox/rasterizer_xbox_decals.c")
    math = read("source/math/real_math.h")
    types = "".join(re.search(r"^#define %s .*\n" % name, math, re.M).group(0) for name in ["REAL_MIN", "REAL_MAX"])
    types += re.search(r"^union real_rectangle2d\n\{.*?^\};\n", math, re.M | re.S).group(0)
    types += "typedef union real_rectangle2d real_rectangle2d;\n"
    types += structure(math, "real_matrix4x3") + "\ntypedef struct real_matrix4x3 real_matrix4x3;\n"
    for member in ["_decal_layer_alpha_tested", "_shader_framebuffer_blend_function_double_multiply", "MAXIMUM_DECALS_PER_MAP"]:
        types += enum_with(decals, member) + "\n"
    types += structure(decals, "decal_vertex") + "\n"
    start = decals.index("#define DECAL_ORDER_MAXIMUM_DECALS")
    end = decals.index("static void rasterizer_decals_draw_ordered(void);")
    code = decals[start:end] + "static void rasterizer_decals_draw_ordered(void);\n"
    for name in ["rasterizer_decal_blend_function_commutes", "rasterizer_decal_screen_bounds",
                 "rasterizer_decal_rectangles_overlap", "rasterizer_decal_order_add"]:
        code += "\n" + function(decals, name)
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("decal_order", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("decal_order", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
