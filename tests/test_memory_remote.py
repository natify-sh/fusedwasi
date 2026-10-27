"""A guest memory that lives outside this process (`read(offset, length)`, `write(offset, data)`, a size; no buffer), for both families.

The same scripts and scenarios run once over a `bytearray` and once over `OutOfProcessMemory` and must give the same lines; and what a WASI call copies is what it touches, never the memory.
"""

import sys
from collections.abc import Callable
from pathlib import Path

import guests
import pytest
import test_wasi_p2_cli as p2_cli
import test_wasi_p2_clocks as p2_clocks
import test_wasi_p2_filesystem as p2_fs
import test_wasi_p2_io as p2_io
import test_wasi_p2_random as p2_random
from fake_memory import MethodSize, OutOfProcessMemory, wrapping_guest
from p1_script import run as run_p1_script
from test_wasi_p1_core import DATA, SCRIPTS, make_tree

from fusedwasi.p1 import wasi_p1, wasi_p1_core
from fusedwasi.p2 import wasi_p2_cli, wasi_p2_clocks, wasi_p2_core, wasi_p2_io, wasi_p2_random
from fusedwasi.p2.wasi_p2_core import Guest, State, Trap
from fusedwasi.p2.wasi_p2_io import BufferOutput, MemoryInput

MIB = 1 << 20


# ---- (a) the same results over a buffer and over the object -------------------------------------------------------------------------


@pytest.mark.parametrize("name", SCRIPTS)
def test_p1_scripts_give_the_same_lines(name: str, tmp_path: Path) -> None:
    make_tree(tmp_path / "a")
    make_tree(tmp_path / "b")
    script = DATA / "p1" / f"{name}.txt"
    assert run_p1_script(script, str(tmp_path / "a"), remote=True) == run_p1_script(script, str(tmp_path / "b"))


def _both(monkeypatch: pytest.MonkeyPatch, make: Callable[[], str]) -> tuple[str, list[OutOfProcessMemory]]:
    """(the output over a buffer == the output over the object is the caller's to assert; the output over the object, the memories it used)."""
    made: list[OutOfProcessMemory] = []
    monkeypatch.setattr(guests, "Guest", wrapping_guest(made))
    return make(), made


P2_CASES: list[tuple[str, str]] = (
    [("cli", s) for s in p2_cli.SCENARIOS]
    + [("clocks", s) for s in p2_clocks.SCENARIOS]
    + [("random", s) for s in p2_random.SCENARIOS]
    + [("io", s) for s in p2_io.SCENARIOS]
    + [("filesystem", s) for s in p2_fs.SCENARIOS]
)


