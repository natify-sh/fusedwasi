"""The state of a host of WASI 0.1 and the helpers that every call uses (the twin of `include/fusedwasi/p1/wasi_p1_core.h`).

No mutable state at the level of the module: everything that changes is in a `State`, which is the first argument of every call of `wasi_p1`. A call returns the
errno of WASI (0 is success) and writes its results into the memory of the module; a trap is an exception (a hook may raise any), `Exit` and `Signal` are the
ends of the program (`proc_exit`, `proc_raise`). The memory is any writable buffer (`bytearray`, `memoryview`) or a function that gives it (an engine whose memory moves
when it grows).
"""

from __future__ import annotations

import errno as _errno
import os
import stat as _stat
import struct
import sys
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from typing import TYPE_CHECKING, Any

if TYPE_CHECKING:
    from .wasi_p1_files import Files, Listing

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

END_NONE, END_EXIT, END_SIGNAL, END_TRAP = range(4)

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


class Exit(Exception):
    """`proc_exit`: the program is over with this code."""

    def __init__(self, code: int) -> None:
        super().__init__(f"exit {code}")
        self.code = code


class Signal(Exception):
    """`proc_raise`: the program raised a signal that ends it."""

    def __init__(self, number: int) -> None:
        super().__init__(f"signal {number}")
        self.number = number


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


class Entry:
    """An open descriptor: a standard stream, a file (an operating system descriptor) or a directory."""

    __slots__ = ("fd", "filetype", "flags", "inheriting", "kind", "listing", "owns_fd", "preopen", "rights")

    def __init__(self, kind: int = K_NONE) -> None:
        self.kind = kind
        self.filetype = 0
        self.flags = 0  # fdflags
        self.fd = -1  # a file or a directory; for a standard stream, which one it is (0, 1 or 2)
        self.owns_fd = False
        self.rights = 0
        self.inheriting = 0
        self.preopen: bytes | None = None  # the name the program sees, for a preopen
        self.listing: list[Listing] | None = None  # fd_readdir: the names of the directory, made at cookie 0
        self.listing: list[Listing] | None = None  # fd_readdir: the names of the directory, made at cookie 0


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


def _bytes(text: str | bytes) -> bytes:
    return text if isinstance(text, bytes) else os.fsencode(text)


