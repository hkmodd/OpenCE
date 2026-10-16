"""A tick under way when a client's clock begins to wait for the host is run whole and taken (display.tick_slices):
left under way, it holds off the network update the clock waits for, and the server browser never joins a game."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure

CASES = ["held-after-ahead", "held-after-due", "local", "client"]
CONTROLS = {
    "not-taken": ((" || ticks_per_second <= 0.f || game_time_held();", " || ticks_per_second <= 0.f;"),
                  "held-after-ahead"),
    "taken-always": ((" || ticks_per_second <= 0.f || game_time_held();", " || TRUE;"), "local"),
}
UNDER_TEST = ["game_time_held", "game_time_ticks_may_slice", "game_time_get_speed", "game_time_tick_ended",
              "game_time_update", "game_time_update_sliced", "game_time_sliced_frame_end"]


def generated(control=None):
    source = read("source/game/game_time.c").replace("\r\n", "\n")
    types = [enum_with(read("source/game/game.h"), "_game_connection_film_playback"),
             "enum { TICKS_PER_SECOND = %d, SOME_LARGE_NUMBER_OF_TICKS = %d };" %
             (constant(read("source/cseries/cseries.h"), "TICKS_PER_SECOND"),
              constant(source, "SOME_LARGE_NUMBER_OF_TICKS")),
             structure(source, "game_time_globals_struct"),
             "static boolean game_time_slicing_allowed;", "static boolean game_time_updated;",
             "void game_time_update(real time_delta_sec);"]
    code = "\n".join(function(source, name) for name in UNDER_TEST)
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("types.inc", "\n".join(types) + "\n"), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("tick_slices_held", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("tick_slices_held", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
