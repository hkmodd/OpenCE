"""The high-res HUD's upscale on a thread of its own: uploaded on the GL thread once the thread has ended, and its memory
freed once, by whichever of its owner and its thread lets it go last.

    python -m pytest -q tools/harness/tests/test_hud_upscale_job.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["ended", "abandon-running", "abandon-ended", "failed", "refused"]

# a fault, and the case that must catch it
NEGATIVE_CONTROLS = {
    "not-waiting": (("if (!ended)\n", "if (!ended && 0)\n"), "ended"),
    "not-outlined": (("\t\tupscale_outline(job->pixels, job->bgra, job->width, job->height, job->kind);\n", "\t\t;\n"),
                     "ended"),
    "texels-kept": (("\tfree(job->pixels);\n", ""), "ended"),
    "freed-while-running": (("\tif (ended)\n\t\tupscale_job_free(job);\n}", "\tif (ended || 1)\n\t\tupscale_job_free(job);\n}"),
                            "abandon-running"),
    "not-marked": (("\tjob->abandoned = 1;\n", ""), "abandon-running"),
    "thread-not-freeing": (("\tif (abandoned)\n", "\tif (abandoned && 0)\n"), "abandon-running"),
    "ended-not-freed": (("\tif (ended)\n\t\tupscale_job_free(job);\n}", "\tif (ended && 0)\n\t\tupscale_job_free(job);\n}"),
                        "abandon-ended"),
    "refused-takes-texels": (("\t\tfree(job);\n\t\treturn NULL;", "\t\tupscale_job_free(job);\n\t\treturn NULL;"),
                             "refused"),
}


def under_test(fault=None):
    source = read("port/linux/src/hud_hires.c")
    start = source.index("#ifndef HALO_GLES\n\nstruct hud_hires_upscale_job")
    text = source[start + len("#ifndef HALO_GLES\n"):source.index("\n#else\n", start)] + "\n"
    if fault:
        text = mutated(text, *fault)
    return (("under_test.inc", text),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("hud_upscale_job", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("hud_upscale_job", under_test(fault)), case)
    assert status == CHECK_FAILED, f"the job with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
