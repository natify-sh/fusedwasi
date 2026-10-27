"""What the functions of WASI 0.1 share (the twin of `include/fusedwasi/p1/wasi_p1_core.h`): the constants of the specification, the descriptor map and the access to the memory of a guest.

There is one `State` and one `Guest` for both families (`fusedwasi.p2.wasi_p2_core`, re-exported by `fusedwasi.p1`). The configuration of 0.1 is the state's: arguments, environment, standard
streams, preopens, clocks, random bytes. A descriptor of 0.1 is a map to a resource of 0.2 plus what 0.1 keeps for each descriptor (rights, position, the name of a preopen): a file or a
directory is a `descriptor` in the table of the state, the same resource that a module of 0.2 sees in it, and the functions of `wasi_p1` work through the operations of 0.2
(`State.filesystem`, the standard streams of the state). The map (`state.p1`) is private; `open_descriptors` makes it now.

A function of 0.1 returns the errno of WASI (0 is success) and writes its results into the memory of the module (memory 0 of the guest); a trap is an exception (`trap(guest, message)`). The end of
the program is the guest's, as in 0.2: `proc_exit` is `wasi:cli/exit`. `proc_raise` asks the hook `signal` of the guest.
"""

from __future__ import annotations

import errno as _errno
import stat as _stat
import struct
from dataclasses import dataclass
from typing import Any

from ..p2.wasi_p2_cli import FdInput, FdOutput, std
from ..p2.wasi_p2_core import INDEX_BITS, INDEX_MASK, KIND_DESCRIPTOR, Guest, State, flat_view, table_add
from ..p2.wasi_p2_core import Exit as Exit  # noqa: PLC0414 (one class of exit for both families, re-exported)
from ..p2.wasi_p2_filesystem import MUTATE, READ, FsError
from ..p2.wasi_p2_io import StreamClosed, StreamFailed

# errno of WASI (not the C library's): the whole list of the specification
E_SUCCESS, E_2BIG, E_ACCES, E_ADDRINUSE, E_ADDRNOTAVAIL, E_AFNOSUPPORT, E_AGAIN, E_ALREADY, E_BADF, E_BADMSG = range(10)
E_BUSY, E_CANCELED, E_CHILD, E_CONNABORTED, E_CONNREFUSED, E_CONNRESET, E_DEADLK, E_DESTADDRREQ, E_DOM, E_DQUOT = range(
    10, 20
)
E_EXIST, E_FAULT, E_FBIG, E_HOSTUNREACH, E_IDRM, E_ILSEQ, E_INPROGRESS, E_INTR, E_INVAL, E_IO = range(20, 30)
E_ISCONN, E_ISDIR, E_LOOP, E_MFILE, E_MLINK, E_MSGSIZE, E_MULTIHOP, E_NAMETOOLONG, E_NETDOWN, E_NETRESET = range(30, 40)
E_NETUNREACH, E_NFILE, E_NOBUFS, E_NODEV, E_NOENT, E_NOEXEC, E_NOLCK, E_NOLINK, E_NOMEM, E_NOMSG = range(40, 50)
E_NOPROTOOPT, E_NOSPC, E_NOSYS, E_NOTCONN, E_NOTDIR, E_NOTEMPTY, E_NOTRECOVERABLE, E_NOTSOCK, E_NOTSUP, E_NOTTY = range(
    50, 60
)
E_NXIO, E_OVERFLOW, E_OWNERDEAD, E_PERM, E_PIPE, E_PROTO, E_PROTONOSUPPORT, E_PROTOTYPE, E_RANGE, E_ROFS = range(60, 70)
E_SPIPE, E_SRCH, E_STALE, E_TIMEDOUT, E_TXTBSY, E_XDEV, E_NOTCAPABLE = range(70, 77)

FT_UNKNOWN, FT_BLOCK, FT_CHAR, FT_DIR, FT_REGULAR, FT_SOCKET_DGRAM, FT_SOCKET_STREAM, FT_SYMLINK = range(8)

