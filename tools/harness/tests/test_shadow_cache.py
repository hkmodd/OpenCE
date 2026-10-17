"""The shadow cache draws a shadow from a kept map only when its record is the kept one's, byte for byte, and keeps a
map only once it is blurred and only for an object that stood still."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure

CASES = ["first", "steady", "hit", "changed", "unsteady", "collision", "pending", "busy", "unblurred", "batch-blurred",
         "map-refused", "scale", "flush", "not-recording"]
NEGATIVE_CONTROLS = {
    "hash-only": (("!memcmp(shadow_cache[entry].record, shadow_record, shadow_record_size * sizeof(long))", "1"),
        "collision"),
    "stored-without-standing-still": (("shadow_cache_objects[index].pass + 1 == shadow_cache_pass", "1"), "unsteady"),
    "evicts-a-map-in-use": (("shadow_cache[entry].pass != shadow_cache_pass &&", ""), "busy"),
    "drawn-from-an-unwritten-map": (("shadow_cache[store].size = 0;", ""), "pending"),
    "keeps-an-unblurred-map": (("|| shadow_convolved || shadow_setup", "|| TRUE"), "unblurred"),
    "hit-still-blurred": (("shadow_active = FALSE;\n\t\tshadow_convolved = TRUE;", "shadow_convolved = TRUE;"), "hit"),
    "kept-across-sizes": (("rasterizer_environment_shadow_cache_flush();\n\t\tshadow_cache_scale", "shadow_cache_scale"),
        "scale"),
}


def objects_table(source):
    """The table of the objects' last records, a static of an unnamed structure."""
    match = re.search(r"^static struct\s*\{[^{}]*\} shadow_cache_objects\[[A-Z_]+\];", source, re.M)
    if not match:
        raise LookupError("shadow_cache_objects not found in the sources")
    return match.group(0)


def generated(fault=None):
    source = read("source/rasterizer/xbox/rasterizer_xbox_shadows.c").replace("\r\n", "\n")
    types = "".join(f"#define {name} {constant(source, name)}\n"
        for name in ["SHADOW_RECORD_MAXIMUM", "SHADOW_CACHE_ENTRIES", "SHADOW_CACHE_OBJECTS"])
    types += structure(source, "shadow_record_header") + "\n" + structure(source, "shadow_cache_entry") + "\n"
    types += objects_table(source) + "\n"
    code = "\n".join(function(source, name) for name in ["rasterizer_environment_shadow_cache_flush",
        "rasterizer_environment_shadow_cast", "_rasterizer_environment_shadow_end"])
    if fault:
        code = mutated(code, *fault)
    return (("types.inc", types), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("shadow_cache", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("shadow_cache", generated(fault)), case)
    assert status == CHECK_FAILED, f"{control} escaped {case}: {output}"
