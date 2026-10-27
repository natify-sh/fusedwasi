"""A runtime adapter for the official wasi-testsuite (https://github.com/WebAssembly/wasi-testsuite, branch prod/testsuite-base): the runtime is
`neutral_wasmtime_run.py`, which runs the module on wasmtime-py with the functions of fusedwasi.

    python run-tests --runtime-adapter tests/neutral/wasi_testsuite_adapter.py

(from the checkout of the testsuite). It answers for WASI 0.1 only (`wasm32-wasip1`, world `wasi:cli/command`); the suites for other versions or worlds are skipped by the runner.
Loaded by the runner as a module: nothing here may print when it is run as a program.
"""

from __future__ import annotations

import sys
from pathlib import Path

RUN = str(Path(__file__).resolve().with_name("neutral_wasmtime_run.py"))


def get_name() -> str:
    return "fusedwasi over wasmtime-py"


def get_version() -> str:
    from importlib.metadata import version

    return version("wasmtime")


def get_wasi_versions() -> list[str]:
    return ["wasm32-wasip1"]


def get_wasi_worlds() -> list[str]:
    return ["wasi:cli/command"]


def get_timeout_seconds() -> float:
    return 60.0  # a process of Python and a first compile of the module, on a loaded runner


def compute_argv(
    test_path: str,
    args_env_root: tuple[list[str], dict[str, str], str | None],
    proposals: list[str],
    wasi_world: str,
    wasi_version: str,
) -> list[str]:
    args, env, root = args_env_root
    argv = [sys.executable, RUN]
    for name, value in env.items():
        argv += ["--env", f"{name}={value}"]
    if root:
        argv += ["--dir", f"{root}::/"]
    return [*argv, test_path, *args]
