"""The state of a host of WASI 0.2 and the helpers of the canonical ABI that every package uses (the twin of `include/fusedwasi/p2/wasi_p2_core.h`).

No mutable state at the level of the module: everything that changes is in a `Host`, which is passed as the first argument of every function of the packages.
A memory is any writable buffer (`bytearray`, `memoryview`, a `ctypes` array, `mmap`: seen as flat bytes by `flat_view`, in place, no copy) or a function of no arguments that gives it (as `memory` of 0.1; it is called at each access, so a memory
that moves is followed: the twin of `fusedwasi_p2_bind_memory`); a module fused from components has one for each component, and a function takes the number of its own.
A memory that lives outside this process (a JavaScript engine, Pyodide) is an object with `read(offset, length)`, `write(offset, data)` and a size (`len(memory)`, or a method `len()`), or a function that gives one: every load, store, string, list
and allocation then reads or writes only the range it touches, in one call for each access (never the whole memory); the bounds and the traps are the ones of a buffer. The C twin keeps its memory by pointers and has no such form.

Integers are unsigned, in the parameters and in the results: an offset, a length or a handle is 0 to 2**32 - 1, an `i64` is 0 to 2**64 - 1, as the `uint32_t` and `uint64_t` of the C twin. An engine
that hands a program's `i32` and `i64` over as signed Python integers (wasmtime-py, pywasm3) masks every argument by its width (`x & 0xFFFFFFFF`, `x & 0xFFFFFFFFFFFFFFFF`) and, if it wants the
range of its own type back, re-signs a result (`x - 2**32 if x >= 2**31`). The functions do not do it themselves: a negative offset is outside the memory.
"""

from __future__ import annotations

import os
import struct
import time
from collections.abc import Callable, Sequence
from typing import TYPE_CHECKING, Any, Literal, NoReturn

if TYPE_CHECKING:
    from .wasi_p2_filesystem import Filesystem

_INDEX_BITS = 20  # a handle is (generation << INDEX_BITS) | index; index 0 is never given out
_INDEX_MASK = (1 << _INDEX_BITS) - 1
_GENERATION_MASK = (1 << (32 - _INDEX_BITS)) - 1

KIND_POLLABLE = 1
KIND_ERROR = 2
KIND_INPUT_STREAM = 3
KIND_OUTPUT_STREAM = 4
KIND_TERMINAL_INPUT = 5
KIND_TERMINAL_OUTPUT = 6
KIND_DESCRIPTOR = 7
KIND_DIRECTORY_ENTRY_STREAM = 8
KIND_NETWORK = 9
KIND_TCP_SOCKET = 10
KIND_UDP_SOCKET = 11
KIND_INCOMING_DATAGRAM_STREAM = 12
KIND_OUTGOING_DATAGRAM_STREAM = 13
KIND_RESOLVE_STREAM = 14
KIND_FIELDS = 15
KIND_INCOMING_REQUEST = 16
KIND_OUTGOING_REQUEST = 17
KIND_REQUEST_OPTIONS = 18
KIND_RESPONSE_OUTPARAM = 19
KIND_INCOMING_RESPONSE = 20
KIND_INCOMING_BODY = 21
KIND_FUTURE_TRAILERS = 22
KIND_OUTGOING_RESPONSE = 23
KIND_OUTGOING_BODY = 24
KIND_FUTURE_INCOMING_RESPONSE = 25


# `use` of `Host.socket_addr_check`: the same numbers as FUSEDWASI_P2_USE_* in C (the order is part of the interface: the UDP ones come last)
USE_TCP_BIND, USE_TCP_CONNECT, USE_UDP_BIND, USE_UDP_CONNECT, USE_UDP_OUTGOING = range(5)
_USE_NAMES = ("tcp-bind", "tcp-connect", "udp-bind", "udp-connect", "udp-outgoing")

# `Host.terminals` is a mask of these, as `terminals` of the C state with FUSEDWASI_P2_TERMINAL_*
TERMINAL_STDIN, TERMINAL_STDOUT, TERMINAL_STDERR = 1, 2, 4


def use_name(use: int) -> str:
    """The name of a `use`: "tcp-bind", "tcp-connect", "udp-bind", "udp-connect" or "udp-outgoing" (`fusedwasi_p2_use_name` in C)."""
    return _USE_NAMES[use] if 0 <= use < len(_USE_NAMES) else "unknown"


class Trap(Exception):
    """What the canonical ABI calls a trap: a bad handle, a pointer outside the memory."""


