"""Collision's real sphere test against a bsp gives the same answers with its memo, and asks the recursion only once.

    python tools/harness/tests/test_sphere_memo.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["repeated", "different", "flags-change", "invalidated", "crowd"]

# a fault in the memo, and the case that must catch it
NEGATIVE_CONTROLS = {
    "ignores-the-flags-content": (("memcmp(breakable_surface_flags, sphere_memo_flags, flags_bytes)", "0"),
                                  "flags-change"),
    "ignores-invalidation": (("*found = entry->generation == sphere_memo_generation &&", "*found ="), "invalidated"),
    "drops-the-radius": (("memcpy(&sphere[3], &radius, sizeof(real));", "sphere[3] = 0;"), "different"),
}


def under_test(fault=None):
    collision_bsp = read("source/physics/collision_bsp.c")
    context = structure(collision_bsp, "test_sphere_data") + "\n"
    memo = read("port/linux/game/collision_sphere_memo.c")
    memo = memo[memo.rindex("#include"):].split("\n", 1)[1]
    text = (mutated(memo, *fault) if fault else memo) + "\n"
    text += function(collision_bsp, "collision_bsp_test_sphere") + "\n"
    return (("context.inc", context), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("sphere_memo", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("sphere_memo", under_test(fault)), case)
    assert status == CHECK_FAILED, f"the memo with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
