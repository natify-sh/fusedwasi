"""Compiles C for the tests, and shows what the compiler said when it fails (a bare `check=True` leaves the diagnostics out of the log)."""

import subprocess
from collections.abc import Sequence
from typing import Any


def build(command: Sequence[str], **options: Any) -> subprocess.CompletedProcess[str]:
    done = subprocess.run(command, capture_output=True, text=True, check=False, **options)
    if done.returncode:
        raise AssertionError(f"{' '.join(map(str, command))}\nexit {done.returncode}\n{done.stdout}{done.stderr}")
    return done
