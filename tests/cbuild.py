"""Compiles C for the tests, and shows what the compiler said when it fails (a bare `check=True` leaves the diagnostics out of the log)."""

import subprocess
import sys
from collections.abc import Sequence
from typing import Any

# the system libraries that a program built with MinGW links (the headers use `#pragma comment(lib, ...)` for MSVC only)
LIBS: list[str] = ["-lbcrypt", "-lws2_32"] if sys.platform == "win32" else []


def build(command: Sequence[str], **options: Any) -> subprocess.CompletedProcess[str]:
    if LIBS and not {"-c", "-fsyntax-only"} & set(command):  # a program is linked, a compilation or a check is not
        command = [*command, *LIBS]
    done = subprocess.run(command, capture_output=True, text=True, check=False, **options)
    if done.returncode:
        raise AssertionError(f"{' '.join(map(str, command))}\nexit {done.returncode}\n{done.stdout}{done.stderr}")
    return done
