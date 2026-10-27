"""What the two Python users of fusedwasi (over wasmtime-py and over wasm3) both do to turn the import of a core module into a function of fusedwasi. No engine in it.

The rule that gives the name of the function and the test of `comp` are written here because fusedwasi does not publish them (see docs/design_model.md, section 11).
"""

from __future__ import annotations

import importlib
import inspect
import re
from collections.abc import Callable
from types import ModuleType
from typing import Any

from fusedwasi import signatures

P2_API = signatures.load("p2").api.split("@")[1]  # the version of the table of 0.2, "0.2.12"


def p2_function_name(function: signatures.Function) -> str:
    """The name of the function of 0.2 in fusedwasi: `wasi_<package>_<interface>[_<resource>]_<function>`.

    There is no public function that gives it (the rule is written in a test of fusedwasi, and in `docs/signatures.md` only as examples), so it is written again here.
    """
    assert function.package is not None and function.interface is not None
    last = {"constructor": "new", "resource-drop": "drop"}.get(function.kind or "", function.official_name)
    parts = ["wasi", function.package.split(":")[1], function.interface, function.resource or "", last]
    return "_".join(part.replace("-", "_") for part in parts if part)


def p2_implementation(module: str, name: str) -> tuple[Callable[..., Any], signatures.Function]:
    """The function of fusedwasi and its row of the table for an import of a core module (`wasi:io/streams@0.2.6`, `[method]output-stream.write`)."""
    table = signatures.load("p2")
    versionless = re.sub(
        r"@.*$", "@" + P2_API, module
    )  # the table is of one version; matching another is the user's work
    row = table.get(versionless, name)
    if row is None:
        raise LookupError(f"no such import in WASI 0.2: {module} {name}")
    assert row.package is not None
    package: ModuleType = importlib.import_module(f"fusedwasi.p2.wasi_p2_{row.package.split(':')[1]}")
    function = getattr(package, p2_function_name(row), None)
    if function is None:
        raise LookupError(f"fusedwasi has no function for {module} {name}")
    return function, row


def wants_component(function: Callable[..., Any], row: signatures.Function) -> bool:
    """Whether a function of 0.2 takes the number of the memory (`comp`) after the guest: the table does not say, so the signature of the Python function is read."""
    return len(inspect.signature(function).parameters) - 1 == len(row.params) + 1


def unsigned(value: int, bits: int) -> int:
    return value & ((1 << bits) - 1)


def signed(value: int, bits: int) -> int:
    value = unsigned(value, bits)
    return value - (1 << bits) if value >> (bits - 1) else value
