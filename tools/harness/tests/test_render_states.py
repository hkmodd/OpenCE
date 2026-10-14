"""The render and texture states sent to the renderer before each draw (d3d8_gl.c): each state that changed, once, and
no other, so that the renderer's copy is the game's."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ["random", "edges"]

# a fault in d3d8_gl.c and the case that must catch it
NEGATIVE_CONTROLS = {
    "group-short-of-its-end": (("words_differ(sent_words + group, current + group, end - group)",
                                "words_differ(sent_words + group, current + group, end - group - 1)"), "edges"),
    "group-skipped-when-changed": (("if (!words_differ(sent_words + group", "if (words_differ(sent_words + group"),
                                   "random"),
    "texture-state-as-render-state": (("D3DTSS_MAXSTAGES * D3DTSS_MAX, D3DRS_MAX, words, count);",
                                       "D3DTSS_MAXSTAGES * D3DTSS_MAX, 0, words, count);"), "edges"),
}


def generated(fault=None):
    source = read("port/linux/src/d3d8_gl.c")
    pdb = read("port/include/xdk/xdk_pdb.h")
    config = "".join("#define %s %d\n" % (name, constant(text, name)) for name, text in
                     [("D3DRS_MAX", pdb), ("D3DTSS_MAX", pdb), ("D3DTSS_MAXSTAGES", read("port/include/xdk/xdk_d3d8.h")),
                      ("STATES_GROUP", source)])
    code = "\n".join(function(source, name) for name in
                     ["states_command", "words_differ", "states_changed", "states_sync"])
    if fault:
        code = mutated(code, *fault)
    return (("config.inc", config), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("render_states", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("render_states", generated(fault)), case)
    assert status == CHECK_FAILED, f"d3d8_gl.c with a fault ({control}) passed '{case}': the test cannot see it"
