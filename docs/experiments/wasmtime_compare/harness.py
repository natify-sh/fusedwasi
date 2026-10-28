"""Runs one core module ("the guest") in Wasmtime and over the Python twin of fusedwasi, and gives both the same small protocol.

The guest is generated here as WAT from a table of imports (`P2_OPS`, `P1_OPS`): it imports the flat core form of the WASI functions and exports

    run(op)    calls import number `op` with the arguments found at ARGS (8 bytes each); the scalar result goes to SCALAR
    memory     (and, for 0.2, `cabi_realloc`, a bump allocator that is reset by `run`)

A host (this script) pokes bytes into the memory, calls `run`, and peeks the result. The same bytes of the same guest are therefore served by

* Wasmtime: for WASI 0.1 the core module with `define_wasi`; for WASI 0.2 the module turned into a component (`wasm-tools component embed` and
  `new`, with the WIT of WASI 0.2.12) and linked with `add_wasip2`;
* fusedwasi: the core module in Wasmtime's core engine, with every import bound to the function of `fusedwasi.p1.wasi_p1` or `fusedwasi.p2.wasi_p2_*` of
  the same name, over the memory of the instance.

The protocol is the same on both sides, so a scenario (scenarios.py) is written once.
"""

from __future__ import annotations

import ctypes
import inspect
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, ClassVar

import wasmtime as wt
import wasmtime.component as wc

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / "src"))

from fusedwasi.p1 import Exit as P1Exit
from fusedwasi.p1 import wasi_p1
from fusedwasi.p2 import wasi_p2_cli, wasi_p2_clocks, wasi_p2_filesystem, wasi_p2_io, wasi_p2_random
from fusedwasi.p2.wasi_p2_core import Exit as P2Exit
from fusedwasi.p2.wasi_p2_core import Guest, State
from fusedwasi.p2.wasi_p2_io import BufferOutput, MemoryInput

WASI_VERSION = "0.2.12"
ARGS, SCALAR, RET, DATA, HEAP, PAIR = 0x1000, 0x1100, 0x1200, 0x2000, 0x8000, 0x1F00
WINDOW = RET + 0x200 - SCALAR  # SCALAR .. the end of the return area
DATA_FLAG, RET_FLAG = 1 << 63, 1 << 62


def D(offset: int) -> int:
    """An argument that is the address of `offset` in the data blob."""
    return DATA_FLAG | offset


def R(offset: int = 0) -> int:
    """An argument that is the address `offset` in the return area."""
    return RET_FLAG | offset


