"""The visibility traversal over a fake structure makes each portal's hull once a frame from each side, and finds what it found making it each time.

    python tools/harness/tests/test_portal_hulls.py
"""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

VISIBILITY = "source/structures/structure_visibility.c"

# the hulls made each time the traversal reaches a portal, as before they were kept
MEMO_OFF = ("if (portal_index < MAXIMUM_CLUSTER_PORTALS_PER_STRUCTURE)", "if (FALSE)")
# room for few hulls' vertices, so that most are made each time
SMALL_MEMO = ("#define MAXIMUM_PORTAL_HULL_MEMO_VERTICES 0x4000", "#define MAXIMUM_PORTAL_HULL_MEMO_VERTICES 16")

# a fault in the memo, and the check that must catch it: a case that fails, or results unlike the reference's
NEGATIVE_CONTROLS = {
    "memo-off": (MEMO_OFF, "once"),
    "never-emptied": (("\t\tportal_hull_memo_vertex_count = 0;\n", "\n"), "once"),
    "ignores-the-side": (("[portal_index][direction ? 1 : 0]", "[portal_index][0]"), "results"),
    "ignores-the-traversal": (("if (memo->pass == portal_hull_memo_pass)", "if (memo->pass != 0)"), "results"),
    "wrong-vertices": (("memo->first_vertex_index = portal_hull_memo_vertex_count;",
                        "memo->first_vertex_index = 0;"), "results"),
}


def under_test(*faults):
    visibility = read(VISIBILITY)
    for fault in faults:
        visibility = mutated(visibility, *fault)
    cseries = read("source/cseries/cseries.h")
    enums = enum_with(visibility, "_portal_hull_from_portal_degenerate") + "\n"
    for header, name in (("source/structures/structures.h", "MAXIMUM_CLUSTER_PORTALS_PER_STRUCTURE"),
                         ("source/structures/structures.h", "MAXIMUM_CLUSTERS_PER_STRUCTURE"),
                         ("source/render/render.h", "MAXIMUM_RENDERED_CLUSTERS"),
                         ("source/cseries/cseries.h", "LONG_BITS"), ("source/cseries/cseries.h", "LONG_BITS_BITS"),
                         ("source/cseries/cseries.h", "NUMBER_OF_VERTICES_PER_TRIANGLE"),
                         ("source/cseries/cseries.h", "NUMBER_OF_POINTS_PER_RECTANGLE")):
        enums += f"#define {name} {constant(read(header), name)}\n"
    enums += "".join(line + "\n" for line in cseries.splitlines()
                     if line.startswith(("#define SET_FLAG(", "#define BIT_VECTOR_TEST_FLAG(", "#define BIT_VECTOR_SET_FLAG(")))
    start = visibility.index("#define valid_portal_hull(hull)")
    text = visibility[start:visibility.index("#define structure_visibility_shader_environment_get", start)]
    text += structure(visibility, "portal_hull") + ";\n"
    text += structure(visibility, "portal_hull_memo") + ";\n"
    text += structure(visibility, "structure_visibility_globals") + ";\n"
    start = visibility.index("static const real_plane3d screen_plane")
    text += visibility[start:visibility.index("};", start) + 2] + "\n"
    text += "static struct structure_visibility_globals structure_visibility_globals;\n"
    start = visibility.index("/* port: the hulls made in this traversal")
    end = visibility.index("portal_hull_memo_vertices[MAXIMUM_PORTAL_HULL_MEMO_VERTICES];", start)
    text += visibility[start:end] + "portal_hull_memo_vertices[MAXIMUM_PORTAL_HULL_MEMO_VERTICES];\n"
    for name in ("grow_clipping_rectangle_by_portal_hull", "points_within_distance", "portal_hull_from_points",
                 "portal_hull_from_portal", "portal_hull_from_portal_once", "structure_visibility_traverse_cluster",
                 "structure_visibility_find_clusters"):
        text += function(visibility, name) + "\n"
    return (("enums.inc", enums), ("under_test.inc", text))


def results(case, *faults):
    status, output = run(build("portal_hulls", under_test(*faults)), case)
    assert status == 0, output
    return output


@pytest.mark.parametrize("case", ["once", "results", "results-wide"])
def test_case(case):
    status, output = run(build("portal_hulls", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("case", ["results", "results-wide"])
@pytest.mark.parametrize("room", ["room", "little-room"])
def test_same_results(case, room):
    memo = (SMALL_MEMO,) if room == "little-room" else ()
    assert results(case, *memo) == results(case, MEMO_OFF), "the traversal finds otherwise with the hulls kept"


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, check = NEGATIVE_CONTROLS[control]
    if check == "once":
        status, output = run(build("portal_hulls", under_test(fault)), "once")
        assert status == CHECK_FAILED, f"the memo with a fault ({control}) passed 'once': the test cannot see it"
    else:
        assert results("results", fault) != results("results", MEMO_OFF), \
            f"the memo with a fault ({control}) finds what the reference finds: the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
