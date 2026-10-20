"""A map's bsps kept as they were read and checked, and what is attached to them, from the production module."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run

SOURCE = "port/linux/game/structure_bsp_images.c"
CASES = ["keep-restore", "keep-once", "budget", "count", "attach", "attach-follows-load", "dispose", "kept-flag",
         "keep-allocation-failure", "attach-allocation-failure"]
CONTROLS = {
    "ignore-address": (("image->address == address", "TRUE"), "keep-restore"),
    "ignore-size": (("image->size == size &&", ""), "keep-restore"),
    "ignore-budget": (("(unsigned long)size > MAXIMUM_KEPT_BYTES - structure_bsp_images.bytes ||", ""), "budget"),
    "restore-keeps-current": (("structure_bsp_images.current = image_index;", ""), "attach-follows-load"),
    "refused-keeps-current": (("structure_bsp_images.current = NONE;\n\tif (size <= 0", "if (size <= 0"), "attach-follows-load"),
    "detach-without-dispose": (("image->attachment_dispose(image->attachment);", ""), "attach"),
    "dispose-keeps-bytes": (("free(image->bytes);", ""), "dispose"),
    "attach-calloc": (("data = malloc((size_t)size);", "data = calloc(1, (size_t)size);"), "attach"),
    "attach-not-zeroed": (("memset(data, 0, (size_t)size);", ""), "attach"),
}


def generated(control=None, gles=False, fault=None):
    source = read(SOURCE)
    # four pages in place of the desktop's 128 MB, so that the budget is reached
    desktop = "#define MAXIMUM_KEPT_BYTES (128UL * 1024 * 1024)"
    assert desktop in source and "#define MAXIMUM_KEPT_BYTES 0UL" in source
    source = source.replace(desktop, "#define MAXIMUM_KEPT_BYTES 16384UL")
    source = re.sub(r"^#include .*$", "", source, flags=re.M)
    if control:
        fault = CONTROLS[control][0]
    if fault:
        source = mutated(source, *fault)
    return (("under_test.inc", ("#define HALO_GLES\n" if gles else "") + source),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("structure_bsp_images", generated()), case)
    assert status == 0, output


def test_gles_keeps_nothing():
    status, output = run(build("structure_bsp_images", generated(gles=True)), "gles")
    assert status == 0, output


def test_gles_negative_control():
    """A GLES build reading every bsp ahead, for nothing."""
    control = ("return structure_bsp_images.kept || MAXIMUM_KEPT_BYTES == 0;", "return structure_bsp_images.kept;")
    status, output = run(build("structure_bsp_images", generated(gles=True, fault=control)), "gles")
    assert status == CHECK_FAILED and "check failed:" in output, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("structure_bsp_images", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED and "check failed:" in output, f"{control} escaped its CHECK: {status}: {output}"
