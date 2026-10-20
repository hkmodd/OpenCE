"""A structure bsp's load fills the tag cache past the tags as the stock load does, without a fault for each page the
renderer watches."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure

CASES = ["fill", "custom-edition"]
CONTROLS = {
    "fill-not-prepared": (("tag_cache_base_address + cache_file_globals.header.tag_data_size,\n\t\t\t0x01600000",
                           "tag_cache_base_address + cache_file_globals.header.tag_data_size + 0x1000,\n\t\t\t0x01600000"),
                          "fill"),
    "fill-short": (("0xCD,\n\t\t\t0x01600000 - cache_file_globals.header.tag_data_size);",
                    "0xCD,\n\t\t\t0x01000000 - cache_file_globals.header.tag_data_size);"), "fill"),
    "custom-edition-filled": (("if (!custom_edition_cache_tags_loaded())\n\t{", "if (TRUE)\n\t{"), "custom-edition"),
}


def generated(control=None):
    cache_files = read("source/cache/cache_files.c")
    types = f"#define TAG_CACHE_SIZE {constant(read('source/cache/physical_memory_map.h'), 'TAG_CACHE_SIZE')}\n"
    types += "\n".join(re.search(r"^#define " + name + r"\b.*$", cache_files, re.M).group(0)
                       for name in ["STRUCTURE_BSP_TAG", "CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE"])
    types += "\n" + enum_with(cache_files, "CACHE_FILE_SECTOR_SIZE")
    types += "\n" + structure(read("source/tag_files/tag_groups.h"), "tag_reference")
    types += "\n" + structure(read("source/scenario/scenario_definitions.h"), "scenario_structure_bsp_reference")
    for name in ["cache_file_tag_instance", "cache_file_tag_header", "cache_file_structure_bsp_header",
                 "cache_file_header", "cache_file_globals"]:
        types += "\n" + structure(cache_files, name)
    code = function(cache_files, "cache_file_region_contains")
    code += "\n" + function(cache_files, "scenario_structure_bsp_load")
    if control:
        fault, _ = CONTROLS[control]
        code = mutated(code, *fault)
    return (("types.inc", types + "\n"), ("under_test.inc", code + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("structure_bsp_fill", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("structure_bsp_fill", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