# (module, field, parameters, result); i = i32, l = i64, no result = ""
FS = "wasi:filesystem/types"
P2_OPS: dict[str, tuple[str, str, str, str]] = {
    "preopens": ("wasi:filesystem/preopens", "get-directories", "i", ""),
    "get-type": (FS, "[method]descriptor.get-type", "ii", ""),
    "stat": (FS, "[method]descriptor.stat", "ii", ""),
    "stat-at": (FS, "[method]descriptor.stat-at", "iiiii", ""),
    "open-at": (FS, "[method]descriptor.open-at", "iiiiiii", ""),
    "read": (FS, "[method]descriptor.read", "illi", ""),
    "write": (FS, "[method]descriptor.write", "iiili", ""),
    "read-directory": (FS, "[method]descriptor.read-directory", "ii", ""),
    "read-entry": (FS, "[method]directory-entry-stream.read-directory-entry", "ii", ""),
    "unlink": (FS, "[method]descriptor.unlink-file-at", "iiii", ""),
    "rmdir": (FS, "[method]descriptor.remove-directory-at", "iiii", ""),
    "mkdir": (FS, "[method]descriptor.create-directory-at", "iiii", ""),
    "rename": (FS, "[method]descriptor.rename-at", "iiiiiii", ""),
    "readlink": (FS, "[method]descriptor.readlink-at", "iiii", ""),
    "symlink": (FS, "[method]descriptor.symlink-at", "iiiiii", ""),
    "link": (FS, "[method]descriptor.link-at", "iiiiiiii", ""),
    "set-size": (FS, "[method]descriptor.set-size", "ili", ""),
    "drop-descriptor": (FS, "[resource-drop]descriptor", "i", ""),
    "drop-entries": (FS, "[resource-drop]directory-entry-stream", "i", ""),
    "args": ("wasi:cli/environment", "get-arguments", "i", ""),
    "env": ("wasi:cli/environment", "get-environment", "i", ""),
    "cwd": ("wasi:cli/environment", "initial-cwd", "i", ""),
    "exit": ("wasi:cli/exit", "exit", "i", ""),
    "exit-with-code": ("wasi:cli/exit", "exit-with-code", "i", ""),
    "stdout": ("wasi:cli/stdout", "get-stdout", "", "i"),
    "stderr": ("wasi:cli/stderr", "get-stderr", "", "i"),
    "check-write": ("wasi:io/streams", "[method]output-stream.check-write", "ii", ""),
    "write-flush": ("wasi:io/streams", "[method]output-stream.blocking-write-and-flush", "iiii", ""),
    "drop-output": ("wasi:io/streams", "[resource-drop]output-stream", "i", ""),
    "wall-now": ("wasi:clocks/wall-clock", "now", "i", ""),
    "wall-res": ("wasi:clocks/wall-clock", "resolution", "i", ""),
    "mono-now": ("wasi:clocks/monotonic-clock", "now", "", "l"),
    "mono-res": ("wasi:clocks/monotonic-clock", "resolution", "", "l"),
    "subscribe-duration": ("wasi:clocks/monotonic-clock", "subscribe-duration", "l", "i"),
    "subscribe-instant": ("wasi:clocks/monotonic-clock", "subscribe-instant", "l", "i"),
    "pollable-ready": ("wasi:io/poll", "[method]pollable.ready", "i", "i"),
    "pollable-block": ("wasi:io/poll", "[method]pollable.block", "i", ""),
    "poll": ("wasi:io/poll", "poll", "iii", ""),
    "drop-pollable": ("wasi:io/poll", "[resource-drop]pollable", "i", ""),
    "random-bytes": ("wasi:random/random", "get-random-bytes", "li", ""),
    "random-u64": ("wasi:random/random", "get-random-u64", "", "l"),
    "insecure-bytes": ("wasi:random/insecure", "get-insecure-random-bytes", "li", ""),
    "insecure-u64": ("wasi:random/insecure", "get-insecure-random-u64", "", "l"),
    "insecure-seed": ("wasi:random/insecure-seed", "insecure-seed", "i", ""),
}

P1 = "wasi_snapshot_preview1"
P1_OPS: dict[str, tuple[str, str, str, str]] = {
    name: (P1, name, params, "i")
    for name, params in {
        "args_sizes_get": "ii",
        "args_get": "ii",
        "environ_sizes_get": "ii",
        "environ_get": "ii",
        "clock_res_get": "ii",
        "clock_time_get": "ili",
        "random_get": "ii",
        "fd_write": "iiii",
        "fd_read": "iiii",
        "fd_close": "i",
        "fd_seek": "ilii",
        "fd_tell": "ii",
        "fd_fdstat_get": "ii",
        "fd_filestat_get": "ii",
        "fd_prestat_get": "ii",
        "fd_prestat_dir_name": "iii",
        "fd_readdir": "iiili",
        "path_open": "iiiiillii",
        "path_unlink_file": "iii",
        "path_remove_directory": "iii",
        "path_create_directory": "iii",
        "path_rename": "iiiiii",
        "path_filestat_get": "iiiii",
        "path_readlink": "iiiiii",
        "path_symlink": "iiiii",
        "path_link": "iiiiiii",
        "fd_filestat_set_size": "il",
        "sched_yield": "",
    }.items()
}
P1_OPS["proc_exit"] = (P1, "proc_exit", "i", "")

TYPES = {"i": "i32", "l": "i64"}


