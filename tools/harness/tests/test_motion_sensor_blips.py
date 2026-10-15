"""The motion sensor's blips drawn batched: the same quads, a draw each bitmap, the same bitmap left set."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

SOURCE = "source/rasterizer/xbox/rasterizer_xbox_motion_sensor.c"
CASES = ["mixed", "small", "last-large", "full", "hidden", "bitmaps-unready", "none"]
CONTROLS = {
    "last-bitmap-not-set": (("if (blip_batch_last_bitmap)\n\t\trasterizer_set_texture_bitmap_data(0, blip_batch_last_bitmap);", ""),
                            "mixed"),
    "one-batch": (("short batch_index = large_blip ? 1 : 0;", "short batch_index = 0;"), "mixed"),
    "intensity-ignored": (("blip->color.green = color->green * intensity;", "blip->color.green = color->green;"), "small"),
    "full-dropped": (("if (blip_batch_count[batch_index] == BLIP_BATCH_MAXIMUM_BLIPS)\n\t\t\t\trasterizer_hud_motion_sensor_blips_draw();",
                      "if (blip_batch_count[batch_index] == BLIP_BATCH_MAXIMUM_BLIPS)\n\t\t\t\tblip_batch_count[batch_index] = 0;"),
                     "full"),
    "hidden-bitmap-not-set": (("\tblip_batch_last_bitmap = large_blip ? large_blip_bitmap : blip_bitmap;\n\tif",
                               "\tif"), "hidden"),
}


def union(source, name):
    return re.search(r"^union %s\n\{.*?^\};\n" % name, source, re.M | re.S).group(0) + \
        "typedef union %s %s;\n" % (name, name)


def generated(control=None):
    source = read(SOURCE)
    math = read("source/math/real_math.h")
    types = union(math, "real_point2d") + union(math, "real_rgb_color")
    start = source.index("#define BLIP_BATCH_MAXIMUM_BLIPS")
    end = source.index("static void rasterizer_hud_motion_sensor_blips_draw(void);")
    code = source[start:end] + "static void rasterizer_hud_motion_sensor_blips_draw(void);\n"
    code += "\n" + function(source, "_rasterizer_hud_motion_sensor_blip_draw").replace("void _rasterizer", "static void _rasterizer", 1)
    code += "\n" + function(source, "rasterizer_hud_motion_sensor_blips_draw")
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("motion_sensor_blips", generated()), case)
    assert status == 0, output


def test_end_draws_the_batch_first():
    # The blips are drawn when they end, before the sweep changes the target, the textures and the blending.
    end = function(read(SOURCE), "_rasterizer_hud_motion_sensor_blip_end")
    flush = end.index("rasterizer_hud_motion_sensor_blips_draw();")
    first_state = min(end.index(call) for call in ["IDirect3DDevice8_", "rasterizer_set_"] if call in end)
    assert flush < first_state


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("motion_sensor_blips", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