@pytest.mark.parametrize("package,scenario", P2_CASES)
def test_p2_scenarios_give_the_same_lines(
    package: str, scenario: str, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    if package == "cli" and scenario == "fd" and sys.platform == "win32":
        pytest.skip("the scenario uses pipes")
    (tmp_path / "plain").mkdir()
    (tmp_path / "remote").mkdir()

    def output(root: Path) -> str:
        if package == "filesystem":
            p2_fs.make_tree(root)
            return p2_fs.python_output(scenario, root)
        module = {"cli": p2_cli, "clocks": p2_clocks, "random": p2_random, "io": p2_io}[package]
        return str(module.python_output(scenario))

    expected = output(tmp_path / "plain")
    over, made = _both(monkeypatch, lambda: output(tmp_path / "remote"))
    assert over == expected
    if scenario in ("main", "fd", "streams", "child_dir"):  # the others trap or answer before they touch the memory
        assert any(m.reads or m.writes for m in made)  # the object was the memory


def test_a_function_that_gives_the_object_and_a_size_method_work() -> None:
    data = bytearray(64)
    for memory in (lambda: OutOfProcessMemory(data), MethodSize(data), lambda: MethodSize(data)):
        guest = Guest(State(), [memory])
        wasi_p2_core._store_u32(guest, 0, 8, 0xAABBCCDD)
        wasi_p2_core._store_bytes(guest, 0, 16, b"xyz")
        assert data[8:12] == bytes.fromhex("ddccbbaa") and wasi_p2_core._load_bytes(guest, 0, 16, 3) == b"xyz"
        assert wasi_p2_core._load_u16(guest, 0, 8) == 0xCCDD and wasi_p2_core._load_u8(guest, 0, 11) == 0xAA
        assert wasi_p2_core._load_u32(guest, 0, 8) == 0xAABBCCDD
        wasi_p2_core._store_u8(guest, 0, 20, 7)
        wasi_p2_core._store_u16(guest, 0, 22, 0x0102)
        wasi_p2_core._store_u64(guest, 0, 24, 5)
        assert bytes(data[20:32]) == bytes([7, 0, 2, 1, 5, 0, 0, 0, 0, 0, 0, 0])
        wasi_p1_core._put_u32(Guest(State(), memory), 40, 9)
        assert data[40] == 9
        data[:] = bytes(64)


def test_the_bounds_and_the_traps_are_those_of_a_buffer() -> None:
    told: list[str] = []
    plain = Guest(State(), [bytearray(16)], trap=told.append)
    remote = Guest(State(), [OutOfProcessMemory(bytearray(16))], trap=told.append)
    for guest in (plain, remote):
        wasi_p2_core._store_u32(guest, 0, 12, 1)  # the last four bytes
        for ptr, size in ((13, 4), (16, 1), (1 << 32, 1), (-1, 1)):
            with pytest.raises(Trap):
                wasi_p2_core._load_bytes(guest, 0, ptr, size)
        with pytest.raises(Trap):
            wasi_p2_core._store_bytes(guest, 0, 15, b"ab")
        with pytest.raises(Trap):
            wasi_p2_core._load_u8(guest, 1, 0)  # no memory 1
        assert wasi_p2_core._load_bytes(guest, 0, 16, 0) == b""  # an empty range at the end is inside
    assert told[: len(told) // 2] == told[len(told) // 2 :] and told  # the same messages
    # 0.1 answers with an errno, as for a buffer
    for memory in (bytearray(32), OutOfProcessMemory(bytearray(32))):
        state = State(args=["a"])
        guest = Guest(state, memory)
        assert wasi_p1.args_sizes_get(guest, 0, 28) == wasi_p1_core.E_SUCCESS
        assert wasi_p1.args_sizes_get(guest, 0, 30) == wasi_p1_core.E_FAULT
        assert wasi_p1.args_get(guest, 1 << 31, 8) == wasi_p1_core.E_FAULT


def test_a_short_read_is_a_trap() -> None:
    class Short(OutOfProcessMemory):
        def read(self, offset: int, length: int) -> bytes:
            return super().read(offset, length)[:-1]

    with pytest.raises(Trap):
        wasi_p2_core._load_u32(Guest(State(), [Short(bytearray(16))]), 0, 0)


# ---- (b) a call copies what it touches ------------------------------------------------------------------------------------------------


def test_a_call_touching_n_bytes_copies_order_n_bytes_whatever_the_memory() -> None:
    data = bytearray(1 << 16)
    memory = OutOfProcessMemory(data, size=64 * MIB)
    guest = Guest(State(args=["prog", "x" * 100], env=[("K", "V")]), [memory], realloc=lambda c, o, s, a, n: 1024)
    top = 64 * MIB - 64  # a result at the end of the 64 MiB
    memory.write(top, b"\0" * 8)
    memory.reset()
    wasi_p2_core._store_u64(guest, 0, top, 7)
    assert (memory.bytes_written, memory.bytes_read) == (8, 0) and memory.writes == 1
    memory.reset()
    wasi_p2_cli.wasi_cli_environment_get_arguments(guest, 0, 0)  # a list of two strings through cabi_realloc
    assert 100 <= memory.bytes_written < 1024 and memory.bytes_read < 1024
    memory.reset()
    wasi_p2_clocks.wasi_clocks_wall_clock_now(guest, 0, 0)
    assert memory.copied == 12  # seconds and nanoseconds
    memory.reset()
    wasi_p2_random.wasi_random_random_get_random_bytes(guest, 0, 1000, 0)
    assert memory.bytes_written < 2048 and memory.bytes_read < 64
    assert memory.copied < 64 * MIB // 1024  # far below the size of the memory, in every case above


def test_a_file_written_and_read_through_big_iovecs_copies_the_data_once(tmp_path: Path) -> None:
    """The official-style `fd_write` / `fd_read` of 0.1: the iovec list is read, each buffer once, the result words are written; the memory is 64 MiB."""
    size = 64 * MIB
    data = bytearray(4 * MIB + 4096)
    memory = OutOfProcessMemory(data, size=size)
    out = BufferOutput(limit=1 << 30)
    state = State(stdout=out, stdin=MemoryInput(bytes(range(256)) * 8192), preopens=[(str(tmp_path), "/")])
    guest = Guest(state, memory)
    iovs, nwritten, nread, buf = 16, 64, 72, 4096
    payload = bytes((i * 7) & 0xFF for i in range(MIB))
    data[buf : buf + len(payload)] = payload
    for i, (base, length) in enumerate(((buf, MIB // 2), (buf + MIB // 2, MIB // 2), (buf, 0))):
        data[iovs + 8 * i : iovs + 8 * i + 8] = base.to_bytes(4, "little") + length.to_bytes(4, "little")
    memory.reset()
    assert wasi_p1.fd_write(guest, 1, iovs, 3, nwritten) == 0
    assert int.from_bytes(data[nwritten : nwritten + 4], "little") == MIB and bytes(out.data) == payload
    assert MIB <= memory.bytes_read <= MIB + 256  # the payload once, and the iovecs
    assert memory.bytes_written == 4  # nwritten, nothing else
    # fd_read of stdin into the same iovecs
    data[buf : buf + MIB] = bytes(MIB)
    memory.reset()
    assert wasi_p1.fd_read(guest, 0, iovs, 3, nread) == 0
    got = int.from_bytes(data[nread : nread + 4], "little")
    assert got > 0 and bytes(data[buf : buf + got]) == (bytes(range(256)) * 8192)[:got]
    assert got <= memory.bytes_written <= got + 4 and memory.bytes_read < 256
    # a file: path_open, fd_write, fd_seek, fd_read with the same iovecs; the file has what was written
    path = 2 * MIB
    data[path : path + 5] = b"f.bin"
    opened = 128
    assert wasi_p1.path_open(guest, 3, 0, path, 5, 1, wasi_p1_core.R_ALL, wasi_p1_core.R_ALL, 0, opened) == 0
    fd = int.from_bytes(data[opened : opened + 4], "little")
    data[buf : buf + len(payload)] = payload
    memory.reset()
    assert wasi_p1.fd_write(guest, fd, iovs, 3, nwritten) == 0
    assert (tmp_path / "f.bin").read_bytes() == payload
    assert MIB <= memory.bytes_read <= MIB + 256 and memory.bytes_written == 4
    assert wasi_p1.fd_seek(guest, fd, 0, 0, 136) == 0
    data[buf : buf + MIB] = bytes(MIB)
    memory.reset()
    assert wasi_p1.fd_read(guest, fd, iovs, 3, nread) == 0
    assert int.from_bytes(data[nread : nread + 4], "little") == MIB and bytes(data[buf : buf + MIB]) == payload
    assert MIB <= memory.bytes_written <= MIB + 4 and memory.bytes_read < 256
    assert memory.sizes < 64  # the size is asked a few times for each call, not per byte


def test_an_iovec_outside_the_memory_is_a_fault_and_writes_nothing() -> None:
    memory = OutOfProcessMemory(bytearray(256), size=256)
    guest = Guest(State(stdout=wasi_p2_io.BufferOutput()), memory)
    memory.data[16:24] = (250).to_bytes(4, "little") + (100).to_bytes(4, "little")  # 250..350 is outside
    assert wasi_p1.fd_write(guest, 1, 16, 1, 64) == wasi_p1_core.E_FAULT
    assert memory.bytes_written == 0