def make_wat(ops: dict[str, tuple[str, str, str, str]], component: bool) -> str:
    names = list(ops)
    out = ["(module"]
    for i, name in enumerate(names):
        mod, field, params, result = ops[name]
        version = "" if mod == P1 else f"@{WASI_VERSION}"
        sig = "".join(f" (param {TYPES[p]})" for p in params) + (f" (result {TYPES[result]})" if result else "")
        out.append(f'  (import "{mod}{version}" "{field}" (func $f{i}{sig}))')
    out.append(f"""  (memory (export "memory") 8)
  (global $heap (mut i32) (i32.const {HEAP}))
  (func $arg (param $k i32) (result i64) (local $v i64)
    (local.set $v (i64.load (i32.add (i32.const {ARGS}) (i32.shl (local.get $k) (i32.const 3)))))
    (if (i64.lt_s (local.get $v) (i64.const 0))
      (then (return (i64.add (i64.const {DATA}) (i64.and (local.get $v) (i64.const 0xffffffff))))))
    (if (i64.ne (i64.and (local.get $v) (i64.const 0x4000000000000000)) (i64.const 0))
      (then (return (i64.add (i64.const {RET}) (i64.and (local.get $v) (i64.const 0xffffffff))))))
    (local.get $v))
  (func (export "cabi_realloc") (param $old i32) (param $olds i32) (param $align i32) (param $size i32) (result i32)
    (local $p i32)
    (local.set $p (i32.and (i32.add (global.get $heap) (i32.sub (local.get $align) (i32.const 1)))
                           (i32.sub (i32.const 0) (local.get $align))))
    (global.set $heap (i32.add (local.get $p) (local.get $size)))
    (local.get $p))""")
    if component:
        out.append(f"""  (func (export "poke") (param $addr i32) (param $ptr i32) (param $len i32)
    (memory.copy (local.get $addr) (local.get $ptr) (local.get $len)))
  (func (export "peek") (param $addr i32) (param $len i32) (result i32)
    (i32.store (i32.const {PAIR}) (local.get $addr))
    (i32.store (i32.const {PAIR + 4}) (local.get $len))
    (i32.const {PAIR}))""")
    out.append('  (func (export "run") (param $op i32)')
    out.append(f"    (global.set $heap (i32.const {HEAP}))")
    out.append("    (block $end")
    for i in range(len(names) - 1, -1, -1):
        out.append(f"      (block $c{i}")
    out.append("      (br_table " + " ".join(f"$c{i}" for i in range(len(names))) + " $end (local.get $op))")
    for i, name in enumerate(names):
        out.append(f"      ) ;; {name}")
        _, _, params, result = ops[name]
        calls = "".join(
            f" (i32.wrap_i64 (call $arg (i32.const {k})))" if p == "i" else f" (call $arg (i32.const {k}))"
            for k, p in enumerate(params)
        )
        call = f"(call $f{i}{calls})"
        if result == "i":
            out.append(f"      (i64.store (i32.const {SCALAR}) (i64.extend_i32_u {call}))")
        elif result == "l":
            out.append(f"      (i64.store (i32.const {SCALAR}) {call})")
        else:
            out.append(f"      {call}")
        out.append("      (br $end)")
    out.append("    )")
    out.append("  )")
    out.append(")")
    return "\n".join(out)


def find_wit() -> Path:
    """The WIT of WASI 0.2.12 (`deps/*.wit`): $WASI_WIT, else the crate `wasip2` of the cargo registry, if there is one."""
    import os

    candidates = [Path(p) for p in os.environ.get("WASI_WIT", "").split(os.pathsep) if p]
    candidates += sorted(Path.home().glob(f".cargo/registry/src/*/wasip2-*+wasi-{WASI_VERSION}/wit/deps"))
    for path in candidates:
        if (path / "filesystem.wit").is_file():
            return path
    raise SystemExit("the WIT of WASI 0.2.12 (filesystem.wit, cli.wit, ...) is needed: set WASI_WIT to its directory")


