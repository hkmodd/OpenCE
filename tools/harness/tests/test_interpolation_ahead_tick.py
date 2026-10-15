"""The frames draw the tick taken while the next runs in pieces (display.tick_slices): the first-person weapon, and
an object first taken on the tick drawn, keep that tick's pose (render_interpolation.c)."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run, structure

CASES = ["first-person-whole", "first-person-ahead", "first-person-ahead-before-frame", "first-person-weapon-changed",
         "first-person-weapon-more-nodes", "new-object-whole", "new-object-ahead", "new-object-corrected-ahead"]
CONTROLS = {
    # the pose taken every frame from the animation of the tick under way (the reload stepped)
    "first-person-every-frame": (("kept = first_person->has_latest && first_person_updated_tick > interpolation_tick &&",
                                  "kept = FALSE &&"), "first-person-ahead"),
    "first-person-updated-as-drawn": (("first_person_updated_tick = interpolation_tick + 1;",
                                       "first_person_updated_tick = interpolation_tick;"), "first-person-ahead"),
    "first-person-latest-kept": (("\t\tfirst_person->tick = interpolation_tick;\n\t\tfirst_person->has_latest = FALSE;\n",
                                  "\t\tfirst_person->tick = interpolation_tick;\n"), "first-person-ahead-before-frame"),
    "first-person-weapon-unseen": (("for (node_index = 0; kept && node_index < node_count; node_index++)",
                                    "for (node_index = 0; FALSE && node_index < node_count; node_index++)"),
                                   "first-person-weapon-changed"),
    "first-person-weapon-blended": (("\t\t\tfirst_person->has_previous = FALSE;\n", "\t\t\t;\n"),
                                    "first-person-weapon-more-nodes"),
    # a new object drawn as the tick under way left it, then blended back from the tick drawn
    "new-object-live": (("if (!record->has_previous && !tick_slices_in_flight())", "if (!record->has_previous)"),
                        "new-object-ahead"),
    "new-object-blended": (("record->poses_snap = !record->has_previous ||", "record->poses_snap ="),
                           "new-object-ahead"),
    "new-object-glides": (("boolean correcting = record->has_previous &&", "boolean correcting = TRUE &&"),
                          "new-object-corrected-ahead"),
}

INTERPOLATION = "port/linux/game/render_interpolation.c"
HELPERS = ["lerp", "point_lerp", "vector_length", "vector_nlerp", "basis_is_rotation", "quaternion_from_basis",
           "basis_from_quaternion", "rotation_from_matrix", "matrix_blend_rotations", "correction_significant",
           "correction_advance", "correction_drawn", "correction_add", "distance_squared", "poses_snap", "spin_pause",
           "record_blend", "record_claim_and_blend", "blend_objects", "workers_begin", "workers_end"]
UNDER_TEST = ["render_interpolation_tick", "render_interpolation_object_node_matrices",
              "render_interpolation_correct_object", "render_interpolation_first_person_updated",
              "render_interpolation_first_person"]
DEFINES = ["MAXIMUM_INTERPOLATED_NODES", "WORKER_RECORDS","OBJECT_SNAP_DISTANCE", "NODE_SNAP_DISTANCE", "FIRST_PERSON_SNAP_DISTANCE",
           "CORRECTION_DECAY", "CORRECTION_NEGLIGIBLE", "CORRECTION_MAXIMUM"]


def generated(control=None):
    source = read(INTERPOLATION).replace("\r\n", "\n")
    maths = read("source/math/matrix_math.c").replace("\r\n", "\n")
    types = [structure(read("source/math/real_math.h").replace("\r\n", "\n"), "real_matrix4x3"),
             "typedef struct real_matrix4x3 real_matrix4x3;"]
    types += [re.search(r"^#define " + name + r"\b.*$", source, re.M).group(0) for name in DEFINES]
    types += [structure(source, name) for name in
              ["interpolation_quaternion", "interpolation_rotation", "interpolated_object", "interpolated_camera",
               "interpolated_first_person"]]
    code = "\n".join(function(maths, name) for name in
                     ["matrix4x3_inverse", "matrix4x3_rotation_from_vectors", "matrix4x3_from_point_and_vectors",
                      "matrix4x3_inverse_transform_point", "matrix4x3_multiply"])
    code += "\n" + "\n".join(function(source, name) for name in HELPERS + UNDER_TEST)
    if control:
        code = mutated(code, *CONTROLS[control][0])
    return (("types.inc", "\n".join(types) + "\n"), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("interpolation_ahead_tick", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", CONTROLS)
def test_negative_control(control):
    status, output = run(build("interpolation_ahead_tick", generated(control)), CONTROLS[control][1])
    assert status == CHECK_FAILED, f"{control} escaped its check: {output}"