class Exit(Exception):
    """The guest called `wasi:cli/exit`: the program is over with this code (0 is success)."""

    def __init__(self, code: int) -> None:
        super().__init__(f"exit {code}")
        self.code = code


class Pollable:
    """Something a guest can wait for; `wasi:clocks` and `wasi:io` make the kinds. An object of the world: `ready` takes the state, never a guest; a failure is an exception (the
    function that asked has the guest and the exception is the trap), as a hook of the state that raises."""

    def ready(self, state: State) -> bool:
        raise NotImplementedError


class _Slot:
    __slots__ = ("generation", "kind", "live", "obj")

    def __init__(self) -> None:
        self.generation = 0
        self.kind = 0
        self.live = False
        self.obj: object = None


def _cpu_time_ns() -> int:
    return time.process_time_ns()


# a system with no clock of the CPU time of the thread (Pyodide) gives the one of the process
_CLOCKS = (time.time_ns, time.monotonic_ns, time.process_time_ns, getattr(time, "thread_time_ns", _cpu_time_ns))


def _resolution(name: Literal["time", "monotonic", "process_time", "thread_time"]) -> int:
    try:
        return max(1, round(time.get_clock_info(name).resolution * 1e9))
    except ValueError:
        return 1


_RESOLUTIONS = tuple(_resolution(name) for name in ("time", "monotonic", "process_time", "thread_time"))


class State:
    """The world: what the guests share (the twin of `fusedwasi_p2_state`). Configuration, the table of handles, the policies, the sources of time and random bytes.

    One for all the guests that share files and sockets; many `Guest`s may point to one `State`. Python-only on it: `network=True` and `http=True` (booleans for the policies),
    `stdin=`, `stdout=` and `stderr=` (streams of `wasi_p2_io`, or None for the default of `wasi_p2_cli`).
    """

    def __init__(
        self,
        *,
        clock: Callable[[int], int] = lambda ident: _CLOCKS[ident](),
        resolution_ns: Sequence[int] | None = None,
        sleep: Callable[[int], None] | None = None,
        random_bytes: Callable[[int], bytes] = os.urandom,
        insecure_random_bytes: Callable[[int], bytes] | None = None,
        args: Sequence[str] = (),
        env: Sequence[tuple[str, str]] = (),
        initial_cwd: str | None = None,
        stdin: object = None,
        stdout: object = None,
        stderr: object = None,
        terminals: int = 0,
        preopens: Sequence[tuple[str, str] | tuple[str, str, bool]] = (),
        network: bool = False,
        socket_addr_check: Callable[[str, int, int], bool] | None = None,
        allow_ip_name_lookup: bool | None = None,
        http: bool = False,
        http_check: Callable[[str, str], bool] | None = None,
        http_send: Callable[[Any], Any] | None = None,
        filesystem: Filesystem | None = None,
        user: object = None,
    ) -> None:
        self._slots: list[_Slot] = [_Slot()]  # slot 0 is never used
        self.args = list(args)  # wasi:cli/environment
        self.env = list(env)
        self.initial_cwd = initial_cwd
        # the standard streams: an `InputStream` / `OutputStream` of `wasi_p2_io`, or None for the default of `wasi_p2_cli` (made when first asked for)
        self.std: dict[str, object] = {
            k: v for k, v in (("stdin", stdin), ("stdout", stdout), ("stderr", stderr)) if v is not None
        }
        # wasi:filesystem/preopens: (path on the host, name for the guest) or (path, name, writable); a directory is writable unless the third item is False
        self.preopens = [(p[0], p[1], p[2] if len(p) > 2 else True) for p in preopens]  # type: ignore[misc]
        # the files behind wasi:filesystem and WASI 0.1: the operations of `wasi_p2_filesystem` on host values. The default is the files of the system; `Filesystem()` is a host without
        # files, and a host that has others (in memory, say) gives an object with the same methods (Python-only: in C the host writes functions)
        if filesystem is None:
            from .wasi_p2_filesystem import SystemFilesystem

            filesystem = SystemFilesystem()
        self.filesystem = filesystem
        # wasi:sockets: nothing is allowed unless the caller says so, as in Wasmtime. `network=True` allows everything; `socket_addr_check(ip, port, use)` decides
        # for each bind or connect (`use` is one of `USE_TCP_BIND` ... `USE_UDP_OUTGOING`, the numbers of FUSEDWASI_P2_USE_* in C; `use_name(use)` is its name); `allow_ip_name_lookup` for `resolve-addresses`
        self.socket_addr_check: Callable[[str, int, int], bool] = socket_addr_check or (lambda ip, port, use: network)
        self.allow_ip_name_lookup = network if allow_ip_name_lookup is None else allow_ip_name_lookup
        # wasi:http: an outgoing request is refused (`HTTP-request-denied`) unless the caller says so. `http=True` allows every request; `http_check(scheme, authority)` decides for each one
        # (`scheme` is "http" or "https"); `http_send(request) -> response` replaces the transport of `wasi_p2_http` (it raises `ErrorCode` for a failure)
        self.http_check: Callable[[str, str], bool] = http_check or (lambda scheme, authority: http)
        self.http_send = http_send
        self.terminals = (
            terminals  # a mask of TERMINAL_STDIN, TERMINAL_STDOUT, TERMINAL_STDERR: which of the three are terminals
        )
        self.user = user  # the pointer of whoever made the state: the library never reads or writes it (the twin of `fusedwasi_p2_state.user`)
        self.random_bytes = random_bytes  # the secure source
        self.insecure_random_bytes = (
            insecure_random_bytes or random_bytes
        )  # `wasi:random/insecure*`: the same source unless replaced
        # the clocks of WASI 0.1 and 0.2, in nanoseconds: `clock(ident)` with ident 0 real time, 1 monotonic, 2 CPU time of the process, 3 CPU time of the thread
        # (an exception is a trap); `resolution_ns` is the resolution of each; `sleep(ns)` is how a call of 0.1 waits
        self.clock = clock
        self.resolution_ns = list(resolution_ns) if resolution_ns is not None else list(_RESOLUTIONS)
        self.sleep = sleep or self._sleep
        self._p1: Any = None  # private: the descriptors of WASI 0.1 (`fusedwasi.p1`), made by the first function of 0.1 on the state
        self._closed = False

    @property
    def closed(self) -> bool:
        """Whether `close_all()` has been called (the twin of `fusedwasi_p2_state_closed`; read only). A closed state holds no handle and makes none: every handle is dead (a function of 0.2 that is given
        one traps, as for any dead handle; one that would make a resource traps with "the state is closed"), and every descriptor of 0.1, 0 to 2 included, answers BADF (the map is not made again, so the
        preopens do not come back). The configuration (arguments, environment, clocks, random bytes) is still read. There is no way to open it again: make another `State`."""
        return self._closed

    def close_all(self) -> None:
        """Closes what the state holds open: the descriptors (files and directories, of 0.2 and of 0.1), the streams of files, and closes the state (`closed`): nothing is made in it again. The streams the
        caller gave are the caller's. Calling it twice does no harm (`fusedwasi_p2_free` in C)."""
        self._closed = True
        for slot in self._slots:
            if not slot.live:
                continue
            slot.live = False
            if slot.kind == KIND_DESCRIPTOR:
                self.filesystem.close(slot.obj)  # type: ignore[arg-type]
            elif slot.kind in (KIND_INPUT_STREAM, KIND_OUTPUT_STREAM):
                close = getattr(slot.obj, "close", None)
                if close is not None and slot.obj not in self.std.values():
                    close()
        self._p1 = None

    def _sleep(self, ns: int) -> None:
        end = self.clock(1) + ns
        while (left := end - self.clock(1)) > 0:
            time.sleep(min(left, 100_000_000) / 1e9)


