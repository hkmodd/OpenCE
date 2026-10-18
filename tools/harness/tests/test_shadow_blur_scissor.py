"""A shadow map's bound holds every pixel of the map that is not black, and a pass of the blur drawn within the bounds
leaves every map exactly as the whole pass leaves it."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, function, mutated, read, run

CASES = ["silhouette", "cuts", "blur", "edges", "states", "invalidation"]
NEGATIVE_CONTROLS = {
    "rect-not-widened": (("rect[2] = (GLint)x1 + 3;", "rect[2] = (GLint)x1;"), "silhouette"),
    "rows-from-the-bottom": (("(1.0f - y) * 0.5f", "(y + 1.0f) * 0.5f"), "silhouette"),
    "one-node": (("node < draw_bounds.count", "node < 1"), "silhouette"),
    "behind-the-eye": (("if (!(w > 1e-3f))\n\t\t\t\treturn FALSE;", "if (!(w > 1e-3f))\n\t\t\t\tcontinue;"),
        "silhouette"),
    "node-rows-shifted": (("XGPU_MODEL_NODE_REGISTER + first + row", "XGPU_MODEL_NODE_REGISTER + first + row + 3"),
        "silhouette"),
    "reach-short": (("reach = (GLint)reach_texels + 2;", "reach = (GLint)reach_texels;"), "edges"),
    "source-assumed-valid": (("!target_bound_valid(source, source->target.written)", "!source->bound_written"),
        "blur"),
    "destination-assumed-valid": (("target_bound_valid(destination, shadow_instances.written[index])",
        "destination->bound_written"), "blur"),
    "union-skipped": (("else if (!bound_empty(destination->bound))", "else if (0)"), "edges"),
    "no-edge-bands": (("edge && strip < 4", "edge && strip < 0"), "edges"),
    "edge-not-carried": (("bound_edge = source->bound_edge + reach < width ? source->bound_edge + reach : width;",
        "bound_edge = reach;"), "edges"),
    "ignores-the-viewport": (("gl_state.scissor_test && !gl_state.scissor[0] && !gl_state.scissor[1] && "
        "gl_state.scissor[2] == width &&\n\t\tgl_state.scissor[3] == height;", "1;"), "states"),
    "ignores-alpha-kill": (("cut = FALSE;", "(void)0;"), "states"),
    "partial-clear-empties": (("(!count || !rectangles) && ", ""), "invalidation"),
    "half-viewport-clear-empties": (
        ("target_pixel((float)(device.viewport.X + device.viewport.Width), 0) >= (GLint)entry->target.gl_width &&", ""),
        "invalidation"),
    "deferred-draws-bounded": (("|| instancing_drawing ||", "||"), "invalidation"),
    "stale-bound-extended": (("!target_bound_valid(entry, bound_target_previous) || !draw_bounds_rect(rect)",
        "!draw_bounds_rect(rect)"), "invalidation"),
    "nonblack-clear-kept": (("else if (!black || ", "else if ("), "invalidation"),
}


def instances_table(source):
    """The immediate draw's instances, a static of an unnamed structure."""
    match = re.search(r"^static struct\s*\{[^{}]*\} shadow_instances;", source, re.M)
    if not match:
        raise LookupError("shadow_instances not found in the sources")
    return match.group(0)


def generated(fault=None):
    gl = read("port/linux/src/d3d8_gl.c").replace("\r\n", "\n")
    xgpu = read("port/linux/src/xgpu.h").replace("\r\n", "\n")
    fixups = read("port/linux/include/halo_linux_source_fixups.h")
    defines = "".join(re.search(r"^#define " + name + r" .*$", xgpu, re.M).group(0) + "\n"
        for name in ["XGPU_VERTEX_CONSTANT_COUNT", "XGPU_VERTEX_CONSTANT_BIAS", "XGPU_MODEL_NODE_REGISTER"])
    defines += f"#define SHADOW_LAYER_INSTANCES {constant(fixups, 'HALO_SHADOW_LAYERS_MAXIMUM')}\n"
    bounds = gl[gl.index("static struct render_target_entry *bound_target;"):gl.index("\n#endif\n\nvoid halo_draw_bounds(")]
    code = "\n".join([instances_table(gl), function(gl, "scaled_pixel"), function(gl, "target_pixel"), bounds,
        function(gl, "shadow_instances_draw")])
    if fault:
        code = mutated(code, *fault)
    return (("under_test_defines.inc", defines), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("shadow_blur_scissor", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("shadow_blur_scissor", generated(fault)), case)
    assert status == CHECK_FAILED, f"{control} escaped {case}: {output}"
