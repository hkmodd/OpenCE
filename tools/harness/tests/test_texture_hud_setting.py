"""The texture cache reads display.high_res_hud, asked for each texture of each draw, only once the settings change.

    python tools/harness/tests/test_texture_hud_setting.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["reads-once", "follows-a-change", "off-keeps-the-map"]

# a fault in the code, and the case that must catch it
NEGATIVE_CONTROLS = {
    "reads-every-time": (("if (read_at != config_changes())", "if (read_at != config_changes() + 1)"), "reads-once"),
    "never-reads-again": (("if (read_at != config_changes())", "if (read_at == (unsigned long)-1)"),
                          "follows-a-change"),
    "ignores-the-setting": (("if (high_res_hud())", "if (1)"), "off-keeps-the-map"),
}


def under_test(fault=None):
    textures = read("port/linux/src/xbox_textures.c")
    text = function(textures, "high_res_hud") + "\n" + function(textures, "texture_entry_result") + "\n"
    return (("under_test.inc", mutated(text, *fault) if fault else text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("texture_hud_setting", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("texture_hud_setting", under_test(fault)), case)
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