class Guest:
    """The caller (the twin of `fusedwasi_p2_guest`): the memories, the trap and `cabi_realloc`, the end of its call, over a `State`. The same for WASI 0.1 and 0.2.

    One for each instance, made once; many guests may point to one state, and nothing is rebound per call. `memories` is a list of memories (a module fused from components has one for
    each component), or one memory for an instance of 0.1 (which has memory 0 only): a writable buffer, a function of no arguments that gives it, or an object with `read(offset, length)`,
    `write(offset, data)` and a size, `len(memory)` or a method `len()` (an engine that keeps its memory out of this process: each access reads or writes only the bytes it touches, so what a call costs is a copy of
    what it touches, never of the memory; the same for 0.1 and 0.2). The hooks are callables of no `Guest`
    argument (a closure carries its own data): `trap(message)` is called before the exception `Trap` is raised (it may raise another); `exit(code)` replaces the default, which records
    `exited` and `exit_code` and raises `Exit` (`wasi:cli/exit` and `proc_exit` of 0.1 both come to it); `wait()` is what a blocking call does between two looks at a pollable;
    `signal(number) -> errno` is asked by `proc_raise` of 0.1 (it returns the errno of WASI for the call, or ends the program by raising; without it the answer is NOTSUP and the program goes on).
    """

    def __init__(
        self,
        state: State,
        memories: Sequence[object | Callable[[], object]] | object | Callable[[], object] | None = (),
        *,
        realloc: Callable[[int, int, int, int, int], int] | None = None,
        wait: Callable[[], None] = lambda: time.sleep(0.001),
        trap: Callable[[str], None] | None = None,
        exit: Callable[[int], None] | None = None,
        signal: Callable[[int], int] | None = None,
        user: object = None,
    ) -> None:
        self.state = state
        self._memories: list[object | Callable[[], object]] = (
            list(memories) if isinstance(memories, (list, tuple)) else [] if memories is None else [memories]  # type: ignore[arg-type]
        )
        self.realloc = realloc  # (comp, old_ptr, old_size, align, new_size) -> ptr: the `cabi_realloc` of the component
        self.wait = wait
        self.trap = trap
        self.exit = exit
        self.signal = signal
        self.exited = False  # the guest called wasi:cli/exit or proc_exit: ... with `exit_code`; `Exit` is raised as well. The host clears it before the next call
        self.exit_code = 0
        self.user = user  # the pointer of whoever made the guest: the library never reads or writes it (the twin of `fusedwasi_p2_guest.user`)