def build_p2(work: Path) -> tuple[bytes, bytes]:
    """(the component, the core module) for P2_OPS (needs wat2wasm or wasm-tools, and the WIT)."""
    wit = work / "wit"
    (wit / "deps").mkdir(parents=True, exist_ok=True)
    for source in find_wit().glob("*.wit"):
        # a file of the crate holds one package; wasm-tools wants a directory for each
        package = wit / "deps" / source.stem
        package.mkdir(exist_ok=True)
        shutil.copy(source, package / source.name)
    imports = sorted({f"{m}@{WASI_VERSION}" for m, *_ in P2_OPS.values()})
    (wit / "world.wit").write_text(
        "package test:cmp;\nworld w {\n"
        + "".join(f"  import {i};\n" for i in imports)
        + "  export poke: func(addr: u32, data: list<u8>);\n"
        + "  export peek: func(addr: u32, len: u32) -> list<u8>;\n"
        + "  export run: func(op: u32);\n}\n"
    )
    (work / "p2.wat").write_text(make_wat(P2_OPS, True))
    run = lambda *a: subprocess.run(a, check=True, capture_output=True, text=True)
    run("wasm-tools", "parse", str(work / "p2.wat"), "-o", str(work / "p2.core.wasm"))
    run(
        "wasm-tools",
        "component",
        "embed",
        str(wit),
        "--world",
        "w",
        str(work / "p2.core.wasm"),
        "-o",
        str(work / "p2.emb.wasm"),
    )
    run("wasm-tools", "component", "new", str(work / "p2.emb.wasm"), "-o", str(work / "p2.wasm"))
    return (work / "p2.wasm").read_bytes(), (work / "p2.core.wasm").read_bytes()


def build_p1(work: Path) -> bytes:
    (work / "p1.wat").write_text(make_wat(P1_OPS, False))
    subprocess.run(["wasm-tools", "parse", str(work / "p1.wat"), "-o", str(work / "p1.wasm")], check=True)
    return (work / "p1.wasm").read_bytes()


class Ended(Exception):
    """The guest ended the program (exit); `code` is the exit code the host saw."""

    def __init__(self, code: int) -> None:
        super().__init__(f"exit {code}")
        self.code = code


class Rt:
    """poke / peek / run on a guest; subclasses say where it runs."""

    name = "?"

    def poke(self, addr: int, data: bytes) -> None:
        raise NotImplementedError

    def peek(self, addr: int, n: int) -> bytes:
        raise NotImplementedError

    def run(self, op: str) -> None:
        raise NotImplementedError

    def stdout_text(self) -> bytes:
        raise NotImplementedError

    def call(self, op: str, *args: int, data: bytes = b"") -> bytes:
        """Calls the import `op`; returns the window SCALAR..RET+0x200 of the memory (the scalar result, the return area)."""
        self.poke(ARGS, b"".join((a & (2**64 - 1)).to_bytes(8, "little") for a in args))
        if data:
            self.poke(DATA, data)
        self.poke(RET, bytes(0x200))
        self.poke(SCALAR, bytes(8))
        self.run(op)
        return self.peek(SCALAR, WINDOW)


def window_u32(w: bytes, off: int) -> int:
    """A u32 of the return area at offset `off` of it."""
    return int.from_bytes(w[RET - SCALAR + off : RET - SCALAR + off + 4], "little")


def window_u64(w: bytes, off: int) -> int:
    return int.from_bytes(w[RET - SCALAR + off : RET - SCALAR + off + 8], "little")


def window_u8(w: bytes, off: int) -> int:
    return w[RET - SCALAR + off]


def scalar(w: bytes) -> int:
    return int.from_bytes(w[:8], "little")


