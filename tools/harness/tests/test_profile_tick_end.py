"""A game tick's timer ended in the frame it began in, and none in a later one (display.tick_slices)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run

CASES = ["in-frame", "later-frame"]
CONTROLS = {
    "unguarded": (("if (profile_globals.current_frame.game_tick_count > 0)\n", "if (TRUE)\n"), "later-frame"),
}


def generated(control=None):
    source = read("source/cseries/profile.c")
    types = "#define TEST_MAXIMUM_GAME_TICKS_PER_FRAME %d\n" % constant(source, "MAXIMUM_GAME_TICKS_PER_FRAME")
    code = function(source, "profile_tick_end")
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("profile_tick_end", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("profile_tick_end", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
