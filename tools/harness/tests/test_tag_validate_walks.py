"""The map validator's walks (port/linux/game/tag_validate.c), taken whole, over made-up schemas: the elements of a
block that would do nothing in a pass are not entered, and that changes no correction, check or refusal."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, mutated, read, run, structure  # noqa: E402

CASES = ["idle-elements-skipped", "values", "checks", "nested-checks", "structure-checks", "too-deep"]

# a fault in tag_validate.c, and the case that must catch it
NEGATIVE_CONTROLS = {
    "every-element-walked": (("if (block->count > 0 && block_walk_idle(validation, field->definition))",
                              "if (0)"), "idle-elements-skipped"),
    "checks-not-passed-up": (("\t\t\tflags |= inner->flags;\n", ""), "nested-checks"),
    "checks-bit-in-both-walks": (("bit = validation->pass == _pass_checks ? _definition_walk_checks_bit : "
                                  "_definition_walk_first_bit;", "bit = _definition_walk_checks_bit;"), "values"),
    "first-bit-in-both-walks": (("bit = validation->pass == _pass_checks ? _definition_walk_checks_bit : "
                                 "_definition_walk_first_bit;", "bit = _definition_walk_first_bit;"), "checks"),
    "structures-not-passed-up": (("\t\tcase _tag_schema_struct:\n\t\tcase _tag_schema_block:\n\t\t\tinner",
                                  "\t\tcase _tag_schema_block:\n\t\t\tinner"), "structure-checks"),
    "deep-elements-skipped": (("validation->depth + walk->height <= MAXIMUM_VALIDATION_DEPTH &&", ""), "too-deep"),
}

VALIDATOR = "port/linux/game/tag_validate.c"


def game_types():
    groups = read("source/tag_files/tag_groups.h")
    return "\n".join([
        "struct tag_block_definition;",
        "struct tag_data_definition;",
        structure(groups, "tag_block"),
        structure(groups, "tag_reference"),
        structure(groups, "tag_data"),
        "#define TAG_CACHE_SIZE %#x" % constant(read("source/cache/physical_memory_map.h"), "TAG_CACHE_SIZE"),
        "",
    ])


def chain():
    """MAXIMUM_VALIDATION_DEPTH blocks nested under a root: each element's block is the next level's."""
    length = constant(read(VALIDATOR), "MAXIMUM_VALIDATION_DEPTH")
    lines = ["enum { DEPTH_CHAIN_LENGTH = %d };" % length,
             "static struct tag_schema_field const chain_%d_fields[] = { TAG_SCHEMA_END };" % length,
             "static struct tag_schema_definition const chain_%d = TAG_SCHEMA_DEFINITION(chain_%d, struct root, "
             "chain_%d_fields);" % (length, length, length)]
    for level in range(length - 1, -1, -1):
        name = "chain_root" if level == 0 else "chain_%d" % level
        lines.append("static struct tag_schema_field const %s_fields[] = { TAG_SCHEMA_BLOCK(struct root, block, "
                     "chain_%d, 0), TAG_SCHEMA_END };" % (name, level + 1))
        lines.append("static struct tag_schema_definition const %s = TAG_SCHEMA_DEFINITION(%s, struct root, "
                     "%s_fields);" % (name, name, name))
    lines.append("static struct tag_schema_definition const *const chain_definitions[] = { %s };" %
                 ", ".join("&chain_%d" % level for level in range(1, length + 1)))
    return "\n".join(lines) + "\n"


def sources(validator=None):
    text = validator if validator is not None else read(VALIDATOR)
    for line in ('#include "cseries.h"\n', '#include "cache/physical_memory_map.h"\n'):
        text = mutated(text, line, "")
    # (the elements it enters, counted)
    text = mutated(text, "\tframe = &validation->frames[validation->depth++];",
                   "\tharness_entered++;\n\tframe = &validation->frames[validation->depth++];")
    schema = mutated(read("port/linux/game/tag_schema.h"), '#include "tag_files/tag_groups.h"\n', "")
    return (("game_types.inc", game_types()), ("tag_schema.h", schema), ("chain.inc", chain()),
            ("under_test.inc", text))


@pytest.mark.parametrize("case", CASES)
def test_walks(case):
    status, output = run(build("tag_validate_walks", sources()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", sorted(NEGATIVE_CONTROLS))
def test_negative_control(control):
    (before, after), case = NEGATIVE_CONTROLS[control]
    status, output = run(build("tag_validate_walks", sources(mutated(read(VALIDATOR), before, after))), case)
    assert status == CHECK_FAILED, f"{control} was not caught by {case}: {output}"
