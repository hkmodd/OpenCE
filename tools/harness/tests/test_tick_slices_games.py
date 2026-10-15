"""Any game but a film runs its ticks in pieces, a client's once the host has started (display.tick_slices)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run

CASES = ["local", "host", "client", "held-client", "film", "not-main-loop"]
CONTROLS = {
    "host-only": (("game_connection() != _game_connection_film_playback",
                   "(game_connection() == _game_connection_local || game_connection() == _game_connection_network_server)"),
                  "client"),
    "films": (("game_connection() != _game_connection_film_playback && ", ""), "film"),
    "unheld": ((" && !game_time_held()", ""), "held-client"),
    "outside-main-loop": (("game_time_slicing_allowed && ", ""), "not-main-loop"),
}


def generated(control=None):
    source = read("source/game/game_time.c")
    types = enum_with(read("source/game/game.h"), "_game_connection_film_playback") + "\n"
    code = function(source, "game_time_held") + "\n" + function(source, "game_time_ticks_may_slice")
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("tick_slices_games", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("tick_slices_games", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
