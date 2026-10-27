"""A guest memory that lives outside the Python process, as an engine keeps it (a JavaScript engine, Pyodide): `read(offset, length)`, `write(offset, data)` and a size, nothing else (no buffer).

It counts what crosses: the bytes read and written, and the number of calls of each, so a test can say that what a WASI call costs is a copy of what it touches and not of the memory.
`size` may be bigger than the bytes behind it (a memory of 64 MiB that is a few pages in a test).
"""

from __future__ import annotations

from collections.abc import Callable, Sequence
from typing import Any

from fusedwasi.p2.wasi_p2_core import Guest, State


class OutOfProcessMemory:
    def __init__(self, data: bytearray, *, size: int | None = None) -> None:
        self.data = data
        self.size = len(data) if size is None else size
        self.bytes_read = self.bytes_written = 0
        self.reads = self.writes = self.sizes = 0

    def __len__(self) -> int:
        self.sizes += 1
        return self.size

    def read(self, offset: int, length: int) -> bytes:
        assert 0 <= offset and offset + length <= self.size, "the library read outside the memory"
        self.reads += 1
        self.bytes_read += length
        return bytes(self.data[offset : offset + length])

    def write(self, offset: int, data: bytes) -> None:
        assert 0 <= offset and offset + len(data) <= self.size, "the library wrote outside the memory"
        self.writes += 1
        self.bytes_written += len(data)
        self.data[offset : offset + len(data)] = data

    def reset(self) -> None:
        self.bytes_read = self.bytes_written = self.reads = self.writes = self.sizes = 0

    @property
    def copied(self) -> int:
        return self.bytes_read + self.bytes_written


class MethodSize:
    """The same over a `bytearray`, with the size as a method `len()` (an object that is not a sequence)."""

    def __init__(self, data: bytearray) -> None:
        self.data = data

    def len(self) -> int:
        return len(self.data)

    def read(self, offset: int, length: int) -> bytes:
        return bytes(self.data[offset : offset + length])

    def write(self, offset: int, data: bytes) -> None:
        self.data[offset : offset + len(data)] = data


def wrapping_guest(made: list[OutOfProcessMemory]) -> Callable[..., Guest]:
    """A replacement for `Guest` that hands the library an `OutOfProcessMemory` over each `bytearray` it is given (the test keeps the bytearray to look at the result); the wrappers are appended to `made`."""

    def make(state: State, memories: Sequence[Any] = (), **keywords: Any) -> Guest:
        wrapped: list[Any] = []
        for memory in memories:
            if isinstance(memory, bytearray):
                memory = OutOfProcessMemory(memory)
                made.append(memory)
            wrapped.append(memory)
        return Guest(state, wrapped, **keywords)

    return make
