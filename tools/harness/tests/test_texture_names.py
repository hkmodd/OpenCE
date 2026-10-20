"""A new texture's name comes from a block made at once, not from a glGenTextures call of its own."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CASES = ['one', 'burst', 'many']
SOURCE = 'port/linux/src/xbox_textures.c'


def generated(fault=None, config_fault=None):
    source = read(SOURCE)
    config = '\n'.join(re.findall(r'^#define TEXTURE_NAME_\w+.*$', source, re.M) + re.findall(
        r'^static (?:GLuint|unsigned long) texture_name\w*\b.*;$', source, re.M))
    code = function(source, 'texture_name')
    if config_fault:
        config = mutated(config, *config_fault)
    if fault:
        code = mutated(code, *fault)
    return (('config.inc', config + '\n'), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('texture_names', generated()), case)
    assert status == 0, output


def test_lookup_takes_a_name_from_the_block():
    # (a texture the lookup makes is named from the block: a glGenTextures
    # call of its own waits for the driver)
    code = function(read(SOURCE), 'xgpu_texture_get')
    assert 'glGenTextures' not in code
    assert 'entry->texture = texture_name();' in code


@pytest.mark.parametrize('fault,case', [
    # a block made for every name
    (('\t\ttexture_name_count = TEXTURE_NAME_BLOCK;\n', '\t\ttexture_name_count = 1;\n'), 'burst'),
    # the block given from its end: not in the order made
    (('texture_names[TEXTURE_NAME_BLOCK - texture_name_count--]', 'texture_names[--texture_name_count]'), 'one'),
    # a block made before the last one was given
    (('\tif (!texture_name_count)\n', '\tif (texture_name_count < 2)\n'), 'many'),
])
def test_negative_control(fault, case):
    status, output = run(build('texture_names', generated(fault)), case)
    assert status == CHECK_FAILED, output


def test_negative_control_block_size():
    fault = ('#define TEXTURE_NAME_BLOCK 256', '#define TEXTURE_NAME_BLOCK 1')
    status, output = run(build('texture_names', generated(config_fault=fault)), 'burst')
    assert status == CHECK_FAILED, output
