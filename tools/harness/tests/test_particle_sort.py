"""The rendered particles' radix sort: compare_rendered_particles' order, equal particles kept in particle_data's."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure

CASES = ["small", "equal", "full", "wide", "reversed"]
CONTROLS = {
    "sign-ignored": (
        ("(unsigned short)particle->cluster_index ^ 0x8000;", "(unsigned short)particle->cluster_index;"),
        "wide",
    ),
    "high-byte-dropped": (("default: result = definition >> 8; break;", "default: result = 0; break;"), "wide"),
    "scattered-backwards": (
        (
            "for (index = 0; index < count; index++)\n\t\t\tto[",
            "for (index = count - 1; index >= 0; index--)\n\t\t\tto[",
        ),
        "small",
    ),
    "counts-kept": (("csmemset(digit_counts, 0, sizeof(digit_counts));", "(void)0;"), "small"),
    "not-copied-back": (("csmemcpy(particles, from, count * sizeof(*particles));", "(void)0;"), "wide"),
}


def generated(control=None):
    source = read("source/render/render_particles.c")
    types = structure(source, "rendered_particle_datum")
    types += f"\n#define MAXIMUM_RENDERED_PARTICLES {constant(source, 'MAXIMUM_RENDERED_PARTICLES')}\n"
    code = "\n".join(
        function(source, name)
        for name in ("compare_rendered_particles", "rendered_particle_sort_digit", "sort_rendered_particles")
    )
    if control:
        before, after = CONTROLS[control][0]
        code = mutated(code, before, after)
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("particle_sort", generated()), case)
    assert status == 0, output


def test_sorted_with_it():
    # render_particles sorts with it, not with the C library's qsort.
    body = function(read("source/render/render_particles.c"), "render_particles")
    assert "sort_rendered_particles(rendered_particles, rendered_particle_count);" in body
    assert "qsort(" not in body


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("particle_sort", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
