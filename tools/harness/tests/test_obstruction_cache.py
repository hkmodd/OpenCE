"""The sound manager's obstruction over a fake battle: each sound tested from the camera once a tick, rightly.

    python tools/harness/tests/test_obstruction_cache.py
"""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, inline, mutated, read, run  # noqa: E402

CASES = ["once-per-tick", "all-sounds", "next-tick", "players", "crowded-slot"]

# a fault in the cache, and the case that must catch it
NEGATIVE_CONTROLS = {
    "one-slot-each": (("#define OBSTRUCTION_CACHE_PROBES 8", "#define OBSTRUCTION_CACHE_PROBES 1"), "once-per-tick"),
    "keeps-last-tick": (("|| obstruction_cache[index].game_time != now", ""), "next-tick"),
    "ignores-the-player": (("obstruction_cache[index].local_player_index == local_player_index &&", ""), "players"),
    "ignores-the-height": (("obstruction_cache[index].position.z == source->location.position.z", "TRUE"),
                           "crowded-slot"),
}

# the game's enums the code under test uses: (file, a member of the enum)
ENUMS = [("source/physics/collisions.h", "_collision_test_front_facing_surfaces_bit"),
         ("source/physics/collision_usage.h", "_collision_user_sounds"),
         ("source/physics/collision_usage.h", "MAXIMUM_COLLISION_USER_STACK_DEPTH")]
# and constants, by value: (file, name)
CONSTANTS = [("source/cseries/cseries.h", "LONG_BITS"), ("source/cseries/cseries.h", "LONG_BITS_BITS"),
             ("source/sound/game_sound.c", "_cluster_sound_distance_unreachable_bit"),
             ("source/sound/game_sound.c", "CLUSTER_SOUND_DISTANCE_VALUE_MASK")]


def define(source, name):
    """A #define's line."""
    return re.search(r"^#define " + re.escape(name) + r"\b.*$", source, re.M).group(0)


def under_test(fault=None):
    cseries, sound = read("source/cseries/cseries.h"), read("source/sound/game_sound.c")
    enums = "\n".join(enum_with(read(path), member) for path, member in ENUMS) + "\n"
    enums += "\n".join(define(cseries, name) for name in ("CEILING", "PIN", "BIT_VECTOR_TEST_FLAG")) + "\n"
    enums += "".join(f"#define {name} {constant(read(path), name)}\n" for path, name in CONSTANTS)
    enums += define(sound, "MAXIMUM_CLUSTER_SOUND_DISTANCE") + "\n"
    text = inline(read("source/math/real_math.h"), "vector_from_points3d") + "\n"
    obstruction = function(sound, "compute_sound_obstruction")
    cache = sound[sound.index("#define OBSTRUCTION_CACHE_SLOTS"):sound.index(obstruction) + len(obstruction)]
    text += (mutated(cache, *fault) if fault else cache) + "\n"
    return (("enums.inc", enums), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("obstruction_cache", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("obstruction_cache", under_test(fault)), case)
    assert status == CHECK_FAILED, f"the cache with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
