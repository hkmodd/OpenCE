"""What each rumble impulse is felt in: the body, or a weapon's shots in the hand that holds it (a headset's
controllers), the continuous and the scripted rumble the body's (source/game/player_rumble.c, weapons.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ['hands', 'body', 'tag']
CONTROLS = [
    ('\timpulse->hand = (byte)rumble_impulse_hand;\n', '', 'tag'),
    ('\t\tif (hand != NONE && player->impulses[impulse_index].hand != hand)\n\t\t\tcontinue;\n', '', 'hands'),
    ('motors[0] = body ? player->continuous_left : 0.0f;', 'motors[0] = player->continuous_left;', 'body'),
    ('if (body && rumble_globals->scripted_scale != 0.0f)', 'if (rumble_globals->scripted_scale != 0.0f)', 'body'),
    ('boolean body = hand == NONE || hand == _rumble_hand_body;', 'boolean body = hand == _rumble_hand_body;', 'body'),
]


def generated(fault=None):
    header = read('source/game/player_rumble.h').replace('\r\n', '\n')
    source = read('source/game/player_rumble.c').replace('\r\n', '\n')
    config = '\n'.join([
        'enum { MAXIMUM_LOCAL_PLAYERS = 4 };',
        enum_with(header, 'NUMBER_OF_RUMBLE_MOTORS = 2'),
        enum_with(header, '_rumble_hand_body = 0'),
        structure(header, 'rumble_motor'),
        structure(header, 'rumble_definition'),
        enum_with(source, 'MAXIMUM_RUMBLE_IMPULSES = 8'),
        structure(source, 'rumble_player'),
        structure(source, 'rumble_globals'),
        structure(source, 'rumble_motor_values'),
    ]) + '\n'
    code = '\n'.join([
        'static short rumble_impulse_hand = _rumble_hand_body;',
        function(source, 'rumble_set_impulse_hand'),
        function(source, 'rumble_player_impulse'),
        function(source[source.index('/* ---------- private code */'):], 'rumble_calculate'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('rumble_hands', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('rumble_hands', generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_shots_say_their_hand():
    # A shot's own damage effect is felt in the hand that holds its weapon, and what follows in the body again.
    weapons = read('source/items/weapons.c').replace('\r\n', '\n')
    at = weapons.index('object_cause_damage(&damage, owner_object_index, NONE, NONE, NONE, NULL);')
    before = weapons.rindex('rumble_set_impulse_hand(', 0, at)
    assert 'weapon_index==dual_wield_weapon(owner_object_index) ?\n\t\t\t\t_rumble_hand_other : _rumble_hand_weapon' \
        in weapons[before:at]
    assert weapons.index('rumble_set_impulse_hand(_rumble_hand_body);', at) - at < 100
    # Any other gamepad's motors are every impulse's, a headset's controllers' as each hand feels them.
    update = function(read('source/game/player_rumble.c'), 'rumble_update')
    assert 'rumble_calculate(player, NONE)' in update
    assert update.index('rumble_hands(player, (short)controller_index, &motors);') < \
        update.index('input_set_gamepad_rumbler_state(')
