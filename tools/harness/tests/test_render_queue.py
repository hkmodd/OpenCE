"""The real render thread's queue (render_thread.c) with a real reader thread: commands run once each, in order and
intact across many turns of the ring; a call returns once its command has run; a resource is busy until the command
that reads it has run; a frame ends, handed over, once no more than the frames asked for are queued."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run  # noqa: E402

CASES = ["runs-in-order-across-turns", "waits-for-room", "call-returns-once-run", "resource-busy-until-read",
         "one-frame-queued", "two-frames-queued", "frame-handed-over"]

# a fault in render_thread.c and the case that must catch it (the ring's
# room with the reader held, not as fast as it happens to be)
NEGATIVE_CONTROLS = {
    "writes-over-commands-not-run": (("else if (!reached_seen(recorded + advance - RING_SIZE))", "else if (0)"), "waits-for-room"),
    "room-from-its-own-position": (("if ((long)(completed_seen - position) >= 0)",
                                    "if ((long)(recorded - position) >= 0)"), "waits-for-room"),
    "call-does-not-wait": (("memcpy(copy, payload, size);\n\trender_thread_submit();\n\trender_thread_wait();",
                            "memcpy(copy, payload, size);\n\trender_thread_submit();"), "call-returns-once-run"),
    "busy-from-what-has-run": (("busy_until[busy_slot(data[index])] = recorded;",
                                "busy_until[busy_slot(data[index])] = reader_side.completed;"), "resource-busy-until-read"),
    "frame-end-does-not-wait": (("if (frames_ended >= frames)", "if (0)"), "one-frame-queued"),
    "frame-end-waits-for-the-latest": (("frame_ends[(frames_ended - frames) %", "frame_ends[(frames_ended - 1) %"),
                                       "two-frames-queued"),
    "frame-end-waits-for-itself": (("position = frame_ends[(frames_ended - frames) % RENDER_THREAD_MAXIMUM_FRAMES];",
                                    "position = recorded;"), "one-frame-queued"),
    "frame-not-handed-over": (("frames = RENDER_THREAD_MAXIMUM_FRAMES;\n\trender_thread_submit();",
                               "frames = RENDER_THREAD_MAXIMUM_FRAMES;"), "frame-handed-over"),
}


def generated(fault=None):
    source = read("port/linux/src/render_thread.c")
    # the port's headers give way to the test's stand-ins
    code = re.sub(r'^#include "(platform|port_config|sdl_platform)\.h"\n', "", source, flags=re.M)
    assert code.count("#include") == source.count("#include") - 3, "render_thread.c's includes have changed"
    if fault:
        code = mutated(code, *fault)
    return (("render_thread.h", read("port/linux/src/render_thread.h")), ("under_test.inc", code))


@pytest.mark.parametrize("case", CASES)
def test_case(case):
    status, output = run(build("render_queue", generated()), case)
    assert status == 0, output


@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(control):
    fault, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("render_queue", generated(fault)), case)
    assert status == CHECK_FAILED, f"render_thread.c with a fault ({control}) passed '{case}': the test cannot see it"
