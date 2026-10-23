"""The calls of `wasi_snapshot_preview1`, one function for each (the twin of the `fusedwasi_p1_<name>` of `include/fusedwasi/p1/wasi_p1_core.h`).

Every function takes the `State` first, then the parameters of the call as the core module passes them (integers: a pointer is an offset in the memory), and returns the errno of
WASI; what the call gives back is written into the memory. A trap is an exception. Done so far (the rest of the 46 follows, a stage at a time): arguments and environment, clocks,
random bytes, `proc_exit` and `proc_raise`, `sched_yield`, the standard streams and the descriptors of the preopened directories (`fd_write`, `fd_read`, `fd_close`,
`fd_renumber`, `fd_seek`, `fd_tell`, `fd_advise`, `fd_fdstat_get`, `fd_prestat_get`, `fd_prestat_dir_name`).
"""

from __future__ import annotations

import os

from .wasi_p1_core import (
    E_BADF,
    E_FAULT,
    E_INVAL,
    E_IO,
    E_ISDIR,
    E_NAMETOOLONG,
    E_NOTCAPABLE,
    E_NOTSUP,
    E_SPIPE,
    E_SUCCESS,
    END_EXIT,
    END_SIGNAL,
    K_DIR,
    K_FILE,
    K_STD,
    R_FD_ADVISE,
    R_FD_READ,
    R_FD_SEEK,
    R_FD_TELL,
    R_FD_WRITE,
    RIGHTS_STDIN,
    RIGHTS_STDOUT,
    Entry,
    Exit,
    Signal,
    State,
    errno_of,
)

# ---- arguments and environment ------------------------------------------------------------------------------------------------


def _sizes(s: State, count: int, size: int, count_ptr: int, size_ptr: int) -> int:
    if not (s.put_u32(count_ptr, count) and s.put_u32(size_ptr, size)):
        return E_FAULT
    return E_SUCCESS


def _strings(s: State, buf: bytes, count: int, table: int, out: int) -> int:
    if not (s.in_memory(out, len(buf)) and s.in_memory(table, count * 4)):
        return E_FAULT
    s.put_bytes(out, buf)
    offset = 0
    for i in range(count):
        s.put_u32(table + 4 * i, out + offset)
        offset += buf.index(b"\0", offset) - offset + 1
    return E_SUCCESS


def args_sizes_get(s: State, argc: int, size: int) -> int:
    return _sizes(s, s.argc, len(s.args), argc, size)


def args_get(s: State, argv: int, buf: int) -> int:
    return _strings(s, s.args, s.argc, argv, buf)


def environ_sizes_get(s: State, count: int, size: int) -> int:
    return _sizes(s, s.envc, len(s.env), count, size)


def environ_get(s: State, environ: int, buf: int) -> int:
    return _strings(s, s.env, s.envc, environ, buf)


# ---- clocks and random bytes --------------------------------------------------------------------------------------------------


def clock_res_get(s: State, ident: int, ptr: int) -> int:
    if ident > 3:
        return E_INVAL
    resolution = s.clock_resolution(ident)
    if resolution is None:
        return E_NOTSUP
    return E_SUCCESS if s.put_u64(ptr, resolution) else E_FAULT


def clock_time_get(s: State, ident: int, precision: int, ptr: int) -> int:
    status, ns = s.clock_ns(ident)
    if status:
        return status
    return E_SUCCESS if s.put_u64(ptr, ns) else E_FAULT


def random_get(s: State, buf: int, size: int) -> int:
    if not s.in_memory(buf, size):
        return E_FAULT
    if size == 0:
        return E_SUCCESS
    if s.random is not None:
        status, data = s.hooked(s.random, size)
        if status:
            return status
        if len(data) != size:
            return E_IO
    else:
        try:
            data = os.urandom(size)
        except OSError:
            return E_IO
    s.put_bytes(buf, data)
    return E_SUCCESS


def sched_yield(s: State) -> int:
    return E_SUCCESS


def proc_exit(s: State, code: int) -> None:
    if s.end is not None:
        s.end(END_EXIT, code)
    s.finish(END_EXIT, code)
    raise Exit(code)