def _trap(guest: Guest, message: str) -> NoReturn:
    """A trap: the hook `guest.trap` is told first (it may raise its own exception); then `Trap` is raised."""
    if guest.trap is not None:
        guest.trap(message)
    raise Trap(message)


# the table of handles (own and borrow share it; a handle is an i32 for the guest)


def table_add(state: State, kind: int, obj: object) -> int:
    """Adds an object to the table of the state (no guest needed: the streams of a state are made while it is configured). The handle, or 0 when the table is full or the state is closed."""
    if state._closed:
        return 0
    for index, slot in enumerate(state._slots):
        if index and not slot.live:
            break
    else:
        index = len(state._slots)
        if index > _INDEX_MASK:
            return 0
        state._slots.append(_Slot())
        slot = state._slots[index]
    slot.generation = (slot.generation + 1) & _GENERATION_MASK
    slot.kind, slot.live, slot.obj = kind, True, obj
    return (slot.generation << _INDEX_BITS) | index


def _table_new(guest: Guest, kind: int, obj: object) -> int:
    """The same in a function that has the guest: a full table is a trap."""
    handle = table_add(guest.state, kind, obj)
    if not handle:
        _trap(guest, "the state is closed" if guest.state._closed else "table of handles is full")
    return handle


def _table_get(guest: Guest, handle: int, kind: int) -> object:
    """The object of a live handle of the given kind; anything else is a trap."""
    index = handle & _INDEX_MASK
    if index == 0 or index >= len(guest.state._slots):
        _trap(guest, f"bad handle {handle}")
    slot = guest.state._slots[index]
    if not slot.live or slot.generation != handle >> _INDEX_BITS or slot.kind != kind:
        _trap(guest, f"bad handle {handle}")
    return slot.obj


def _table_alive(guest: Guest, handle: int, kind: int) -> bool:
    index = handle & _INDEX_MASK
    if index == 0 or index >= len(guest.state._slots):
        return False
    slot = guest.state._slots[index]
    return slot.live and slot.generation == handle >> _INDEX_BITS and slot.kind == kind


def _table_drop(guest: Guest, handle: int, kind: int) -> None:
    _table_get(guest, handle, kind)
    slot = guest.state._slots[handle & _INDEX_MASK]
    slot.live, slot.obj = False, None


# memory: little-endian, checked


def flat_view(memory: object) -> memoryview:
    """A writable buffer as a flat view of bytes (format `B`, one dimension), whatever the buffer is: a `bytearray`, a `memoryview`, an `mmap`, a view of a `ctypes` array (format `<B`), an array of
    another item size. The twin of a base and a size in C. Raises `TypeError` for an object that is not a buffer."""
    view = memoryview(memory)  # type: ignore[call-overload]
    if view.format != "B" or view.ndim != 1:
        view = view.cast("B")
    return view


def _split(memory: object) -> tuple[memoryview | None, Any]:
    """(a flat view, the memory) for a buffer; (None, the memory) for an object of an engine that keeps its memory out of this process (`read(offset, length)`,
    `write(offset, data)` and a size: `len(memory)`, or a method `len()`). Anything else is a `TypeError`."""
    try:
        return flat_view(memory), memory
    except TypeError:
        if callable(getattr(memory, "read", None)) and callable(getattr(memory, "write", None)):
            return None, memory
        raise


