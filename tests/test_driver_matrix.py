"""Every C program of the repository builds without a warning, optimised, with the compilers that are here.

A warning of the headers (`-Wmaybe-uninitialized`, `-Wclobbered`, ...) appears only when the compiler optimises and inlines a function into the program that calls it, so compiling a
header alone (as `test_wasi_p1_headers.py` does) is not enough: a user who builds with `-Wall -Wextra -Werror -O1` meets it. Each driver of `tests/data/` and each program of
`tools/wasi_p2_conformance/` (the programs that are compared with Wasmtime; they are compiled here, not run) is compiled at -O1, -O2, -O3 and -Os with gcc, clang and the MinGW
cross compiler, whichever are installed. No `-Wno-...` flag: a finding is fixed in the header. The driver of the neutral user over wasm3 has its own matrix in
`tests/neutral/test_neutral_wasm3.py` (it needs the sources of wasm3).
"""

import shutil
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import pytest
from cbuild import build

REPO = Path(__file__).resolve().parent.parent
INCLUDE = REPO / "src" / "fusedwasi" / "include"
PROGRAMS = sorted((REPO / "tests" / "data").glob("*.c")) + sorted((REPO / "tools" / "wasi_p2_conformance").glob("*.c"))
COMPILERS = [c for c in ("gcc", "clang", "x86_64-w64-mingw32-gcc") if shutil.which(c)]
# the drivers that use the POSIX calls pipe() and poll() are not built for Windows (their test is POSIX only, too)
POSIX_ONLY = {"wasi_p2_cli_driver.c"}


def for_windows(compiler: str) -> bool:
    return sys.platform == "win32" or "mingw" in compiler


def test_there_are_programs_to_build() -> None:
    names = {p.name for p in PROGRAMS}
    assert {"wasi_p1_driver.c", "wasi_p2_filesystem_driver.c", "ours_c.c", "ours_http_c.c"} <= names


@pytest.mark.skipif(not COMPILERS, reason="no C compiler")
@pytest.mark.parametrize("level", ["-O1", "-O2", "-O3", "-Os"])
@pytest.mark.parametrize("compiler", COMPILERS)
def test_every_program_builds_without_a_warning(compiler: str, level: str, tmp_path: Path) -> None:
    flags = [
        "-std=gnu11",
        level,
        "-Wall",
        "-Wextra",
        "-Werror",
        "-c",
        f"-I{INCLUDE}",
        f"-I{INCLUDE / 'fusedwasi' / 'p1'}",
        f"-I{INCLUDE / 'fusedwasi' / 'p2'}",
        f"-I{REPO / 'tests' / 'data'}",
    ]
    programs = [p for p in PROGRAMS if not (for_windows(compiler) and p.name in POSIX_ONLY)]

    def compile_one(program: Path) -> None:
        build([compiler, *flags, str(program), "-o", str(tmp_path / (program.stem + ".o"))])

    # the compilations are independent, and a few of them take seconds when optimised
    with ThreadPoolExecutor() as pool:
        list(pool.map(compile_one, programs))
