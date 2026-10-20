"""The renderer's write watch (port/linux/src/memory_watch.c, port/windows/src/win32_memory_watch.c) over a fake page
table: a write to a watched page is seen, and a page written once and watched again is read-only again, so the next
write is seen too (when it was not, a texture slot reused by another bitmap, or a vertex buffer, was drawn stale)."""

import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, function, mutated, read, run  # noqa: E402

CASES = ["write-seen", "write-seen-after-watching-again", "watched-page-protected-once"]

FILES = {
    "linux": ("port/linux/src/memory_watch.c",
              ["lock_pages", "unlock_pages", "page_index", "mark_written", "crash_handler", "memory_watch_protect",
               "memory_watch_generation"]),
    "windows": ("port/windows/src/win32_memory_watch.c",
                ["lock_pages", "unlock_pages", "in_window", "page_index", "mark_written", "watch_handler",
                 "memory_watch_protect", "memory_watch_generation"]),
}

BUMP = {"linux": "page_generation[page] = __sync_add_and_fetch(&current_generation, 1);\n\tpage_protected[page] = 2;",
        "windows": "page_generation[page] = InterlockedIncrement(&current_generation);\n\tpage_protected[page] = 2;"}

# faults in the watch and the case that must catch each (per file)
NEGATIVE_CONTROLS = {
    "written-page-left-writable": (lambda platform: [("lock_pages();\n\t\t\tif (page_protected[page] != 1)",
                                                      "lock_pages();\n\t\t\tif (!page_protected[page])")],
                                   "write-seen-after-watching-again"),
    "write-not-counted": (lambda platform: [(BUMP[platform], "page_protected[page] = 2;")], "write-seen"),
    "every-page-protected-again": (lambda platform: [("\t\tif (page_protected[page] != 1)\n\t\t{", "\t\tif (1)\n\t\t{"),
                                                     ("lock_pages();\n\t\t\tif (page_protected[page] != 1)",
                                                      "lock_pages();\n\t\t\tif (1)")],
                                   "watched-page-protected-once"),
}


def generated(platform, faults=()):
    path, names = FILES[platform]
    source = read(path)
    # the page states and the lock, then the functions (the window: platform.h's, or win32_memory_watch.c's own)
    start = source.index("#define WATCH_PAGE_SIZE")
    code = source[start:source.index("\n", source.index("watch_lock;")) + 1]
    code += "\n".join(function(source, name) for name in names) + "\n"
    for before, after in faults:
        code = mutated(code, before, after)
    if platform == "windows":
        prefix = "#define WATCH_WINDOWS 1\n" + \
            source[source.index("#define PLATFORM_CONTIGUOUS_BASE"):source.index("#define WATCH_PAGE_SIZE")]
    else:
        base = re.search(r"#define PLATFORM_CONTIGUOUS_BASE \w+", read("port/linux/src/platform.h")).group(0)
        prefix = "#define WATCH_WINDOWS 0\n%s\n#define PLATFORM_CONTIGUOUS_SIZE 0x20000000UL\n" % base
    return (("watch_platform.h", prefix), ("under_test.inc", code))


@pytest.mark.parametrize("platform", FILES)
@pytest.mark.parametrize("case", CASES)
def test_case(platform, case):
    status, output = run(build("memory_watch", generated(platform)), case)
    assert status == 0, output


@pytest.mark.parametrize("platform", FILES)
@pytest.mark.parametrize("control", NEGATIVE_CONTROLS)
def test_negative_control(platform, control):
    faults, case = NEGATIVE_CONTROLS[control]
    status, output = run(build("memory_watch", generated(platform, tuple(faults(platform)))), case)
    assert status == CHECK_FAILED, f"{FILES[platform][0]} with a fault ({control}) passed '{case}': the test cannot see it"