# the rights: one bit each, in the order of the specification
(
    R_FD_DATASYNC,
    R_FD_READ,
    R_FD_SEEK,
    R_FD_FDSTAT_SET_FLAGS,
    R_FD_SYNC,
    R_FD_TELL,
    R_FD_WRITE,
    R_FD_ADVISE,
    R_FD_ALLOCATE,
) = (1 << n for n in range(9))
R_PATH_CREATE_DIRECTORY, R_PATH_CREATE_FILE, R_PATH_LINK_SOURCE, R_PATH_LINK_TARGET, R_PATH_OPEN, R_FD_READDIR = (
    1 << n for n in range(9, 15)
)
(
    R_PATH_READLINK,
    R_PATH_RENAME_SOURCE,
    R_PATH_RENAME_TARGET,
    R_PATH_FILESTAT_GET,
    R_PATH_FILESTAT_SET_SIZE,
    R_PATH_FILESTAT_SET_TIMES,
) = (1 << n for n in range(15, 21))
(
    R_FD_FILESTAT_GET,
    R_FD_FILESTAT_SET_SIZE,
    R_FD_FILESTAT_SET_TIMES,
    R_PATH_SYMLINK,
    R_PATH_REMOVE_DIRECTORY,
    R_PATH_UNLINK_FILE,
    R_POLL_FD_READWRITE,
    R_SOCK_SHUTDOWN,
) = (1 << n for n in range(21, 29))
R_ALL = (1 << 30) - 1
# what changes something: taken away from a preopen that is read-only
R_MUTATING = sum(1 << n for n in (0, 4, 6, 8, 9, 10, 11, 12, 16, 17, 19, 20, 22, 23, 24, 25, 26))
RIGHTS_STDIN = 0x820000A
RIGHTS_STDOUT = 0x8200058

# the oflags, fdflags and lookupflags of path_open
O_CREAT, O_DIRECTORY, O_EXCL, O_TRUNC = 1, 2, 4, 8
LOOKUP_FOLLOW = 1
FD_APPEND, FD_DSYNC, FD_NONBLOCK, FD_RSYNC, FD_SYNC = 1, 2, 4, 8, 16
# the flags of the times: which of the two are set, to a value or to now
T_ATIM, T_ATIM_NOW, T_MTIM, T_MTIM_NOW = 1, 2, 4, 8
# the rights only a directory has: a program that asks for one of them along with the right to write (wasi-libc asks for all the rights there are) may mean a directory
R_OF_DIRECTORY = sum(1 << n for n in (9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 24, 25, 26))


K_NONE, K_STD, K_FILE, K_DIR = range(4)

_NAMES = [
    "E2BIG",
    "EACCES",
    "EADDRINUSE",
    "EADDRNOTAVAIL",
    "EAFNOSUPPORT",
    "EAGAIN",
    "EALREADY",
    "EBADF",
    "EBADMSG",
    "EBUSY",
    "ECANCELED",
    "ECHILD",
    "ECONNABORTED",
    "ECONNREFUSED",
    "ECONNRESET",
    "EDEADLK",
    "EDESTADDRREQ",
    "EDOM",
    "EDQUOT",
    "EEXIST",
    "EFAULT",
    "EFBIG",
    "EHOSTUNREACH",
    "EIDRM",
    "EILSEQ",
    "EINPROGRESS",
    "EINTR",
    "EINVAL",
    "EIO",
    "EISCONN",
    "EISDIR",
    "ELOOP",
    "EMFILE",
    "EMLINK",
    "EMSGSIZE",
    "EMULTIHOP",
    "ENAMETOOLONG",
    "ENETDOWN",
    "ENETRESET",
    "ENETUNREACH",
    "ENFILE",
    "ENOBUFS",
    "ENODEV",
    "ENOENT",
    "ENOEXEC",
    "ENOLCK",
    "ENOLINK",
    "ENOMEM",
    "ENOMSG",
    "ENOPROTOOPT",
    "ENOSPC",
    "ENOSYS",
    "ENOTCONN",
    "ENOTDIR",
    "ENOTEMPTY",
    "ENOTRECOVERABLE",
    "ENOTSOCK",
    "ENOTSUP",
    "ENOTTY",
    "ENXIO",
    "EOVERFLOW",
    "EOWNERDEAD",
    "EPERM",
    "EPIPE",
    "EPROTO",
    "EPROTONOSUPPORT",
    "EPROTOTYPE",
    "ERANGE",
    "EROFS",
    "ESPIPE",
    "ESRCH",
    "ESTALE",
    "ETIMEDOUT",
    "ETXTBSY",
    "EXDEV",
]
# the errno of the C library as an errno of WASI, by number (a name the system does not have is left out); anything else is EIO
_ERRNO: dict[int, int] = {}
for _number, _name in enumerate(_NAMES, 1):
    _value = getattr(_errno, _name, None)
    if _value is not None:
        _ERRNO.setdefault(_value, _number)
