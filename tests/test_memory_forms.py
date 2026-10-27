"""A memory of the Python twin is any writable buffer, or a function that gives one: a `bytearray`, a `memoryview`, a view of a `ctypes` array (format `<B`), an `array` of 32-bit items, an `mmap`."""

import array
import ctypes
import mmap
from collections.abc import Callable

import pytest

from fusedwasi.p1 import wasi_p1, wasi_p1_core
from fusedwasi.p2 import wasi_p2_core as core


def forms() -> list[tuple[str, Callable[[], object]]]:
    c_array = (ctypes.c_ubyte * 256)()
    words = array.array("I", bytes(256))
    return [
        ("bytearray", lambda: bytearray(256)),
        ("memoryview", lambda: memoryview(bytearray(256))),
        ("ctypes", lambda: memoryview(c_array)),
        ("ctypes array itself", lambda: c_array),
        ("array of words", lambda: words),
        ("mmap", lambda: mmap.mmap(-1, 256)),
    ]


@pytest.mark.parametrize("name,make", forms(), ids=[n for n, _ in forms()])
def test_p2_reads_and_writes_any_buffer(name: str, make: Callable[[], object]) -> None:
    memory = make()
    for guest_memory in (memory, lambda: memory):  # the buffer, and a function that gives it
        guest = core.Guest(core.State(), [guest_memory])
        core._store_bytes(guest, 0, 8, b"abc")
        core._store_u32(guest, 0, 16, 0x01020304)
        core._store_u64(guest, 0, 24, 5)
        assert core._load_bytes(guest, 0, 8, 3) == b"abc" and core._load_u32(guest, 0, 16) == 0x01020304
        assert core._load_u8(guest, 0, 16) == 4  # little-endian, whatever the item size of the buffer
        with pytest.raises(core.Trap):
            core._load_u32(guest, 0, 254)  # the size is in bytes


@pytest.mark.parametrize("name,make", forms(), ids=[n for n, _ in forms()])
def test_p1_reads_and_writes_any_buffer(name: str, make: Callable[[], object]) -> None:
    memory = make()
    for guest_memory in (memory, lambda: memory):
        state = wasi_p1_core.State(args=["ab", "c"])
        guest = wasi_p1_core.Guest(state, guest_memory)
        assert wasi_p1.args_sizes_get(guest, 0, 4) == 0
        assert wasi_p1_core._get_u32(guest, 0) == 2 and wasi_p1_core._get_u32(guest, 4) == 5
        assert wasi_p1.args_get(guest, 16, 64) == 0
        assert wasi_p1_core._read_bytes(guest, 64, 5) == b"ab\0c\0"
