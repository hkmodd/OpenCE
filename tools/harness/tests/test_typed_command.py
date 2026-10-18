"""A command typed while a tick is under way runs after the tick has run to its end (display.tick_slices)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ["typed", "idle", "from-tick"]
CONTROLS = {
    "not-completed": (("\ttick_slices_complete();\n", "\n"), "typed"),
    "completed-after": (("\ttick_slices_complete();\n\tterminal_command_running = TRUE;\n"
                         "\tresult = hs_compile_and_evaluate_command(expression);\n",
                         "\tterminal_command_running = TRUE;\n\tresult = hs_compile_and_evaluate_command(expression);\n"
                         "\ttick_slices_complete();\n"), "typed"),
}


def generated(control=None):
    code = function(read("source/hs/hs.c"), "hs_compile_and_evaluate") + "\n"
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("typed_command", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("typed_command", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
