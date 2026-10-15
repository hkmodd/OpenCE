"""A free particle's physics in 30ths of a second, as on the Xbox, ahead of the frame drawn and drawn between its last
two steps; a step a frame at 30 frames a second or fewer, and one on an object every frame."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run

CASES = ['stepped', 'slow', 'attached', 'reused', 'moved', 'died']
CONTROLS = [
    ('step->particle_index != particle_index ||', 'FALSE ||', 'reused'),
    ('particle->position.x != step->shown_position.x ||', '', 'moved'),
    ('particle->rotation != step->shown_rotation)', 'FALSE)', 'moved'),
    ('MAX(PARTICLE_STEP_SECONDS, -step->lead)', 'PARTICLE_STEP_SECONDS', 'slow'),
    ('if (particle->object_index == NONE)', 'if (TRUE)', 'attached'),
    ('\t\tparticle_update_physics_stepped(particle_index, dt);', '\t\tparticle_update_physics(particle_index, dt);',
     'stepped'),
    ('if (!particle_update_physics(particle_index, step_dt))\n\t\t\treturn FALSE;',
     'particle_update_physics(particle_index, step_dt);', 'died'),
    ('step->lead += step_dt;', ';', 'stepped'),
    ('1.0f - step->lead / PARTICLE_STEP_SECONDS', '1.0f', 'stepped'),
]


def generated(fault=None):
    particles = read('source/effects/particles.c')
    config = '#define TICKS_PER_SECOND %d\n' % constant(read('source/cseries/cseries.h'), 'TICKS_PER_SECOND')
    config += '#define MAXIMUM_PARTICLE_UNRENDERED_FRAME_COUNT %d\n' % constant(
        particles, 'MAXIMUM_PARTICLE_UNRENDERED_FRAME_COUNT')
    steps = re.search(r'#define PARTICLE_STEP_SECONDS.*?particle_steps\[HALO_PORT_MAXIMUM_PARTICLES\];', particles,
                      re.S)
    if not steps:
        raise LookupError('particle_steps not found')
    code = '\n'.join([
        steps.group(0),
        function(particles, 'particle_update_physics_stepped'),
        function(particles, 'particles_update'),
    ])
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('particle_steps', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('particle_steps', generated((before, after))), case)
    assert status == CHECK_FAILED, output
