"""The high-res HUD's textures decoded as a map loads (hud_preload.c): each once, on threads of their own, none left
for its first draw, those drawn, failed or with their setting off skipped, the texels held within the budget, the
uploads made on the render thread, and nothing decoded ahead without threads or without a GL context."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run, structure  # noqa: E402

SOURCE = "port/linux/src/hud_hires.c"
CASES = ["decoded-ahead", "skipped", "undecodable", "budget", "render-thread", "no-threads", "no-renderer"]

# a fault in the preloading, and the case that must catch it
NEGATIVE_CONTROLS = {
    "drawn-decoded-again": (("!textures[asset].texture && !textures[asset].failed &&", "!textures[asset].failed &&"),
                            "skipped"),
    "setting-ignored": (("(hud_hires_embedded[asset].title ? titles_enabled : hud_enabled)", "1"), "skipped"),
    "budget-ignored": (("preload.held && preload.held + preload_size(preload.assets[index]) > PRELOAD_BUDGET", "0"),
                       "budget"),
    "failure-unmarked": (("\t\ttextures[asset].failed = 1;\n", ""), "undecodable"),
    "uploaded-here": (("if (render_thread_elsewhere())", "if (0)"), "render-thread"),
    "one-thread": (("while (preload.threads < PRELOAD_THREADS)", "while (preload.threads < 1)"), "decoded-ahead"),
    "no-context-ignored": (("if (!wanted_count || !xgpu_gl_ready())", "if (!wanted_count)"), "no-renderer"),
}


def generated(fault=None):
    source = read(SOURCE)
    header = read("port/linux/src/hud_hires.h")
    start = source.index("static struct\n{\n\tunsigned int texture;")
    world = (f"#define MAXIMUM_TEXTURES {constant(source, 'MAXIMUM_TEXTURES')}\n"
             + structure(header, "hud_hires_embedded") + "\n"
             + source[start:source.index(";", source.index("} textures[", start)) + 1] + "\n")
    preloading = source[source.index("/* the threads that decode a map's textures"):
                        source.index("#else\n\nvoid hud_hires_preload")]
    if fault:
        preloading = mutated(preloading, *fault)
    return (("world.inc", world),
            ("under_test.inc", preloading + "\n" + function(source, "hud_hires_override_texture") + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("hud_preload", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("hud_preload", generated(fault)), case)
    assert status == CHECK_FAILED, f"the preloading with a fault ({control}) passed '{case}': the test cannot see it"
