"""An object drawn between two ticks: its two snapshots compared once a tick, not every frame, and one at rest drawn as
it is, without blending."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure

CASES = ['at_rest', 'moving', 'snapped', 'corrected', 'fading']
CONTROLS = [
    ('memcmp(previous, latest, record->node_count * sizeof(real_matrix4x3)) == 0;', 'FALSE;', 'at_rest'),
    ('record->poses_compared = FALSE;\n\t\trecord->blend_kept = FALSE;', 'record->blend_kept = FALSE;', 'at_rest'),
    ('record->poses_compared = TRUE;', ';', 'moving'),
    ('record->blend_kept = !correcting;', 'record->blend_kept = TRUE;', 'fading'),
    ('record->blended_frame = NONE;\n\t\trecord->blend_kept = FALSE;', 'record->blended_frame = NONE;', 'corrected'),
]
DEFINES = ['OBJECT_SNAP_DISTANCE', 'NODE_SNAP_DISTANCE', 'CORRECTION_DECAY', 'CORRECTION_NEGLIGIBLE',
           'CORRECTION_MAXIMUM']
STRUCTURES = ['interpolation_quaternion', 'interpolation_rotation', 'interpolated_object', 'interpolated_camera']
FUNCTIONS = ['lerp', 'point_lerp', 'vector_length', 'basis_is_rotation', 'quaternion_from_basis',
             'basis_from_quaternion', 'rotation_from_matrix', 'matrix_blend_rotations', 'correction_significant',
             'correction_advance', 'correction_drawn', 'correction_add', 'distance_squared', 'poses_snap',
             'spin_pause', 'record_blend', 'record_claim_and_blend', 'blend_objects', 'workers_begin', 'workers_end',
             'render_interpolation_tick', 'render_interpolation_frame_begin', 'render_interpolation_frame_end',
             'render_interpolation_object_node_matrices', 'render_interpolation_correct_object']


def generated(fault=None):
    source = read('port/linux/game/render_interpolation.c')
    config = structure(read('source/math/real_math.h'), 'real_matrix4x3') + '\n'
    config += 'typedef struct real_matrix4x3 real_matrix4x3;\n'
    config += '#define MAXIMUM_INTERPOLATED_NODES %d\n' % constant(source, 'MAXIMUM_INTERPOLATED_NODES')
    config += '#define WORKER_RECORDS %d\n' % constant(source, 'WORKER_RECORDS')
    for name in DEFINES:
        config += '#define %s %sf\n' % (name, re.search(r'#define\s+' + name + r'\s+([\d.]+)f', source).group(1))
    config += '\n'.join(structure(source, name) for name in STRUCTURES) + '\n'
    code = '\n'.join(function(source, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('interpolation_once', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('interpolation_once', generated((before, after))), case)
    assert status == CHECK_FAILED, output
