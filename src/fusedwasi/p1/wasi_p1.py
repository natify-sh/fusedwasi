"""The calls of `wasi_snapshot_preview1`, one function for each (the twin of the `fusedwasi_p1_<name>` of `include/fusedwasi/p1/wasi_p1_core.h`).

Every function takes the `State` first, then the parameters of the call as the core module passes them (integers: a pointer is an offset in the memory), and returns the errno of
WASI; what the call gives back is written into the memory. A trap is an exception. Done so far (the rest of the 46 follows, a stage at a time): arguments and environment, clocks,
random bytes, `proc_exit` and `proc_raise`, `sched_yield`, the standard streams and the descriptors of the preopened directories (`fd_write`, `fd_read`, `fd_close`,
`fd_renumber`, `fd_seek`, `fd_tell`, `fd_advise`, `fd_fdstat_get`, `fd_prestat_get`, `fd_prestat_dir_name`).
"""

from __future__ import annotations

import os
import struct
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
    E_NOTSUP,
    E_SPIPE,
    E_SUCCESS,
    END_EXIT,
    END_SIGNAL,
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
    RIGHTS_STDIN,
    RIGHTS_STDOUT,
    Entry,
    Exit,
    Signal,
    Stat,
    State,
    errno_of,
)


def _signed(value: int) -> int:
    """A 64-bit parameter of the call as the signed number the system takes."""
    return value - (1 << 64) if value >= 1 << 63 else value


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
    bad = s.iovs_valid(iovs, count)
    if bad:
        return bad
    total = 0
    for i in range(count):
        base, length = s.iov(iovs, i)
        if not s.in_memory(base, length):
            return E_FAULT
        if length == 0:
            continue
        number, n = s.files.write(entry, s.read_bytes(base, length))  # straight from the memory of the module
        if number:
            if total:
                break
            return number
        total += n
        if n < length:
            break
    return E_SUCCESS if s.put_u32(nwritten, total) else E_FAULT


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
    bad = s.iovs_valid(iovs, count)
    if bad:
        return bad
    total = 0
    for i in range(count):
        base, length = s.iov(iovs, i)
        if not s.in_memory(base, length):
            return E_FAULT
        if length == 0:
            continue
        number, data = s.files.read(entry, length)
        if number:
            if total:
                break  # what was read is reported; the error comes with the next call
            return number
        s.put_bytes(base, data)
        total += len(data)
        if len(data) < length:
            break
        # A pipe, a FIFO, a terminal: when the first buffer is full, a read into the next one may wait for more although something has been read, and a short read is allowed.
        # Only a regular file goes on.
        if data and i + 1 < count and not s.files.is_regular(entry):
            break
    return E_SUCCESS if s.put_u32(nread, total) else E_FAULT


def _seek_native(s: State, fd: int, offset: int, whence: int, ptr: int, right: int) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        return E_SPIPE
    if entry.kind != K_FILE:
        return E_BADF
    if not entry.rights & right:
        return E_NOTCAPABLE
    number, position = s.files.seek(entry, offset, whence)
    if number:
        return number
    return E_SUCCESS if s.put_u64(ptr, position) else E_FAULT


def fd_seek(s: State, fd: int, offset: int, whence: int, ptr: int) -> int:
    """The order of whence in this snapshot: set, cur, end."""
    if whence > 2:
        return E_INVAL
    return _seek_native(s, fd, _signed(offset), whence, ptr, R_FD_SEEK)


def fd_tell(s: State, fd: int, ptr: int) -> int:
    return _seek_native(s, fd, 0, 1, ptr, R_FD_TELL)


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


# ---- what a file or a directory is --------------------------------------------------------------------------------------------


def _put_filestat(s: State, ptr: int, st: Stat, legacy: bool) -> int:
    """The filestat of the first snapshot has a 32-bit link count and 56 bytes."""
    total = 56 if legacy else 64
    if not s.in_memory(ptr, total):
        return E_FAULT
    s.put_bytes(ptr, bytes(total))
    s.put_u64(ptr, st.dev)
    s.put_u64(ptr + 8, st.ino)
    s.put_u8(ptr + 16, st.filetype)
    if legacy:
        s.put_u32(ptr + 20, st.nlink)
        s.put_u64(ptr + 24, st.size)
        s.put_u64(ptr + 32, st.atim)
        s.put_u64(ptr + 40, st.mtim)
        s.put_u64(ptr + 48, st.ctim)
    else:
        s.put_u64(ptr + 24, st.nlink)
        s.put_u64(ptr + 32, st.size)
        s.put_u64(ptr + 40, st.atim)
        s.put_u64(ptr + 48, st.mtim)
        s.put_u64(ptr + 56, st.ctim)
    return E_SUCCESS


