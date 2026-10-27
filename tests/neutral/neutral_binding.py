"""What the two Python users of fusedwasi (over wasmtime-py and over wasm3) both do to turn the import of a core module into a function of fusedwasi. No engine in it.

The lookup is the public one: `fusedwasi.signatures.find_import(module, name)` gives the row of the table, which says where the Python function is and whether it takes the number of the
memory (`takes_comp`); `Function.python_function()` imports it.
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

from fusedwasi import signatures


def implementation(module: str, name: str) -> tuple[Callable[..., Any], signatures.Function]:
    """The function of fusedwasi and its row of the table for an import of a core module, of 0.1 or of any version of 0.2."""
    row = signatures.find_import(module, name)
    if row is None:
        raise LookupError(f"no such import in WASI: {module} {name}")
    return row.python_function(), row


def unsigned(value: int, bits: int) -> int:
    return value & ((1 << bits) - 1)


def signed(value: int, bits: int) -> int:
    value = unsigned(value, bits)
    return value - (1 << bits) if value >> (bits - 1) else value
