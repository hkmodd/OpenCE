"""By whose rules a headset's player plays, failing closed in a network game, and the melee through the fist by a VR
server's alone (port/linux/game/vr_view.c)."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run


def generated(fault=None):
    view = read('port/linux/game/vr_view.c').replace('\r\n', '\n')
    code = '\n'.join(function(view, name) for name in (
        'vr_rules_active', 'vr_render_melee_point', 'vr_render_off_hand_strike'))
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['rules', 'closed', 'melee'])
def test_case(case):
    status, output = run(build('vr_rules', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('return game && TEST_FLAG', 'return !game || TEST_FLAG'), 'closed'),
    (('if (!vr_rules_active() || !vr_view.stereo', 'if (!vr_view.stereo'), 'melee'),
    (('return vr_rules_active() && vr_view.stereo', 'return vr_view.stereo'), 'melee'),
    (('game_connection() == _game_connection_film_playback ||', '0 ||'), 'rules'),
])
def test_negative_control(fault, case):
    status, output = run(build('vr_rules', generated(fault)), case)
    assert status == CHECK_FAILED, output
