"""A headset's hands in a vehicle's seat: how far a hand is on the vehicle's control as its controller comes near and goes
away (port/linux/game/vr_view.c's vr_seat_hold_eased)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402


def generated(fault=None):
    source = read('port/linux/game/vr_view.c')
    defines = re.findall(r'^#define VR_SEAT_HOLD_\w+ [^\n]*$', source, re.M)
    code = '\n'.join(defines + [function(source, 'vr_seat_hold_eased')])
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


def test_hold():
    status, output = run(build('seat_body', generated()), 'hold')
    assert status == 0, output


@pytest.mark.parametrize('fault', [
    ('*held ? VR_SEAT_HOLD_KEEP : VR_SEAT_HOLD_REACH', 'VR_SEAT_HOLD_REACH'),
    ('*held ? VR_SEAT_HOLD_KEEP : VR_SEAT_HOLD_REACH', 'VR_SEAT_HOLD_KEEP'),
    ('hold > 1.0f ? 1.0f : hold', 'hold'),
    ('hold < 0.0f ? 0.0f : ', ''),
    (' / VR_SEAT_HOLD_SECONDS', ''),
])
def test_negative_control(fault):
    status, output = run(build('seat_body', generated(fault)), 'hold')
    assert status == CHECK_FAILED, output
