"""The host's budget of a client's hits (port/linux/game/network_damage.c, distributed_report_paid): a bucket of
seconds of fire filling at a second a second, twice that for a VR server's headset's player with a weapon in each
hand (dual_wield.c) in the last few seconds, a hit taking what its weapon's rate says."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run  # noqa: E402

CASES = ['one-weapon', 'two-weapons', 'two-weapons-long-past']
CONTROLS = [
    (('\t\t\tearned *= 2.0f;\n', ''), 'two-weapons'),
    (('<= REPORT_MAXIMUM_AGE_TICKS)\n\t\t\tearned', '<= 1000000)\n\t\t\tearned'), 'two-weapons-long-past'),
    (('*hit_seconds -= 1.0f / rate;', ';'), 'one-weapon'),
]


def generated(fault=None):
    source = read('port/linux/game/network_damage.c')
    config = ''.join('#define %s %d\n' % (name, constant(source, name))
                     for name in ('HIT_REPORT_BURST_SECONDS', 'MAXIMUM_EXPLOSIONS_PER_TICK'))
    code = function(source, 'distributed_report_paid')
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('hit_report_rate', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', CONTROLS)
def test_negative_control(fault, case):
    status, output = run(build('hit_report_rate', generated(fault)), case)
    assert status == CHECK_FAILED, f'{fault} escaped its check: {output}'
