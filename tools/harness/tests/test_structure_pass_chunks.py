"""structure_render_pass draws the environment's surfaces from the chunks structure_render_preprocess found as the original's walk did."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run, structure  # noqa: E402

CASES = ["environment", "unordered", "uncovered", "other-lists", "stale", "invalid", "empty"]

# a fault in a function of the chunks, and the case that must catch it
NEGATIVE_CONTROLS = {
    "chunks-unused": ("structure_render_pass", ("if (surface_indices == render.environment_surface_indices &&",
                                                "if (0 && surface_indices == render.environment_surface_indices &&"),
                      "environment"),
    "any-list": ("structure_render_pass", ("surface_indices == render.environment_surface_indices &&\n", ""),
                 "other-lists"),
    "any-count": ("structure_render_pass", ("surface_count == environment_chunk_surface_count &&\n", ""), "stale"),
    "any-triangles": ("structure_render_pass", ("dynamic_triangles_index == environment_chunk_triangles_index)",
                                                "1)"), "stale"),
    "lightmap-not-ended": ("structure_render_pass", ("if (lightmap_index != NONE && end_lightmap)\n\t\t\t\t{",
                                                     "if (0)\n\t\t\t\t{"), "environment"),
    "triangles-not-advanced": ("structure_render_pass", ("first_triangle_index += chunk->surface_count;", ""),
                               "environment"),
    "lightmap-not-skipped": ("structure_render_find_chunks",
                             ("\t\t\tcontinue;\n\t\t}\n\n\t\tfor (material_index", "\t\t}\n\n\t\tfor (material_index"),
                             "unordered"),
    "uncovered-kept": ("find_environment_chunks", ("if (chunked_surface_count == render.environment_surface_count)",
                                                   "if (1)"), "uncovered"),
    "not-reset": ("find_environment_chunks", ("environment_chunk_surface_count = NONE;\n", ""), "invalid"),
}


def between(text, start, end):
    at = text.index(start)
    return text[at:text.index(end, at) + len(end)]


def under_test(fault=None):
    source = read("source/structures/structure_render.c").replace("\r\n", "\n")
    enums = enum_with(source, "_structure_material_coplanar_bit") + "\n"
    enums += "#define MAXIMUM_RENDERED_ENVIRONMENT_SURFACES %d\n" % constant(
        read("source/render/render.h"), "MAXIMUM_RENDERED_ENVIRONMENT_SURFACES")
    parts = {name: function(source, name) for name in
             ("structure_render_pass", "structure_render_find_chunks", "structure_render_draw_chunk")}
    find = between(source, "\t/* port: the chunks the passes draw", "\n\t\t}\n\t}\n")
    parts["find_environment_chunks"] = "static void find_environment_chunks(void)\n{\n" + find + "}\n"
    if fault:
        name, change, _ = NEGATIVE_CONTROLS[fault]
        parts[name] = mutated(parts[name], *change)
    text = structure(source, "structure_render_globals") + "\n"
    text += "static struct structure_render_globals structure_render_globals;\n"
    text += structure(source, "structure_render_chunk") + "\n"
    text += between(source, "static long structure_render_find_chunks(", ");\n")
    text += between(source, "static void structure_render_draw_chunk(", ");\n")
    text += between(source, "static struct structure_render_chunk environment_chunks", "= NONE;\n")
    text += between(source, "static long environment_chunk_triangles_index", "= NONE;\n")
    text += "\n".join(parts.values()) + "\n"
    return (("enums.inc", enums), ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("structure_pass_chunks", under_test()), case)
    assert status == 0, output


@pytest.mark.parametrize("fault", NEGATIVE_CONTROLS)
def test_negative_control(fault):
    status, output = run(build("structure_pass_chunks", under_test(fault)), NEGATIVE_CONTROLS[fault][2])
    assert status == CHECK_FAILED, f"{fault} escaped its check: {output}"
