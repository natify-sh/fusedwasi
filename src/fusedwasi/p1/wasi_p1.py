"""The calls of `wasi_snapshot_preview1`, one function for each (the twin of the `fusedwasi_p1_<name>` of `include/fusedwasi/p1/wasi_p1_core.h`).

Every function takes the `Guest` first, then the parameters of the call as the core module passes them (integers: a pointer is an offset in the memory), and returns the errno of
WASI; what the call gives back is written into the memory. Integers are unsigned, parameters and results (see `fusedwasi.p2.wasi_p2_core`: an engine that passes signed values masks them first). A trap is an exception. Done so far (the rest of the 46 follows, a stage at a time): arguments and environment, clocks,
random bytes, `proc_exit` and `proc_raise`, `sched_yield`, the standard streams and the descriptors of the preopened directories (`fd_write`, `fd_read`, `fd_close`,
`fd_renumber`, `fd_seek`, `fd_tell`, `fd_advise`, `fd_fdstat_get`, `fd_prestat_get`, `fd_prestat_dir_name`).
"""

from __future__ import annotations

import os
import select
import stat
import struct
import sys
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .wasi_p1_files import Listing

from .wasi_p1_core import (
    E_BADF,
    E_FAULT,
    E_FBIG,
    E_INVAL,
    E_IO,
    E_ISDIR,
    E_NAMETOOLONG,
    E_NOENT,
    E_NOTCAPABLE,
    E_NOTDIR,
    E_NOTSOCK,
    E_NOTSUP,
    E_SPIPE,
    E_SUCCESS,
    FD_APPEND,
    FT_CHAR,
    K_DIR,
    K_FILE,
    K_STD,
    LOOKUP_FOLLOW,
    O_CREAT,
    O_DIRECTORY,
    O_TRUNC,
    R_ALL,
    R_FD_ADVISE,
    R_FD_ALLOCATE,
    R_FD_DATASYNC,
    R_FD_FDSTAT_SET_FLAGS,
    R_FD_FILESTAT_GET,
    R_FD_FILESTAT_SET_SIZE,
    R_FD_FILESTAT_SET_TIMES,
    R_FD_READ,
    R_FD_READDIR,
    R_FD_SEEK,
    R_FD_SYNC,
    R_FD_TELL,
    R_FD_WRITE,
    R_PATH_CREATE_DIRECTORY,
    R_PATH_CREATE_FILE,
    R_PATH_FILESTAT_GET,
    R_PATH_FILESTAT_SET_SIZE,
    R_PATH_FILESTAT_SET_TIMES,
    R_PATH_LINK_SOURCE,
    R_PATH_LINK_TARGET,
    R_PATH_OPEN,
    R_PATH_READLINK,
    R_PATH_REMOVE_DIRECTORY,
    R_PATH_RENAME_SOURCE,
    R_PATH_RENAME_TARGET,
    R_PATH_SYMLINK,
    R_PATH_UNLINK_FILE,
    R_POLL_FD_READWRITE,
    RIGHTS_STDIN,
    RIGHTS_STDOUT,
    Entry,
    Exit,
    Guest,
    Stat,
    errno_of,
)


def _signed(value: int) -> int:
    """A 64-bit parameter of the call as the signed number the system takes."""
    return value - (1 << 64) if value >= 1 << 63 else value


# ---- arguments and environment ------------------------------------------------------------------------------------------------


def _sizes(guest: Guest, count: int, size: int, count_ptr: int, size_ptr: int) -> int:
    if not (guest.put_u32(count_ptr, count) and guest.put_u32(size_ptr, size)):
        return E_FAULT
    return E_SUCCESS


def _strings(guest: Guest, items: list[bytes], table: int, out: int) -> int:
    buf = b"".join(item + b"\0" for item in items)
    if not (guest.in_memory(out, len(buf)) and guest.in_memory(table, len(items) * 4)):
        return E_FAULT
    guest.put_bytes(out, buf)
    offset = 0
    for i, item in enumerate(items):
        guest.put_u32(table + 4 * i, out + offset)
        offset += len(item) + 1
    return E_SUCCESS


def _args(guest: Guest) -> list[bytes]:
    return [os.fsencode(a) for a in guest.state.core.args]


def _env(guest: Guest) -> list[bytes]:
    return [os.fsencode(f"{name}={value}") for name, value in guest.state.core.env]


