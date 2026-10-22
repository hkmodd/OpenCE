"""An OpenXR runtime's manifest as port/linux/src/vr_openxr.c reads it (the game loads the runtime itself): Meta's and
SteamVR's 32-bit manifests, JSON's escapes, and where a relative library is."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run

CONTROLS = {
    "escape-kept": (("case '\"': case '\\\\': case '/': code = (unsigned char)escaped; break;",
                     "case '\"': case '/': code = (unsigned char)escaped; break;"), "manifest"),
    "suffix-matches": (("if (at == text || at[-1] != '\"' || *value != '\"')", "if (*value != '\"')"), "manifest"),
    "relative-from-here": (("folder = separator ? (size_t)(separator - manifest) + 1 : 0;",
                            "folder = separator ? 0 : 0;"),
                           "manifest"),
    "no-room-check": (("if (used + 1 >= size)\n\t\t\t\treturn 0;\n", ""), "manifest"),
}


def generated(control=None):
    source = read("port/linux/src/vr_openxr.c")
    code = "#include <stddef.h>\n#include <string.h>\n"
    code += function(source, "vr_manifest_string") + "\n" + function(source, "vr_manifest_library_path")
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("under_test.inc", code),)


def test_manifest():
    status, output = run(build("vr_manifest", generated()), "manifest")
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("vr_manifest", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
