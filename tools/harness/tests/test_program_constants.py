"""The vertex constants of a linked program (program_constants.c): counted exactly with consecutive or scattered
locations, nothing more asked of a program without c, a handful of lookups, each location asked for once, and a changed
range uploaded from its first register's location."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

SOURCE = "port/linux/src/d3d8_gl.c"
CASES = ["count", "unused", "lookups", "cached", "upload"]

# a fault, the piece it is in, and the case that must catch it
NEGATIVE_CONTROLS = {
    "one-short": ("lookup", ("\t\t\t\thigh = middle;\n", "\t\t\t\thigh = middle - (middle > low);\n"), "count"),
    "one-at-a-time": ("lookup", ("unsigned long middle = (low + high) / 2;", "unsigned long middle = low;"),
                      "lookups"),
    "asked-again": ("function", ("if (entry->constant_locations[index] < 0)", "if (1)"), "cached"),
    "consecutive-taken": ("upload", ("constant_location(entry, first)", "entry->constants + (GLint)first"), "upload"),
}


def generated(fault=None):
    source = read(SOURCE).replace("\r\n", "\n")
    start = source.index("\tentry->constants = glGetUniformLocation(entry->program, \"c\");")
    pieces = {
        "function": function(source, "constant_location"),
        "lookup": source[start:source.index("\tentry->viewport_scale = ", start)],
    }
    start = source.index("\t\t/* (the elements of an array from the one at a location on")
    end = source.index("device.constants[first]);\n", start) + len("device.constants[first]);\n")
    pieces["upload"] = source[start:end]
    if fault:
        piece, change, _ = fault
        pieces[piece] = mutated(pieces[piece], *change)
    world = f"#define XGPU_VERTEX_CONSTANT_COUNT {constant(read('port/linux/src/xgpu.h'), 'XGPU_VERTEX_CONSTANT_COUNT')}\n"
    return (("world.inc", world), ("under_test.inc", pieces["function"] + "\n"), ("lookup.inc", pieces["lookup"]),
            ("upload.inc", pieces["upload"]))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("program_constants", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault = NEGATIVE_CONTROLS[control]
    status, output = run(build("program_constants", generated(fault)), fault[2])
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed its case: the test cannot see it"