def args_sizes_get(guest: Guest, argc: int, size: int) -> int:
    items = _args(guest)
    return _sizes(guest, len(items), sum(len(a) + 1 for a in items), argc, size)


def args_get(guest: Guest, argv: int, buf: int) -> int:
    return _strings(guest, _args(guest), argv, buf)


def environ_sizes_get(guest: Guest, count: int, size: int) -> int:
    items = _env(guest)
    return _sizes(guest, len(items), sum(len(v) + 1 for v in items), count, size)


def environ_get(guest: Guest, environ: int, buf: int) -> int:
    return _strings(guest, _env(guest), environ, buf)


# ---- clocks and random bytes --------------------------------------------------------------------------------------------------


def clock_res_get(guest: Guest, ident: int, ptr: int) -> int:
    if ident > 3:
        return E_INVAL
    return E_SUCCESS if guest.put_u64(ptr, guest.state.core.resolution_ns[ident]) else E_FAULT


def clock_time_get(guest: Guest, ident: int, precision: int, ptr: int) -> int:
    status, ns = guest.state.clock_ns(ident)
    if status:
        return status
    return E_SUCCESS if guest.put_u64(ptr, ns) else E_FAULT


def random_get(guest: Guest, buf: int, size: int) -> int:
    if not guest.in_memory(buf, size):
        return E_FAULT
    if size == 0:
        return E_SUCCESS
    try:
        data = guest.state.core.random_bytes(size)
    except OSError:
        return E_IO
    if len(data) != size:
        return E_IO
    guest.put_bytes(buf, data)
    return E_SUCCESS


def sched_yield(guest: Guest) -> int:
    return E_SUCCESS


def proc_exit(guest: Guest, code: int) -> None:
    guest.exited, guest.exit_code = True, code  # the end of a call is the guest's
    raise Exit(code)


def proc_raise(guest: Guest, sig: int) -> int:
    """The signals of WASI 0.1 by number. What a signal does to a program is the host's decision, not WASI's, so the library does not end the program: it asks the hook `signal` of the
    guest, which returns the errno of the call or ends the program by raising. Without the hook the answer is NOTSUP and the program goes on (as in Wasmtime). A number above 30 is
    INVAL, and 0 (no signal) is success."""
    if sig > 30:
        return E_INVAL
    if sig == 0:
        return E_SUCCESS
    return guest.signal(sig) if guest.signal is not None else E_NOTSUP


# ---- descriptors --------------------------------------------------------------------------------------------------------------


