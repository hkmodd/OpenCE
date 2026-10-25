"""The ray a player's shot would take, without the shot (source/items/weapons.c's weapon_get_projectile_ray, the
headset's reticle): a vehicle's gun its gunner's player's, along the barrel, as trigger_create_projectiles shoots it."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ['vehicle-gunner', 'vehicle-unmanned', 'on-foot']
CONTROLS = [
    ('\t\tuse_aiming_vector = FALSE;\n', '', 'vehicle-gunner'),
    ('\t\tplayer_index = unit_get(unit->unit.gunner_object_index)->unit.player_index;\n', '', 'vehicle-gunner'),
    ('\tif (player_index == NONE)\n\t\treturn FALSE;\n', '', 'vehicle-unmanned'),
]


def generated(fault=None):
    code = function(read('source/items/weapons.c').replace('\r\n', '\n'), 'weapon_get_projectile_ray')
    if fault:
        code = mutated(code, fault[0], fault[1])
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('projectile_ray', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('projectile_ray', generated((before, after))), case)
    assert status == CHECK_FAILED, output


def test_the_shot_takes_the_gunner_too():
    # The preview follows the shot: trigger_create_projectiles takes a vehicle's gunner's player and its barrel.
    shot = function(read('source/items/weapons.c'), 'trigger_create_projectiles')
    assert 'player_index= gunner->unit.player_index;' in shot
    assert 'if (unit->unit.gunner_object_index!=NONE)\n\t\t\t{\n\t\t\t\tuse_aiming_vector= FALSE;' in \
        shot.replace('\r\n', '\n')