class State:
    """The state of one instance of a program that imports `wasi_snapshot_preview1`."""

    def __init__(
        self,
        memory: object | Callable[[], object] | None = None,
        *,
        args: Sequence[str | bytes] | None = None,
        program: str | bytes = "program",
        env: Sequence[str | bytes | tuple[str, str]] = (),
        preopens: Sequence[tuple[str, str] | tuple[str, str, bool]] = (),
        stdin_read: Callable[[int], bytes] | None = None,
        stdout_write: Callable[[int, bytes], None] | None = None,
        clock: Callable[[int], int] | None = None,
        random: Callable[[int], bytes] | None = None,
        sleep: Callable[[int], None] | None = None,
        end: Callable[[int, int], None] | None = None,
        files: Files | None = None,
    ) -> None:
        self.memory = memory
        if files is None:  # the backend of the system's files; a host without files gives `Files()`
            if sys.platform == "win32":
                from .wasi_p1_files import Files as _Files

                files = _Files()
            else:
                from .wasi_p1_files_posix import PosixFiles

                files = PosixFiles()
        self.files = files
        # the hooks: what the host decides (the standard streams, the clocks, the random bytes, the sleep, the end of the program); where one is absent
        # the system's own is used. A hook that raises `WasiError(n)` gives the program the errno n; any other exception is a trap and goes on to the caller.
        self.stdin_read = stdin_read  # (capacity) -> up to that many bytes
        self.stdout_write = stdout_write  # (fd 1 or 2, data); it stands for both streams
        self.clock = clock  # (id of the clock of WASI) -> nanoseconds
        self.random = random  # (n) -> n bytes
        self.sleep = sleep  # (nanoseconds)
        self.end = end  # (how: END_EXIT or END_SIGNAL, value), told before the program is marked as over
        self.finished = END_NONE  # the program is over: 0, or how it ended
        self.end_value = 0
        arguments = [_bytes(a) for a in (args if args is not None else [program])]
        self.args = b"".join(a + b"\0" for a in arguments)
        self.argc = len(arguments)
        variables = [_bytes(f"{v[0]}={v[1]}" if isinstance(v, tuple) else v) for v in env]
        self.env = b"".join(v + b"\0" for v in variables)
        self.envc = len(variables)
        self.entries: list[Entry] = []
        for stream in range(3):
            entry = Entry(K_STD)
            entry.fd = stream  # which stream it is, wherever it is renumbered to
            self.entries.append(entry)
        for index, item in enumerate(preopens):
            host, name = item[0], item[1]
            writable = item[2] if len(item) > 2 else True  # type: ignore[misc]
            try:
                fd = self.files.preopen(host)
            except OSError as caught:
                self.close_all()
                failure = PreopenError(caught.errno, caught.strerror, host)
                failure.index = index
                raise failure from None
            entry = Entry(K_DIR)
            entry.filetype = FT_DIR
            entry.fd = fd
            entry.owns_fd = True
            entry.rights = entry.inheriting = R_ALL if writable else R_ALL & ~R_MUTATING
            entry.preopen = _bytes(name)
            self.entries.append(entry)

    # ---- the memory of the instance (little-endian, as WebAssembly says) ----------------------------------------------------

    def mem(self) -> memoryview | None:
        memory = self.memory() if callable(self.memory) else self.memory
        return None if memory is None else memoryview(memory)  # type: ignore[arg-type]

    def in_memory(self, ptr: int, length: int) -> bool:
        memory = self.mem()
        return memory is not None and ptr + length <= len(memory)

    def put_u8(self, ptr: int, value: int) -> bool:
        memory = self.mem()
        if memory is None or ptr + 1 > len(memory):
            return False
        memory[ptr] = value & 0xFF
        return True

    def put(self, ptr: int, fmt: str, *values: int) -> bool:
        memory = self.mem()
        size = struct.calcsize("<" + fmt)
        if memory is None or ptr + size > len(memory):
            return False
        struct.pack_into("<" + fmt, memory, ptr, *values)
        return True

    def put_u16(self, ptr: int, value: int) -> bool:
        return self.put(ptr, "H", value & 0xFFFF)

    def put_u32(self, ptr: int, value: int) -> bool:
        return self.put(ptr, "I", value & 0xFFFFFFFF)

    def put_u64(self, ptr: int, value: int) -> bool:
        return self.put(ptr, "Q", value & 0xFFFFFFFFFFFFFFFF)

    def get_u32(self, ptr: int) -> int | None:
        memory = self.mem()
        if memory is None or ptr + 4 > len(memory):
            return None
        return struct.unpack_from("<I", memory, ptr)[0]

    def put_bytes(self, ptr: int, data: bytes) -> bool:
        memory = self.mem()
        if memory is None or ptr + len(data) > len(memory):
            return False
        memory[ptr : ptr + len(data)] = data
        return True

    def read_bytes(self, ptr: int, length: int) -> bytes:
        memory = self.mem()
        assert memory is not None
        return bytes(memory[ptr : ptr + length])

    # ---- descriptors --------------------------------------------------------------------------------------------------------

    def iov(self, iovs: int, index: int) -> tuple[int, int]:
        """(base, length) of the buffer `index` of an iovec that `iovs_valid` has looked at."""
        return self.get_u32(iovs + 8 * index) or 0, self.get_u32(iovs + 8 * index + 4) or 0

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

    def path_at(self, ptr: int, length: int) -> bytes | None:
        """The bytes of a path in the memory, or None if they are not all there."""
        return self.read_bytes(ptr, length) if self.in_memory(ptr, length) else None

    def release(self, entry: Entry) -> int:
        """Closes what the descriptor holds; 0, or the errno of the C library of the close of a file."""
        number = 0
        if entry.owns_fd:
            try:
                os.close(entry.fd)
            except OSError as caught:
                number = caught.errno or _errno.EIO
        entry.__init__()  # type: ignore[misc]
        return number

    def close_all(self) -> None:
        for entry in self.entries:
            self.release(entry)

    def finish(self, how: int, value: int) -> None:
        """The program is over (`proc_exit`, `proc_raise`, or a trap of a command): what it opened is closed at once, and the instance says so afterwards."""
        if self.finished:
            return
        self.finished = how
        self.end_value = value
        self.close_all()

    def hooked(self, hook: Callable[..., Any] | None, *args: Any) -> tuple[int, Any]:
        """Calls a hook: (errno of WASI, result)."""
        assert hook is not None
        try:
            return E_SUCCESS, hook(*args)
        except WasiError as caught:
            return caught.number, None

    # ---- the standard streams -----------------------------------------------------------------------------------------------

    def iovs_valid(self, iovs: int, count: int) -> int:
        """All the buffers of a call are looked at before any of them is used: a bad one in the middle must not leave the data before it already written."""
        if not self.in_memory(iovs, count * 8):
            return E_FAULT
        for i in range(count):
            base, length = self.get_u32(iovs + 8 * i), self.get_u32(iovs + 8 * i + 4)
            assert base is not None and length is not None
            if not self.in_memory(base, length):
                return E_FAULT
        return E_SUCCESS

    def std_is_real(self, entry: Entry) -> bool:
        """A standard stream that is the descriptor of the process (no hook stands in for it): it is what the system says it is."""
        return entry.kind == K_STD and (self.stdin_read is None if entry.fd == 0 else self.stdout_write is None)

    def std_filetype(self, entry: Entry) -> int:
        if self.std_is_real(entry):
            try:
                return filetype_of(os.fstat(entry.fd).st_mode)  # a pipe is unknown: not a terminal
            except OSError:
                pass
        return FT_CHAR

    def write_std(self, fd: int, iovs: int, count: int, nwritten: int) -> int:
        bad = self.iovs_valid(iovs, count)
        if bad:
            return bad
        total = 0
        for i in range(count):
            base, length = self.get_u32(iovs + 8 * i), self.get_u32(iovs + 8 * i + 4)
            assert base is not None and length is not None
            if length == 0:
                continue
            data = self.read_bytes(base, length)
            if self.stdout_write is not None:
                status, _ = self.hooked(self.stdout_write, fd, data)
                if status:  # what was written is reported (a short write); the error comes with the next call
                    if total == 0:
                        return status
                    break
            else:
                done = 0
                failed = False
                while done < length:  # all of it: a short write of a pipe is not the end of the iovec
                    try:
                        done += os.write(fd, data[done:])
                    except InterruptedError:
                        continue
                    except OSError as caught:
                        if total + done == 0:
                            return errno_of(caught.errno)
                        failed = True
                        break
                if failed:
                    total += done
                    break
            total += length
        return E_SUCCESS if self.put_u32(nwritten, total) else E_FAULT

    def read_std(self, iovs: int, count: int, nread: int) -> int:
        bad = self.iovs_valid(iovs, count)
        if bad:
            return bad
        base = length = 0
        for i in range(count):  # the first buffer that is not empty: a short read is allowed
            base, length = self.get_u32(iovs + 8 * i) or 0, self.get_u32(iovs + 8 * i + 4) or 0
            if length:
                break
        if not self.in_memory(base, length):
            return E_FAULT
        data = b""
        if length:
            if self.stdin_read is not None:
                status, data = self.hooked(self.stdin_read, length)
                if status:
                    return status
                if len(data) > length:
                    return E_IO
            else:
                while True:
                    try:
                        data = os.read(0, length)
                        break
                    except InterruptedError:
                        continue
                    except OSError as caught:
                        return errno_of(caught.errno)
            self.put_bytes(base, data)
        return E_SUCCESS if self.put_u32(nread, len(data)) else E_FAULT

    # ---- clocks and random bytes --------------------------------------------------------------------------------------------

    def clock_ns(self, ident: int) -> tuple[int, int]:
        """(errno, nanoseconds) of the clock `ident` of WASI (0 real time, 1 monotonic, 2 of the process, 3 of the thread): the hook of the host or the system's."""
        if ident > 3:
            return E_INVAL, 0
        if self.clock is not None:
            status, value = self.hooked(self.clock, ident)
            return status, int(value or 0)
        try:
            if sys.platform == "win32":
                return E_SUCCESS, (time.time_ns, time.perf_counter_ns, time.process_time_ns, time.thread_time_ns)[
                    ident
                ]()
            clocks = (
                time.CLOCK_REALTIME,
                time.CLOCK_MONOTONIC,
                time.CLOCK_PROCESS_CPUTIME_ID,
                time.CLOCK_THREAD_CPUTIME_ID,
            )
            return E_SUCCESS, time.clock_gettime_ns(clocks[ident])
        except OSError:
            return E_NOTSUP, 0

    def clock_resolution(self, ident: int) -> int | None:
        """The resolution of a clock in nanoseconds (at least 1), or None if the system has no such clock."""
        try:
            if sys.platform == "win32":
                names = ("time", "monotonic", "process_time", "thread_time")
                return max(1, round(time.get_clock_info(names[ident]).resolution * 1e9))
            clocks = (
                time.CLOCK_REALTIME,
                time.CLOCK_MONOTONIC,
                time.CLOCK_PROCESS_CPUTIME_ID,
                time.CLOCK_THREAD_CPUTIME_ID,
            )
            return max(1, round(time.clock_getres(clocks[ident]) * 1e9))
        except (OSError, ValueError):
            return None
