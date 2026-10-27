"""The calls of `wasi_snapshot_preview1`, one function for each (the twin of the `fusedwasi_p1_<name>` of `include/fusedwasi/p1/wasi_p1_core.h`).

Every function takes the `Guest` first (the same class as for WASI 0.2), then the parameters of the call as the core module passes them (integers: a pointer is an offset in the memory 0 of the
guest), and returns the errno of WASI; what the call gives back is written into the memory. Integers are unsigned, parameters and results (see `fusedwasi.p2.wasi_p2_core`: an engine that
passes signed values masks them first). A trap is an exception. The calls are implemented over WASI 0.2: the files and directories are `descriptor` resources in the table of the state
(`State.filesystem` does the work), the standard streams, the clocks and the random bytes are the state's; what 0.1 keeps itself is in `wasi_p1_core` (rights, positions, the names of preopens).
"""

from __future__ import annotations

import os
import select
import stat
import struct
import sys

from ..p2.wasi_p2_core import KIND_DESCRIPTOR, Exit, Guest, State, table_add
from ..p2.wasi_p2_filesystem import (
    CREATE,
    DATA_SYNC,
    DIRECTORY,
    EXCLUSIVE,
    FILE_SYNC,
    READ,
    SYMLINK_FOLLOW,
    TRUNCATE,
    WRITE,
    Descriptor,
    FsError,
    Times,
)
from .wasi_p1_core import (
    _K_DIR,
    _K_FILE,
    _K_STD,
    E_BADF,
    E_FAULT,
    E_FBIG,
    E_INVAL,
    E_IO,
    E_ISDIR,
    E_NAMETOOLONG,
    E_NOENT,
    E_NOMEM,
    E_NOTCAPABLE,
    E_NOTDIR,
    E_NOTSOCK,
    E_NOTSUP,
    E_SPIPE,
    E_SUCCESS,
    FD_APPEND,
    FD_DSYNC,
    FD_NONBLOCK,
    FD_RSYNC,
    FD_SYNC,
    FT_CHAR,
    FT_DIR,
    FT_REGULAR,
    LOOKUP_FOLLOW,
    O_CREAT,
    O_DIRECTORY,
    O_EXCL,
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
    R_OF_DIRECTORY,
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
    _descriptor_of,
    _Entry,
    _errno_of_error,
    _Fds,
    _fds_of,
    _filetype_of,
    _in_memory,
    _iov,
    _iovs_valid,
    _Listing,
    _load,
    _path_at,
    _put_bytes,
    _put_u8,
    _put_u16,
    _put_u32,
    _put_u64,
    _read_bytes,
    _read_std,
    _release,
    _Stat,
    _std_filetype,
    _std_is_real,
    _store,
    _write_std,
    errno_of,
)

_INT64_MAX = (1 << 63) - 1


def _signed(value: int) -> int:
    """A 64-bit parameter of the call as the signed number the system takes."""
    return value - (1 << 64) if value >= 1 << 63 else value


# ---- arguments and environment (wasi:cli/environment: the fields of the state) ----------------------------------------------------


def _sizes(guest: Guest, count: int, size: int, count_ptr: int, size_ptr: int) -> int:
    if not (_put_u32(guest, count_ptr, count) and _put_u32(guest, size_ptr, size)):
        return E_FAULT
    return E_SUCCESS


def _strings(guest: Guest, items: list[bytes], table: int, out: int) -> int:
    buf = b"".join(item + b"\0" for item in items)
    if not (_in_memory(guest, out, len(buf)) and _in_memory(guest, table, len(items) * 4)):
        return E_FAULT
    _put_bytes(guest, out, buf)
    offset = 0
    for i, item in enumerate(items):
        _put_u32(guest, table + 4 * i, out + offset)
        offset += len(item) + 1
    return E_SUCCESS


def _args(guest: Guest) -> list[bytes]:
    return [os.fsencode(a) for a in guest.state.args]


def _env(guest: Guest) -> list[bytes]:
    return [os.fsencode(f"{name}={value}") for name, value in guest.state.env]


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


# ---- clocks and random bytes (the clocks, the random source and the sleep of the state) ----------------------------------------------


def clock_res_get(guest: Guest, ident: int, ptr: int) -> int:
    if ident > 3:
        return E_INVAL
    return E_SUCCESS if _put_u64(guest, ptr, guest.state.resolution_ns[ident]) else E_FAULT


def _clock_ns(state: State, ident: int) -> tuple[int, int]:
    """(errno, nanoseconds) of the clock `ident` of WASI (0 real time, 1 monotonic, 2 of the process, 3 of the thread): the clock of the state; one that cannot be read is NOTSUP (0.2 traps)."""
    if ident > 3:
        return E_INVAL, 0
    try:
        return E_SUCCESS, int(state.clock(ident))
    except OSError:
        return E_NOTSUP, 0


def clock_time_get(guest: Guest, ident: int, precision: int, ptr: int) -> int:
    status, ns = _clock_ns(guest.state, ident)
    if status:
        return status
    return E_SUCCESS if _put_u64(guest, ptr, ns) else E_FAULT


def random_get(guest: Guest, buf: int, size: int) -> int:
    if not _in_memory(guest, buf, size):
        return E_FAULT
    if size == 0:
        return E_SUCCESS
    try:
        data = guest.state.random_bytes(size)
    except OSError:
        return E_IO
    if len(data) != size:
        return E_IO
    _put_bytes(guest, buf, data)
    return E_SUCCESS


