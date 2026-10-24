"""The headset's controllers as the game's controller (port/linux/src/vr_input.c): the buttons in the game and the
menus, for either hand, their bindings (vr.bindings), the menu button's short and long presses, the stick's snap and
smooth turns, the punch, the off grip's tap at the head, the crouch, the grenade's throw, the weapon's hand-over, both hands on it (a
pistol's support, a rifle's foregrip), where a hand points, the rumble in the hands, and where a hand's ray meets a
panel."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

CASES = ["gameplay", "move", "menus", "menu-button", "turn", "seat", "drive", "room", "gestures", "hands", "ray",
         "bindings", "swap", "throw", "grab", "bare", "physical"]
CONTROLS = {
    "left-handed-ignored": (("main_index = input->left_handed ? VR_HAND_LEFT : VR_HAND_RIGHT;",
                             "main_index = VR_HAND_RIGHT;"), "gameplay"),
    "grenade-on-fire": (("vr_bind_hand(pad, off_hand, bindings[VR_ROLE_OFF],",
                         "vr_bind_hand(pad, main_hand, bindings[VR_ROLE_OFF],"), "gameplay"),
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
    "holster-strikes": (("input->holster || input->grabbing[main_index] ? VR_BIND_NONE :",
                         "input->grabbing[main_index] ? VR_BIND_NONE :"), "hands"),
    "shoulder-while-held": (("main_pressed = main_hand->buttons & ~input->held[main_index];",
                             "main_pressed = main_hand->buttons;"), "hands"),
    "shoulders-crossed": (("return shoulder ? VR_HAND_RIGHT : VR_HAND_LEFT;", "return shoulder ? VR_HAND_LEFT : VR_HAND_RIGHT;"),
                          "hands"),
    "grab-strikes": (("input->holster || input->grabbing[main_index] ? VR_BIND_NONE :", "input->holster ? VR_BIND_NONE :"),
                     "grab"),
    "grab-held-on": (("input->grab_seconds[hand] <= VR_GRAB_SECONDS && ", ""), "grab"),
    "grab-out-of-reach": ((" && settings->grab_reach[hand])\n", ")\n"), "grab"),
    "taken-stays": (("if (input->grab_pending > 0.0f && (input->grab_hand == VR_HAND_LEFT) != (input->left_handed != 0))",
                     "if (0)"), "grab"),
    "taken-any-time": (("\tif (input->grab_pending > 0.0f)\n\t\tinput->grab_pending -= seconds;\n", ""), "grab"),
    "bare-fires": (("(action == VR_BIND_FIRE || action == VR_BIND_ZOOM)))", "action == VR_BIND_ZOOM))"), "bare"),
    "physical-handover-ignored": (("\t\tif (input->supporting)\n\t\t{\n\t\t\tvr_swap_hands(input);",
                                   "\t\tif (0)\n\t\t{\n\t\t\tvr_swap_hands(input);"), "physical"),
    "physical-let-go-on-press": (("(settings->grip_toggle ? pressed[main_index] : released[main_index])",
                                  "pressed[main_index]"), "physical"),
    "physical-empty-shoulder-draws": (("if (side >= 0 && settings->shoulder_full[side])", "if (side >= 0)"),
                                      "physical"),
    "physical-grip-strikes": (("input->physical || input->holster ||", "input->holster ||"), "physical"),
    "physical-empty-hand-fires": (("action = input->holding ? VR_BIND_FIRE : VR_BIND_THROW_GRENADE;",
                                   "action = VR_BIND_FIRE;"), "physical"),
    "physical-support-throws": (("action = input->supporting ? VR_BIND_NONE : VR_BIND_THROW_GRENADE;",
                                 "action = VR_BIND_THROW_GRENADE;"), "physical"),
    "physical-off-punch-ignored": (("(input->physical && !input->supporting &&", "(0 &&"), "physical"),
    "physical-stale-off-hand": (("\t\t\tif (!input->physical || input->supporting)\n\t\t\t\tinput->off_position_valid = 0;\n",
                                 ""), "physical"),
    "physical-draw-stays": (("\t\t\tif (hand != main_index)\n\t\t\t\tvr_swap_hands(input);\n", ""), "physical"),
    "physical-new-life-gripped": (("\t\tif (input->grab_pending > 0.0f)\n\t\t\tinput->gripped = 1;\n",
                                   "\t\tinput->gripped = 1;\n"), "physical"),
    "physical-in-seats": (("settings->physical && settings->gestures && settings->on_foot;",
                           "settings->physical && settings->gestures;"), "physical"),
    "physical-seat-lets-go": (("\t\tinput->holding = settings->physical && settings->gestures;\n",
                               "\t\tinput->holding = 0;\n"), "physical"),
    "two-hands-backwards": (("z[axis] = (main_hand->grip.position[axis] - off_hand->grip.position[axis]) / length;",
                             "z[axis] = (off_hand->grip.position[axis] - main_hand->grip.position[axis]) / length;"),
                            "hands"),
    "pistol-held-ahead": (("if (pistol_grip || length > VR_TWO_HANDED_REACH)", "if (length > VR_TWO_HANDED_REACH)"),
                          "hands"),
    "support-not-at-hand": (("\tif (length < VR_SWAP_REACH)\n", "\tif (0)\n"), "hands"),
    "pointing-pitch-down": (("turn[0] = sinf(pitch * 0.5f);", "turn[0] = -sinf(pitch * 0.5f);"), "hands"),
    "pointing-aim-pose": (("vr_quaternion_multiply(hand->grip.orientation, turn, orientation);",
                           "vr_quaternion_multiply(hand->aim.orientation, turn, orientation);"), "hands"),
    "rumble-shots-in-free-hand": (("amplitude[!main_index] = both ? amplitude[main_index] : left;",
                                   "amplitude[!main_index] = amplitude[main_index];"), "hands"),
    "bindings-ignored": (("vr_bind(pad, bindings[VR_CONTROL_LOWER],",
                          "vr_bind(pad, vr_default_bindings[VR_ROLE_MAIN][VR_CONTROL_LOWER],"), "bindings"),
    "bad-items-uncounted": (("\t\t\tbad++;\n", "\t\t\t;\n"), "bindings"),
    "trigger-pull-ignored": (("trigger == VR_BIND_FIRE || trigger == VR_BIND_THROW_GRENADE ? hand->trigger :",
                              "0 ? hand->trigger :"), "bindings"),
    "swap-while-held": (("off_pressed = off_hand->buttons & ~input->held[off_index];",
                         "off_pressed = off_hand->buttons;"), "swap"),
    "swap-on-grab": (("\t\t\t\t\tinput->handover = 1;\n", "\t\t\t\t\tinput->handover = 1;\n\t\t\t\t\tinput->swaps++;\n"),
                     "swap"),
    "handover-either-hand": (("vr_distance(main_hand->grip.position, input->handover_main) >\n"
                              "\t\t\t\t\t\tvr_distance(off_hand->grip.position, input->handover_off)", "1"), "swap"),
    "handover-no-timeout": ((" || input->handover_seconds > VR_HANDOVER_SECONDS", ""), "swap"),
    "swap-apart": (("vr_distance(off_hand->grip.position, main_hand->grip.position) < VR_SWAP_REACH",
                    "vr_distance(off_hand->grip.position, main_hand->grip.position) < 1.0f"), "swap"),
    "swap-kept-past-setting": (("input->setting_left_handed != settings->left_handed)", "0)"), "swap"),
    "throw-button-presses": (("bindings[role][control] = VR_BIND_NONE;", ";"), "throw"),
    "throw-let-go-slowly": (("return was_held && !held && input->throw_speed[index] > VR_THROW_SPEED;",
                             "return was_held && !held;"), "throw"),
    "throw-speed-of-the-moment": (("input->throw_speed[index] -= VR_THROW_FALL * seconds;",
                                   "input->throw_speed[index] = 0.0f;"), "throw"),
    "throw-on-press": (("return was_held && !held &&", "return !was_held && held &&"), "throw"),
    "punch-after-menus": (("\t\tinput->main_position_valid = 0;\n\t\tfor (hand", "\t\tfor (hand"), "throw"),
    "throw-after-menus": (("\t\t\tinput->throw_position_valid[hand] = 0;\n\t\t\tinput->throw_speed[hand] = 0.0f;\n"
                           "\t\t}\n\t\tinput->off_position_valid", "\t\t}\n\t\tinput->off_position_valid"), "throw"),
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
