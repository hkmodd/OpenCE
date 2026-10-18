"""The portal traversal with its hulls remembered renders what the game's (projecting every portal it reaches) renders,
and projects each portal at most once in each direction a traversal.

    python tools/harness/tests/test_portal_hull_cache.py
"""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["same-as-uncached", "camera-moves", "many-portals", "large-hulls", "numbering-wraps"]

# a fault in the cache, and the case that must catch it
NEGATIVE_CONTROLS = {
    "ignores-the-direction": (("2 * (long)portal_index + (direction ? 1 : 0)", "2 * (long)portal_index"), "same-as-uncached"),
    "keeps-the-result-of-another": (("entry->result = hull_result;", "entry->result = _portal_hull_from_portal_succeeded;"),
                                    "same-as-uncached"),
    "copies-the-wrong-vertices": (("\t\t\tresult->vertices,\n\t\t\t&portal_hull_cache.vertices[entry->first_vertex_index],",
                                   "\t\t\tresult->vertices,\n\t\t\t&portal_hull_cache.vertices[entry->first_vertex_index + 1],"),
                                  "same-as-uncached"),
    "never-forgets": (("if (++portal_hull_cache.traversal == 0)", "if (portal_hull_cache.traversal == 0)"), "camera-moves"),
    "wrap-keeps-old-hulls": (("\t\t\t\tsizeof(portal_hull_cache.entries));", "\t\t\t\t0);"), "numbering-wraps"),
}

TRAVERSAL = ("structure_visibility_traverse_cluster", "structure_visibility_find_clusters")


def under_test(fault=None):
    visibility = read("source/structures/structure_visibility.c").replace("\r\n", "\n")
    code = "\n".join(function(visibility, name) for name in
                     ("portal_hull_from_portal_cached", "grow_clipping_rectangle_by_portal_hull") + TRAVERSAL)
    # the game's traversal: every portal projected, nothing remembered
    reference = "\n".join(function(visibility, name) for name in TRAVERSAL)
    reference, forgotten = re.subn(r"\t\t/\* port: the hulls of an earlier traversal.*?portal_hull_cache\.vertex_count = 0;\n", "",
                                   reference, flags=re.S)
    assert forgotten == 1, "the traversal no longer begins by forgetting the hulls of the one before"
    reference = reference.replace("portal_hull_from_portal_cached(", "portal_hull_from_portal(")
    for name in TRAVERSAL:
        reference = reference.replace(name + "(", name + "_reference(")
    context = "enum { MAXIMUM_CLUSTERS_PER_STRUCTURE = %d, MAXIMUM_RENDERED_CLUSTERS = %d };\n" % (
        constant(read("source/structures/structures.h"), "MAXIMUM_CLUSTERS_PER_STRUCTURE"),
        constant(read("source/render/render.h"), "MAXIMUM_RENDERED_CLUSTERS"))
    context += enum_with(visibility, "MAXIMUM_PORTAL_HULL_CACHE_ENTRIES") + "\n"
    render = read("source/render/render.h")
    context += "\n".join([structure(visibility, name) for name in (
        "portal_hull", "portal_hull_cache_entry", "portal_hull_cache", "structure_visibility_cluster",
        "structure_visibility_portal", "structure_visibility_globals")] + [structure(render, "rendered_cluster")]) + "\n"
    code = mutated(code, *fault) if fault else code
    return (("context.inc", context), ("under_test.inc", code + "\n" + reference + "\n"))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("portal_hull_cache", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("portal_hull_cache", under_test(fault)), case)
    assert status == CHECK_FAILED, f"the cache with a fault ({control}) passed '{case}': the test cannot see it"


if __name__ == "__main__":
    sys.exit(pytest.main(["-q", __file__]))