def sched_yield(guest: Guest) -> int:
    return E_SUCCESS


def proc_exit(guest: Guest, code: int) -> None:
    """`wasi:cli/exit`: the hook `exit` of the guest is told; the default records `exited` and `exit_code` and raises `Exit`."""
    if guest.exit is not None:
        guest.exit(code)
        return
    guest.exited, guest.exit_code = True, code
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


def _desc(guest: Guest, entry: _Entry) -> Descriptor | None:
    """The resource of 0.2 behind a file or a directory."""
    return _descriptor_of(guest.state, entry)  # type: ignore[no-any-return]


def _unseekable(failure: FsError) -> bool:
    """A file that cannot be read or written at an offset (a pipe, a FIFO, a terminal opened by a path) is read and written at the position of the system, as before."""
    return failure.name == "invalid-seek"


def fd_write(guest: Guest, fd: int, iovs: int, count: int, nwritten: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == _K_DIR:
        return E_ISDIR
    if entry.kind == _K_STD:
        return E_BADF if entry.handle == 0 else _write_std(guest, entry.handle, iovs, count, nwritten)
    if not entry.rights & R_FD_WRITE:
        return E_NOTCAPABLE
    bad = _iovs_valid(guest, iovs, count)
    if bad:
        return bad
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    total = 0
    for i in range(count):
        base, length = _iov(guest, iovs, i)
        if not _in_memory(guest, base, length):
            return E_FAULT
        if length == 0:
            continue
        at = entry.position
        if entry.flags & FD_APPEND:  # the write goes to the end of the file
            try:
                at = guest.state.filesystem.stat(descriptor).st_size
            except FsError:
                pass
        data = _read_bytes(guest, base, length)  # straight from the memory of the module
        try:
            done = guest.state.filesystem.write(descriptor, data, at, p1=True)
        except FsError as failure:
            if _unseekable(failure):
                try:
                    n = os.write(descriptor.fd, data)
                except OSError as caught:
                    if total:
                        break
                    return errno_of(caught.errno)
                total += n
                if n < length:
                    break
                continue
            entry.position = at + failure.done
            total += failure.done
            if total:
                break
            return _errno_of_error(failure)
        entry.position = at + done
        total += done
    return E_SUCCESS if _put_u32(guest, nwritten, total) else E_FAULT


def fd_read(guest: Guest, fd: int, iovs: int, count: int, nread: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == _K_STD:
        return _read_std(guest, iovs, count, nread) if entry.handle == 0 else E_BADF
    if entry.kind == _K_DIR:
        return E_ISDIR
    if not entry.rights & R_FD_READ:
        return E_NOTCAPABLE
    bad = _iovs_valid(guest, iovs, count)
    if bad:
        return bad
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    total = 0
    for i in range(count):
        base, length = _iov(guest, iovs, i)
        if not _in_memory(guest, base, length):
            return E_FAULT
        if length == 0:
            continue
        try:
            data = guest.state.filesystem.read(descriptor, length, entry.position, p1=True)
            entry.position += len(data)
        except FsError as failure:
            if not _unseekable(failure):
                if total:
                    break  # what was read is reported; the error comes with the next call
                return _errno_of_error(failure)
            try:
                data = os.read(descriptor.fd, length)
            except OSError as caught:
                if total:
                    break
                return errno_of(caught.errno)
        _put_bytes(guest, base, data)
        total += len(data)
        if len(data) < length:
            break
        # A pipe, a FIFO, a terminal: when the first buffer is full, a read into the next one may wait for more although something has been read, and a short read is allowed.
        # Only a regular file goes on.
        if data and i + 1 < count and entry.filetype != FT_REGULAR:
            break
    return E_SUCCESS if _put_u32(guest, nread, total) else E_FAULT


def _seek_right(offset: int, whence: int) -> int:
    """What fd_seek asks of the descriptor: its right, or (a seek by 0 bytes from the current position is a tell) the right to tell."""
    return R_FD_SEEK | (R_FD_TELL if offset == 0 and whence == 1 else 0)


def _seek_native(guest: Guest, fd: int, offset: int, whence: int, ptr: int, right: int) -> int:
    """The position is the adapter's. A file that is not a regular one (a pipe, a device) is asked of the system, which says what it says."""
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == _K_STD:
        return E_SPIPE
    if entry.kind != _K_FILE:
        return E_BADF
    if not entry.rights & right:
        return E_NOTCAPABLE
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    if entry.filetype != FT_REGULAR:
        try:
            position = os.lseek(descriptor.fd, offset, (os.SEEK_SET, os.SEEK_CUR, os.SEEK_END)[whence])
        except OSError as caught:
            return errno_of(caught.errno)
    else:
        origin = 0
        if whence == 1:
            origin = entry.position
        elif whence == 2:
            try:
                origin = guest.state.filesystem.stat(descriptor).st_size
            except FsError as failure:
                return _errno_of_error(failure)
        position = origin + offset
        if position < 0 or position > _INT64_MAX:
            return E_INVAL
        entry.position = position
    return E_SUCCESS if _put_u64(guest, ptr, position) else E_FAULT


def fd_seek(guest: Guest, fd: int, offset: int, whence: int, ptr: int) -> int:
    """The order of whence in this snapshot: set, cur, end."""
    if whence > 2:
        return E_INVAL
    return _seek_native(guest, fd, _signed(offset), whence, ptr, _seek_right(_signed(offset), whence))


def fd_tell(guest: Guest, fd: int, ptr: int) -> int:
    return _seek_native(guest, fd, 0, 1, ptr, R_FD_TELL)


def fd_close(guest: Guest, fd: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if (
        entry.kind == _K_STD
    ):  # the stream of the host stays open, but the program has let go of it: the number is free and a write to it is BADF
        entry.__init__()  # type: ignore[misc]
        return E_SUCCESS
    _release(guest.state, entry)  # the resource is dropped from the table of the state, and closed
    return E_SUCCESS


def fd_renumber(guest: Guest, source: int, target: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    a, b = fds.entry_of(source), fds.entry_of(target)
    if a is None or b is None:
        return E_BADF
    if source == target:
        return E_SUCCESS
    _release(guest.state, b)
    fds.entries[target] = a
    fds.entries[source] = _Entry()
    return E_SUCCESS


def fd_advise(guest: Guest, fd: int, offset: int, length: int, advice: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None or entry.kind != _K_FILE:
        return E_BADF
    if not entry.rights & R_FD_ADVISE:
        return E_NOTCAPABLE
    if advice > 5:  # the advice is one of six
        return E_INVAL
    return E_SUCCESS  # a hint (descriptor.advise of 0.2 does nothing either)


def fd_fdstat_get(guest: Guest, fd: int, ptr: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    filetype = _std_filetype(guest.state, entry) if entry.kind == _K_STD else entry.filetype
    base = (RIGHTS_STDIN if entry.handle == 0 else RIGHTS_STDOUT) if entry.kind == _K_STD else entry.rights
    if not _in_memory(guest, ptr, 24):
        return E_FAULT
    _put_bytes(guest, ptr, bytes(24))
    _put_u8(guest, ptr, filetype)
    _put_u16(guest, ptr + 2, entry.flags)
    _put_u64(guest, ptr + 8, base)
    _put_u64(guest, ptr + 16, entry.inheriting)
    return E_SUCCESS


def fd_prestat_get(guest: Guest, fd: int, ptr: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None or entry.preopen is None:
        return E_BADF  # the end of the list the program walks
    if not _in_memory(guest, ptr, 8):
        return E_FAULT
    _put_bytes(guest, ptr, bytes(8))  # the type of a preopen: a directory, 0
    _put_u32(guest, ptr + 4, len(entry.preopen))
    return E_SUCCESS


def fd_prestat_dir_name(guest: Guest, fd: int, path: int, size: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None or entry.preopen is None:
        return E_BADF
    if size < len(entry.preopen):
        return E_NAMETOOLONG
    if not _in_memory(guest, path, len(entry.preopen)):
        return E_FAULT
    _put_bytes(guest, path, entry.preopen)
    return E_SUCCESS


# ---- what a file or a directory is --------------------------------------------------------------------------------------------


def _ns(nanoseconds: int) -> int:
    """A time of a file in nanoseconds since 1970 as WASI has it (unsigned): before 1970 is 0, and past the year 2262 it saturates."""
    if nanoseconds < 0:
        return 0
    if nanoseconds // 1_000_000_000 > _INT64_MAX // 1_000_000_000 - 1:
        return _INT64_MAX
    return nanoseconds


def _stat_of(st: os.stat_result) -> _Stat:
    return _Stat(
        dev=st.st_dev & 0xFFFFFFFFFFFFFFFF,
        ino=st.st_ino & 0xFFFFFFFFFFFFFFFF,
        nlink=st.st_nlink,
        size=st.st_size,
        atim=_ns(st.st_atime_ns),
        mtim=_ns(st.st_mtime_ns),
        ctim=_ns(st.st_ctime_ns),
        filetype=_filetype_of(st.st_mode),
    )


def _put_filestat(guest: Guest, ptr: int, st: _Stat, legacy: bool) -> int:
    """The filestat of the first snapshot has a 32-bit link count and 56 bytes. It is written at once: for an engine that keeps its memory out of this
    process every write is a round trip."""
    total = 56 if legacy else 64
    if not _in_memory(guest, ptr, total):
        return E_FAULT
    if legacy:
        record = struct.pack("<QQB3xIQQQQ", st.dev, st.ino, st.filetype, st.nlink, st.size, st.atim, st.mtim, st.ctim)
    else:
        record = struct.pack("<QQB7xQQQQQ", st.dev, st.ino, st.filetype, st.nlink, st.size, st.atim, st.mtim, st.ctim)
    _put_bytes(guest, ptr, record)
    return E_SUCCESS


def _filestat_of_fd(guest: Guest, fd: int, ptr: int, legacy: bool) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == _K_STD:
        if _std_is_real(guest.state, entry):
            try:
                return _put_filestat(guest, ptr, _stat_of(os.fstat(entry.handle)), legacy)  # what it is
            except OSError:
                pass
        return _put_filestat(
            guest, ptr, _Stat(filetype=FT_CHAR, nlink=1), legacy
        )  # a stream of the host: nothing to say but that it is a character device
    if not entry.rights & R_FD_FILESTAT_GET:
        return E_NOTCAPABLE
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    try:
        st = guest.state.filesystem.stat(descriptor)
    except FsError as failure:
        return _errno_of_error(failure)
    return _put_filestat(guest, ptr, _stat_of(st), legacy)


def _filestat_of_path(guest: Guest, fd: int, flags: int, path: int, length: int, ptr: int, legacy: bool) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if not entry.rights & R_PATH_FILESTAT_GET:
        return E_NOTCAPABLE
    if entry.kind != _K_DIR:
        return E_NOTDIR
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    text = _path_at(guest, path, length)
    if text is None:
        return E_FAULT
    try:
        st = guest.state.filesystem.stat_at(descriptor, text, SYMLINK_FOLLOW if flags & LOOKUP_FOLLOW else 0, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return _put_filestat(guest, ptr, _stat_of(st), legacy)


def fd_filestat_get(guest: Guest, fd: int, ptr: int) -> int:
    return _filestat_of_fd(guest, fd, ptr, False)


def path_filestat_get(guest: Guest, fd: int, flags: int, path: int, length: int, ptr: int) -> int:
    return _filestat_of_path(guest, fd, flags, path, length, ptr, False)


def _dirent_order(item: _Listing) -> tuple[int, bytes]:
    return (0 if item.name == b"." else 1 if item.name == b".." else 2), item.name


def _list(guest: Guest, descriptor: Descriptor) -> tuple[int, list[_Listing]]:
    """The names of the directory (descriptor.read-directory), with `.` and `..`, the inode of each from a stat of the name."""
    fs = guest.state.filesystem
    try:
        entries = fs.read_directory(descriptor)
    except FsError as failure:
        return _errno_of_error(failure), []
    items: list[_Listing] = []
    for name in (b".", b".."):
        try:
            items.append(_Listing(name, fs.stat_entry(descriptor, name).st_ino, 3))
        except FsError:
            items.append(_Listing(name, 0, 3))
    for kind, name in entries:
        try:
            ino = fs.stat_entry(descriptor, name).st_ino
        except FsError:  # gone since the listing
            ino = 0
        items.append(_Listing(name, ino, _FILETYPE_OF_TYPE.get(kind, 0)))
    return E_SUCCESS, items


# descriptor-type of 0.2 as the filetype of WASI (unknown, block, character, directory, fifo, symbolic link, regular, socket)
_FILETYPE_OF_TYPE = {0: 0, 1: 1, 2: FT_CHAR, 3: FT_DIR, 4: 0, 5: 7, 6: FT_REGULAR, 7: 6}


def fd_readdir(guest: Guest, fd: int, buf: int, size: int, cookie: int, used: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind != _K_DIR:
        return E_NOTDIR
    if not entry.rights & R_FD_READDIR:
        return E_NOTCAPABLE
    if not _in_memory(guest, buf, size):
        return E_FAULT
    if cookie == 0 or entry.listing is None:  # the listing is made again when the program starts from the beginning
        descriptor = _desc(guest, entry)
        if descriptor is None:
            return E_BADF
        number, names = _list(guest, descriptor)
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
        _put_bytes(guest, buf + at, head[:part])
        at += part
        if part < len(head):
            break
        part = min(size - at, len(item.name))
        _put_bytes(guest, buf + at, item.name[:part])
        at += part
        index += 1
    return E_SUCCESS if _put_u32(guest, used, at) else E_FAULT


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
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    directory = fds.entry_of(fd)
    if directory is None:
        return E_BADF
    if directory.kind != _K_DIR:
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
    parent = _desc(guest, directory)
    if parent is None:
        return E_BADF
    text = _path_at(guest, path, length)
    if text is None:
        return E_FAULT
    # writing is what the rights say: to write, to sync the data, to allocate, to set the size (uvwasi; wasi-libc leaves these out of a read-only open)
    writable = bool(base & (R_FD_WRITE | R_FD_DATASYNC | R_FD_ALLOCATE | R_FD_FILESTAT_SET_SIZE)) or bool(
        oflags & O_TRUNC
    )
    readable = bool(base & R_FD_READ)
    flags = (WRITE if writable else 0) | (READ if readable or not writable else 0)
    if fdflags & (FD_RSYNC | FD_SYNC):
        flags |= FILE_SYNC
    if fdflags & FD_DSYNC:
        flags |= DATA_SYNC
    open_flags = (
        (CREATE if oflags & O_CREAT else 0)
        | (DIRECTORY if oflags & O_DIRECTORY else 0)
        | (EXCLUSIVE if oflags & O_EXCL else 0)
        | (TRUNCATE if oflags & O_TRUNC else 0)
    )
    follow = SYMLINK_FOLLOW if dirflags & LOOKUP_FOLLOW else 0
    fs = guest.state.filesystem
    try:
        try:
            made = fs.open_at(parent, text, follow, open_flags, flags, p1=True)
        except FsError as failure:
            if failure.name == "is-directory" and base & R_OF_DIRECTORY and not oflags & (O_CREAT | O_TRUNC):
                made = fs.open_at(
                    parent, text, follow, DIRECTORY, READ, p1=True
                )  # it was a directory, and asked for as one
            else:
                raise
        try:
            st = fs.stat(made)
        except FsError:
            fs.close(made)
            raise
    except FsError as failure:
        return _errno_of_error(failure)
    if (
        sys.platform != "win32" and fdflags & FD_NONBLOCK and made.fd >= 0
    ):  # what the open of 0.2 does not take: not blocking (append is the adapter's own)
        import fcntl

        try:
            fcntl.fcntl(made.fd, fcntl.F_SETFL, fcntl.fcntl(made.fd, fcntl.F_GETFL) | _native_fdflags(FD_NONBLOCK))
        except OSError as caught:
            fs.close(made)
            return errno_of(caught.errno)
    handle = table_add(guest.state, KIND_DESCRIPTOR, made)
    if not handle:
        fs.close(made)
        return E_NOMEM
    number = fds.new_fd()
    entry = fds.entries[number]
    entry.kind = _K_DIR if made.is_dir else _K_FILE
    entry.filetype = _filetype_of(st.st_mode)
    entry.handle = handle
    entry.rights = base
    if entry.kind == _K_DIR:
        entry.rights &= ~(R_FD_SEEK | R_FD_TELL)  # a directory is not seeked
    entry.inheriting = inheriting
    entry.flags = fdflags
    if not _put_u32(guest, out, number):
        _release(guest.state, entry)
        return E_FAULT
    return E_SUCCESS


def _native_fdflags(fdflags: int) -> int:
    """The fdflags as flags of the system (the ones it has); append is the adapter's own."""
    flags = 0
    for bit, name in ((2, "O_DSYNC"), (8, "O_RSYNC"), (16, "O_SYNC"), (4, "O_NONBLOCK")):
        if fdflags & bit:
            flags |= getattr(os, name, 0)
    return flags


# ---- writing, creating, removing ----------------------------------------------------------------------------------------------


def _sync(guest: Guest, fd: int, right: int, data_only: bool) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, entry = fds.entry_with(fd, right)
    if number or entry is None:
        return number
    if entry.kind == _K_STD:
        return E_SUCCESS  # the stream is the host's: it flushes when it wants
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    try:
        guest.state.filesystem.sync(descriptor, data_only)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


def fd_datasync(guest: Guest, fd: int) -> int:
    return _sync(guest, fd, R_FD_DATASYNC, True)


def fd_sync(guest: Guest, fd: int) -> int:
    return _sync(guest, fd, R_FD_SYNC, False)


def _set_size(guest: Guest, entry: _Entry, size: int) -> int:
    if entry.kind == _K_DIR:
        return E_ISDIR
    if entry.kind != _K_FILE:
        return E_BADF
    if size > _INT64_MAX:
        return E_FBIG
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    try:
        guest.state.filesystem.set_size(descriptor, size, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


def fd_filestat_set_size(guest: Guest, fd: int, size: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, entry = fds.entry_with(fd, R_FD_FILESTAT_SET_SIZE)
    return number if number or entry is None else _set_size(guest, entry, size)


def fd_allocate(guest: Guest, fd: int, offset: int, length: int) -> int:
    """Makes the file at least offset + length long (the file system may not reserve the room, but it is as long)."""
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, entry = fds.entry_with(fd, R_FD_ALLOCATE)
    if number or entry is None:
        return number
    if entry.kind == _K_DIR:
        return E_ISDIR
    if entry.kind != _K_FILE:
        return E_BADF
    if offset > _INT64_MAX or length > _INT64_MAX - offset:
        return E_FBIG
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    if sys.platform != "win32" and sys.platform != "darwin" and hasattr(os, "posix_fallocate"):
        import errno as _errno

        try:  # no operation of 0.2 reserves room: it is reserved here (and can run out: NOSPC), and a descriptor that is not a file says so (SPIPE)
            os.posix_fallocate(descriptor.fd, offset, length)
            return E_SUCCESS
        except OSError as caught:
            if caught.errno not in (_errno.EINVAL, _errno.EOPNOTSUPP, _errno.ENODEV):
                return errno_of(caught.errno)  # a file system that cannot do it gets the old way
    try:
        st = guest.state.filesystem.stat(descriptor)
    except FsError as failure:
        return _errno_of_error(failure)
    if sys.platform != "win32" and not stat.S_ISREG(st.st_mode):
        return E_SPIPE  # a pipe: macOS would truncate it without a word
    return E_SUCCESS if st.st_size >= offset + length else _set_size(guest, entry, offset + length)


def _times_flags_ok(flags: int) -> int:
    bad = flags > 15 or (flags & 1 and flags & 2) or (flags & 4 and flags & 8)
    return E_INVAL if bad else E_SUCCESS


def _times(atim: int, mtim: int, flags: int) -> Times:
    """The flags of 0.1 (a value, now, none) as the new-timestamps of 0.2."""
    return (
        2 if flags & 1 else 1 if flags & 2 else 0,
        _signed(atim),
        2 if flags & 4 else 1 if flags & 8 else 0,
        _signed(mtim),
    )


def fd_filestat_set_times(guest: Guest, fd: int, atim: int, mtim: int, flags: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, entry = fds.entry_with(fd, R_FD_FILESTAT_SET_TIMES)
    if number or entry is None:
        return number
    if entry.kind == _K_STD:
        return E_BADF
    number = _times_flags_ok(flags)
    if number:
        return number
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    try:
        guest.state.filesystem.set_times(descriptor, _times(atim, mtim, flags), lambda: guest.state.clock(0), p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


def path_filestat_set_times(
    guest: Guest, fd: int, lookup: int, path: int, length: int, atim: int, mtim: int, flags: int
) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, directory = fds.dir_with(fd, R_PATH_FILESTAT_SET_TIMES)
    if number or directory is None:
        return number
    number = _times_flags_ok(flags)
    if number:
        return number
    descriptor = _desc(guest, directory)
    if descriptor is None:
        return E_BADF
    text = _path_at(guest, path, length)
    if text is None:
        return E_FAULT
    try:
        guest.state.filesystem.set_times_at(
            descriptor,
            text,
            SYMLINK_FOLLOW if lookup & LOOKUP_FOLLOW else 0,
            _times(atim, mtim, flags),
            lambda: guest.state.clock(0),
            p1=True,
        )
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


def fd_fdstat_set_flags(guest: Guest, fd: int, flags: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, entry = fds.entry_with(fd, R_FD_FDSTAT_SET_FLAGS)
    if number or entry is None:
        return number
    if flags > 31:
        return E_INVAL
    if entry.kind != _K_FILE:
        return E_SUCCESS if entry.kind == _K_STD else E_BADF
    if (
        sys.platform != "win32"
    ):  # append is kept here and done by fd_write; no operation of 0.2 sets the others after the open, so they are set on the system descriptor
        import fcntl

        descriptor = _desc(guest, entry)
        if descriptor is None:
            return E_BADF
        try:
            current = fcntl.fcntl(descriptor.fd, fcntl.F_GETFL)
            every = _native_fdflags(FD_DSYNC | FD_NONBLOCK | FD_RSYNC | FD_SYNC)
            fcntl.fcntl(descriptor.fd, fcntl.F_SETFL, (current & ~every) | _native_fdflags(flags))
        except OSError as caught:
            return errno_of(caught.errno)
    entry.flags = flags
    return E_SUCCESS


def fd_fdstat_set_rights(guest: Guest, fd: int, base: int, inheriting: int) -> int:
    """The rights can only be given up, never gained."""
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if (
        entry.kind == _K_STD
    ):  # the rights of a standard stream are fixed: giving some up (or none) is fine, getting more is not (the narrowing is not kept)
        have = RIGHTS_STDIN if entry.handle == 0 else RIGHTS_STDOUT
        return E_NOTCAPABLE if base & ~have or inheriting else E_SUCCESS
    if base & ~entry.rights or inheriting & ~entry.inheriting:
        return E_NOTCAPABLE
    entry.rights = base
    entry.inheriting = inheriting
    return E_SUCCESS


def _dir_and_path(guest: Guest, fd: int, right: int, path: int, length: int) -> tuple[int, Descriptor | None, bytes]:
    """(errno, the directory's resource of 0.2, the path): a directory with the right, and a path that is in the memory."""
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds, None, b""
    number, directory = fds.dir_with(fd, right)
    if number or directory is None:
        return number, None, b""
    descriptor = _desc(guest, directory)
    if descriptor is None:
        return E_BADF, None, b""
    text = _path_at(guest, path, length)
    if text is None:
        return E_FAULT, None, b""
    return E_SUCCESS, descriptor, text


def path_create_directory(guest: Guest, fd: int, path: int, length: int) -> int:
    number, descriptor, text = _dir_and_path(guest, fd, R_PATH_CREATE_DIRECTORY, path, length)
    if number or descriptor is None:
        return number
    try:
        guest.state.filesystem.create_directory_at(descriptor, text, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


def path_remove_directory(guest: Guest, fd: int, path: int, length: int) -> int:
    number, descriptor, text = _dir_and_path(guest, fd, R_PATH_REMOVE_DIRECTORY, path, length)
    if number or descriptor is None:
        return number
    try:
        guest.state.filesystem.remove_directory_at(descriptor, text, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


def path_unlink_file(guest: Guest, fd: int, path: int, length: int) -> int:
    number, descriptor, text = _dir_and_path(guest, fd, R_PATH_UNLINK_FILE, path, length)
    if number or descriptor is None:
        return number
    try:
        guest.state.filesystem.unlink_file_at(descriptor, text, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


# ---- positions, renaming, links -----------------------------------------------------------------------------------------------


def _at(guest: Guest, fd: int, iovs: int, count: int, offset: int, out: int, writing: bool) -> int:
    """Read or write at a position, leaving the position of the descriptor where it was."""
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    entry = fds.entry_of(fd)
    if entry is None:
        return E_BADF
    if entry.kind == _K_STD:
        return E_SPIPE
    if entry.kind == _K_DIR:
        return E_ISDIR
    # the specification asks for fd_seek as well; wasi-libc opens a file for writing without it and calls pwrite all the same
    if not entry.rights & (R_FD_WRITE if writing else R_FD_READ):
        return E_NOTCAPABLE
    if offset > _INT64_MAX:
        return E_INVAL
    bad = _iovs_valid(guest, iovs, count)
    if bad:
        return bad
    descriptor = _desc(guest, entry)
    if descriptor is None:
        return E_BADF
    fs = guest.state.filesystem
    total = 0
    if writing and entry.flags & FD_APPEND and sys.platform != "win32":
        # a descriptor opened for appending: the system writes at the end whatever the offset (as the descriptor did before 0.1 went over 0.2)
        try:
            offset = fs.stat(descriptor).st_size
        except FsError:
            pass
    for i in range(count):
        base, length = _iov(guest, iovs, i)
        if not _in_memory(guest, base, length):
            return E_FAULT
        if length == 0:
            continue
        try:
            if writing:
                n = fs.write(descriptor, _read_bytes(guest, base, length), offset + total, p1=True)
            else:
                data = fs.read(descriptor, length, offset + total, p1=True)
                n = len(data)
                _put_bytes(guest, base, data)
        except FsError as failure:
            total += failure.done
            if total:
                break
            return _errno_of_error(failure)
        total += n
        if n < length:
            break
    return E_SUCCESS if _put_u32(guest, out, total) else E_FAULT


def fd_pread(guest: Guest, fd: int, iovs: int, count: int, offset: int, nread: int) -> int:
    return _at(guest, fd, iovs, count, offset, nread, False)


def fd_pwrite(guest: Guest, fd: int, iovs: int, count: int, offset: int, nwritten: int) -> int:
    return _at(guest, fd, iovs, count, offset, nwritten, True)


def path_rename(guest: Guest, fd: int, old: int, old_len: int, new_fd: int, new: int, new_len: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, source = fds.dir_with(fd, R_PATH_RENAME_SOURCE)
    if number or source is None:
        return number
    number, target = fds.dir_with(new_fd, R_PATH_RENAME_TARGET)
    if number or target is None:
        return number
    a, b = _desc(guest, source), _desc(guest, target)
    if a is None or b is None:
        return E_BADF
    first, second = _path_at(guest, old, old_len), _path_at(guest, new, new_len)
    if first is None or second is None:
        return E_FAULT
    try:
        guest.state.filesystem.rename_at(a, first, b, second, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


def path_link(guest: Guest, fd: int, flags: int, old: int, old_len: int, new_fd: int, new: int, new_len: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, source = fds.dir_with(fd, R_PATH_LINK_SOURCE)
    if number or source is None:
        return number
    number, target = fds.dir_with(new_fd, R_PATH_LINK_TARGET)
    if number or target is None:
        return number
    a, b = _desc(guest, source), _desc(guest, target)
    if a is None or b is None:
        return E_BADF
    first, second = _path_at(guest, old, old_len), _path_at(guest, new, new_len)
    if first is None or second is None:
        return E_FAULT
    try:
        guest.state.filesystem.link_at(a, SYMLINK_FOLLOW if flags & LOOKUP_FOLLOW else 0, first, b, second, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


_PATH_MAX = 4096


def path_symlink(guest: Guest, target: int, target_len: int, fd: int, path: int, length: int) -> int:
    """A link that leads out cannot be followed, and one that is absolute could not be anywhere else than outside: neither is made."""
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, directory = fds.dir_with(fd, R_PATH_SYMLINK)
    if number or directory is None:
        return number
    if not _in_memory(guest, target, target_len):
        return E_FAULT
    if target_len == 0:
        return E_NOENT
    text = _read_bytes(guest, target, target_len)
    if b"\0" in text:
        return E_INVAL
    if text[:1] == b"/":
        return E_NOTCAPABLE
    if len(text) >= _PATH_MAX:
        return E_NAMETOOLONG
    descriptor = _desc(guest, directory)
    if descriptor is None:
        return E_BADF
    where = _path_at(guest, path, length)
    if where is None:
        return E_FAULT
    try:
        guest.state.filesystem.symlink_at(descriptor, text, where, p1=True)
    except FsError as failure:
        return _errno_of_error(failure)
    return E_SUCCESS


_LINK_MAX = 4096  # the most that path_readlink reads of a link


def path_readlink(guest: Guest, fd: int, path: int, length: int, buf: int, buf_len: int, used: int) -> int:
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    number, directory = fds.dir_with(fd, R_PATH_READLINK)
    if number or directory is None:
        return number
    if not _in_memory(guest, buf, buf_len):
        return E_FAULT
    descriptor = _desc(guest, directory)
    if descriptor is None:
        return E_BADF
    where = _path_at(guest, path, length)
    if where is None:
        return E_FAULT
    try:
        target = guest.state.filesystem.readlink_at(
            descriptor, where, p1=True
        )  # (a rooted target is not-permitted: PERM)
    except FsError as failure:
        return _errno_of_error(failure)
    shown = (
        target[: min(buf_len, _LINK_MAX)] if buf_len else b""
    )  # a buffer of 0 bytes still says whether this is a link
    _put_bytes(guest, buf, shown)
    return E_SUCCESS if _put_u32(guest, used, len(shown)) else E_FAULT


# ---- poll_oneoff and the sockets ----------------------------------------------------------------------------------------------

# A clock subscription is arithmetic on the clock of the state (and, when only clocks are waited for, the hook `sleep`). What a descriptor waits for is a pollable of 0.2: the readiness of the
# stream of a standard input that the state gave, or, for the standard input of the process and for a file that is a pipe, a FIFO or a terminal, the system's own poll with no wait; the
# program waits between two looks with the `wait` of the guest. A regular file, a directory and a standard output are always ready.


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
    raw = _load(guest, sub + 16 + shift, 28)  # the identifier at 0, the timeout at 8, the flags at 24
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
        status, value = _clock_ns(guest.state, ident)
        if status:
            return status, 0
        now[ident] = value
    return E_SUCCESS, max(0, timeout - now[ident])


class _Waiter:
    """What a subscription to a descriptor waits for: `native_fd` (the system's poll on it) or `stream` (the stream of the state: a pollable of 0.2 on it)."""

    __slots__ = ("events", "native_fd", "ready", "revents", "stream")

    def __init__(self) -> None:
        self.native_fd = -1
        self.stream: object = None
        self.events = 0
        self.revents = 0
        self.ready = False

    def ask(self) -> None:
        """Asks once, without waiting."""
        if self.native_fd >= 0:
            if sys.platform != "win32":
                poller = select.poll()
                poller.register(self.native_fd, self.events)
                found = poller.poll(0)
                self.revents = found[0][1] if found else 0
            self.ready = bool(self.revents & (_POLLIN | _POLLOUT | _POLLHUP | _POLLERR | _POLLNVAL))
        else:
            self.ready = bool(getattr(self.stream, "dead", False)) or bool(self.stream.ready())  # type: ignore[attr-defined]


def _poll_kind(guest: Guest, fds: _Fds, sub: int, tag: int) -> tuple[int, _Waiter | None]:
    """(errno of the event, what has to be waited for or None when the answer is at once)."""
    entry = fds.entry_of(struct.unpack("<I", _load(guest, sub + 16, 4))[0])
    if entry is None:
        return E_BADF, None
    need = R_POLL_FD_READWRITE | (R_FD_READ if tag == 1 else R_FD_WRITE)
    if entry.kind == _K_STD:  # the rights of the standard streams are fixed and include polling
        if tag == 1 and entry.handle == 0:
            waiter = _Waiter()
            stream = guest.state.std.get("stdin")
            if sys.platform != "win32" and _std_is_real(guest.state, entry):  # the standard input of the process
                waiter.native_fd, waiter.events = 0, _POLLIN
                return E_SUCCESS, waiter
            if stream is not None and hasattr(
                stream, "ready"
            ):  # a stream of the state that can say whether it is ready
                waiter.stream = stream
                return E_SUCCESS, waiter
        return E_SUCCESS, None
    if entry.rights & need != need:
        return E_NOTCAPABLE, None
    if entry.kind == _K_FILE and entry.filetype != FT_REGULAR and sys.platform != "win32":
        descriptor = _desc(guest, entry)
        if descriptor is not None and descriptor.fd >= 0:
            waiter = _Waiter()
            waiter.native_fd, waiter.events = descriptor.fd, _POLLIN if tag == 1 else _POLLOUT
            return E_SUCCESS, waiter
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
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    stride = 56 if legacy else 48
    if count == 0:
        return E_INVAL
    if not _in_memory(guest, in_, count * stride) or not _in_memory(guest, out, count * 32):
        return E_FAULT
    subs = _load(guest, in_, count * stride)  # the subscriptions, read once
    now: dict[int, int] = {}
    shortest: int | None = None  # a clock was subscribed
    immediate = False
    watched: dict[int, _Waiter] = {}  # subscription -> what it waits for
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
            _, waiter = _poll_kind(guest, fds, sub, tag)
            if waiter is None:
                immediate = True
            else:
                watched[i] = waiter
        else:
            return E_INVAL
    waited = 0
    if watched:
        started = guest.state.clock(1)
        ready = 0
        while True:  # ask the pollables; when none is ready, wait until one is or the clocks are due
            ready = 0
            for waiter in watched.values():
                waiter.ask()
                ready += waiter.ready
            if ready or immediate:
                break
            if shortest is not None and guest.state.clock(1) - started >= shortest:
                break
            guest.wait()
        if not ready and not immediate and shortest is not None:
            waited = shortest  # the wait is over: the clocks are due
        elif ready:
            waited = guest.state.clock(1) - started
    elif not immediate and shortest:
        guest.state.sleep(shortest)
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
            entry = fds.entry_of(number)
            if i in watched:
                waiter = watched[i]
                if not waiter.ready:
                    continue  # not ready
                if waiter.native_fd >= 0:
                    revents = waiter.revents
                    if revents & _POLLNVAL:
                        error = E_BADF
                    elif revents & (_POLLHUP | _POLLERR) and not revents & (_POLLIN | _POLLOUT):
                        flags = 1  # fd_readwrite_hangup
                    if tag == 1 and not revents & _POLLNVAL:
                        nbytes = _pending(waiter.native_fd)
            else:
                error, _ = _poll_kind(guest, fds, sub, tag)
                if error == E_SUCCESS and entry is not None and entry.kind == _K_FILE and tag == 1:
                    descriptor = _desc(guest, entry)
                    if descriptor is not None:
                        try:
                            size = guest.state.filesystem.stat(descriptor).st_size
                        except FsError:
                            size = 0
                        if size > entry.position:
                            nbytes = size - entry.position
        record = bytearray(32)
        record[0:8] = subs[i * stride : i * stride + 8]  # userdata
        struct.pack_into("<H", record, 8, error)
        record[10] = tag
        if tag != 0:
            struct.pack_into("<Q", record, 16, nbytes)
            struct.pack_into("<H", record, 24, flags)
        _store(guest, event, bytes(record))
        made += 1
    return E_SUCCESS if _put_u32(guest, nevents, made) else E_FAULT


def poll_oneoff(guest: Guest, in_: int, out: int, count: int, nevents: int) -> int:
    return _poll(guest, in_, out, count, nevents, False)


def _socket(guest: Guest, fd: int) -> int:
    """A descriptor is never a socket (a program cannot make one): the call says so, or that there is no such descriptor."""
    fds = _fds_of(guest)
    if isinstance(fds, int):
        return fds
    return E_BADF if fds.entry_of(fd) is None else E_NOTSOCK


def sock_accept(guest: Guest, fd: int, flags: int, result: int) -> int:
    return _socket(guest, fd)


def sock_recv(guest: Guest, fd: int, iovs: int, count: int, flags: int, nread: int, roflags: int) -> int:
    return _socket(guest, fd)


def sock_send(guest: Guest, fd: int, iovs: int, count: int, flags: int, nsent: int) -> int:
    return _socket(guest, fd)


def sock_shutdown(guest: Guest, fd: int, how: int) -> int:
    return _socket(guest, fd)