def filestat_of_fd(s: State, fd: int, ptr: int, legacy: bool) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == K_STD:
        if s.std_is_real(entry):
            number, st = s.files.stat_fd(entry)
            if number == E_SUCCESS and st is not None:
                return _put_filestat(s, ptr, st, legacy)  # what it is
        return _put_filestat(
            s, ptr, Stat(filetype=FT_CHAR), legacy
        )  # a stream of the host: nothing to say but that it is a character device
    if not entry.rights & R_FD_FILESTAT_GET:
        return E_NOTCAPABLE
    number, st = s.files.stat_fd(entry)
    return number if number or st is None else _put_filestat(s, ptr, st, legacy)


def filestat_of_path(s: State, fd: int, flags: int, path: int, length: int, ptr: int, legacy: bool) -> int:
    directory = s.entry_of(fd)
    if directory is None:
        return E_BADF
    if not directory.rights & R_PATH_FILESTAT_GET:
        return E_NOTCAPABLE
    number, st = s.files.stat_path(directory, s.path_at(path, length), bool(flags & LOOKUP_FOLLOW))
    return number if number or st is None else _put_filestat(s, ptr, st, legacy)


def fd_filestat_get(s: State, fd: int, ptr: int) -> int:
    return filestat_of_fd(s, fd, ptr, False)


def path_filestat_get(s: State, fd: int, flags: int, path: int, length: int, ptr: int) -> int:
    return filestat_of_path(s, fd, flags, path, length, ptr, False)


def _dirent_order(item: Listing) -> tuple[int, bytes]:
    return (0 if item.name == b"." else 1 if item.name == b".." else 2), item.name


def fd_readdir(s: State, fd: int, buf: int, size: int, cookie: int, used: int) -> int:
    entry = s.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind != K_DIR:
        return E_NOTDIR
    if not entry.rights & R_FD_READDIR:
        return E_NOTCAPABLE
    if not s.in_memory(buf, size):
        return E_FAULT
    if cookie == 0 or entry.listing is None:  # the listing is made again when the program starts from the beginning
        number, names = s.files.list(entry)
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
        s.put_bytes(buf + at, head[:part])
        at += part
        if part < len(head):
            break
        part = min(size - at, len(item.name))
        s.put_bytes(buf + at, item.name[:part])
        at += part
        index += 1
    return E_SUCCESS if s.put_u32(used, at) else E_FAULT


