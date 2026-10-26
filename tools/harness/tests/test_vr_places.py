"""A headset's player's weapons in their places by a VR server's rules: a new unit's, a weapon asked for by a hand,
the exchange at a shoulder, the switch button's side, the weapons the game readies by itself
(port/linux/game/vr_places.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run


def generated(fault=None):
    header = read('port/linux/game/vr_places.h').replace('\r\n', '\n')
    code = read('port/linux/game/vr_places.c').replace('\r\n', '\n').replace('#include "vr_places.h"\n', '')
    if fault:
        code = mutated(code, *fault)
    return (('vr_places.h', header), ('under_test.inc', code))


CASES = ['spawn', 'pending_off', 'pending_main', 'pending_off_main_empty', 'refused', 'exchange', 'switch_side',
         'game_readies_shoulder', 'game_readies_new', 'forced', 'seat_change', 'off_let_go', 'falling', 'invariants']


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('vr_places', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('if (places->slot[VR_PLACE_SHOULDER(main_hand)] < 0)\n\t\treturn VR_PLACE_SHOULDER(main_hand);',
      'if (places->slot[VR_PLACE_SHOULDER(!main_hand)] < 0)\n\t\treturn VR_PLACE_SHOULDER(!main_hand);'), 'spawn'),
    (('\t\t\t\torders->off_take = slot;\n\t\t\t}\n\t\t}', '\t\t\t\torders->ready = slot;\n\t\t\t}\n\t\t}'),
     'pending_off'),
    (('\t\t\t\tmain_hand = hand;\n', ''), 'pending_off_main_empty'),
    (('game->now - places->pending_time > game->ticks_per_second * 3 / 2', '0'), 'refused'),
    (('\tvr_places_put(places, shoulder, places->slot[hand], places->weapon[hand]);\n', ''), 'exchange'),
    (('\tif (places->slot[VR_PLACE_SHOULDER(!main_hand)] >= 0)\n\t\treturn (short)!main_hand;\n', ''),
     'switch_side'),
    (('if (place < 0 || readied == game->forced)', 'if (1)'), 'game_readies_shoulder'),
    (('if (place < 0 || readied == game->forced)', 'if (place < 0)'), 'forced'),
    (('\t\telse if (main_slot >= 0)\n\t\t{\n\t\t\torders->ready = main_slot;',
      '\t\telse if (main_slot >= 0)\n\t\t{\n\t\t\torders->ready = -1;'), 'seat_change'),
    (('(!game->off_holding || game->dual != places->slot[off])', '(game->dual != places->slot[off])'), 'off_let_go'),
    (('game->now - places->falling_time < game->ticks_per_second', '0'), 'falling'),
])
def test_negative_control(fault, case):
    status, output = run(build('vr_places', generated(fault)), case)
    assert status == CHECK_FAILED, output
