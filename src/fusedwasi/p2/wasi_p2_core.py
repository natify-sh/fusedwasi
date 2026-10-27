"""The state of a host of WASI 0.2 and the helpers of the canonical ABI that every package uses (the twin of `include/fusedwasi/p2/wasi_p2_core.h`).

No mutable state at the level of the module: everything that changes is in a `Host`, which is passed as the first argument of every function of the packages.
A memory is any writable buffer (`bytearray`, `memoryview`) or a function of no arguments that gives it (as `memory` of 0.1; it is called at each access, so a memory
that moves is followed: the twin of `fusedwasi_p2_bind_memory`); a module fused from components has one for each component, and a function takes the number of its own.
"""

from __future__ import annotations

import os
import struct
import time
from collections.abc import Callable, Sequence
from typing import Any, Literal

INDEX_BITS = 20  # a handle is (generation << INDEX_BITS) | index; index 0 is never given out
INDEX_MASK = (1 << INDEX_BITS) - 1
GENERATION_MASK = (1 << (32 - INDEX_BITS)) - 1

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
    """Something a guest can wait for; `wasi:clocks` and `wasi:io` make the kinds."""

    def ready(self, host: Host) -> bool:
        raise NotImplementedError


class Slot:
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


class Host:
    """The state of one instance: its memories, the table of handles and the clocks."""

    def __init__(
        self,
        memories: Sequence[object | Callable[[], object]] = (),
        *,
        clock: Callable[[int], int] = lambda ident: _CLOCKS[ident](),
        resolution_ns: Sequence[int] | None = None,
        sleep: Callable[[int], None] | None = None,
        realloc: Callable[[int, int, int, int, int], int] | None = None,
        wait: Callable[[], None] = lambda: time.sleep(0.001),
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
        user: object = None,
    ) -> None:
        self.memories: list[object | Callable[[], object]] = list(memories)
        self.slots: list[Slot] = [Slot()]  # slot 0 is never used
        self.realloc = realloc  # (comp, old_ptr, old_size, align, new_size) -> ptr: the `cabi_realloc` of the component
        self.wait = wait  # what a blocking call does between two looks at a pollable
        self.args = list(args)  # wasi:cli/environment
        self.env = list(env)
        self.initial_cwd = initial_cwd
        # the standard streams: an `InputStream` / `OutputStream` of `wasi_p2_io`, or None for the default of `wasi_p2_cli` (made when first asked for)
        self.std: dict[str, object] = {
            k: v for k, v in (("stdin", stdin), ("stdout", stdout), ("stderr", stderr)) if v is not None
        }
        # wasi:filesystem/preopens: (path on the host, name for the guest) or (path, name, writable); a directory is writable unless the third item is False
        self.preopens = [(p[0], p[1], p[2] if len(p) > 2 else True) for p in preopens]  # type: ignore[misc]
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
        self.user = user  # the host's own: the library never reads or writes it (the twin of `fusedwasi_p2_state.user`)
        self.exited = False  # the guest called wasi:cli/exit (or proc_exit of WASI 0.1): ... with `exit_code`; `Exit` is raised as well
        self.exit_code = 0
        self.random_bytes = random_bytes  # the secure source
        self.insecure_random_bytes = (
            insecure_random_bytes or random_bytes
        )  # `wasi:random/insecure*`: the same source unless replaced
        # the clocks of WASI 0.1 and 0.2, in nanoseconds: `clock(ident)` with ident 0 real time, 1 monotonic, 2 CPU time of the process, 3 CPU time of the thread
        # (an exception is a trap); `resolution_ns` is the resolution of each; `sleep(ns)` is how a call of 0.1 waits
        self.clock = clock
        self.resolution_ns = list(resolution_ns) if resolution_ns is not None else list(_RESOLUTIONS)
        self.sleep = sleep or self._sleep

    def _sleep(self, ns: int) -> None:
        end = self.clock(1) + ns
        while (left := end - self.clock(1)) > 0:
            time.sleep(min(left, 100_000_000) / 1e9)


# the table of handles (own and borrow share it; a handle is an i32 for the guest)


