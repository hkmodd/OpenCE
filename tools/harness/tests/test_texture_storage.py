"""A 2D texture's levels get their storage once, and each upload replaces their texels."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CASES = ['dxt', 'dxt_part', 'converted', 'narrow', 'linear', 'too_many_levels', 'cube', 'volume']
SOURCE = 'port/linux/src/xbox_textures.c'


def generated(fault=None, gles=False, everywhere=False):
    source = read(SOURCE)
    types = structure(read('port/linux/src/xgpu.h'), 'xgpu_texture_description') + '\n' + structure(
        source, 'format_information') + '\n'
    code = function(source, 'upload')
    if fault and everywhere:
        assert fault[0] in code, fault
        code = code.replace(*fault)
    elif fault:
        code = mutated(code, *fault)
    return (('defines.inc', '#define HALO_GLES 1\n' if gles else '\n'), ('types.inc', types),
            ('helpers.inc', function(source, 'level_dimension') + '\n'), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('texture_storage', generated()), case)
    assert status == 0, output


def test_gles():
    status, output = run(build('texture_storage', generated(gles=True)), 'gles')
    assert status == 0, output


def test_cache_keeps_whether_a_texture_has_storage():
    # (an entry's texture is never described again: it is found by its format
    # and size, so its storage, made once, stays right for it)
    source = read(SOURCE)
    entry = structure(source, 'texture_entry')
    assert re.search(r'^\tBOOL storage;$', entry, re.M)
    assert 'custom_edition_texels_order(entry->address), &entry->storage);' in function(source, 'xgpu_texture_get')
    assert 'entry = calloc(1, sizeof(*entry));' in function(source, 'xgpu_texture_get')
    desktop = read('port/linux/src/gl.h').split('/* ANDROID_GL_FUNCTIONS_END */', 1)[1]
    assert 'X(glTexStorage2D)' in desktop and 'X(glCompressedTexSubImage2D)' in desktop


@pytest.mark.parametrize('fault,case', [
    # no storage: every level defined again on each upload, as before
    (('\t\tif (description->levels <= levels)\n', '\t\tif (description->levels + 99 <= levels)\n'), 'dxt'),
    # storage made on every upload
    (('\t\t\tif (!*storage)\n', '\t\t\tif (storage)\n'), 'dxt'),
    # storage made, but the levels defined again
    (('\t\t\treplace = TRUE;\n', '\n'), 'converted'),
    # storage for more levels than the size has
    (('\t\tif (description->levels <= levels)\n', '\t\tif (description->levels <= levels + 99)\n'),
     'too_many_levels'),
    # the levels counted one short
    (('\t\tunsigned long levels = 1;\n', '\t\tunsigned long levels = 0;\n'), 'narrow'),
    # cube maps given 2D storage
    (('\tif (target == GL_TEXTURE_2D && description->levels >= 1)\n', '\tif (description->levels >= 1)\n'), 'cube'),
    # compressed levels given converted storage
    (('description->compressed ? compressed_format(information.kind) : GL_RGBA8', 'GL_RGBA8'), 'dxt'),
])
def test_negative_control(fault, case):
    status, output = run(build('texture_storage', generated(fault)), case)
    assert status == CHECK_FAILED, output


def test_negative_control_gles():
    # storage and replaced levels on OpenGL ES too
    fault = ('#ifndef HALO_GLES\n', '#if 1\n')
    status, output = run(build('texture_storage', generated(fault, gles=True, everywhere=True)), 'gles')
    assert status == CHECK_FAILED, output
