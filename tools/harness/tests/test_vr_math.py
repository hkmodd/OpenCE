"""The headset's poses and fields of view in the game's terms (port/linux/game/vr_math.c): an eye's field through the
game's own render_camera_build_frustum, and OpenXR's LOCAL poses in the world's axes."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure

CASES = ["bounds", "pose", "helpers", "between"]
CONTROLS = {
    "bottom-is-top": (("bounds[2] = (float)tan(fov->down);", "bounds[2] = (float)tan(fov->up);"), "bounds"),
    "no-aspect": (("bounds[0] = (float)tan(fov->left) / aspect;", "bounds[0] = (float)tan(fov->left);"), "bounds"),
    "left-is-right": (("left = -local[0]", "left = local[0]"), "pose"),
    "heading-turned-back": (("out[1] = forward * sine + left * cosine;", "out[1] = -forward * sine + left * cosine;"),
                            "pose"),
    "metres-as-units": (("turned[axis] * VR_UNITS_PER_METRE;", "turned[axis];"), "pose"),
    "multiply-swapped": (("float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];",
                          "float x = a[3] * b[0] + a[0] * b[3] - a[1] * b[2] + a[2] * b[1];"), "helpers"),
    "world-to-local-unturned": (("float forward = world[0] * cosine + world[1] * sine,",
                                 "float forward = world[0] * cosine - world[1] * sine,"), "helpers"),
    "between-backwards": (("out[0] = from[1] * to[2] - from[2] * to[1];\n\t\tout[1] = from[2] * to[0] - from[0] * to[2];\n"
                           "\t\tout[2] = from[0] * to[1] - from[1] * to[0];",
                           "out[0] = to[1] * from[2] - to[2] * from[1];\n\t\tout[1] = to[2] * from[0] - to[0] * from[2];\n"
                           "\t\tout[2] = to[0] * from[1] - to[1] * from[0];"), "between"),
    "between-opposite-not-across": (("out[1] = -from[2];\n\t\t\tout[2] = from[1];", "out[1] = from[1];\n\t\t\tout[2] = from[2];"),
                                    "between"),
    "infinity-finite": (("values[index] - values[index] != 0.0f", "values[index] != values[index]"), "helpers"),
}


def generated(control=None):
    cameras = read("source/render/render_cameras.h")
    code = enum_with(read("source/render/render_cameras.c"), "_render_frustum_plane_left")
    code += "\n" + enum_with(read("source/render/render_cameras.c"), "_render_camera_warning_left_bottom_left")
    code += "\n" + structure(cameras, "render_camera") + "\n" + structure(cameras, "render_frustum")
    code += "\n" + function(read("source/render/render_cameras.c"), "render_camera_build_frustum")
    code += "\n" + read("port/linux/include/vr_math.h")
    code += "\n" + read("port/linux/game/vr_math.c").replace('#include "vr_math.h"', "")
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("vr_math", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("vr_math", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