_ERRNO[0] = E_SUCCESS
if hasattr(_errno, "EOPNOTSUPP"):
    _ERRNO.setdefault(_errno.EOPNOTSUPP, E_NOTSUP)


def errno_of(number: int | None) -> int:
    """The errno of the C library (or of an `OSError`) as an errno of WASI."""
    return _ERRNO.get(number or 0, E_IO)


class WasiError(Exception):
    """What a hook raises to give the program an errno of WASI as it is (any other exception is a trap)."""

    def __init__(self, number: int) -> None:
        super().__init__(f"errno {number}")
        self.number = number


class PreopenError(OSError):
    """A directory of `preopens` could not be opened; `index` is its place in the list."""

    index: int = -1


@dataclass
class Stat:
    """What the specification calls filestat, the same for every way of getting it."""

    dev: int = 0
    ino: int = 0
    nlink: int = 0
    size: int = 0
    atim: int = 0
    mtim: int = 0
    ctim: int = 0
    filetype: int = 0


def filetype_of(mode: int) -> int:
    kind = _stat.S_IFMT(mode)
    return {
        _stat.S_IFDIR: FT_DIR,
        _stat.S_IFREG: FT_REGULAR,
        _stat.S_IFLNK: FT_SYMLINK,
        _stat.S_IFCHR: FT_CHAR,
        _stat.S_IFBLK: FT_BLOCK,
        _stat.S_IFSOCK: FT_SOCKET_STREAM,
    }.get(kind, FT_UNKNOWN)


_ERRNO_OF_CODE = {  # the errno of WASI for an error-code of 0.2 (by name); a way out of the directory is `escape`
    "access": E_ACCES, "would-block": E_AGAIN, "already": E_ALREADY, "bad-descriptor": E_BADF, "busy": E_BUSY, "deadlock": E_DEADLK, "quota": E_DQUOT, "exist": E_EXIST,
    "file-too-large": E_FBIG, "illegal-byte-sequence": E_ILSEQ, "in-progress": E_INPROGRESS, "interrupted": E_INTR, "invalid": E_INVAL, "io": E_IO, "is-directory": E_ISDIR,
    "loop": E_LOOP, "too-many-links": E_MLINK, "message-size": E_MSGSIZE, "name-too-long": E_NAMETOOLONG, "no-device": E_NODEV, "no-entry": E_NOENT, "no-lock": E_NOLCK,
    "insufficient-memory": E_NOMEM, "insufficient-space": E_NOSPC, "not-directory": E_NOTDIR, "not-empty": E_NOTEMPTY, "not-recoverable": E_NOTRECOVERABLE,
    "unsupported": E_NOSYS, "no-tty": E_NOTTY, "no-such-device": E_NXIO, "overflow": E_OVERFLOW, "not-permitted": E_PERM, "pipe": E_PIPE, "read-only": E_ROFS,
    "invalid-seek": E_SPIPE, "text-file-busy": E_TXTBSY, "cross-device": E_XDEV,
}  # fmt: skip


def errno_of_error(caught: FsError) -> int:
    """The errno of WASI for a failure of an operation of 0.2 (the host of Wasmtime does the same for 0.1 over 0.2): a way out of the directory is not a capability the program has."""
    return E_NOTCAPABLE if caught.escape else _ERRNO_OF_CODE.get(caught.name, E_IO)


