"""Compiles C for the tests, and shows what the compiler said when it fails (a bare `check=True` leaves the diagnostics out of the log).

The compiler is `cc`, `gcc` or `clang`, whichever is first on PATH; `FUSEDWASI_TEST_CC` names another (`cl` on Windows, in the environment of vcvars64.bat, builds the same programs with MSVC
at /W4 /WX). A compiler that is named and is not found is an error, not a skipped test: a run that was meant to use MSVC must not pass by running nothing.
"""

import os
import shutil
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any


def find_cc() -> str | None:
    named = os.environ.get("FUSEDWASI_TEST_CC")
    if named:
        found = shutil.which(named)
        if found is None:
            raise RuntimeError(f"FUSEDWASI_TEST_CC={named!r} is not an executable that can be found on PATH")
        return found
    return shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")


def is_msvc(compiler: str | None) -> bool:
    return compiler is not None and Path(compiler).stem.lower() in ("cl", "clang-cl")


CC: str | None = find_cc()
MSVC: bool = is_msvc(CC)

# the system libraries that a program built with MinGW links (MSVC gets them from the `#pragma comment(lib, ...)` of the headers)
LIBS: list[str] = ["-lbcrypt", "-lws2_32"] if sys.platform == "win32" else []

# the optimisation levels of the GNU compilers, and what cl calls them (-Os is the size)
_MSVC_LEVEL = {"-O0": "/Od", "-O1": "/O1", "-O2": "/O2", "-O3": "/Ox", "-Os": "/Os"}


def command(
    source: Path,
    output: Path,
    includes: Sequence[Path],
    *,
    compiler: str | None = None,
    level: str | None = None,
    werror: bool = True,
    compile_only: bool = False,
    extra: Sequence[str] = (),
) -> list[str]:
    """The command line that builds `source` into `output` (a program, or an object with `compile_only`), warnings on, and an error when there is one (`werror`)."""
    cc = compiler or CC
    if cc is None:
        raise RuntimeError("no C compiler")
    if is_msvc(cc):
        flags = [
            "/nologo",
            "/std:c11",
            "/utf-8",
            "/W4",
            *(["/WX"] if werror else []),
            *_MSVC_LEVEL.get(level or "", "").split(),
        ]
        flags += [f"/I{include}" for include in includes]
        flags += list(extra)
        if compile_only:
            return [cc, *flags, "/c", f"/Fo:{output}", str(source)]
        return [cc, *flags, f"/Fe:{output}", f"/Fo:{output.with_suffix('.obj')}", str(source)]
    flags = ["-std=gnu11", *([level] if level else []), "-Wall", "-Wextra", *(["-Werror"] if werror else [])]
    flags += [arg for include in includes for arg in ("-I", str(include))]
    flags += list(extra)
    return [cc, *flags, *(["-c"] if compile_only else []), str(source), "-o", str(output)]


def compile_c(source: Path, output: Path, includes: Sequence[Path], **options: Any) -> subprocess.CompletedProcess[str]:
    """`build(command(...))`."""
    return build(command(source, output, includes, **options))


def build(cmd: Sequence[str], **options: Any) -> subprocess.CompletedProcess[str]:
    if (
        LIBS and not is_msvc(cmd[0]) and not {"-c", "-fsyntax-only"} & set(cmd)
    ):  # a program is linked, a compilation or a check is not
        cmd = [*cmd, *LIBS]
    done = subprocess.run(cmd, capture_output=True, text=True, check=False, **options)
    if done.returncode:
        raise AssertionError(f"{' '.join(map(str, cmd))}\nexit {done.returncode}\n{done.stdout}{done.stderr}")
    return done
