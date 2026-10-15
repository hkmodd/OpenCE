"""A long tick run in pieces on a second stack (display.tick_slices): its share of the frame it is begun on, the rest in
the spare time and the frames after it, run whole once it has been due for too many, and the next begun ahead of its
time by as long as the last took."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

CASES = ['disabled', 'short', 'spare', 'held', 'forced', 'ahead', 'lead', 'finish', 'collision']
CONTROLS = [
    ('whole = TRUE;', 'whole = FALSE;', 'forced'),
    ('if (tick_pending_counted)\n\t\t\tframes_due++;', 'frames_due++;', 'ahead'),
    ('\tframes_due = 0;\n', '\n', 'ahead'),
    ('return !tick_pending_ended;', 'return FALSE;', 'spare'),
    ('tick_duration * 1.25', 'tick_duration * 1.0', 'lead'),
    ('tick_pending_counted = counted;', 'tick_pending_counted = TRUE;', 'lead'),
    ('platform_fiber_clock() >= slice_deadline', 'platform_fiber_clock() > slice_deadline', 'held'),
    ('collision_log_continue_period(0);', ';', 'collision'),
    ('share * clock_rate / 1000000u', 'share', 'spare'),
    ('* 1000000u / clock_rate', '', 'lead'),
]


def without_includes(source):
    return re.sub(r'^#include [^\n]*\n', '', source, flags=re.M)


def generated(fault=None):
    code = without_includes(read('port/linux/game/tick_slices.c'))
    if fault:
        code = mutated(code, *fault)
    return (('tick_slices.h', read('port/linux/game/tick_slices.h')),
            ('posix_fiber.inc', read('port/linux/src/posix_fiber.c')),
            ('win32_fiber.inc', read('port/windows/src/win32_fiber.c')),
            ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('tick_slices', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('before,after,case', CONTROLS)
def test_negative_control(before, after, case):
    status, output = run(build('tick_slices', generated((before, after))), case)
    assert status == CHECK_FAILED, output
