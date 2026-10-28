"""A user of fusedwasi over wasmtime-py: the imports of a core module are the functions of fusedwasi, one `Guest` for each instance, one `State` for as many instances as the caller wants.

It knows nothing of wasm2c or any particular host and it touches only what fusedwasi publishes: the family packages `fusedwasi.p1` and `fusedwasi.p2` (`State`, `Guest`, `Exit`, ...), the table of
imports `fusedwasi.signatures` (`find_import` gives the Python function of an import and whether it takes the number of the memory) and the functions it points to. What it needed
that is not published is listed in `docs/design_model.md`, section 11.

How an engine that is not ours fits the model:

* the **memory** of the guest is a function: `Instance.memory()` gives a writable view of the linear memory of the instance, read again at each access, so `memory.grow` is followed;
* the **`cabi_realloc`** of a module of 0.2 is `Instance.realloc`, which calls the export of the instance that is in call (wasmtime-py needs the `Caller` of that call for it);
* a **trap** is a Python exception: the functions of fusedwasi raise, wasmtime-py unwinds the call and raises the same exception out of it, and `Instance.start` gives it as `Outcome.trap`;
* the **end of a program** (`proc_exit`, `wasi:cli/exit`) is read from the guest (`exited`, `exit_code`), where fusedwasi records it.
"""

from __future__ import annotations

import ctypes
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

import wasmtime as wt
from neutral_binding import implementation, signed, unsigned

from fusedwasi import p2

P1_MODULE = "wasi_snapshot_preview1"


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


class Instance:
    """One instance of a core module, with its `Guest`. The state may be shared with other instances: it is given, never made here."""

    def __init__(
        self,
        engine: wt.Engine,
        wasm: str | bytes,
        state: p2.State,
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
        # one guest for both families: a module of 0.1 uses memory 0 and the hook `signal`, a module of 0.2 also `cabi_realloc`
        self.guest = p2.Guest(state, [self.memory], realloc=self.realloc, trap=self.traps.append, signal=p1_signal)
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
        return memoryview((ctypes.c_ubyte * size).from_address(ctypes.addressof(pointer.contents)))

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
        function, row = implementation(item.module, item.name)
        call = self._call(function, bool(row.takes_comp))
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

    def _call(self, function: Callable[..., Any], component: bool) -> Callable[..., Any]:
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
