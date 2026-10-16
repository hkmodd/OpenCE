"""A tag found by its group and name with the index a reference holds (tag_loaded_at, cache_files.c, which
verify_tag_reference calls) is the tag tag_loaded's walk of the tags finds, whatever the index."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["every-tag", "unloaded", "menu-tags"]

# a fault in tag_loaded_at and the case that must catch it
NEGATIVE_CONTROLS = {
    "index-not-compared": (("global_tag_instances[absolute_index].tag_index == tag_index &&", ""), "every-tag"),
    "group-not-compared": (("global_tag_instances[absolute_index].group_tag == group_tag &&", ""), "every-tag"),
    "name-not-compared": (("&&\n\t\t!_stricmp(name, global_tag_instances[absolute_index].name))", ")"), "every-tag"),
    "count-not-checked": (("absolute_index < global_tag_count &&", ""), "every-tag"),
    "loaded-not-checked": (("cache_file_globals.tags_loaded && ", ""), "unloaded"),
}


def generated(fault=None):
    source = read("source/cache/cache_files.c")
    found_at = function(source, "tag_loaded_at")
    if fault:
        found_at = mutated(found_at, *fault)
    return (("under_test.inc", function(source, "tag_loaded") + "\n" + found_at),)


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("tag_loaded_at", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("tag_loaded_at", generated(fault)), case)
    assert status == CHECK_FAILED, f"tag_loaded_at with a fault ({control}) passed '{case}': the test cannot see it"
