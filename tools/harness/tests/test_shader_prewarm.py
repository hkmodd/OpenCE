"""The device's shaders and a map's programs linked ahead (shader_prewarm.c): a text compiled once, a program written
down once for each map and linked ahead in its order when the map next loads, a file cut short kept as far as it reads,
a file anything may have written linking only what reads as it should, nothing written with the setting off, and a
file name made of the map's name."""

import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

SOURCE = "port/linux/src/xgpu_shaders.c"
CASES = ["once", "record", "once_a_map", "cut", "untrusted", "off", "name"]

# the files' folder, the data root's shaders folder
_root = tempfile.TemporaryDirectory(prefix="halo-shader-prewarm-")
(Path(_root.name) / "shaders").mkdir()

# a fault and the case that must catch it
NEGATIVE_CONTROLS = {
    "compiled-again": (("entry->type == type && !strcmp(entry->source, source)", "entry->type == type && 0"),
                       "once"),
    "written-each-time": (("\tif (*serial == shaders.serial)\n\t\treturn;", "\tif (0)\n\t\treturn;"), "once_a_map"),
    "ahead-written": (("\t\tgood = file_read(buffer, size);\n",
                       "\t\tshaders.file = fopen(path, \"ab\");\n\t\tgood = file_read(buffer, size);\n"
                       "\t\tfclose(shaders.file);\n\t\tshaders.file = NULL;\n"), "once_a_map"),
    "cut-dropped": (("\t\tgood = file_read(buffer, size);", "\t\tgood = file_read(buffer, size) == size ? size : 0;"),
                    "cut"),
    "types-trusted": (("\t\t\t\ttypes[vertex] != GL_VERTEX_SHADER || types[fragment] != GL_FRAGMENT_SHADER)",
                       "\t\t\t\t0)"), "untrusted"),
    "name-unbounded": (("\tchar name[32 + 1];", "\tchar name[64];"), "name"),
    "setting-ignored":(("!config_boolean(\"display.shader_prewarm\") || ", ""), "off"),
}


def generated(fault=None):
    source = read(SOURCE).replace("\r\n", "\n")
    if fault:
        source = mutated(source, *fault[0])
    root = Path(_root.name).as_posix()
    return (("world.inc", f'#define DATA_ROOT "{root}"\n'), ("xgpu.h", ""), ("posix.h", ""), ("port_config.h", ""),
            ("under_test.inc", source))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("shader_prewarm", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault = NEGATIVE_CONTROLS[control]
    status, output = run(build("shader_prewarm", generated(fault)), fault[1])
    assert status == CHECK_FAILED, f"the code with a fault ({control}) passed its case: the test cannot see it"
