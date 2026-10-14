"""The real matrix4x3_multiply (matrix_math.c): its SSE block gives every element exactly as the loop the builds without
SSE run, and result may be a or b."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

CASES = ["same-as-the-loop", "result-is-a", "result-is-b", "writes-only-the-result"]

# faults in the SSE block, applied in turn, and the case that must catch them
NEGATIVE_CONTROLS = {
    # the last row of b summed first: the same products, added in another order
    "sums-in-another-order": ((("_mm_set1_ps(b_elements[row * 3 + 0]), row0)", "_mm_set1_ps(b_elements[row * 3 + @]), row@)"),
                               ("_mm_set1_ps(b_elements[row * 3 + 2]), row2)", "_mm_set1_ps(b_elements[row * 3 + 0]), row0)"),
                               ("_mm_set1_ps(b_elements[row * 3 + @]), row@)", "_mm_set1_ps(b_elements[row * 3 + 2]), row2)")),
                              "same-as-the-loop"),
    "position-not-scaled": (((" _mm_set1_ps(*a_scale)", " _mm_set1_ps(1.f)"),), "same-as-the-loop"),
    # a's first row read for each row of b, and each of the first three rows written as soon as it is made: over a,
    # the second row reads the first row written
    "written-before-a-is-read": ((("_mm_set1_ps(b_elements[row * 3 + 0]), row0)",
                                   "_mm_set1_ps(b_elements[row * 3 + 0]), "
                                   "_mm_loadh_pi(_mm_load_ss(a_elements + 0), (__m64 const *)(a_elements + 1)))"),
                                  ("row2));\n\t\t}",
                                   "row2));\n\t\t\tif (row < 3)\n\t\t\t{\n"
                                   "\t\t\t\t_mm_store_ss(result_elements + row * 3, product[row]);\n"
                                   "\t\t\t\t_mm_storeh_pi((__m64 *)(result_elements + row * 3 + 1), product[row]);\n"
                                   "\t\t\t}\n\t\t}")),
                                 "result-is-a"),
    "writes-a-fourth-lane": ((("_mm_store_ss(result_elements + row * 3, product[row]);",
                               "_mm_storeu_ps(result_elements + row * 3, product[row]);"),), "writes-only-the-result"),
}


def generated(faults=()):
    source = read("source/math/matrix_math.c")
    code = function(source, "matrix4x3_multiply")
    assert code.count("#if defined(__SSE__)") == 1, "matrix4x3_multiply's SSE block has changed"
    sse = code
    for fault in faults:
        sse = mutated(sse, *fault)
    loop = code.replace("#if defined(__SSE__)", "#if 0", 1).replace("matrix4x3_multiply(", "loop_multiply(", 1)
    config = structure(read("source/math/real_math.h"), "real_matrix4x3") + "\ntypedef struct real_matrix4x3 real_matrix4x3;\n"
    return (("config.inc", config), ("under_test.inc", sse + "\n" + loop + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("matrix_multiply", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    faults, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("matrix_multiply", generated(faults)), case)
    assert status == CHECK_FAILED, f"matrix_math.c with a fault ({control}) passed '{case}': the test cannot see it"