def proc_raise(s: State, sig: int) -> int:
    """The signals of WASI 0.1 by number. A signal that is ignored when nothing handles it (the child, continue, urgent data, the size of the window) or that stops a
    process (stop, and the ones of a terminal) is answered with success and the program goes on; any other ends it. 0 is no signal."""
    if sig > 30:
        return E_INVAL
    if sig == 0 or 16 <= sig <= 22 or sig == 27:
        return E_SUCCESS
    if s.end is not None:
        s.end(END_SIGNAL, sig)
    s.finish(END_SIGNAL, sig)
    raise Signal(sig)


# ---- descriptors --------------------------------------------------------------------------------------------------------------


def fd_write(s: State, fd: int, iovs: int, count: int, nwritten: int) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_DIR:
        return E_ISDIR
    if entry.kind == K_STD:
        return E_BADF if entry.fd == 0 else s.write_std(entry.fd, iovs, count, nwritten)
    if not entry.rights & R_FD_WRITE:
        return E_NOTCAPABLE
    raise NotImplementedError("fd_write of a file")  # no call opens a file yet


def fd_read(s: State, fd: int, iovs: int, count: int, nread: int) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        return s.read_std(iovs, count, nread) if entry.fd == 0 else E_BADF
    if entry.kind == K_DIR:
        return E_ISDIR
    if not entry.rights & R_FD_READ:
        return E_NOTCAPABLE
    raise NotImplementedError("fd_read of a file")


def _seek_native(s: State, fd: int, right: int) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        return E_SPIPE
    if entry.kind != K_FILE:
        return E_BADF
    if not entry.rights & right:
        return E_NOTCAPABLE
    raise NotImplementedError("fd_seek of a file")


def fd_seek(s: State, fd: int, offset: int, whence: int, ptr: int) -> int:
    """The order of whence in this snapshot: set, cur, end."""
    if whence > 2:
        return E_INVAL
    return _seek_native(s, fd, R_FD_SEEK)


def fd_tell(s: State, fd: int, ptr: int) -> int:
    return _seek_native(s, fd, R_FD_TELL)


def fd_close(s: State, fd: int) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    if (
        entry.kind == K_STD
    ):  # the stream of the host stays open, but the program has let go of it: the number is free and a write to it is BADF
        entry.__init__()  # type: ignore[misc]
        return E_SUCCESS
    number = s.release(entry)
    return E_SUCCESS if number == 0 else errno_of(number)


def fd_renumber(s: State, source: int, target: int) -> int:
    a, b = s.entry_of(source), s.entry_of(target)
    if a is None or b is None:
        return E_BADF
    if source == target:
        return E_SUCCESS
    s.release(b)
    s.entries[target] = a
    s.entries[source] = Entry()
    return E_SUCCESS


def fd_advise(s: State, fd: int, offset: int, length: int, advice: int) -> int:
    entry = s.entry_of(fd)
    if entry is None or entry.kind != K_FILE:
        return E_BADF
    if not entry.rights & R_FD_ADVISE:
        return E_NOTCAPABLE
    if advice > 5:  # the advice is one of six
        return E_INVAL
    return E_SUCCESS  # a hint: nothing is done with it


def fd_fdstat_get(s: State, fd: int, ptr: int) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    filetype = s.std_filetype(entry) if entry.kind == K_STD else entry.filetype
    base = (RIGHTS_STDIN if entry.fd == 0 else RIGHTS_STDOUT) if entry.kind == K_STD else entry.rights
    if not s.in_memory(ptr, 24):
        return E_FAULT
    s.put_bytes(ptr, bytes(24))
    s.put_u8(ptr, filetype)
    s.put_u16(ptr + 2, entry.flags)
    s.put_u64(ptr + 8, base)
    s.put_u64(ptr + 16, entry.inheriting)
    return E_SUCCESS


def fd_prestat_get(s: State, fd: int, ptr: int) -> int:
    entry = s.entry_of(fd)
    if entry is None or entry.preopen is None:
        return E_BADF  # the end of the list the program walks
    if not s.in_memory(ptr, 8):
        return E_FAULT
    s.put_bytes(ptr, bytes(8))  # the type of a preopen: a directory, 0
    s.put_u32(ptr + 4, len(entry.preopen))
    return E_SUCCESS


def fd_prestat_dir_name(s: State, fd: int, path: int, size: int) -> int:
    entry = s.entry_of(fd)
    if entry is None or entry.preopen is None:
        return E_BADF
    if size < len(entry.preopen):
        return E_NAMETOOLONG
    if not s.in_memory(path, len(entry.preopen)):
        return E_FAULT
    s.put_bytes(path, entry.preopen)
    return E_SUCCESS
