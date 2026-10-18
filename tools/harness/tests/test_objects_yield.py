"""The real objects_update over a fake world hands a tick in pieces back between objects' updates, those made this tick too.

    python -m pytest -q tools/harness/tests/test_objects_yield.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run  # noqa: E402

CASES = ["active", "created", "deleted"]

# a fault, and the case that must catch it
NEGATIVE_CONTROLS = {
    "no-yield-between-created": (("(display.tick_slices) */\n\t\t\t\ttick_slices_yield();", "(display.tick_slices) */"),
                                 "created"),
    "no-yield-between-active": (("objects (display.tick_slices) */\n\t\t\t\t\ttick_slices_yield();",
                                 "objects (display.tick_slices) */"), "active"),
}

# the game's enums the fake world and the code under test use: (header, a member of the enum)
ENUMS = [("source/objects/object_types.h", "_object_type_biped"),
         ("source/objects/objects.h", "_object_header_being_created_bit"),
         ("source/objects/objects.h", "_object_deleted_when_deactivated_bit")]


def under_test(fault=None):
    enums = "\n".join(enum_with(read(header), member) for header, member in ENUMS)
    text = function(read("source/objects/objects.c"), "objects_update")
    return (("enums.inc", enums), ("under_test.inc", mutated(text, *fault) if fault else text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("objects_yield", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("objects_yield", under_test(fault)), case)
    assert status == CHECK_FAILED, f"objects_update with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
