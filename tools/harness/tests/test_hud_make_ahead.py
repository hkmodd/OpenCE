"""The high-res HUD's GL textures made as a map loads (hud_make_ahead.c): by a command queued for the render thread
with display.render_thread, never on the game's thread, and at once without it; each PNG decoded once whichever of
the decoding thread and the command reaches it first, its texels freed, none left for its first draw, the driver's
work finished; titles, those drawn or failed, and those with the setting off or no renderer not made.

    python -m pytest -q tools/harness/tests/test_hud_make_ahead.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure  # noqa: E402

SOURCE = "port/linux/src/hud_hires.c"
CASES = ["render-thread", "render-thread-first", "no-render-thread", "skipped", "undecodable", "no-thread"]

# a fault, and the case that must catch it
NEGATIVE_CONTROLS = {
    "not-queued": (("\t\t\trender_thread_record(textures_make_ahead, 0);\n\t\t\trender_thread_submit();\n", "\t\t\t;\n"),
                   "render-thread"),
    "on-the-game-thread": (("\t\tif (render_thread_elsewhere())\n\t\t{\n\t\t\trender_thread_record",
                            "\t\tif (0)\n\t\t{\n\t\t\trender_thread_record"), "render-thread"),
    "not-made-at-once": (("\t\telse\n\t\t\ttextures_make_ahead(NULL);\n", ""), "no-render-thread"),
    "only-those-decoded": (("queued = textures[asset].decode_state != _decode_none;",
                            "queued = textures[asset].decode_state == _decode_done;"), "render-thread-first"),
    "not-finished": (("\tglFinish();\n}\n", "}\n"), "render-thread"),
}


def generated(fault=None):
    source = read(SOURCE).replace("\r\n", "\n")
    header = read("port/linux/src/hud_hires.h").replace("\r\n", "\n")
    states = source.index("/* a texture's PNG decoded ahead of its first draw (decode_state) */")
    world = ("#define MAXIMUM_TEXTURES 256\n" + structure(header, "hud_hires_embedded") + "\n"
             + source[states:source.index("long hud_hires_asset_count(void)", states)])
    loaded = function(source, "hud_hires_map_loaded")
    start = source.index("/* ---------- decoding ahead */")
    ahead = source[start:source.index(loaded) + len(loaded)]
    if fault:
        ahead = mutated(ahead, *fault)
    return (("world.inc", world),
            ("under_test.inc", function(source, "hud_hires_override_texture") + "\n\n" + ahead + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("hud_make_ahead", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("hud_make_ahead", generated(fault)), case)
    assert status == CHECK_FAILED, f"the textures made with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
