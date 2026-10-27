"""A user of fusedwasi over wasmtime-py: the imports of a core module are the functions of fusedwasi, one `Guest` for each instance, one `State` for as many instances as the caller wants.

It knows nothing of wasm2c, wasmext or wasmhost and it touches only what fusedwasi publishes: `fusedwasi.p1.wasi_p1` and `fusedwasi.p1.wasi_p1_core` (the functions of 0.1, their
`State`, `Guest` and `Exit`), `fusedwasi.p2.wasi_p2_*` and `fusedwasi.p2.wasi_p2_core` (the functions of 0.2, their `State` and `Guest`) and `fusedwasi.signatures` (the table of the
imports). What it needed that is not published is listed in `docs/design_model.md`, section 11.

How an engine that is not ours fits the model:

* the **memory** of the guest is a function: `Instance.memory()` gives a writable view of the linear memory of the instance, read again at each access, so `memory.grow` is followed;
* the **`cabi_realloc`** of a module of 0.2 is `Instance.realloc`, which calls the export of the instance that is in call (wasmtime-py needs the `Caller` of that call for it);
* a **trap** is a Python exception: the functions of fusedwasi raise, wasmtime-py unwinds the call and raises the same exception out of it, and `Instance.start` gives it as `Outcome.trap`;
* the **end of a program** (`proc_exit`, `wasi:cli/exit`) is read from the guest (`exited`, `exit_code`), where fusedwasi records it.
"""

from __future__ import annotations

import ctypes
import importlib
import inspect
import re
from collections.abc import Callable
from dataclasses import dataclass
from types import ModuleType
from typing import Any

import wasmtime as wt

from fusedwasi import signatures
from fusedwasi.p1 import wasi_p1, wasi_p1_core
from fusedwasi.p2 import wasi_p2_core

P1_MODULE = "wasi_snapshot_preview1"
P2_API = signatures.load("p2").api.split("@")[1]  # the version of the table of 0.2, "0.2.12"


@dataclass
class Outcome:
    """How a call ended: `exit_code` is the code of `proc_exit` or `wasi:cli/exit` (None when the program did not end by itself), `trap` the message of a trap."""

    exit_code: int | None = None
    trap: str | None = None

    @property
    def code(self) -> int:
        """The status of a process: the code of the exit, 0 for a program that returned, 1 for a trap."""
        if self.trap is not None:
            return 1
        return self.exit_code or 0


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


class Instance:
    """One instance of a core module, with its `Guest`. The state may be shared with other instances: it is given, never made here."""

    def __init__(
        self,
        engine: wt.Engine,
        wasm: str | bytes,
        state: wasi_p1_core.State | wasi_p2_core.State,
        *,
        p1_signal: Callable[[int], int] | None = None,
        traps: list[str] | None = None,
    ) -> None:
        self.store = wt.Store(engine)
        self.module = wt.Module(engine, wasm)
        self.caller: wt.Caller | None = (
            None  # the call that is running: wasmtime-py wants it to call back into the instance
        )
        self.traps = traps if traps is not None else []
        self.guest: wasi_p1_core.Guest | wasi_p2_core.Guest
        if isinstance(state, wasi_p1_core.State):
            self.guest = wasi_p1_core.Guest(state, self.memory, signal=p1_signal)
        else:
            self.guest = wasi_p2_core.Guest(state, [self.memory], realloc=self.realloc, trap=self.traps.append)
        self.instance = wt.Instance(self.store, self.module, [self._import(i) for i in self.module.imports])
        exports = self.instance.exports(self.store)
        memory = exports["memory"]
        assert isinstance(memory, wt.Memory)
        self._memory = memory
        realloc = exports.get("cabi_realloc")
        self._realloc = realloc if isinstance(realloc, wt.Func) else None

    # ---- what fusedwasi asks of the engine -----------------------------------------------------------------------------------

    def memory(self) -> memoryview:
        """The linear memory of the instance as it is now (it moves when it grows)."""
        pointer = self._memory.data_ptr(self.caller or self.store)
        size = self._memory.data_len(self.caller or self.store)
        # `cast("B")`: the view of a ctypes array has the format "<B", which fusedwasi cannot assign slices to (see docs/design_model.md, section 11)
        return memoryview((ctypes.c_ubyte * size).from_address(ctypes.addressof(pointer.contents))).cast("B")

    def realloc(self, comp: int, old_ptr: int, old_size: int, align: int, new_size: int) -> int:
        """The `cabi_realloc` of the module."""
        assert comp == 0 and self.caller is not None and self._realloc is not None
        result = self._realloc(self.caller, old_ptr, old_size, align, new_size)
        assert isinstance(result, int)
        return unsigned(result, 32)

    # ---- the imports --------------------------------------------------------------------------------------------------------

    def _import(self, item: wt.ImportType) -> wt.Func:
        func_type = item.type
        assert item.name is not None
        assert isinstance(func_type, wt.FuncType), f"{item.module}.{item.name}: only functions are imported"
        function: Callable[..., Any]
        if item.module == P1_MODULE:
            function = getattr(wasi_p1, item.name)
            call = self._call_p1(function)
        else:
            function, row = p2_implementation(item.module, item.name)
            call = self._call_p2(function, wants_component(function, row))
        widths = [64 if kind == wt.ValType.i64() else 32 for kind in func_type.params]
        results = [64 if kind == wt.ValType.i64() else 32 for kind in func_type.results]

        def host(caller: wt.Caller, *arguments: int) -> int | None:
            outer, self.caller = self.caller, caller
            try:
                result = call(*[unsigned(a, w) for a, w in zip(arguments, widths, strict=True)])
            finally:
                self.caller = outer
            return signed(result, results[0]) if results else None

        return wt.Func(self.store, func_type, host, access_caller=True)

    def _call_p1(self, function: Callable[..., Any]) -> Callable[..., Any]:
        return lambda *arguments: function(self.guest, *arguments)

    def _call_p2(self, function: Callable[..., Any], component: bool) -> Callable[..., Any]:
        if component:
            return lambda *arguments: function(self.guest, 0, *arguments)
        return lambda *arguments: function(self.guest, *arguments)

    # ---- a call into the module ---------------------------------------------------------------------------------------------

    def start(self, export: str = "_start") -> Outcome:
        """Calls an export that takes nothing. The flags of the end of the call are the host's to clear before it."""
        self.guest.exited, self.guest.exit_code = False, 0
        function = self.instance.exports(self.store)[export]
        assert isinstance(function, wt.Func)
        try:
            function(self.store)
        except Exception as caught:  # noqa: BLE001 - wasmtime-py gives back the exception that a function of fusedwasi raised, or a WasmtimeError of its own
            # proc_exit and wasi:cli/exit leave the call as an exception; fusedwasi has recorded why
            if self.guest.exited:
                return Outcome(exit_code=self.guest.exit_code)
            return Outcome(trap=str(caught).splitlines()[0])
        return Outcome(exit_code=self.guest.exit_code if self.guest.exited else None)