def path_open(
    s: State,
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
    directory = s.entry_of(fd)
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
    number = s.new_fd()
    entry = s.entries[number]
    result = s.files.open(
        directory, s.path_at(path, length), bool(dirflags & LOOKUP_FOLLOW), oflags, base, fdflags, entry
    )
    if result:
        s.release(entry)
        return result
    entry.rights = base
    if entry.kind == K_DIR:
        entry.rights &= ~(R_FD_SEEK | R_FD_TELL)  # a directory is not seeked
    entry.inheriting = inheriting
    entry.flags = fdflags
    if not s.put_u32(out, number):
        s.release(entry)
        return E_FAULT
    return E_SUCCESS


# ---- writing, creating, removing ----------------------------------------------------------------------------------------------


def _sync(s: State, fd: int, right: int, data_only: bool) -> int:
    number, entry = s.entry_with(fd, right)
    if number or entry is None:
        return number
    if entry.kind == K_STD:
        return E_SUCCESS  # the stream is Python's: it flushes when it wants
    return s.files.sync(entry, data_only)


def fd_datasync(s: State, fd: int) -> int:
    return _sync(s, fd, R_FD_DATASYNC, True)


def fd_sync(s: State, fd: int) -> int:
    return _sync(s, fd, R_FD_SYNC, False)


def fd_filestat_set_size(s: State, fd: int, size: int) -> int:
    number, entry = s.entry_with(fd, R_FD_FILESTAT_SET_SIZE)
    return number if number or entry is None else s.files.set_size(entry, size)


def fd_allocate(s: State, fd: int, offset: int, length: int) -> int:
    """Makes the file at least offset + length long (the file system may not reserve the room, but it is as long)."""
    number, entry = s.entry_with(fd, R_FD_ALLOCATE)
    if number or entry is None:
        return number
    if entry.kind == K_DIR:
        return E_ISDIR
    if entry.kind != K_FILE:
        return E_BADF
    if offset > 2**63 - 1 or length > 2**63 - 1 - offset:
        return E_FBIG
    return s.files.allocate(entry, offset, length)


def _times_flags_ok(flags: int) -> int:
    bad = flags > 15 or (flags & 1 and flags & 2) or (flags & 4 and flags & 8)
    return E_INVAL if bad else E_SUCCESS


def fd_filestat_set_times(s: State, fd: int, atim: int, mtim: int, flags: int) -> int:
    number, entry = s.entry_with(fd, R_FD_FILESTAT_SET_TIMES)
    if number or entry is None:
        return number
    if entry.kind == K_STD:
        return E_BADF
    number = _times_flags_ok(flags)
    return number or s.files.times_fd(entry, atim, mtim, flags)


def path_filestat_set_times(
    s: State, fd: int, lookup: int, path: int, length: int, atim: int, mtim: int, flags: int
) -> int:
    number, directory = s.dir_with(fd, R_PATH_FILESTAT_SET_TIMES)
    if number or directory is None:
        return number
    number = _times_flags_ok(flags)
    return number or s.files.times_path(
        directory, s.path_at(path, length), bool(lookup & LOOKUP_FOLLOW), atim, mtim, flags
    )


def fd_fdstat_set_flags(s: State, fd: int, flags: int) -> int:
    number, entry = s.entry_with(fd, R_FD_FDSTAT_SET_FLAGS)
    if number or entry is None:
        return number
    if flags > 31:
        return E_INVAL
    if entry.kind != K_FILE:
        return E_SUCCESS if entry.kind == K_STD else E_BADF
    number = s.files.set_flags(entry, flags)
    if number:
        return number
    entry.flags = flags
    return E_SUCCESS


def fd_fdstat_set_rights(s: State, fd: int, base: int, inheriting: int) -> int:
    """The rights can only be given up, never gained."""
    entry = s.entry_of(fd)
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


def path_create_directory(s: State, fd: int, path: int, length: int) -> int:
    number, directory = s.dir_with(fd, R_PATH_CREATE_DIRECTORY)
    return number if number or directory is None else s.files.mkdir(directory, s.path_at(path, length))


def path_remove_directory(s: State, fd: int, path: int, length: int) -> int:
    number, directory = s.dir_with(fd, R_PATH_REMOVE_DIRECTORY)
    return number if number or directory is None else s.files.remove(directory, s.path_at(path, length), True)


def path_unlink_file(s: State, fd: int, path: int, length: int) -> int:
    number, directory = s.dir_with(fd, R_PATH_UNLINK_FILE)
    return number if number or directory is None else s.files.remove(directory, s.path_at(path, length), False)


# ---- positions, renaming, links -----------------------------------------------------------------------------------------------


def _at(s: State, fd: int, iovs: int, count: int, offset: int, out: int, writing: bool) -> int:
    """Read or write at a position, leaving the position of the descriptor where it was."""
    entry = s.entry_of(fd)
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
    bad = s.iovs_valid(iovs, count)
    if bad:
        return bad
    total = 0
    for i in range(count):
        base, length = s.iov(iovs, i)
        if not s.in_memory(base, length):
            return E_FAULT
        if length == 0:
            continue
        if writing:
            number, n = s.files.write_at(entry, s.read_bytes(base, length), offset + total)
        else:
            number, data = s.files.read_at(entry, length, offset + total)
            n = len(data)
            if not number:
                s.put_bytes(base, data)
        if number:
            if total:
                break
            return number
        total += n
        if n < length:
            break
    return E_SUCCESS if s.put_u32(out, total) else E_FAULT


def fd_pread(s: State, fd: int, iovs: int, count: int, offset: int, nread: int) -> int:
    return _at(s, fd, iovs, count, offset, nread, False)


def fd_pwrite(s: State, fd: int, iovs: int, count: int, offset: int, nwritten: int) -> int:
    return _at(s, fd, iovs, count, offset, nwritten, True)


def path_rename(s: State, fd: int, old: int, old_len: int, new_fd: int, new: int, new_len: int) -> int:
    number, source = s.dir_with(fd, R_PATH_RENAME_SOURCE)
    if number or source is None:
        return number
    number, target = s.dir_with(new_fd, R_PATH_RENAME_TARGET)
    if number or target is None:
        return number
    return s.files.rename(source, s.path_at(old, old_len), target, s.path_at(new, new_len))


def path_link(s: State, fd: int, flags: int, old: int, old_len: int, new_fd: int, new: int, new_len: int) -> int:
    number, source = s.dir_with(fd, R_PATH_LINK_SOURCE)
    if number or source is None:
        return number
    number, target = s.dir_with(new_fd, R_PATH_LINK_TARGET)
    if number or target is None:
        return number
    return s.files.link(source, s.path_at(old, old_len), bool(flags & LOOKUP_FOLLOW), target, s.path_at(new, new_len))


def path_symlink(s: State, target: int, target_len: int, fd: int, path: int, length: int) -> int:
    """A link that leads out cannot be followed, and one that is absolute could not be anywhere else than outside: neither is made."""
    number, directory = s.dir_with(fd, R_PATH_SYMLINK)
    if number or directory is None:
        return number
    if not s.in_memory(target, target_len):
        return E_FAULT
    if target_len == 0:
        return E_NOENT
    text = s.read_bytes(target, target_len)
    if b"\0" in text:
        return E_INVAL
    if text[:1] == b"/":
        return E_NOTCAPABLE
    return s.files.symlink(text, directory, s.path_at(path, length))


LINK_MAX = 4096  # the most that path_readlink reads of a link


def path_readlink(s: State, fd: int, path: int, length: int, buf: int, buf_len: int, used: int) -> int:
    number, directory = s.dir_with(fd, R_PATH_READLINK)
    if number or directory is None:
        return number
    if not s.in_memory(buf, buf_len):
        return E_FAULT
    number, text = s.files.readlink(directory, s.path_at(path, length), min(buf_len, LINK_MAX))
    if number:
        return number
    s.put_bytes(buf, text)
    return E_SUCCESS if s.put_u32(used, len(text)) else E_FAULT