class WasmtimeP2(Rt):
    name = "wasmtime"

    def __init__(
        self, wasm: bytes, preopens: list[tuple[str, str]], args: list[str], env: list[tuple[str, str]]
    ) -> None:
        self.engine = wt.Engine()
        self.store = wt.Store(self.engine)
        cfg = wt.WasiConfig()
        cfg.argv = args
        cfg.env = env
        self.out = bytearray()
        cfg.stdout_custom = self.out.extend
        for path, guest in preopens:
            cfg.preopen_dir(path, guest)
        self.store.set_wasi(cfg)
        linker = wc.Linker(self.engine)
        linker.add_wasip2()
        component = wc.Component(self.engine, wasm)
        instance = linker.instantiate(self.store, component)
        self.f_poke = instance.get_func(self.store, "poke")
        self.f_peek = instance.get_func(self.store, "peek")
        self.f_run = instance.get_func(self.store, "run")
        self.ops = list(P2_OPS)

    def poke(self, addr: int, data: bytes) -> None:
        self.f_poke(self.store, addr, bytes(data))
        self.f_poke.post_return(self.store)

    def peek(self, addr: int, n: int) -> bytes:
        got = bytes(self.f_peek(self.store, addr, n))
        self.f_peek.post_return(self.store)
        return got

    def run(self, op: str) -> None:
        try:
            self.f_run(self.store, self.ops.index(op))
        except wt.ExitTrap as caught:
            raise Ended(caught.code) from None
        except wt.WasmtimeError as caught:
            m = re.search(r"[Ee]xited with i32 exit status (\d+)", str(caught))
            if m:
                raise Ended(int(m.group(1))) from None
            raise

    def stdout_text(self) -> bytes:
        return bytes(self.out)


class WasmtimeP1(Rt):
    name = "wasmtime"

    def __init__(
        self, wasm: bytes, preopens: list[tuple[str, str]], args: list[str], env: list[tuple[str, str]]
    ) -> None:
        self.engine = wt.Engine()
        self.store = wt.Store(self.engine)
        cfg = wt.WasiConfig()
        cfg.argv = args
        cfg.env = env
        self.out = bytearray()
        cfg.stdout_custom = self.out.extend
        for path, guest in preopens:
            cfg.preopen_dir(path, guest)
        self.store.set_wasi(cfg)
        linker = wt.Linker(self.engine)
        linker.define_wasi()
        self.instance = linker.instantiate(self.store, wt.Module(self.engine, wasm))
        self.memory = self.instance.exports(self.store)["memory"]
        self.f_run = self.instance.exports(self.store)["run"]
        self.ops = list(P1_OPS)

    def poke(self, addr: int, data: bytes) -> None:
        self.memory.write(self.store, data, addr)

    def peek(self, addr: int, n: int) -> bytes:
        return bytes(self.memory.read(self.store, addr, addr + n))

    def run(self, op: str) -> None:
        try:
            self.f_run(self.store, self.ops.index(op))
        except wt.ExitTrap as caught:
            raise Ended(caught.code) from None

    def stdout_text(self) -> bytes:
        return bytes(self.out)


def _signed(kind: str, value: int) -> int:
    bits = 32 if kind == "i" else 64
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


def _p2_function(module: str, field: str) -> Any:
    base = module.replace(":", "/")
    name = field
    if name.startswith("[resource-drop]"):
        name = name[len("[resource-drop]") :] + "-drop"
    name = re.sub(r"^\[(method|static)\]", "", name).replace(".", "-")
    cname = re.sub(r"[^0-9A-Za-z]+", "_", f"{base}/{name}").strip("_")
    for mod in (wasi_p2_cli, wasi_p2_clocks, wasi_p2_filesystem, wasi_p2_io, wasi_p2_random):
        if hasattr(mod, cname):
            return getattr(mod, cname)
    raise KeyError(cname)


class FusedBase(Rt):
    name = "fusedwasi"

    def _core(self, wasm: bytes, bind: Any) -> None:
        self.engine = wt.Engine()
        self.store = wt.Store(self.engine)
        module = wt.Module(self.engine, wasm)
        linker = wt.Linker(self.engine)
        for imp in module.imports:
            ty = imp.type
            kinds = ["i" if str(p) == "i32" else "l" for p in ty.params]
            results = ["i" if str(r) == "i32" else "l" for r in ty.results]
            linker.define_func(imp.module, imp.name, ty, self._wrap(bind(imp.module, imp.name), kinds, results))
        instance = linker.instantiate(self.store, module)
        exports = self.exports = instance.exports(self.store)
        self.memory = exports["memory"]
        self.f_run = exports["run"]
        n = self.memory.data_len(self.store)
        self.buffer = (ctypes.c_ubyte * n).from_address(ctypes.addressof(self.memory.data_ptr(self.store).contents))
        self.view = memoryview(self.buffer).cast("B")

    def _wrap(self, fn: Any, kinds: list[str], results: list[str]) -> Any:
        def call(*args: int) -> Any:
            try:
                out = fn(*[a & ((1 << (32 if k == "i" else 64)) - 1) for a, k in zip(args, kinds, strict=True)])
            except (P1Exit, P2Exit) as caught:
                self.ended = caught.code
                raise
            return _signed(results[0], int(out or 0)) if results else None

        return call

    ended: int | None = None

    def poke(self, addr: int, data: bytes) -> None:
        self.view[addr : addr + len(data)] = data

    def peek(self, addr: int, n: int) -> bytes:
        return bytes(self.view[addr : addr + n])

    def run(self, op: str) -> None:
        self.ended = None
        try:
            self.f_run(self.store, self.ops.index(op))
        except (wt.WasmtimeError, P1Exit, P2Exit):
            if self.ended is not None:
                raise Ended(self.ended) from None
            raise


