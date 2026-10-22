"""The headset's controllers as the game's controller (port/linux/src/vr_input.c): the buttons in the game and the
menus, for either hand, their bindings (vr.bindings), the menu button's short and long presses, the stick's snap and
smooth turns, the punch, the off grip's tap at the head, the crouch, the hands' swap, and where a hand's ray meets a
panel."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

CASES = ["gameplay", "move", "menus", "menu-button", "turn", "seat", "drive", "room", "gestures", "hands", "ray",
         "bindings", "swap"]
CONTROLS = {
    "left-handed-ignored": (("main_index = input->left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT;",
                             "main_index = VR_HAND_RIGHT;"), "gameplay"),
    "grenade-on-fire": (("vr_bind_hand(pad, off_hand, settings->bindings[VR_ROLE_OFF],",
                         "vr_bind_hand(pad, main_hand, settings->bindings[VR_ROLE_OFF],"), "gameplay"),
    "move-turned-back": (("move[0] * cosine - move[1] * sine", "move[0] * cosine + move[1] * sine"), "move"),
    "room-no-hysteresis": (("input->room_walking ? VR_ROOM_STOP : VR_ROOM_START", "VR_ROOM_START"), "room"),
    "room-head-ignored": (("move[0] = (-room[0] * forward[2] + room[1] * forward[0]) * push;",
                           "move[0] = room[0] * push * length;"), "room"),
    "room-over-stick": (("move[0] * move[0] + move[1] * move[1] < VR_ROOM_STILL * VR_ROOM_STILL &&", ""), "room"),
    "clicks-on-any-hand": (("if (hand == input->pointer_hand)\n\t\t\t\t\tinput->clicks++;",
                            "if (1)\n\t\t\t\t\tinput->clicks++;"), "menus"),
    "short-press-at-once": (("if (input->menu_seconds > 0.0f && !input->menu_long)",
                             "if (input->menu_seconds > 0.0f && input->menu_long)"), "menu-button"),
    "snap-wrong-way": (("return x > 0.0f ? -settings->snap_angle : settings->snap_angle;",
                        "return x > 0.0f ? settings->snap_angle : -settings->snap_angle;"), "turn"),
    "snap-repeats": (("\t\tinput->snapped = 1;\n", "\t\tinput->snapped = 0;\n"), "turn"),
    "seat-stick-turns": (("if (settings->seat_stick)\n", "if (0)\n"), "seat"),
    "drive-strafes": (("\t\t\tif (settings->drive)\n\t\t\t\tmove[0] = 0.0f;\n", ""), "drive"),
    "seat-stick-sideways": (("pad->thumb[2] = vr_thumb(main_hand->stick[0]);", "pad->thumb[2] = 0;"), "seat"),
    "punch-any-way": (("along > VR_PUNCH_ALONG * speed", "along > -speed"), "gestures"),
    "crouch-no-hysteresis": (("input->crouched = height < input->standing - VR_CROUCH_RISE;",
                              "input->crouched = height < input->standing - VR_CROUCH_DEPTH;"), "gestures"),
    "holster-ignored": (("input->holster ? VR_BIND_SWITCH_WEAPON :", "0 ? VR_BIND_SWITCH_WEAPON :"), "hands"),
    "two-hands-backwards": (("z[axis] = main_hand->grip.position[axis] - off_hand->grip.position[axis];",
                             "z[axis] = off_hand->grip.position[axis] - main_hand->grip.position[axis];"), "hands"),
    "bindings-ignored": (("vr_bind(pad, bindings[VR_CONTROL_LOWER],",
                          "vr_bind(pad, vr_default_bindings[VR_ROLE_MAIN][VR_CONTROL_LOWER],"), "bindings"),
    "bad-items-uncounted": (("\t\t\tbad++;\n", "\t\t\t;\n"), "bindings"),
    "trigger-pull-ignored": (("trigger == VR_BIND_FIRE || trigger == VR_BIND_THROW_GRENADE ? hand->trigger :",
                              "0 ? hand->trigger :"), "bindings"),
    "swap-while-held": (("off_pressed = off_hand->buttons & ~input->held[off_index];",
                         "off_pressed = off_hand->buttons;"), "swap"),
    "swap-apart": (("vr_distance(off_hand->grip.position, main_hand->grip.position) < VR_SWAP_REACH",
                    "vr_distance(off_hand->grip.position, main_hand->grip.position) < 1.0f"), "swap"),
    "swap-kept-past-setting": (("input->setting_left_handed != settings->left_handed)", "0)"), "swap"),
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
