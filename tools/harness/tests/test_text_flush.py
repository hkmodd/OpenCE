"""A string's batched characters (rasterizer_text_flush, rasterizer_xbox_text.c) are handed to immediate mode
(d3d8_gl.c) at once: the draw holds the vertices, and the registers the values, that the three calls a vertex
(SetVertexDataColor(9), SetVertexData2f(4), SetVertexData2f(0)) the characters were drawn with gave."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["full", "strings", "empty"]

# a fault in rasterizer_text_flush and the case that must catch it
NEGATIVE_CONTROLS = {
    "red-from-green": (("data[2][0] = ((vertex->color >> 16) & 0xFF)", "data[2][0] = ((vertex->color >> 8) & 0xFF)"),
                       "full"),
    "alpha-scaled-wrong": (("((vertex->color >> 24) & 0xFF) / 255.0f", "((vertex->color >> 24) & 0xFF) / 256.0f"),
                           "full"),
    "texture-w-zero": (("data[1][3] = 1.0f;", "data[1][3] = 0.0f;"), "full"),
    "texture-register-wrong": (("(1UL << 0) | (1UL << 4) | (1UL << 9)", "(1UL << 0) | (1UL << 5) | (1UL << 9)"),
                               "full"),
    "last-vertex-dropped": (("&vertex_data[0][0][0], vertex_count);", "&vertex_data[0][0][0], vertex_count - 1);"),
                            "strings"),
    "batch-kept": (("text_batch_character_count = 0;", ""), "strings"),
}


def generated(fault=None):
    device = read("port/linux/src/d3d8_gl.c")
    text = read("source/rasterizer/xbox/rasterizer_xbox_text.c")
    config = "#define XGPU_VERTEX_ATTRIBUTE_COUNT %d\n" % constant(read("port/linux/src/xgpu.h"),
                                                                  "XGPU_VERTEX_ATTRIBUTE_COUNT")
    code = "\n".join(function(device, name) for name in
                     ["D3DDevice_Begin", "immediate_add_register", "immediate_emit", "set_attribute",
                      "halo_immediate_vertices", "color_to_vec4", "D3DDevice_SetVertexData2f",
                      "D3DDevice_SetVertexDataColor"])
    batch = re.search(r"^struct rasterizer_text_vertex\n\{.*?^\};", text, re.M | re.S).group(0)
    batch += "\n" + "\n".join(re.search(pattern, text, re.M).group(0) for pattern in
                              [r"^#define TEXT_BATCH_MAXIMUM_CHARACTERS .*$",
                               r"^static struct rasterizer_text_vertex text_batch_vertices\[.*$",
                               r"^static short text_batch_character_count = .*$"])
    flush = function(text, "rasterizer_text_flush")
    if fault:
        flush = mutated(flush, *fault)
    return (("config.inc", config), ("under_test.inc", code + "\n" + batch + "\n" + flush))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("text_flush", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("text_flush", generated(fault)), case)
    assert status == CHECK_FAILED, f"rasterizer_text_flush with a fault ({control}) passed '{case}': the test cannot see it"