class FusedP2(FusedBase):
    def __init__(
        self, wasm_core: bytes, preopens: list[tuple[str, str]], args: list[str], env: list[tuple[str, str]]
    ) -> None:
        self.out = BufferOutput(1 << 20)
        self.state = State(
            args=args,
            env=env,
            stdin=MemoryInput(b""),
            stdout=self.out,
            preopens=[(p, g, True) for p, g in preopens],
        )
        self.host = Guest(self.state, [], realloc=self._realloc)
        self.ops = list(P2_OPS)

        def bind(module: str, field: str) -> Any:
            fn = _p2_function(module.split("@")[0], field)
            takes_comp = "comp" in inspect.signature(fn).parameters
            if takes_comp:
                return lambda *a: fn(self.host, 0, *a)
            return lambda *a: fn(self.host, *a)

        self._core(wasm_core, bind)
        self.host._memories.append(self.view)

    def _realloc(self, comp: int, old_ptr: int, old_size: int, align: int, new_size: int) -> int:
        # the guest's own allocator: its export `cabi_realloc`
        return int(self.exports["cabi_realloc"](self.store, old_ptr, old_size, align, new_size))

    def stdout_text(self) -> bytes:
        return bytes(self.out.data)


class FusedP1(FusedBase):
    def __init__(
        self, wasm_core: bytes, preopens: list[tuple[str, str]], args: list[str], env: list[tuple[str, str]]
    ) -> None:
        self.out = BufferOutput(1 << 20)
        self.state = State(
            args=args,
            env=env,
            stdin=MemoryInput(b""),
            stdout=self.out,
            preopens=[(p, g, True) for p, g in preopens],
        )
        self.host = Guest(self.state, [])
        self.ops = list(P1_OPS)

        def bind(module: str, field: str) -> Any:
            fn = getattr(wasi_p1, field)
            return lambda *a: fn(self.host, *a)

        self._core(wasm_core, bind)
        self.host._memories.append(self.view)

    def stdout_text(self) -> bytes:
        return bytes(self.out.data)


# ---- the C headers of fusedwasi, over wasm3 (cmp_wasm3.c) --------------------------------------------------------------------

WASM3_SOURCES = [
    "m3_bind", "m3_code", "m3_compile", "m3_core", "m3_emit", "m3_env", "m3_exec", "m3_function", "m3_info", "m3_module",
    "m3_parse", "extensions/m3_extensions",
]  # fmt: skip