_ERRNO_OF_NAME = {
    "access": _errno.EACCES,
    "no-entry": _errno.ENOENT,
    "not-directory": _errno.ENOTDIR,
    "name-too-long": _errno.ENAMETOOLONG,
    "insufficient-memory": _errno.ENOMEM,
}


class Entry:
    """An entry of the descriptor map. `kind`: a standard stream (`handle` is 0, 1 or 2: the place in `std` of the state), a file or a directory (`handle` is the handle of a `descriptor` in
    the table of the state: the resource of 0.2). The rest is what 0.1 keeps for the descriptor."""

    __slots__ = ("filetype", "flags", "handle", "inheriting", "kind", "listing", "position", "preopen", "rights")

    def __init__(self, kind: int = K_NONE) -> None:
        self.kind = kind
        self.filetype = 0
        self.flags = 0  # fdflags
        self.handle = 0
        self.rights = 0
        self.inheriting = 0
        self.position = 0  # the offset of the next fd_read and fd_write of a file (0.2 has none in a descriptor)
        self.preopen: bytes | None = None  # the name the program sees, for a preopen
        self.listing: list[Listing] | None = None  # fd_readdir: the names of the directory, made at cookie 0


@dataclass
class Listing:
    """One name of a directory as `fd_readdir` gives it."""

    name: bytes
    ino: int
    filetype: int


class Fds:
    """The descriptors of 0.1 of a state: 0, 1, 2 the standard streams, 3 and up the preopens in order, then what `path_open` opens."""

    def __init__(self) -> None:
        self.entries: list[Entry] = []

    def new_fd(self) -> int:
        """The lowest free number from 3 on (the table grows by one when there is none)."""
        for number in range(3, len(self.entries)):
            if self.entries[number].kind == K_NONE:
                return number
        self.entries.append(Entry())
        return len(self.entries) - 1

    def entry_of(self, fd: int) -> Entry | None:
        return self.entries[fd] if 0 <= fd < len(self.entries) and self.entries[fd].kind != K_NONE else None

    def entry_with(self, fd: int, right: int) -> tuple[int, Entry | None]:
        """The entry of a descriptor that has the right, or an errno of WASI."""
        entry = self.entry_of(fd)
        if entry is None:
            return E_BADF, None
        if entry.kind != K_STD and not entry.rights & right:
            return E_NOTCAPABLE, entry
        return E_SUCCESS, entry

    def dir_with(self, fd: int, right: int) -> tuple[int, Entry | None]:
        """The directory of a call on a path: it has to be one and to have the right."""
        number, entry = self.entry_with(fd, right)
        if number or entry is None:
            return number, entry
        return (E_SUCCESS if entry.kind == K_DIR else E_NOTDIR), entry


def release(state: State, entry: Entry) -> None:
    """Lets go of what the entry holds: a file or a directory is dropped from the table of the state (and closed), a standard stream stays the state's."""
    if entry.kind in (K_FILE, K_DIR):
        descriptor = descriptor_of(state, entry)
        if descriptor is not None:
            index = entry.handle & INDEX_MASK
            state.slots[index].live, state.slots[index].obj = False, None
            state.filesystem.close(descriptor)
    entry.__init__()  # type: ignore[misc]


def descriptor_of(state: State, entry: Entry) -> Any:
    """The resource of 0.2 behind a file or a directory (None if it is gone)."""
    index = entry.handle & INDEX_MASK
    if index == 0 or index >= len(state.slots):
        return None
    slot = state.slots[index]
    if not slot.live or slot.kind != KIND_DESCRIPTOR or slot.generation != entry.handle >> INDEX_BITS:
        return None
    return slot.obj