def table_new(host: Host, kind: int, obj: object) -> int:
    for index, slot in enumerate(host.slots):
        if index and not slot.live:
            break
    else:
        index = len(host.slots)
        if index > INDEX_MASK:
            raise Trap("table of handles is full")
        host.slots.append(Slot())
        slot = host.slots[index]
    slot.generation = (slot.generation + 1) & GENERATION_MASK
    slot.kind, slot.live, slot.obj = kind, True, obj
    return (slot.generation << INDEX_BITS) | index


def table_get(host: Host, handle: int, kind: int) -> object:
    """The object of a live handle of the given kind; anything else is a trap."""
    index = handle & INDEX_MASK
    if index == 0 or index >= len(host.slots):
        raise Trap(f"bad handle {handle}")
    slot = host.slots[index]
    if not slot.live or slot.generation != handle >> INDEX_BITS or slot.kind != kind:
        raise Trap(f"bad handle {handle}")
    return slot.obj


def table_alive(host: Host, handle: int, kind: int) -> bool:
    index = handle & INDEX_MASK
    if index == 0 or index >= len(host.slots):
        return False
    slot = host.slots[index]
    return slot.live and slot.generation == handle >> INDEX_BITS and slot.kind == kind


def table_drop(host: Host, handle: int, kind: int) -> None:
    table_get(host, handle, kind)
    slot = host.slots[handle & INDEX_MASK]
    slot.live, slot.obj = False, None


# memory: little-endian, checked


def _check(host: Host, comp: int, ptr: int, size: int) -> memoryview:
    if comp >= len(host.memories):
        raise Trap(f"no memory {comp}")
    memory = host.memories[comp]
    if callable(memory):  # a function that gives the buffer: a memory that moves is followed
        memory = memory()
    view = memoryview(memory)  # type: ignore[call-overload]
    if ptr < 0 or ptr + size > len(view):
        raise Trap(f"pointer {ptr:#x}+{size} is outside the memory")
    return view


def store_u8(host: Host, comp: int, ptr: int, value: int) -> None:
    _check(host, comp, ptr, 1)[ptr] = value & 0xFF


def store_u16(host: Host, comp: int, ptr: int, value: int) -> None:
    struct.pack_into("<H", _check(host, comp, ptr, 2), ptr, value & 0xFFFF)


def load_u8(host: Host, comp: int, ptr: int) -> int:
    return _check(host, comp, ptr, 1)[ptr]


def load_u16(host: Host, comp: int, ptr: int) -> int:
    return struct.unpack_from("<H", _check(host, comp, ptr, 2), ptr)[0]


def store_u32(host: Host, comp: int, ptr: int, value: int) -> None:
    struct.pack_into("<I", _check(host, comp, ptr, 4), ptr, value & 0xFFFFFFFF)


def store_u64(host: Host, comp: int, ptr: int, value: int) -> None:
    struct.pack_into("<Q", _check(host, comp, ptr, 8), ptr, value & 0xFFFFFFFFFFFFFFFF)


def store_bytes(host: Host, comp: int, ptr: int, data: bytes) -> None:
    _check(host, comp, ptr, len(data))[ptr : ptr + len(data)] = data


def load_bytes(host: Host, comp: int, ptr: int, size: int) -> bytes:
    return bytes(_check(host, comp, ptr, size)[ptr : ptr + size])


def alloc(host: Host, comp: int, align: int, size: int) -> int:
    """Memory from the component for a list or a string that the host returns (`cabi_realloc`).

    The canonical ABI calls `realloc(0, 0, align, size)` also for an empty list or string (a component that counts its allocations, such as the adapter of WASI 0.1 of
    Wasmtime, relies on it); a host that has no `realloc` can still answer an empty result. A size of 0 may come back as 0: the alignment stands for it.
    """
    if host.realloc is None:
        if size == 0:
            return align
        raise Trap("the component has no cabi_realloc")
    ptr = host.realloc(comp, 0, 0, align, size)
    if not ptr and size:
        raise Trap("cabi_realloc failed")
    return ptr or align


def load_u32(host: Host, comp: int, ptr: int) -> int:
    return struct.unpack_from("<I", _check(host, comp, ptr, 4), ptr)[0]
