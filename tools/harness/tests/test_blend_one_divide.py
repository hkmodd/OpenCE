"""A node's rotation blended between two ticks (port/linux/game/render_interpolation.c), its quaternion normalised in
the basis with one division: the basis a normalised quaternion gives, and the bases blended as they are when the
quaternion is too short."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CASES = ['rotation', 'small', 'not_a_number']
CONTROLS = [
    ('2.0f / length_squared', '2.0f', 'rotation'),
    ('length_squared > 1e-12f', 'length_squared > 1e-10f', 'small'),
]


def generated(fault=None):
    source = read('port/linux/game/render_interpolation.c')
    code = '\n'.join([
        structure(source, 'interpolation_quaternion'),
        structure(source, 'interpolation_rotation'),
        function(source, 'lerp'),
        function(source, 'point_lerp'),
        function(source, 'basis_from_quaternion'),
        function(source, 'matrix_blend_rotations'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('blend_one_divide', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('blend_one_divide', generated((before, after))), case)
    assert status == CHECK_FAILED, output
