"""Objects drawn between two ticks with display.interpolation_threads: the real worker threads (worker_pool.c) blend
them while the game's thread asks for them, each once a frame and drawn as blended alone; an object a worker has in
hand is waited for; ticks, frames, corrections and new maps wait for the workers; a second frame at the same fraction
draws the blends made; at a change the workers stop at what they have in hand, and after a tick they set out again."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure

CASES = ['blends-beside-workers', 'waits-for-worker', 'changes-wait-for-workers', 'workers-stopped',
         'tick-restarts-workers', 'same-frame-blends-kept', 'changed-mid-frame']
CONTROLS = [
    ('\t\twhile (__atomic_load_n(&record->blended_frame, __ATOMIC_ACQUIRE) != interpolation_frame)\n\t\t\tspin_pause();',
     '\t\t;', 'waits-for-worker'),
    ('interpolated_objects_used = absolute_index + 1;', ';', 'blends-beside-workers'),
    ('workers_end();\n\tprevious_tick = interpolation_tick++;', 'previous_tick = interpolation_tick++;',
     'changes-wait-for-workers'),
    ('\t\treturn;\n\tworkers_end();\n\tinterpolation_frame++;', '\t\treturn;\n\tinterpolation_frame++;',
     'changes-wait-for-workers'),
    ('workers_end();\n\t/* (so written that an offset not a number is none) */',
     '/* (so written that an offset not a number is none) */', 'changes-wait-for-workers'),
    ('workers_end();\n\tif (interpolated_objects)\n', 'if (interpolated_objects)\n', 'changes-wait-for-workers'),
    ('if (fraction == interpolation_fraction)\n\t\treturn;', 'if (0)\n\t\treturn;', 'same-frame-blends-kept'),
    ('if (fraction == interpolation_fraction)\n\t\treturn;', 'if (1)\n\t\treturn;', 'same-frame-blends-kept'),
    ('record->blend_kept = FALSE;\n\t\trecord->claimed_frame = NONE;\n\t}\n\t/* the objects',
     'record->blend_kept = FALSE;\n\t}\n\t/* the objects', 'changed-mid-frame'),
    ('record->blend_kept = FALSE;\n\t\trecord->claimed_frame = NONE;\n\t}\n\t/* a local',
     'record->blend_kept = FALSE;\n\t}\n\t/* a local', 'changed-mid-frame'),
    ('__atomic_store_n(&worker_next_record, MAXIMUM_INTERPOLATED_OBJECTS, __ATOMIC_RELAXED);', ';', 'workers-stopped'),
    ('game_time.c) */\n\tworkers_begin();', 'game_time.c) */', 'tick-restarts-workers'),
]
DEFINES = ['OBJECT_SNAP_DISTANCE', 'NODE_SNAP_DISTANCE', 'CORRECTION_DECAY', 'CORRECTION_NEGLIGIBLE',
           'CORRECTION_MAXIMUM']
STRUCTURES = ['interpolation_quaternion', 'interpolation_rotation', 'interpolated_object', 'interpolated_camera',
              'interpolated_first_person']
FUNCTIONS = ['lerp', 'point_lerp', 'vector_length', 'basis_is_rotation', 'quaternion_from_basis',
             'basis_from_quaternion', 'rotation_from_matrix', 'matrix_blend_rotations', 'correction_significant',
             'correction_advance', 'correction_drawn', 'correction_add', 'distance_squared', 'poses_snap',
             'spin_pause', 'record_blend', 'record_claim_and_blend', 'blend_objects', 'workers_begin', 'workers_end',
             'render_interpolation_tick', 'render_interpolation_reset', 'render_interpolation_frame_begin',
             'render_interpolation_frame_end', 'render_interpolation_object_node_matrices',
             'render_interpolation_correct_object']


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
    # the test counts each blend, then makes it
    code = mutated(code, 'static void record_blend(struct interpolated_object *record)\n',
                   'static void record_blend_real(struct interpolated_object *record)\n')
    pool = read('port/linux/src/worker_pool.c')
    pool_code = re.sub(r'^#include "platform\.h"\n', '', pool, flags=re.M)
    assert pool_code.count('#include') == pool.count('#include') - 1, "worker_pool.c's includes have changed"
    pool_code = re.sub(r'\bspin_pause\b', 'pool_spin_pause', pool_code)
    return (('config.inc', config), ('worker_pool.h', read('port/linux/src/worker_pool.h')),
            ('worker_pool.inc', pool_code), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('interpolation_workers', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('interpolation_workers', generated((before, after))), case)
    assert status == CHECK_FAILED, output
