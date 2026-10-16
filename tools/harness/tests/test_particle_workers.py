"""Particles drawn with display.interpolation_threads: the real worker threads (worker_pool.c) work their sprites out
while the game's thread puts them in their groups, and what is drawn, in what order, what is deleted and what each
particle is left with is what the original's loop makes of the same world; the first-person weapon's particles and
nodes an object does not have are left to the game's thread, a worker is never handed the frustum the first-person
weapon's projection changes, nor a particle whose definition or sprite the tags would log reading, a chunk a worker
has in hand is waited for, the particles of objects gone are deleted once no worker reads a particle, and without
workers the game's thread works every chunk out."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure

CASES = ['same-as-original', 'without-workers', 'waits-for-workers', 'group-limit']
CONTROLS = [
    # a chunk read before it is worked out
    ('\twhile (__atomic_load_n(&prepared_chunk_generations[chunk_index], __ATOMIC_ACQUIRE) !=\n'
     '\t\tprepared_generation)\n',
     '\twhile (0)\n', 'waits-for-workers'),
    # last frame's chunks taken for this one's
    ('\t\t\t\tprepared_generation++;\n', '', 'waits-for-workers'),
    # the particles of objects gone deleted while workers read particles
    ('\t\t\t\t\t\t\t/* port: deleted once none is being worked out */\n'
     '\t\t\t\t\t\t\tprepared_deleted_particles[deleted_particle_count++] =\n'
     '\t\t\t\t\t\t\t\trendered_particle->particle_index;\n',
     '\t\t\t\t\t\t\tparticle_delete(rendered_particle->particle_index);\n', 'waits-for-workers'),
    # the first-person weapon's nodes asked for on a worker
    ('\t\t\tif (!reached)\n\t\t\t\treturn;\n\t\t\tnode_matrix =\n', '\t\t\tnode_matrix =\n', 'same-as-original'),
    # a node an object does not have asked for on a worker
    ('\t\t\t\t\treturn;\n\t\t\t\t}\n\t\t\t}\n\t\t\tnode_matrix = object_get_node_matrix(',
     '\t\t\t\t\t;\n\t\t\t\t}\n\t\t\t}\n\t\t\tnode_matrix = object_get_node_matrix(', 'same-as-original'),
    # the first-person weapon's particles worked out on a worker, with the frustum it changes
    ('\t\t(rendered_particle->attached_to_first_person_weapon ||\n', '\t\t(0 ||\n', 'same-as-original'),
    # a definition that is not a tag got on a worker
    ('\tif (!tag_get_gives_tag(PARTICLE_TAG, rendered_particle->definition_index))\n', '\tif (0)\n',
     'same-as-original'),
    # a definition its particle's index names wrongly got on a worker
    ('\t\t\t!tag_get_gives_tag(PARTICLE_TAG, particle->definition_index)))\n', '\t\t\t0))\n', 'same-as-original'),
    # a sprite whose tags log worked out on a worker
    ('\t\t\t\t!build_sprite_reads_tags_only(\n', '\t\t\t\t0 && !build_sprite_reads_tags_only(\n', 'same-as-original'),
    # a worker handed the frustum the first-person weapon's projection changes
    ('\t\t\t&prepared_frustum,\n\t\t\tFALSE,\n', '\t\t\t&render.frustum,\n\t\t\tFALSE,\n', 'same-as-original'),
    # a group's radius left out
    ('\t\t\t\t\t\t\t\ttotal_radius += prepared->radius;\n', '', 'without-workers'),
]
STRUCTURES = ['build_sprite_prepared']
PARTICLE_STRUCTURES = ['rendered_particle_datum', 'prepared_particle']
FUNCTIONS = ['compare_rendered_particles', 'rendered_particle_sort_digit', 'sort_rendered_particles',
             'render_particles', 'render_particle_prepare',
             'render_particles_prepare_chunk', 'render_particles_prepare_job', 'render_particles_get_prepared']


def between(source, first, last):
    start = source.index(first)
    return source[start:source.index(last, start) + len(last)]


def generated(fault=None):
    source = read('source/render/render_particles.c')
    sprite = read('source/render/render_sprite.h')
    config = structure(read('source/math/real_math.h'), 'real_matrix4x3') + '\n'
    config += 'typedef struct real_matrix4x3 real_matrix4x3;\n'
    config += '#define NUMBER_OF_VERTICES_PER_QUADRILATERAL %d\n' % constant(
        read('source/cseries/cseries.h'), 'NUMBER_OF_VERTICES_PER_QUADRILATERAL')
    for name in ['MAXIMUM_RENDERED_PARTICLES', 'MAXIMUM_RENDERED_PARTICLE_GROUPS', 'PREPARED_PARTICLE_CHUNK_SIZE']:
        config += '#define %s %d\n' % (name, constant(source, name))
    config += enum_with(source, '_prepared_particle_serial') + '\n'
    structures = '\n'.join(structure(sprite, name) for name in STRUCTURES) + '\n'
    structures += '\n'.join(structure(source, name) for name in PARTICLE_STRUCTURES) + '\n'
    code = between(source, "/* port: (render_particles' sprites worked out ahead) */", 'short index);') + '\n'
    code += between(source, 'static struct prepared_particle prepared_particles[',
                    'static short prepared_deleted_particles[MAXIMUM_RENDERED_PARTICLES];') + '\n'
    code += '\n'.join(function(source, name) for name in FUNCTIONS)
    if fault:
        code = mutated(code, *fault)
    # the test counts render_particle_prepare's calls on the workers and slows them
    code = mutated(code, 'static void render_particle_prepare(\n\tstruct rendered_particle_datum const '
                   '*rendered_particle,\n\tstruct particle_definition const *definition,\n\tstruct build_sprite_data '
                   'const *sprite_data,\n\tstruct render_frustum const *frustum,\n\tboolean reached,\n\tstruct '
                   'prepared_particle *prepared)\n{',
                   'static void render_particle_prepare_real(\n\tstruct rendered_particle_datum const '
                   '*rendered_particle,\n\tstruct particle_definition const *definition,\n\tstruct build_sprite_data '
                   'const *sprite_data,\n\tstruct render_frustum const *frustum,\n\tboolean reached,\n\tstruct '
                   'prepared_particle *prepared)\n{')
    code = 'static void render_particle_prepare_real(struct rendered_particle_datum const *, ' \
        'struct particle_definition const *, struct build_sprite_data const *, struct render_frustum const *, ' \
        'boolean, struct prepared_particle *);\n' + code
    pool = read('port/linux/src/worker_pool.c')
    pool_code = re.sub(r'^#include "platform\.h"\n', '', pool, flags=re.M)
    assert pool_code.count('#include') == pool.count('#include') - 1, "worker_pool.c's includes have changed"
    pool_code = re.sub(r'\bspin_pause\b', 'pool_spin_pause', pool_code)
    return (('config.inc', config), ('structures.inc', structures),
            ('worker_pool.h', read('port/linux/src/worker_pool.h')), ('worker_pool.inc', pool_code),
            ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('particle_workers', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('particle_workers', generated((before, after))), case)
    assert status == CHECK_FAILED, output
