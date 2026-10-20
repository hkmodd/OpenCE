"""The shader translators' text: their conversions written without vsnprintf, the same text as vsnprintf's."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CASES = ['quick', 'other', 'empty', 'growth', 'out_of_memory']
SOURCE = 'port/linux/src/xgpu_text.c'
FUNCTIONS = ['text_reserve', 'text_put', 'text_put_number', 'text_append_quickly', 'xgpu_text_append']
TRANSLATORS = ['port/linux/src/nv2a_psh.c', 'port/linux/src/nv2a_vsh.c', 'port/linux/src/xgpu_post.c']


def generated(fault=None):
    header = read('port/linux/src/xgpu.h')
    prototype = re.search(r'^void xgpu_text_append\(.*;$', header, re.M).group(0)
    source = read(SOURCE)
    code = '\n'.join(function(source, name) for name in FUNCTIONS) + '\n'
    if fault:
        code = mutated(code, *fault)
    return (('types.inc', structure(header, 'xgpu_text') + '\n' + prototype + '\n'), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('shader_text', generated()), case)
    assert status == 0, output


def test_translators_use_only_the_quick_conversions():
    # (any other conversion is still right, through vsnprintf, but slow)
    for path in TRANSLATORS:
        conversions = set(re.findall(r'%[-+ #0-9.*]*[hlLzjt]*[a-zA-Z%]', read(path)))
        assert conversions <= {'%s', '%.*s', '%d', '%lu', '%c', '%%'}, (path, conversions)


@pytest.mark.parametrize('fault,case', [
    # every append through vsnprintf, as before
    (('\tappended = text_append_quickly(text, format, arguments);\n', '\tappended = 0;\n'), 'quick'),
    # negative numbers without their sign
    (('\tif (negative)\n', '\tif (0)\n'), 'quick'),
    # %.*s's precision ignored
    (('while (length < (unsigned long)precision && string[length])', 'while (string[length])'), 'quick'),
    # %.*s reading past the string's end
    (('while (length < (unsigned long)precision && string[length])', 'while (length < (unsigned long)precision)'),
     'quick'),
    # %lu's u left in the text
    (('\t\t\tnext++;\n', '\n'), 'quick'),
    # the text left unterminated
    (("\ttext->buffer[at] = '\\0';\n", '\n'), 'quick'),
    # other conversions skipped instead of given to vsnprintf
    (('\t\telse\n\t\t\treturn 0;\n', '\t\telse\n\t\t\twritten = 1;\n'), 'other'),
    # out of memory, part of an append left in the text
    (("\t\ttext->buffer[text->length] = '\\0';\n", '\t\t(void)0;\n'), 'out_of_memory'),
])
def test_negative_control(fault, case):
    status, output = run(build('shader_text', generated(fault)), case)
    assert status == CHECK_FAILED, output
