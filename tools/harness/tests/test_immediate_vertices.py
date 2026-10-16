"""Immediate mode's vertices hold only the registers written since Begin (d3d8_gl.c), and read as vertices of all
sixteen would: a register first written after some vertices holds, in them, the value it had before. Vertices
given at once (halo_immediate_vertices) are those the same writes a register at a time would make."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["decal", "late-register", "many", "random", "at-once", "at-once-random", "at-once-outside"]

# a fault in d3d8_gl.c and the case that must catch it
NEGATIVE_CONTROLS = {
    "earlier-vertices-not-widened": (("for (vertex = game->immediate_count; vertex-- > 0;)",
                                      "for (vertex = 0; vertex-- > 0;)"), "late-register"),
    "register-out-of-order": (("below += (game->immediate_written >> index) & 1;", "below += 0;"), "late-register"),
    "new-value-in-earlier-vertices": (("memcpy(to + below * 4, game->attributes[reg], 4 * sizeof(float));",
                                       "memset(to + below * 4, 0, 4 * sizeof(float));"), "late-register"),
    "vertices-of-every-register": (("vertex = game->immediate_vertices + game->immediate_count * game->immediate_registers * 4;",
                                    "vertex = game->immediate_vertices + game->immediate_count * XGPU_VERTEX_ATTRIBUTE_COUNT * 4;"),
                                   "decal"),
    "registers-not-counted": (("game->immediate_registers = after;", ""), "decal"),
    "at-once-copied-as-given": (("if (written == registers)", "if (1)"), "at-once"),
    "at-once-registers-not-added": (("immediate_add_register((int)reg);", "(void)reg;"), "at-once"),
    "at-once-first-vertex-kept": (("last = vertices + (count - 1) * size * 4;", "last = vertices;"), "at-once"),
    "at-once-room-not-grown": (("while (capacity < game->immediate_count + count)", "while (0)"), "at-once"),
}


def generated(fault=None):
    source = read("port/linux/src/d3d8_gl.c")
    config = "#define XGPU_VERTEX_ATTRIBUTE_COUNT %d\n" % constant(read("port/linux/src/xgpu.h"),
                                                                  "XGPU_VERTEX_ATTRIBUTE_COUNT")
    code = "\n".join(function(source, name) for name in
                     ["D3DDevice_Begin", "immediate_add_register", "immediate_emit", "set_attribute",
                      "halo_immediate_vertices"])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("immediate_vertices", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("immediate_vertices", generated(fault)), case)
    assert status == CHECK_FAILED, f"d3d8_gl.c with a fault ({control}) passed '{case}': the test cannot see it"