def open_descriptors(state: State) -> Fds:
    """Makes the descriptors of 0.1 of the state now (the functions of 0.1 do it at their first call when this was not called): 0, 1 and 2 are the standard streams, 3 and up the preopens in
    order. Raises `PreopenError` (`index` is the place of the directory in `preopens`) when a directory cannot be opened; nothing is made then."""
    if state.p1 is not None:
        return state.p1
    fds = Fds()
    for stream in range(3):
        entry = Entry(K_STD)
        entry.handle = stream  # which stream it is, wherever it is renumbered to
        fds.entries.append(entry)
    try:
        for index, (host, name, writable) in enumerate(state.preopens):
            try:
                made = state.filesystem.open_directory(host, READ | (MUTATE if writable else 0))
            except FsError as caught:
                failure = PreopenError(_ERRNO_OF_NAME.get(caught.name, _errno.EIO), caught.name, host)
                failure.index = index
                raise failure from None
            handle = table_add(state, KIND_DESCRIPTOR, made)
            entry = Entry(K_DIR)
            entry.handle = handle
            entry.filetype = FT_DIR
            entry.rights = entry.inheriting = R_ALL if writable else R_ALL & ~R_MUTATING
            entry.preopen = name.encode()
            fds.entries.append(entry)
    except BaseException:
        for entry in fds.entries:
            release(state, entry)
        raise
    state.p1 = fds
    return fds


def fds_of(guest: Guest) -> Fds | int:
    """The descriptors of the state of the guest, made if they were not; or the errno of WASI of the failure to make them."""
    if guest.state.p1 is not None:
        return guest.state.p1
    try:
        return open_descriptors(guest.state)
    except PreopenError as caught:
        return errno_of(caught.errno)


# ---- the memory of the instance (memory 0 of the guest; little-endian, as WebAssembly says) ---------------------------------------


def _buffer(guest: Guest) -> tuple[memoryview | None, Any]:
    """(a view of the memory, else None; the memory as the host gave it). The memory is a buffer (a `bytearray`, a `memoryview`, `mmap`) or, for an engine
    that keeps it out of this process, an object with `read(offset, length)`, `write(offset, data)` and `len()` (what a call then costs is a copy of what it touches)."""
    if not guest.memories:
        return None, None
    memory = guest.memories[0]
    if callable(memory):
        memory = memory()
    if memory is None:
        return None, None
    try:
        return flat_view(memory), memory
    except TypeError:
        return None, memory


def mem_size(guest: Guest) -> int:
    view, memory = _buffer(guest)
    return len(view) if view is not None else len(memory) if memory is not None else 0


def load(guest: Guest, ptr: int, length: int) -> bytes:
    view, memory = _buffer(guest)
    if view is not None:
        return bytes(view[ptr : ptr + length])
    return bytes(memory.read(ptr, length)) if length else b""


def store(guest: Guest, ptr: int, data: bytes) -> None:
    view, memory = _buffer(guest)
    if view is not None:
        view[ptr : ptr + len(data)] = data
    elif data:
        memory.write(ptr, data)


def in_memory(guest: Guest, ptr: int, length: int) -> bool:
    return ptr + length <= mem_size(guest)


def put_bytes(guest: Guest, ptr: int, data: bytes) -> bool:
    if not in_memory(guest, ptr, len(data)):
        return False
    store(guest, ptr, data)
    return True


def put(guest: Guest, ptr: int, fmt: str, *values: int) -> bool:
    return put_bytes(guest, ptr, struct.pack("<" + fmt, *values))


def put_u8(guest: Guest, ptr: int, value: int) -> bool:
    return put_bytes(guest, ptr, bytes((value & 0xFF,)))


def put_u16(guest: Guest, ptr: int, value: int) -> bool:
    return put(guest, ptr, "H", value & 0xFFFF)


def put_u32(guest: Guest, ptr: int, value: int) -> bool:
    return put(guest, ptr, "I", value & 0xFFFFFFFF)


def put_u64(guest: Guest, ptr: int, value: int) -> bool:
    return put(guest, ptr, "Q", value & 0xFFFFFFFFFFFFFFFF)


def get_u32(guest: Guest, ptr: int) -> int | None:
    if not in_memory(guest, ptr, 4):
        return None
    return struct.unpack("<I", load(guest, ptr, 4))[0]


def read_bytes(guest: Guest, ptr: int, length: int) -> bytes:
    return load(guest, ptr, length)