def _memory_pair(guest: Guest, comp: int) -> tuple[memoryview | None, Any] | None:
    """The memory `comp` of the guest as `_split` says (a function that gives it is called, so a memory that moves is followed); None when there is none (or the function gave None)."""
    if comp >= len(guest._memories):
        return None
    memory = guest._memories[comp]
    if callable(memory):
        memory = memory()
    return None if memory is None else _split(memory)


def _size_of(view: memoryview | None, memory: Any) -> int:
    """The size in bytes: of the view, else what the object says (one call; nothing is copied)."""
    if view is not None:
        return len(view)
    try:
        return len(memory)
    except TypeError:  # an object that is not a sequence: the size is a method
        return int(memory.len())


def _remote_read(memory: Any, ptr: int, size: int) -> bytes:
    data = bytes(memory.read(ptr, size))
    if len(data) != size:
        raise Trap(f"the memory gave {len(data)} bytes for {size}")
    return data


def _locate(guest: Guest, comp: int, ptr: int, size: int) -> tuple[memoryview | None, Any]:
    """Checks that `size` bytes at `ptr` are inside memory `comp` and gives it as `_split` does: a view, or (None, the object) to be read and written per access."""
    pair = _memory_pair(guest, comp)
    if pair is None:
        _trap(guest, f"no memory {comp}")
    view, memory = pair
    if ptr < 0 or ptr + size > _size_of(view, memory):
        _trap(guest, f"pointer {ptr:#x}+{size} is outside the memory")
    return view, memory


def _load_fmt(guest: Guest, comp: int, ptr: int, fmt: str, size: int) -> int:
    view, memory = _locate(guest, comp, ptr, size)
    if view is not None:
        return struct.unpack_from(fmt, view, ptr)[0]  # type: ignore[no-any-return]
    return struct.unpack(fmt, _remote_read(memory, ptr, size))[0]  # type: ignore[no-any-return]


def _store_fmt(guest: Guest, comp: int, ptr: int, fmt: str, size: int, value: int) -> None:
    view, memory = _locate(guest, comp, ptr, size)
    if view is not None:
        struct.pack_into(fmt, view, ptr, value)
    else:
        memory.write(ptr, struct.pack(fmt, value))


def _store_u8(guest: Guest, comp: int, ptr: int, value: int) -> None:
    _store_fmt(guest, comp, ptr, "<B", 1, value & 0xFF)


def _store_u16(guest: Guest, comp: int, ptr: int, value: int) -> None:
    _store_fmt(guest, comp, ptr, "<H", 2, value & 0xFFFF)


def _load_u8(guest: Guest, comp: int, ptr: int) -> int:
    return _load_fmt(guest, comp, ptr, "<B", 1)


def _load_u16(guest: Guest, comp: int, ptr: int) -> int:
    return _load_fmt(guest, comp, ptr, "<H", 2)


def _store_u32(guest: Guest, comp: int, ptr: int, value: int) -> None:
    _store_fmt(guest, comp, ptr, "<I", 4, value & 0xFFFFFFFF)


def _store_u64(guest: Guest, comp: int, ptr: int, value: int) -> None:
    _store_fmt(guest, comp, ptr, "<Q", 8, value & 0xFFFFFFFFFFFFFFFF)


def _store_bytes(guest: Guest, comp: int, ptr: int, data: bytes) -> None:
    view, memory = _locate(guest, comp, ptr, len(data))
    if view is not None:
        view[ptr : ptr + len(data)] = data
    elif data:
        memory.write(ptr, bytes(data))


def _load_bytes(guest: Guest, comp: int, ptr: int, size: int) -> bytes:
    view, memory = _locate(guest, comp, ptr, size)
    if view is not None:
        return bytes(view[ptr : ptr + size])
    return _remote_read(memory, ptr, size) if size else b""


def _alloc(guest: Guest, comp: int, align: int, size: int) -> int:
    """Memory from the component for a list or a string that the host returns (`cabi_realloc`).

    The canonical ABI calls `realloc(0, 0, align, size)` also for an empty list or string (a component that counts its allocations, such as the adapter of WASI 0.1 of
    Wasmtime, relies on it); a host that has no `realloc` can still answer an empty result. A size of 0 may come back as 0: the alignment stands for it.
    """
    if guest.realloc is None:
        if size == 0:
            return align
        _trap(guest, "the component has no cabi_realloc")
    ptr = guest.realloc(comp, 0, 0, align, size)
    if not ptr and size:
        _trap(guest, "cabi_realloc failed")
    return ptr or align


def _load_u32(guest: Guest, comp: int, ptr: int) -> int:
    return _load_fmt(guest, comp, ptr, "<I", 4)
