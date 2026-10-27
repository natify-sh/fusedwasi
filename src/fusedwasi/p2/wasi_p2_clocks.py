"""wasi:clocks (6 imports): `wall-clock` and `monotonic-clock` (the twin of `include/fusedwasi/p2/wasi_p2_clocks.h`).

Names carry the interface, as in C: `wasi_clocks_<interface>_<function>`. A result that does not fit in one flat value is written through the pointer that is the last
argument (the canonical ABI); `datetime` is a record `{seconds: u64, nanoseconds: u32}` of 16 bytes, 8-aligned.
"""

from __future__ import annotations

from .wasi_p2_core import KIND_POLLABLE, Guest, Pollable, State, _store_u32, _store_u64, _table_new

_NS = 1_000_000_000
_MAX_U64 = (1 << 64) - 1


def _datetime(guest: Guest, comp: int, retptr: int, ns: int) -> None:
    _store_u64(guest, comp, retptr, ns // _NS)
    _store_u32(guest, comp, retptr + 8, ns % _NS)


def wasi_clocks_wall_clock_now(guest: Guest, comp: int, retptr: int) -> None:
    _datetime(guest, comp, retptr, guest.state.clock(0))


def wasi_clocks_wall_clock_resolution(guest: Guest, comp: int, retptr: int) -> None:
    _datetime(guest, comp, retptr, guest.state.resolution_ns[0])


def wasi_clocks_monotonic_clock_now(guest: Guest) -> int:
    return guest.state.clock(1)


def wasi_clocks_monotonic_clock_resolution(guest: Guest) -> int:
    return guest.state.resolution_ns[1]


class DeadlinePollable(Pollable):
    """A pollable that is ready at an instant of the monotonic clock."""

    __slots__ = ("deadline",)

    def __init__(self, deadline: int) -> None:
        self.deadline = deadline

    def ready(self, state: State) -> bool:
        return state.clock(1) >= self.deadline


def wasi_clocks_monotonic_clock_subscribe_instant(guest: Guest, when: int) -> int:
    return _table_new(guest, KIND_POLLABLE, DeadlinePollable(when))


def wasi_clocks_monotonic_clock_subscribe_duration(guest: Guest, when: int) -> int:
    return _table_new(guest, KIND_POLLABLE, DeadlinePollable(min(guest.state.clock(1) + when, _MAX_U64)))
