"""The headers of WASI 0.1 (`fusedwasi/p1/`) are plain C: strict C11, no warnings, no Python and no wasm2c runtime on the include path."""

import re
import shutil
from pathlib import Path

import pytest
from cbuild import build

P1 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p1"
SOURCE = (
    '#include "wasi_p1_core.h"\n#include "wasi_p1.h"\n#include "wasi_p1_unstable.h"\nint main(void) { return 0; }\n'
)
COMPILERS = [c for c in ("gcc", "clang", "cc") if shutil.which(c)]


@pytest.mark.skipif(not COMPILERS, reason="a C compiler")
@pytest.mark.parametrize("compiler", sorted(set(COMPILERS)))
def test_the_headers_compile_strictly_with_nothing_else(compiler: str, tmp_path: Path) -> None:
    source = tmp_path / "check.c"
    source.write_text(SOURCE)
    build(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsyntax-only",
            f"-I{P1}",
            f"-I{P1.parent / 'p2'}",
            str(source),
        ],
    )


def test_the_headers_do_not_name_python_or_the_runtime_of_wasm2c() -> None:
    for header in P1.glob("*.h"):
        text = header.read_text(encoding="utf-8")
        assert not re.search(r"Python\.h|PyObject|Py_|PyErr|PyMem", text), header.name
        assert not re.search(r"wasm[-_]rt", text), header.name


def test_every_name_has_the_prefix_of_the_library() -> None:
    """Defined or declared names are `fusedwasi_p1_*` / `FUSEDWASI_P1_*` (the headers are included into the C of other projects)."""
    for header in P1.glob("*.h"):
        text = header.read_text(encoding="utf-8")
        defined = set(re.findall(r"^#\s*define\s+([A-Za-z_]\w*)", text, re.MULTILINE))
        defined |= set(re.findall(r"^static\s+inline\s+[^;{(=]*?\b([A-Za-z_]\w*)\s*\(", text, re.MULTILINE))
        loose = {
            name
            for name in defined
            if not name.startswith(
                ("fusedwasi_p1_", "FUSEDWASI_P1_", "_GNU_SOURCE", "_DARWIN_C_SOURCE", "WIN32_LEAN_AND_MEAN")
            )
        }
        assert not loose, (header.name, sorted(loose))


@pytest.mark.skipif(not COMPILERS, reason="a C compiler")
@pytest.mark.parametrize("compiler", sorted(set(COMPILERS)))
def test_the_headers_are_found_by_the_published_include_path_alone(compiler: str, tmp_path: Path) -> None:
    """`get_include()` and the CMake target `fusedwasi::headers` give one directory, the one that holds `fusedwasi/`: `#include <fusedwasi/p1/wasi_p1.h>` has to work with just that."""
    source = tmp_path / "check.c"
    source.write_text(
        "#include <fusedwasi/p1/wasi_p1.h>\n#include <fusedwasi/p1/wasi_p1_unstable.h>\nint main(void) { return 0; }\n"
    )
    build(
        [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-fsyntax-only",
            f"-I{P1.parent.parent}",
            str(source),
        ],
    )
