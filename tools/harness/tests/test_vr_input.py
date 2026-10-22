"""The headset's controllers as the game's controller (port/linux/src/vr_input.c): the buttons in the game and the
menus, for either hand, the menu button's short and long presses, the stick's snap and smooth turns, the punch, the
flashlight at the head and the crouch, and where a hand's ray meets a panel."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

CASES = ["gameplay", "move", "menus", "menu-button", "turn", "gestures", "hands", "ray"]
CONTROLS = {
    "left-handed-ignored": (("int main_index = settings->left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT;",
                             "int main_index = VR_HAND_RIGHT;"), "gameplay"),
    "grenade-on-fire": (("pad->analog[VR_PAD_LEFT_TRIGGER] = (unsigned char)(off_hand->trigger",
                         "pad->analog[VR_PAD_LEFT_TRIGGER] = (unsigned char)(main_hand->trigger"), "gameplay"),
    "move-turned-back": (("off_hand->stick[0] * cosine - off_hand->stick[1] * sine",
                          "off_hand->stick[0] * cosine + off_hand->stick[1] * sine"), "move"),
    "clicks-on-any-hand": (("if (hand == input->pointer_hand)\n\t\t\t\t\tinput->clicks++;",
                            "if (1)\n\t\t\t\t\tinput->clicks++;"), "menus"),
    "short-press-at-once": (("if (input->menu_seconds > 0.0f && !input->menu_long)",
                             "if (input->menu_seconds > 0.0f && input->menu_long)"), "menu-button"),
    "snap-wrong-way": (("return x > 0.0f ? -settings->snap_angle : settings->snap_angle;",
                        "return x > 0.0f ? settings->snap_angle : -settings->snap_angle;"), "turn"),
    "snap-repeats": (("\t\tinput->snapped = 1;\n", "\t\tinput->snapped = 0;\n"), "turn"),
    "punch-any-way": (("along > VR_PUNCH_ALONG * speed", "along > -speed"), "gestures"),
    "crouch-no-hysteresis": (("input->crouched = height < input->standing - VR_CROUCH_RISE;",
                              "input->crouched = height < input->standing - VR_CROUCH_DEPTH;"), "gestures"),
    "holster-ignored": (("vr_press(pad, input->holster ? VR_PAD_Y : VR_PAD_B,", "vr_press(pad, VR_PAD_B,"), "hands"),
    "two-hands-backwards": (("z[axis] = main_hand->grip.position[axis] - off_hand->grip.position[axis];",
                             "z[axis] = off_hand->grip.position[axis] - main_hand->grip.position[axis];"), "hands"),
    "ray-upside-down": (("*v = 0.5f - y / height;", "*v = 0.5f + y / height;"), "ray"),
    "ray-panel-not-turned": (("vr_rotate(inverse, relative, origin);", "memcpy(origin, relative, sizeof(origin));"),
                             "ray"),
}


def generated(control=None):
    code = read("port/linux/include/vr_math.h")
    code += "\n" + read("port/linux/include/halo_vr.h").replace('#include "vr_math.h"', "")
    code += "\n" + read("port/linux/include/vr_input.h").replace('#include "halo_vr.h"', "")
    code += "\n" + read("port/linux/src/vr_input.c").replace('#include "vr_input.h"', "")
    code += "\n" + read("port/linux/game/vr_math.c").replace('#include "vr_math.h"', "")
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("under_test.inc", code),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("vr_input", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("vr_input", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