def fd_write(guest: Guest, fd: int, iovs: int, count: int, nwritten: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_DIR:
        return E_ISDIR
    if entry.kind == K_STD:
        return E_BADF if entry.fd == 0 else guest.write_std(entry.fd, iovs, count, nwritten)
    if not entry.rights & R_FD_WRITE:
        return E_NOTCAPABLE
    bad = guest.iovs_valid(iovs, count)
    if bad:
        return bad
    total = 0
    for i in range(count):
        base, length = guest.iov(iovs, i)
        if not guest.in_memory(base, length):
            return E_FAULT
        if length == 0:
            continue
        if sys.platform == "win32" and entry.flags & FD_APPEND:
            guest.state.files.seek(
                entry, 0, 2
            )  # the C library of Windows cannot be opened for appending and take it off again: the end is sought here
        number, n = guest.state.files.write(
            entry, guest.read_bytes(base, length)
        )  # straight from the memory of the module
        if number:
            if total:
                break
            return number
        total += n
        if n < length:
            break
    return E_SUCCESS if guest.put_u32(nwritten, total) else E_FAULT


def fd_read(guest: Guest, fd: int, iovs: int, count: int, nread: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        return guest.read_std(iovs, count, nread) if entry.fd == 0 else E_BADF
    if entry.kind == K_DIR:
        return E_ISDIR
    if not entry.rights & R_FD_READ:
        return E_NOTCAPABLE
    bad = guest.iovs_valid(iovs, count)
    if bad:
        return bad
    total = 0
    for i in range(count):
        base, length = guest.iov(iovs, i)
        if not guest.in_memory(base, length):
            return E_FAULT
        if length == 0:
            continue
        number, data = guest.state.files.read(entry, length)
        if number:
            if total:
                break  # what was read is reported; the error comes with the next call
            return number
        guest.put_bytes(base, data)
        total += len(data)
        if len(data) < length:
            break
        # A pipe, a FIFO, a terminal: when the first buffer is full, a read into the next one may wait for more although something has been read, and a short read is allowed.
        # Only a regular file goes on.
        if data and i + 1 < count and not guest.state.files.is_regular(entry):
            break
    return E_SUCCESS if guest.put_u32(nread, total) else E_FAULT


def _seek_right(offset: int, whence: int) -> int:
    """What fd_seek asks of the descriptor: its right, or (a seek by 0 bytes from the current position is a tell) the right to tell."""
    return R_FD_SEEK | (R_FD_TELL if offset == 0 and whence == 1 else 0)


def _seek_native(guest: Guest, fd: int, offset: int, whence: int, ptr: int, right: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        return E_SPIPE
    if entry.kind != K_FILE:
        return E_BADF
    if not entry.rights & right:
        return E_NOTCAPABLE
    number, position = guest.state.files.seek(entry, offset, whence)
    if number:
        return number
    return E_SUCCESS if guest.put_u64(ptr, position) else E_FAULT


def fd_seek(guest: Guest, fd: int, offset: int, whence: int, ptr: int) -> int:
    """The order of whence in this snapshot: set, cur, end."""
    if whence > 2:
        return E_INVAL
    return _seek_native(guest, fd, _signed(offset), whence, ptr, _seek_right(_signed(offset), whence))


def fd_tell(guest: Guest, fd: int, ptr: int) -> int:
    return _seek_native(guest, fd, 0, 1, ptr, R_FD_TELL)


def fd_close(guest: Guest, fd: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if (
        entry.kind == K_STD
    ):  # the stream of the host stays open, but the program has let go of it: the number is free and a write to it is BADF
        entry.__init__()  # type: ignore[misc]
        return E_SUCCESS
    number = guest.state.release(entry)
    return E_SUCCESS if number == 0 else errno_of(number)


def fd_renumber(guest: Guest, source: int, target: int) -> int:
    a, b = guest.state.entry_of(source), guest.state.entry_of(target)
    if a is None or b is None:
        return E_BADF
    if source == target:
        return E_SUCCESS
    guest.state.release(b)
    guest.state.entries[target] = a
    guest.state.entries[source] = Entry()
    return E_SUCCESS


def fd_advise(guest: Guest, fd: int, offset: int, length: int, advice: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None or entry.kind != K_FILE:
        return E_BADF
    if not entry.rights & R_FD_ADVISE:
        return E_NOTCAPABLE
    if advice > 5:  # the advice is one of six
        return E_INVAL
    return E_SUCCESS  # a hint: nothing is done with it


def fd_fdstat_get(guest: Guest, fd: int, ptr: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    filetype = guest.state.std_filetype(entry) if entry.kind == K_STD else entry.filetype
    base = (RIGHTS_STDIN if entry.fd == 0 else RIGHTS_STDOUT) if entry.kind == K_STD else entry.rights
    if not guest.in_memory(ptr, 24):
        return E_FAULT
    guest.put_bytes(ptr, bytes(24))
    guest.put_u8(ptr, filetype)
    guest.put_u16(ptr + 2, entry.flags)
    guest.put_u64(ptr + 8, base)
    guest.put_u64(ptr + 16, entry.inheriting)
    return E_SUCCESS


def fd_prestat_get(guest: Guest, fd: int, ptr: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None or entry.preopen is None:
        return E_BADF  # the end of the list the program walks
    if not guest.in_memory(ptr, 8):
        return E_FAULT
    guest.put_bytes(ptr, bytes(8))  # the type of a preopen: a directory, 0
    guest.put_u32(ptr + 4, len(entry.preopen))
    return E_SUCCESS


def fd_prestat_dir_name(guest: Guest, fd: int, path: int, size: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None or entry.preopen is None:
        return E_BADF
    if size < len(entry.preopen):
        return E_NAMETOOLONG
    if not guest.in_memory(path, len(entry.preopen)):
        return E_FAULT
    guest.put_bytes(path, entry.preopen)
    return E_SUCCESS


# ---- what a file or a directory is --------------------------------------------------------------------------------------------


def _put_filestat(guest: Guest, ptr: int, st: Stat, legacy: bool) -> int:
    """The filestat of the first snapshot has a 32-bit link count and 56 bytes. It is written at once: for an engine that keeps its memory out of this
    process every write is a round trip."""
    total = 56 if legacy else 64
    if not guest.in_memory(ptr, total):
        return E_FAULT
    if legacy:
        record = struct.pack("<QQB3xIQQQQ", st.dev, st.ino, st.filetype, st.nlink, st.size, st.atim, st.mtim, st.ctim)
    else:
        record = struct.pack("<QQB7xQQQQQ", st.dev, st.ino, st.filetype, st.nlink, st.size, st.atim, st.mtim, st.ctim)
    guest.put_bytes(ptr, record)
    return E_SUCCESS


def filestat_of_fd(guest: Guest, fd: int, ptr: int, legacy: bool) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        if guest.state.std_is_real(entry):
            number, st = guest.state.files.stat_fd(entry)
            if number == E_SUCCESS and st is not None:
                return _put_filestat(guest, ptr, st, legacy)  # what it is
        return _put_filestat(
            guest, ptr, Stat(filetype=FT_CHAR, nlink=1), legacy
        )  # a stream of the host: nothing to say but that it is a character device
    if not entry.rights & R_FD_FILESTAT_GET:
        return E_NOTCAPABLE
    number, st = guest.state.files.stat_fd(entry)
    return number if number or st is None else _put_filestat(guest, ptr, st, legacy)


def filestat_of_path(guest: Guest, fd: int, flags: int, path: int, length: int, ptr: int, legacy: bool) -> int:
    directory = guest.state.entry_of(fd)
    if directory is None:
        return E_BADF
    if not directory.rights & R_PATH_FILESTAT_GET:
        return E_NOTCAPABLE
    number, st = guest.state.files.stat_path(directory, guest.path_at(path, length), bool(flags & LOOKUP_FOLLOW))
    return number if number or st is None else _put_filestat(guest, ptr, st, legacy)


def fd_filestat_get(guest: Guest, fd: int, ptr: int) -> int:
    return filestat_of_fd(guest, fd, ptr, False)


def path_filestat_get(guest: Guest, fd: int, flags: int, path: int, length: int, ptr: int) -> int:
    return filestat_of_path(guest, fd, flags, path, length, ptr, False)


def _dirent_order(item: Listing) -> tuple[int, bytes]:
    return (0 if item.name == b"." else 1 if item.name == b".." else 2), item.name


def fd_readdir(guest: Guest, fd: int, buf: int, size: int, cookie: int, used: int) -> int:
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind != K_DIR:
        return E_NOTDIR
    if not entry.rights & R_FD_READDIR:
        return E_NOTCAPABLE
    if not guest.in_memory(buf, size):
        return E_FAULT
    if cookie == 0 or entry.listing is None:  # the listing is made again when the program starts from the beginning
        number, names = guest.state.files.list(entry)
        if number:
            return number
        entry.listing = sorted(
            names, key=_dirent_order
        )  # "." and "..", then the names in order: the same for every way of getting them
    at = 0
    names = entry.listing
    index = cookie
    while index < len(names) and at < size:
        item = names[index]
        head = struct.pack("<QQIB3x", index + 1, item.ino, len(item.name), item.filetype)
        part = min(size - at, len(head))  # the last entry may be cut short
        guest.put_bytes(buf + at, head[:part])
        at += part
        if part < len(head):
            break
        part = min(size - at, len(item.name))
        guest.put_bytes(buf + at, item.name[:part])
        at += part
        index += 1
    return E_SUCCESS if guest.put_u32(used, at) else E_FAULT


def path_open(
    guest: Guest,
    fd: int,
    dirflags: int,
    path: int,
    length: int,
    oflags: int,
    base: int,
    inheriting: int,
    fdflags: int,
    out: int,
) -> int:
    directory = guest.state.entry_of(fd)
    if directory is None:
        return E_BADF
    if directory.kind != K_DIR:
        return E_NOTDIR
    if oflags >> 4 or fdflags >> 5:
        return E_INVAL
    if not directory.rights & R_PATH_OPEN:
        return E_NOTCAPABLE
    base &= directory.inheriting & R_ALL
    inheriting &= directory.inheriting & R_ALL
    if oflags & O_CREAT:
        if oflags & O_DIRECTORY:
            return E_INVAL
        if not directory.rights & R_PATH_CREATE_FILE:
            return E_NOTCAPABLE
    if oflags & O_TRUNC and not directory.rights & R_PATH_FILESTAT_SET_SIZE:
        return E_NOTCAPABLE
    number = guest.state.new_fd()
    entry = guest.state.entries[number]
    result = guest.state.files.open(
        directory, guest.path_at(path, length), bool(dirflags & LOOKUP_FOLLOW), oflags, base, fdflags, entry
    )
    if result:
        guest.state.release(entry)
        return result
    entry.rights = base
    if entry.kind == K_DIR:
        entry.rights &= ~(R_FD_SEEK | R_FD_TELL)  # a directory is not seeked
    entry.inheriting = inheriting
    entry.flags = fdflags
    if not guest.put_u32(out, number):
        guest.state.release(entry)
        return E_FAULT
    return E_SUCCESS


# ---- writing, creating, removing ----------------------------------------------------------------------------------------------


def _sync(guest: Guest, fd: int, right: int, data_only: bool) -> int:
    number, entry = guest.state.entry_with(fd, right)
    if number or entry is None:
        return number
    if entry.kind == K_STD:
        return E_SUCCESS  # the stream is Python's: it flushes when it wants
    return guest.state.files.sync(entry, data_only)


def fd_datasync(guest: Guest, fd: int) -> int:
    return _sync(guest, fd, R_FD_DATASYNC, True)


def fd_sync(guest: Guest, fd: int) -> int:
    return _sync(guest, fd, R_FD_SYNC, False)


def fd_filestat_set_size(guest: Guest, fd: int, size: int) -> int:
    number, entry = guest.state.entry_with(fd, R_FD_FILESTAT_SET_SIZE)
    return number if number or entry is None else guest.state.files.set_size(entry, size)


def fd_allocate(guest: Guest, fd: int, offset: int, length: int) -> int:
    """Makes the file at least offset + length long (the file system may not reserve the room, but it is as long)."""
    number, entry = guest.state.entry_with(fd, R_FD_ALLOCATE)
    if number or entry is None:
        return number
    if entry.kind == K_DIR:
        return E_ISDIR
    if entry.kind != K_FILE:
        return E_BADF
    if offset > 2**63 - 1 or length > 2**63 - 1 - offset:
        return E_FBIG
    return guest.state.files.allocate(entry, offset, length)


def _times_flags_ok(flags: int) -> int:
    bad = flags > 15 or (flags & 1 and flags & 2) or (flags & 4 and flags & 8)
    return E_INVAL if bad else E_SUCCESS


def fd_filestat_set_times(guest: Guest, fd: int, atim: int, mtim: int, flags: int) -> int:
    number, entry = guest.state.entry_with(fd, R_FD_FILESTAT_SET_TIMES)
    if number or entry is None:
        return number
    if entry.kind == K_STD:
        return E_BADF
    number = _times_flags_ok(flags)
    return number or guest.state.files.times_fd(entry, atim, mtim, flags)


def path_filestat_set_times(
    guest: Guest, fd: int, lookup: int, path: int, length: int, atim: int, mtim: int, flags: int
) -> int:
    number, directory = guest.state.dir_with(fd, R_PATH_FILESTAT_SET_TIMES)
    if number or directory is None:
        return number
    number = _times_flags_ok(flags)
    return number or guest.state.files.times_path(
        directory, guest.path_at(path, length), bool(lookup & LOOKUP_FOLLOW), atim, mtim, flags
    )


def fd_fdstat_set_flags(guest: Guest, fd: int, flags: int) -> int:
    number, entry = guest.state.entry_with(fd, R_FD_FDSTAT_SET_FLAGS)
    if number or entry is None:
        return number
    if flags > 31:
        return E_INVAL
    if entry.kind != K_FILE:
        return E_SUCCESS if entry.kind == K_STD else E_BADF
    number = guest.state.files.set_flags(entry, flags)
    if number:
        return number
    entry.flags = flags
    return E_SUCCESS


def fd_fdstat_set_rights(guest: Guest, fd: int, base: int, inheriting: int) -> int:
    """The rights can only be given up, never gained."""
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if (
        entry.kind == K_STD
    ):  # the rights of a standard stream are fixed: giving some up (or none) is fine, getting more is not (the narrowing is not kept)
        have = RIGHTS_STDIN if entry.fd == 0 else RIGHTS_STDOUT
        return E_NOTCAPABLE if base & ~have or inheriting else E_SUCCESS
    if base & ~entry.rights or inheriting & ~entry.inheriting:
        return E_NOTCAPABLE
    entry.rights = base
    entry.inheriting = inheriting
    return E_SUCCESS


def path_create_directory(guest: Guest, fd: int, path: int, length: int) -> int:
    number, directory = guest.state.dir_with(fd, R_PATH_CREATE_DIRECTORY)
    return number if number or directory is None else guest.state.files.mkdir(directory, guest.path_at(path, length))


def path_remove_directory(guest: Guest, fd: int, path: int, length: int) -> int:
    number, directory = guest.state.dir_with(fd, R_PATH_REMOVE_DIRECTORY)
    return (
        number
        if number or directory is None
        else guest.state.files.remove(directory, guest.path_at(path, length), True)
    )


def path_unlink_file(guest: Guest, fd: int, path: int, length: int) -> int:
    number, directory = guest.state.dir_with(fd, R_PATH_UNLINK_FILE)
    return (
        number
        if number or directory is None
        else guest.state.files.remove(directory, guest.path_at(path, length), False)
    )


# ---- positions, renaming, links -----------------------------------------------------------------------------------------------


def _at(guest: Guest, fd: int, iovs: int, count: int, offset: int, out: int, writing: bool) -> int:
    """Read or write at a position, leaving the position of the descriptor where it was."""
    entry = guest.state.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        return E_SPIPE
    if entry.kind == K_DIR:
        return E_ISDIR
    # the specification asks for fd_seek as well; wasi-libc opens a file for writing without it and calls pwrite all the same
    if not entry.rights & (R_FD_WRITE if writing else R_FD_READ):
        return E_NOTCAPABLE
    if offset > 2**63 - 1:
        return E_INVAL
    bad = guest.iovs_valid(iovs, count)
    if bad:
        return bad
    total = 0
    for i in range(count):
        base, length = guest.iov(iovs, i)
        if not guest.in_memory(base, length):
            return E_FAULT
        if length == 0:
            continue
        if writing:
            number, n = guest.state.files.write_at(entry, guest.read_bytes(base, length), offset + total)
        else:
            number, data = guest.state.files.read_at(entry, length, offset + total)
            n = len(data)
            if not number:
                guest.put_bytes(base, data)
        if number:
            if total:
                break
            return number
        total += n
        if n < length:
            break
    return E_SUCCESS if guest.put_u32(out, total) else E_FAULT


def fd_pread(guest: Guest, fd: int, iovs: int, count: int, offset: int, nread: int) -> int:
    return _at(guest, fd, iovs, count, offset, nread, False)


def fd_pwrite(guest: Guest, fd: int, iovs: int, count: int, offset: int, nwritten: int) -> int:
    return _at(guest, fd, iovs, count, offset, nwritten, True)


def path_rename(guest: Guest, fd: int, old: int, old_len: int, new_fd: int, new: int, new_len: int) -> int:
    number, source = guest.state.dir_with(fd, R_PATH_RENAME_SOURCE)
    if number or source is None:
        return number
    number, target = guest.state.dir_with(new_fd, R_PATH_RENAME_TARGET)
    if number or target is None:
        return number
    return guest.state.files.rename(source, guest.path_at(old, old_len), target, guest.path_at(new, new_len))


def path_link(guest: Guest, fd: int, flags: int, old: int, old_len: int, new_fd: int, new: int, new_len: int) -> int:
    number, source = guest.state.dir_with(fd, R_PATH_LINK_SOURCE)
    if number or source is None:
        return number
    number, target = guest.state.dir_with(new_fd, R_PATH_LINK_TARGET)
    if number or target is None:
        return number
    return guest.state.files.link(
        source, guest.path_at(old, old_len), bool(flags & LOOKUP_FOLLOW), target, guest.path_at(new, new_len)
    )


def path_symlink(guest: Guest, target: int, target_len: int, fd: int, path: int, length: int) -> int:
    """A link that leads out cannot be followed, and one that is absolute could not be anywhere else than outside: neither is made."""
    number, directory = guest.state.dir_with(fd, R_PATH_SYMLINK)
    if number or directory is None:
        return number
    if not guest.in_memory(target, target_len):
        return E_FAULT
    if target_len == 0:
        return E_NOENT
    text = guest.read_bytes(target, target_len)
    if b"\0" in text:
        return E_INVAL
    if text[:1] == b"/":
        return E_NOTCAPABLE
    return guest.state.files.symlink(text, directory, guest.path_at(path, length))


LINK_MAX = 4096  # the most that path_readlink reads of a link


def path_readlink(guest: Guest, fd: int, path: int, length: int, buf: int, buf_len: int, used: int) -> int:
    number, directory = guest.state.dir_with(fd, R_PATH_READLINK)
    if number or directory is None:
        return number
    if not guest.in_memory(buf, buf_len):
        return E_FAULT
    number, text = guest.state.files.readlink(directory, guest.path_at(path, length), min(buf_len, LINK_MAX))
    if number:
        return number
    guest.put_bytes(buf, text)
    return E_SUCCESS if guest.put_u32(used, len(text)) else E_FAULT


# ---- poll_oneoff and the sockets ----------------------------------------------------------------------------------------------

# A regular file is always ready; what makes the program wait is a clock, and it sleeps (the hook of the host, or the system's, in slices of
# 100 ms) until the first one. A pipe, a FIFO, a terminal or the standard input of the process is asked of the system (select.poll).


if sys.platform != "win32":
    _POLLIN, _POLLOUT, _POLLHUP, _POLLERR, _POLLNVAL = (
        select.POLLIN,
        select.POLLOUT,
        select.POLLHUP,
        select.POLLERR,
        select.POLLNVAL,
    )
else:  # no poll in the system's select: the descriptors that could need it are answered at once
    _POLLIN, _POLLOUT, _POLLHUP, _POLLERR, _POLLNVAL = 1, 4, 16, 8, 32


def _subscription_wait(guest: Guest, sub: int, legacy: bool, now: dict[int, int]) -> tuple[int, int]:
    """(errno, nanoseconds a clock subscription still has to wait). `legacy`: the first snapshot, whose clock subscription starts with an identifier."""
    shift = 8 if legacy else 0
    raw = guest.load(sub + 16 + shift, 28)  # the identifier at 0, the timeout at 8, the flags at 24
    ident = struct.unpack_from("<I", raw, 0)[0]
    timeout = struct.unpack_from("<Q", raw, 8)[0]
    flags = struct.unpack_from("<H", raw, 24)[0]
    if (
        ident > 1 or flags & ~1
    ):  # the clocks of the process and the thread are not something to wait for; the one flag is abstime
        return E_INVAL, 0
    if not flags & 1:
        return E_SUCCESS, timeout
    if ident not in now:
        status, value = guest.state.clock_ns(ident)
        if status:
            return status, 0
        now[ident] = value
    return E_SUCCESS, max(0, timeout - now[ident])


def _poll_kind(guest: Guest, sub: int, tag: int) -> tuple[int, int | None]:
    """(errno of the event, descriptor the system has to say about, or None when the answer is at once)."""
    entry = guest.state.entry_of(struct.unpack("<I", guest.load(sub + 16, 4))[0])
    if entry is None:
        return E_BADF, None
    need = R_POLL_FD_READWRITE | (R_FD_READ if tag == 1 else R_FD_WRITE)
    if entry.kind == K_STD:  # the rights of the standard streams are fixed and include polling
        return E_SUCCESS, 0 if tag == 1 and entry.fd == 0 and guest.state.std_is_real(
            entry
        ) and sys.platform != "win32" else None
    if entry.rights & need != need:
        return E_NOTCAPABLE, None
    if entry.kind == K_FILE and entry.fd >= 0 and sys.platform != "win32":
        try:
            mode = os.fstat(entry.fd).st_mode
        except OSError:
            return E_SUCCESS, None
        if not stat.S_ISREG(mode) and not stat.S_ISDIR(mode):
            return E_SUCCESS, entry.fd
    return E_SUCCESS, None


def _pending(fd: int) -> int:
    if sys.platform == "win32":
        return 0
    import fcntl
    import termios

    try:
        return struct.unpack("i", fcntl.ioctl(fd, termios.FIONREAD, b"\0\0\0\0"))[0]
    except OSError:
        return 0


def _poll(guest: Guest, in_: int, out: int, count: int, nevents: int, legacy: bool) -> int:
    stride = 56 if legacy else 48
    if count == 0:
        return E_INVAL
    if not guest.in_memory(in_, count * stride) or not guest.in_memory(out, count * 32):
        return E_FAULT
    subs = guest.load(in_, count * stride)  # the subscriptions, read once
    now: dict[int, int] = {}
    shortest: int | None = None  # a clock was subscribed
    immediate = False
    watched: dict[int, int] = {}  # subscription -> the descriptor
    for i in range(count):
        sub = in_ + i * stride
        tag = subs[i * stride + 8]
        if tag == 0:
            status, wait = _subscription_wait(guest, sub, legacy, now)
            if status == E_SUCCESS:
                if shortest is None or wait < shortest:
                    shortest = wait
            elif status == E_INVAL:
                immediate = True
            else:
                return status
        elif tag in (1, 2):
            _, fd = _poll_kind(guest, sub, tag)
            if fd is None:
                immediate = True
            else:
                watched[i] = fd
        else:
            return E_INVAL
    waited = 0
    ready: dict[int, int] = {}
    if watched and sys.platform != "win32":
        poller = select.poll()
        for i, fd in watched.items():
            ready[i] = 0
            poller.register(fd, _POLLIN if subs[i * stride + 8] == 1 else _POLLOUT)
        started = guest.state.core.clock(1)
        timeout: float | None
        if immediate:
            timeout = 0
        elif shortest is None:
            timeout = None
        else:
            timeout = max(0, shortest - (guest.state.core.clock(1) - started)) / 1e6
        while True:
            try:
                events = poller.poll(timeout)
                break
            except InterruptedError:
                continue
        by_fd: dict[int, int] = {}
        for fd, revents in events:
            by_fd[fd] = revents
        for i, fd in watched.items():
            ready[i] = by_fd.get(fd, 0)
        if not events and not immediate and shortest is not None:
            waited = shortest
        elif events:
            waited = guest.state.core.clock(1) - started
    elif not immediate and shortest:
        guest.state.core.sleep(shortest)
        waited = shortest
    made = 0
    for i in range(count):
        sub = in_ + i * stride
        event = out + made * 32
        tag = subs[i * stride + 8]
        error = E_SUCCESS
        nbytes = 0
        flags = 0
        if tag == 0:
            status, wait = _subscription_wait(guest, sub, legacy, now)
            if status == E_SUCCESS and wait > waited:
                continue  # not yet
            error = status
        else:
            number = struct.unpack_from("<I", subs, i * stride + 16)[0]
            entry = guest.state.entry_of(number)
            if i in watched:
                revents = ready[i]
                if not revents & (_POLLIN | _POLLOUT | _POLLHUP | _POLLERR | _POLLNVAL):
                    continue  # not ready
                if revents & _POLLNVAL:
                    error = E_BADF
                elif revents & (_POLLHUP | _POLLERR) and not revents & (_POLLIN | _POLLOUT):
                    flags = 1  # fd_readwrite_hangup
                if tag == 1 and not revents & _POLLNVAL:
                    nbytes = _pending(watched[i])
            else:
                error, _ = _poll_kind(guest, sub, tag)
                if error == E_SUCCESS and entry is not None and entry.kind == K_FILE and tag == 1:
                    where, position = guest.state.files.seek(entry, 0, 1)
                    found, st = guest.state.files.stat_fd(entry)
                    if where == E_SUCCESS and found == E_SUCCESS and st is not None and st.size > position:
                        nbytes = st.size - position
        record = bytearray(32)
        record[0:8] = subs[i * stride : i * stride + 8]  # userdata
        struct.pack_into("<H", record, 8, error)
        record[10] = tag
        if tag != 0:
            struct.pack_into("<Q", record, 16, nbytes)
            struct.pack_into("<H", record, 24, flags)
        guest.store(event, bytes(record))
        made += 1
    return E_SUCCESS if guest.put_u32(nevents, made) else E_FAULT


def poll_oneoff(guest: Guest, in_: int, out: int, count: int, nevents: int) -> int:
    return _poll(guest, in_, out, count, nevents, False)


def _socket(guest: Guest, fd: int) -> int:
    """A descriptor is never a socket (a program cannot make one): the call says so, or that there is no such descriptor."""
    return E_BADF if guest.state.entry_of(fd) is None else E_NOTSOCK


def sock_accept(guest: Guest, fd: int, flags: int, result: int) -> int:
    return _socket(guest, fd)


def sock_recv(guest: Guest, fd: int, iovs: int, count: int, flags: int, nread: int, roflags: int) -> int:
    return _socket(guest, fd)


def sock_send(guest: Guest, fd: int, iovs: int, count: int, flags: int, nsent: int) -> int:
    return _socket(guest, fd)


def sock_shutdown(guest: Guest, fd: int, how: int) -> int:
    return _socket(guest, fd)
