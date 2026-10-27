"""A state that is closed (`State.close_all()`, `fusedwasi_p2_free` in C): every handle is dead, nothing is made in it again, and every descriptor of 0.1 (0 to 2 too) answers BADF.

The C side is compared in tests/data/p1/closed.txt (run by test_wasi_p1_core.py) and in the scenarios `closed_handle` and `closed_new` of test_wasi_p2_filesystem.py.
"""

import errno
from pathlib import Path

import pytest

from fusedwasi.p1 import wasi_p1, wasi_p1_core, wasi_p1_unstable
from fusedwasi.p2 import wasi_p2_cli, wasi_p2_core, wasi_p2_filesystem
from fusedwasi.p2.wasi_p2_core import Guest, State, Trap


def test_the_property_is_read_only_and_close_all_is_idempotent(tmp_path: Path) -> None:
    state = State(preopens=[(str(tmp_path), "/")])
    assert state.closed is False
    with pytest.raises(AttributeError):
        state.closed = True  # type: ignore[misc]
    state.close_all()
    assert state.closed is True
    state.close_all()
    assert state.closed is True


def test_0_1_answers_badf_for_every_descriptor_and_does_not_make_the_map_again(tmp_path: Path) -> None:
    (tmp_path / "f").write_text("x")
    state = State(preopens=[(str(tmp_path), "/data")])
    memory = bytearray(256)
    guest = Guest(state, memory)
    assert wasi_p1.fd_prestat_get(guest, 3, 0) == wasi_p1_core.E_SUCCESS  # the map is made
    assert state._p1 is not None
    state.close_all()
    assert state._p1 is None
    memory[16:24] = (32).to_bytes(4, "little") + (0).to_bytes(4, "little")
    for fd in (0, 1, 2, 3, 4, 100):
        assert wasi_p1.fd_prestat_get(guest, fd, 0) == wasi_p1_core.E_BADF, fd
        assert wasi_p1.fd_fdstat_get(guest, fd, 64) == wasi_p1_core.E_BADF, fd
        assert wasi_p1.fd_write(guest, fd, 16, 1, 128) == wasi_p1_core.E_BADF, fd
        assert wasi_p1.fd_read(guest, fd, 16, 1, 128) == wasi_p1_core.E_BADF, fd
        assert wasi_p1.fd_close(guest, fd) == wasi_p1_core.E_BADF, fd
        assert wasi_p1_unstable.fd_seek(guest, fd, 0, 2, 128) == wasi_p1_core.E_BADF, fd
        assert wasi_p1_unstable.fd_filestat_get(guest, fd, 128) == wasi_p1_core.E_BADF, fd
    memory[0:1] = b"f"
    assert (
        wasi_p1.path_open(guest, 3, 0, 0, 1, 0, wasi_p1_core.R_ALL, wasi_p1_core.R_ALL, 0, 128) == wasi_p1_core.E_BADF
    )
    assert wasi_p1.path_filestat_get(guest, 3, 1, 0, 1, 128) == wasi_p1_core.E_BADF
    assert state._p1 is None  # not made again by any of the calls
    assert not any(slot.live for slot in state._slots)
    # what is configuration is still read
    assert wasi_p1.args_sizes_get(guest, 128, 132) == wasi_p1_core.E_SUCCESS
    assert wasi_p1.random_get(guest, 128, 4) == wasi_p1_core.E_SUCCESS
    with pytest.raises(wasi_p1_core.PreopenError) as caught:
        wasi_p1_core.open_descriptors(state)
    assert caught.value.errno == errno.EBADF and caught.value.index == -1 and state._p1 is None


def test_0_2_handles_are_dead_and_nothing_is_made(tmp_path: Path) -> None:
    state = State(preopens=[(str(tmp_path), "/data")])
    memory = bytearray(4096)
    told: list[str] = []
    free = [1024]

    def bump(comp: int, old_ptr: int, old_size: int, align: int, size: int) -> int:
        ptr = (free[0] + align - 1) // align * align
        free[0] = ptr + size
        return ptr

    guest = Guest(state, [memory], trap=told.append, realloc=bump)
    wasi_p2_filesystem.wasi_filesystem_preopens_get_directories(guest, 0, 0)
    handle = wasi_p2_core._load_u32(guest, 0, wasi_p2_core._load_u32(guest, 0, 0))
    assert wasi_p2_filesystem.wasi_filesystem_types_descriptor_get_flags(guest, 0, handle, 64) is None
    state.close_all()
    with pytest.raises(Trap):
        wasi_p2_filesystem.wasi_filesystem_types_descriptor_get_flags(guest, 0, handle, 64)  # dead, as after a drop
    assert told[-1].startswith("bad handle")
    with pytest.raises(Trap):
        wasi_p2_filesystem.wasi_filesystem_preopens_get_directories(guest, 0, 0)  # would make descriptors
    assert told[-1] == "the state is closed"
    with pytest.raises(Trap):
        wasi_p2_cli.wasi_cli_stdout_get_stdout(guest)
    assert wasi_p2_core.table_add(state, wasi_p2_core.KIND_ERROR, object()) == 0
    assert not any(slot.live for slot in state._slots)