def build_c_driver(work: Path) -> Path:
    """Builds cmp_wasm3 (the C host: the public headers of fusedwasi, the glue generated from `fusedwasi.signatures`, wasm3 as the engine) and returns the executable.

    Needs a C compiler and WASM3_SRC, a checkout of https://github.com/wasm3/wasm3 (the tag v0.5.0, as `tests/neutral/test_neutral_wasm3.py`)."""
    import importlib.util
    import os

    if "WASM3_SRC" not in os.environ:
        raise SystemExit("HOST=c needs WASM3_SRC, a checkout of https://github.com/wasm3/wasm3 (tag v0.5.0)")
    source = Path(os.environ["WASM3_SRC"]) / "source"
    neutral = HERE.parents[2] / "tests" / "neutral" / "c_wasm3"
    spec = importlib.util.spec_from_file_location("gen_wasm3_glue", neutral / "gen_wasm3_glue.py")
    assert spec is not None and spec.loader is not None
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    build = work / "cbuild"
    build.mkdir()
    generator.main(str(build / "neutral_glue.h"))
    cc = os.environ.get("CC", "cc")
    objects: list[str] = []
    for name in WASM3_SOURCES:
        obj = build / (name.replace("/", "_") + ".o")
        subprocess.run(
            [cc, "-O1", "-w", "-c", f"-I{source}", str(source / f"{name}.c"), "-o", str(obj)],
            check=True,
            capture_output=True,
        )
        objects.append(str(obj))
    include = HERE.parents[2] / "src" / "fusedwasi" / "include"
    exe = build / "cmp_wasm3"
    subprocess.run(
        [
            cc,
            "-std=gnu11",
            "-O1",
            "-g",
            "-fsanitize=address,undefined",
            "-fno-sanitize-recover=undefined",
            f"-I{source}",
            f"-I{include}",
            f"-I{neutral}",
            f"-I{build}",
            str(HERE / "cmp_wasm3.c"),
            *objects,
            "-lm",
            "-o",
            str(exe),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return exe


class CHost(Rt):
    """The C headers of fusedwasi: `cmp_wasm3` in a process of its own, spoken to over a pipe (the protocol is at the top of cmp_wasm3.c)."""

    name = "fusedwasi-c"
    world = "p1"
    #: what the sanitizers of the C host wrote to its standard error, over all the instances
    sanitizer_output: ClassVar[list[str]] = []

    def __init__(
        self, exe: Path, module: Path, preopens: list[tuple[str, str]], args: list[str], env: list[tuple[str, str]]
    ) -> None:
        import os

        cmd = [str(exe), "--world", self.world]
        for a in args:
            cmd += ["--arg", a]
        for k, v in env:
            cmd += ["--env", f"{k}={v}"]
        for path, guest in preopens:
            cmd += ["--dir", f"{path}::{guest}"]
        cmd.append(str(module))
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "print_stacktrace=1"},
        )
        assert self.proc.stdin is not None and self.proc.stdout is not None
        self.to, self.back = self.proc.stdin, self.proc.stdout
        self.ops = list(P2_OPS if self.world == "p2" else P1_OPS)

    def _read(self, n: int) -> bytes:
        data = self.back.read(n)
        if len(data) != n:
            self.close()
            raise RuntimeError("the C host died")
        return data

    def close(self) -> None:
        """Ends the process; whatever it wrote to its standard error (a sanitizer's report) is kept in `sanitizer_output`."""
        assert self.proc.stdin is not None and self.proc.stderr is not None
        if self.proc.poll() is None:
            self.proc.stdin.close()
            self.proc.wait(timeout=30)
        text = self.proc.stderr.read().decode(errors="replace")
        if text.strip():
            CHost.sanitizer_output.append(text)

    def poke(self, addr: int, data: bytes) -> None:
        self.to.write(b"P" + addr.to_bytes(4, "little") + len(data).to_bytes(4, "little") + data)
        self.to.flush()
        assert self._read(1) == b"\0"

    def peek(self, addr: int, n: int) -> bytes:
        self.to.write(b"K" + addr.to_bytes(4, "little") + n.to_bytes(4, "little"))
        self.to.flush()
        assert self._read(1) == b"\0"
        return self._read(n)

    def run(self, op: str) -> None:
        self.to.write(b"R" + self.ops.index(op).to_bytes(4, "little"))
        self.to.flush()
        status = self._read(1)[0]
        if status == 1:
            raise Ended(int.from_bytes(self._read(4), "little"))
        if status == 2:
            raise RuntimeError("trap: " + self._read(int.from_bytes(self._read(4), "little")).decode(errors="replace"))
        assert status == 0

    def stdout_text(self) -> bytes:
        self.to.write(b"O")
        self.to.flush()
        assert self._read(1) == b"\0"
        return self._read(int.from_bytes(self._read(4), "little"))


class CHostP1(CHost):
    world = "p1"


class CHostP2(CHost):
    world = "p2"