def iov(guest: Guest, iovs: int, index: int) -> tuple[int, int]:
    """(base, length) of the buffer `index` of an iovec that `iovs_valid` has looked at."""
    return get_u32(guest, iovs + 8 * index) or 0, get_u32(guest, iovs + 8 * index + 4) or 0


def path_at(guest: Guest, ptr: int, length: int) -> bytes | None:
    """The bytes of a path in the memory, or None if they are not all there."""
    return read_bytes(guest, ptr, length) if in_memory(guest, ptr, length) else None


def iovs_valid(guest: Guest, iovs: int, count: int) -> int:
    """All the buffers of a call are looked at before any of them is used: a bad one in the middle must not leave the data before it already written."""
    if not in_memory(guest, iovs, count * 8):
        return E_FAULT
    for i in range(count):
        base, length = get_u32(guest, iovs + 8 * i), get_u32(guest, iovs + 8 * i + 4)
        assert base is not None and length is not None
        if not in_memory(guest, base, length):
            return E_FAULT
    return E_SUCCESS


# ---- the standard streams (`std` of the state, else the descriptors of the process) -------------------------------------------------


def std_stream(state: State, which: int) -> Any:
    """The standard stream `which` (0, 1, 2) of the state."""
    return std(state, ("stdin", "stdout", "stderr")[which])


def std_is_real(state: State, entry: Entry) -> bool:
    """A standard stream that is the descriptor of the process (`FdInput`, `FdOutput`): it is what the system says it is."""
    stream = state.std.get(("stdin", "stdout", "stderr")[entry.handle]) if entry.kind == K_STD else None
    return isinstance(stream, (FdInput, FdOutput)) and stream.fd == entry.handle


def std_filetype(state: State, entry: Entry) -> int:
    if std_is_real(state, entry):
        import os

        try:
            return filetype_of(os.fstat(entry.handle).st_mode)  # a pipe is unknown: not a terminal
        except OSError:
            pass
    return FT_CHAR


def stream_status(failure: StreamClosed | StreamFailed) -> int:
    """A closed stream is a broken pipe; the errno of the system, when the stream knows it, as it is."""
    if isinstance(failure, StreamClosed):
        return E_PIPE
    return errno_of(failure.errno) if failure.errno else E_IO


def write_std(guest: Guest, which: int, iovs: int, count: int, nwritten: int) -> int:
    bad = iovs_valid(guest, iovs, count)
    if bad:
        return bad
    stream = std_stream(guest.state, which)
    total = 0
    for i in range(count):
        base, length = get_u32(guest, iovs + 8 * i), get_u32(guest, iovs + 8 * i + 4)
        assert base is not None and length is not None
        if length == 0:
            continue
        try:
            stream.write(read_bytes(guest, base, length))
        except (
            StreamClosed,
            StreamFailed,
        ) as failure:  # what was written is reported (a short write); the error comes with the next call
            if total == 0:
                return stream_status(failure)
            break
        total += length
    return E_SUCCESS if put_u32(guest, nwritten, total) else E_FAULT


def read_std(guest: Guest, iovs: int, count: int, nread: int) -> int:
    bad = iovs_valid(guest, iovs, count)
    if bad:
        return bad
    base = length = 0
    for i in range(count):  # the first buffer that is not empty: a short read is allowed
        base, length = get_u32(guest, iovs + 8 * i) or 0, get_u32(guest, iovs + 8 * i + 4) or 0
        if length:
            break
    if not in_memory(guest, base, length):
        return E_FAULT
    data = b""
    if length:
        stream = std_stream(guest.state, 0)
        while True:  # a stream of 0.2 gives nothing when nothing is ready; a read of 0.1 waits for a byte or the end
            try:
                data = stream.read(length)
            except StreamClosed:
                data = b""
                break
            except StreamFailed as failure:
                return stream_status(failure)
            if len(data) > length:
                return E_IO
            if data:
                break
            guest.wait()
        put_bytes(guest, base, data)
    return E_SUCCESS if put_u32(guest, nread, len(data)) else E_FAULT
