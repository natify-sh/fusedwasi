# ruff: noqa: PLC0414  (the redundant alias is how a module says that it re-exports)
"""WASI 0.1, `wasi_unstable` (the first snapshot, "preview0"): the calls are those of `wasi_p1`, except those that read or write what differs from
`wasi_snapshot_preview1`, which are defined here. `sock_accept` is not a call of this snapshot."""

from __future__ import annotations

import struct
from typing import TYPE_CHECKING

from .wasi_p1 import (
    _filestat_of_fd,
    _filestat_of_path,
    _poll,  # pyright: ignore[reportPrivateUsage]
    _seek_native,  # pyright: ignore[reportPrivateUsage]
    _seek_right,  # pyright: ignore[reportPrivateUsage]
    _signed,  # pyright: ignore[reportPrivateUsage]
)

# every call that this snapshot has as the later one has it (re-exported: the module is a complete table of the calls)
from .wasi_p1 import (
    args_get as args_get,
)
from .wasi_p1 import (
    args_sizes_get as args_sizes_get,
)
from .wasi_p1 import (
    clock_res_get as clock_res_get,
)
from .wasi_p1 import (
    clock_time_get as clock_time_get,
)
from .wasi_p1 import (
    environ_get as environ_get,
)
from .wasi_p1 import (
    environ_sizes_get as environ_sizes_get,
)
from .wasi_p1 import (
    fd_advise as fd_advise,
)
from .wasi_p1 import (
    fd_allocate as fd_allocate,
)
from .wasi_p1 import (
    fd_close as fd_close,
)
from .wasi_p1 import (
    fd_datasync as fd_datasync,
)
from .wasi_p1 import (
    fd_fdstat_get as _fdstat_get,
)
from .wasi_p1 import (
    fd_fdstat_set_flags as fd_fdstat_set_flags,
)
from .wasi_p1 import (
    fd_fdstat_set_rights as fd_fdstat_set_rights,
)
from .wasi_p1 import (
    fd_filestat_set_size as fd_filestat_set_size,
)
from .wasi_p1 import (
    fd_filestat_set_times as fd_filestat_set_times,
)
from .wasi_p1 import (
    fd_pread as fd_pread,
)
from .wasi_p1 import (
    fd_prestat_dir_name as fd_prestat_dir_name,
)
from .wasi_p1 import (
    fd_prestat_get as fd_prestat_get,
)
from .wasi_p1 import (
    fd_pwrite as fd_pwrite,
)
from .wasi_p1 import (
    fd_read as fd_read,
)
from .wasi_p1 import (
    fd_readdir as fd_readdir,
)
from .wasi_p1 import (
    fd_renumber as fd_renumber,
)
from .wasi_p1 import (
    fd_sync as fd_sync,
)
from .wasi_p1 import (
    fd_tell as fd_tell,
)
from .wasi_p1 import (
    fd_write as fd_write,
)
from .wasi_p1 import (
    path_create_directory as path_create_directory,
)
from .wasi_p1 import (
    path_filestat_set_times as path_filestat_set_times,
)
from .wasi_p1 import (
    path_link as path_link,
)
from .wasi_p1 import (
    path_open as path_open,
)
from .wasi_p1 import (
    path_readlink as path_readlink,
)
from .wasi_p1 import (
    path_remove_directory as path_remove_directory,
)
from .wasi_p1 import (
    path_rename as path_rename,
)
from .wasi_p1 import (
    path_symlink as path_symlink,
)
from .wasi_p1 import (
    path_unlink_file as path_unlink_file,
)
from .wasi_p1 import (
    proc_exit as proc_exit,
)
from .wasi_p1 import (
    proc_raise as proc_raise,
)
from .wasi_p1 import (
    random_get as random_get,
)
from .wasi_p1 import (
    sched_yield as sched_yield,
)
from .wasi_p1 import (
    sock_recv as sock_recv,
)
from .wasi_p1 import (
    sock_send as sock_send,
)
from .wasi_p1 import (
    sock_shutdown as sock_shutdown,
)
from .wasi_p1_core import E_INVAL, E_SUCCESS, _load, _put_bytes

if TYPE_CHECKING:
    from .wasi_p1_core import Guest


def fd_seek(guest: Guest, fd: int, offset: int, whence: int, ptr: int) -> int:
    """The order of whence in this snapshot: cur, end, set."""
    if whence > 2:
        return E_INVAL
    native = (1, 2, 0)[whence]  # cur, end, set
    return _seek_native(guest, fd, _signed(offset), native, ptr, _seek_right(_signed(offset), native))


def fd_fdstat_get(guest: Guest, fd: int, ptr: int) -> int:
    """This snapshot has one right less, the last (sock_accept): the rights that a descriptor tells are of 29 bits."""
    number = _fdstat_get(guest, fd, ptr)
    if number == E_SUCCESS:
        mask = (1 << 29) - 1
        base, inheriting = struct.unpack("<QQ", _load(guest, ptr + 8, 16))
        _put_bytes(guest, ptr + 8, struct.pack("<QQ", base & mask, inheriting & mask))
    return number


def fd_filestat_get(guest: Guest, fd: int, ptr: int) -> int:
    """The filestat of this snapshot has a 32-bit link count (56 bytes)."""
    return _filestat_of_fd(guest, fd, ptr, True)


def path_filestat_get(guest: Guest, fd: int, flags: int, path: int, length: int, ptr: int) -> int:
    return _filestat_of_path(guest, fd, flags, path, length, ptr, True)


def poll_oneoff(guest: Guest, in_: int, out: int, count: int, nevents: int) -> int:
    """The clock subscription of this snapshot starts with an identifier (the subscriptions are 56 bytes)."""
    return _poll(guest, in_, out, count, nevents, True)
