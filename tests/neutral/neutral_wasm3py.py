"""A user of fusedwasi over pywasm3 (the Python binding of wasm3): the imports of a core module are the functions of fusedwasi, one `Guest` for each instance over a `State` that the caller shares.

The smaller sibling of `neutral_wasmtime.py`, and it takes the same public names. What is different is the engine: pywasm3 keeps no list of the imports of a module (they are read from the
binary here), links a Python function under a signature string, lets an exception leave a call as it is, and gives the memory as a `memoryview` (`Runtime.get_memory`, asked again at each
access, so a memory that grew is followed); `cabi_realloc` is a call of the export from inside the import (wasm3 allows it).
"""

from __future__ import annotations

import importlib
from collections.abc import Callable
from typing import Any

from neutral_binding import p2_implementation, signed, unsigned, wants_component

from fusedwasi import signatures
from fusedwasi.p1 import wasi_p1, wasi_p1_core
from fusedwasi.p2 import wasi_p2_core

wasm3: Any = importlib.import_module("wasm3")  # no types, no stubs
P1_MODULE = "wasi_snapshot_preview1"
LETTER = {"i32": "i", "i64": "I"}


def leb(data: bytes, at: int) -> tuple[int, int]:
    value = shift = 0
    while True:
        byte = data[at]
        at += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            return value, at


def function_imports(data: bytes) -> list[tuple[str, str]]:
    """(module, name) of the functions a binary module imports: pywasm3 does not list them."""
    found: list[tuple[str, str]] = []
    at = 8
    while at < len(data):
        section = data[at]
        size, at = leb(data, at + 1)
        end = at + size
        if section == 2:
            count, at = leb(data, at)
            for _ in range(count):
                names: list[str] = []
                for _ in range(2):
                    length, at = leb(data, at)
                    names.append(data[at : at + length].decode())
                    at += length
                kind = data[at]
                at += 1
                if kind == 0:
                    _, at = leb(data, at)
                    found.append((names[0], names[1]))
                elif kind in (1, 2):
                    at += 1 if kind == 1 else 0
                    flags, at = leb(data, at)
                    _, at = leb(data, at)
                    if flags & 1:
                        _, at = leb(data, at)
                else:
                    at += 2
        at = end
    return found


class Instance:
    """One instance of a core module, with its `Guest`."""

    def __init__(
        self,
        wasm: bytes,
        state: wasi_p1_core.State | wasi_p2_core.State,
        *,
        p1_signal: Callable[[int], int] | None = None,
    ) -> None:
        self.environment = wasm3.Environment()
        self.runtime = self.environment.new_runtime(64 * 1024)
        self.module = self.environment.parse_module(wasm)
        self.runtime.load(self.module)
        self.guest: wasi_p1_core.Guest | wasi_p2_core.Guest
        if isinstance(state, wasi_p1_core.State):
            self.guest = wasi_p1_core.Guest(state, self.memory, signal=p1_signal)
        else:
            self.guest = wasi_p2_core.Guest(state, [self.memory], realloc=self.realloc)
        self._realloc: Any = None
        for module, name in function_imports(wasm):
            self._link(module, name)
        try:
            self._realloc = self.runtime.find_function("cabi_realloc")
        except RuntimeError:
            self._realloc = None

    def memory(self) -> memoryview:
        return self.runtime.get_memory(0)

    def realloc(self, comp: int, old_ptr: int, old_size: int, align: int, new_size: int) -> int:
        assert comp == 0 and self._realloc is not None
        return unsigned(self._realloc(old_ptr, old_size, align, new_size), 32)

    def _link(self, module: str, name: str) -> None:
        function: Callable[..., Any]
        if module == P1_MODULE:
            row = signatures.load("p1").get(module, name)
            assert row is not None
            function = getattr(wasi_p1, name)
            call: Callable[..., Any] = lambda *arguments: function(self.guest, *arguments)
        else:
            function, row = p2_implementation(module, name)
            prefix = (0,) if wants_component(function, row) else ()
            call = lambda *arguments: function(self.guest, *prefix, *arguments)
        widths = [64 if p.type == "i64" else 32 for p in row.params]
        result = 64 if row.results and row.results[0].type == "i64" else 32
        has_result = bool(row.results)

        def host(*arguments: int) -> int | None:
            value = call(*[unsigned(a, w) for a, w in zip(arguments, widths, strict=True)])
            return signed(value, result) if has_result else None

        signature = (
            (LETTER[row.results[0].type] if row.results else "v")
            + "("
            + "".join(LETTER[p.type] for p in row.params)
            + ")"
        )
        self.module.link_function(module, name, signature, host)

    def start(self, export: str = "_start") -> tuple[int | None, str | None]:
        """(exit code, trap): the code of proc_exit or wasi:cli/exit, None for a return; a trap is the text of the exception."""
        self.guest.exited, self.guest.exit_code = False, 0
        try:
            self.runtime.find_function(export)()
        except Exception as caught:  # noqa: BLE001 - pywasm3 lets the exception of a function of fusedwasi leave the call as it is
            if self.guest.exited:
                return self.guest.exit_code, None
            return None, str(caught)
        return (self.guest.exit_code if self.guest.exited else None), None
