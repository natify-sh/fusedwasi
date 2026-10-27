"""wasi:filesystem (32 imports): `preopens` and `types` (the twin of `include/fusedwasi/p2/wasi_p2_filesystem.h`). POSIX (descriptors relative to a directory: `dir_fd`) and Windows (below).

**The paths are confined.** A path the guest gives is walked by name, as `wasi_walk` of WASI 0.1 does: every directory on the way is opened with `O_NOFOLLOW` relative to the
one before it; `..` may not go above the directory the descriptor stands for; an absolute path, and a symbolic link whose target is absolute, is `not-permitted`; a link
is read and its target put in front of the rest of the path (at most 40 links, then `loop`); a link at the end is expanded only if `symlink-follow` is set. This is a walk by
name, not the `openat2(RESOLVE_BENEATH)` of the kernel, and a race with another process that changes the tree under the walk is not closed.

**Rights.** A descriptor has the flags `read`, `write`, `mutate-directory` and the sync flags; what it was not given is `bad-descriptor` (read, write) or `read-only` (changing a
directory, as the WIT says), and a `preopens` entry may be read-only. Directory entries come sorted by name, without `.` and `..`, as a snapshot when `read-directory` is called.

**On Windows** there is no `dir_fd` and a directory cannot be opened as a file: a directory descriptor is a path (the real path, with the `\\\\?\\` prefix so that the system changes no name), a file
descriptor is a C-library descriptor with its path, and the same walk goes below the path by name. A name with `\\` or `:` is `invalid` (a way out, or a stream of a file), and a link is a symbolic
link or a junction. The check and the use are two steps, so another process that changes the tree in between is not stopped (as in the Windows back end of WASI 0.1). An open file cannot be
deleted or renamed, `rename-at` replaces a file, and a symbolic link needs the privilege for it.

**The operations.** Each function is an operation on host values (`SystemFilesystem.read(descriptor, length, offset)`, `open_at(...)`, ...) and the Canonical ABI around it; the operations are the methods of
`State.filesystem` (see "the operations" below), which is also what the functions of WASI 0.1 call. A host that has other files gives its own object (`State(filesystem=...)`).

Layouts of the results (Canonical ABI), for `retptr`: `result<_, error-code>`: tag at 0, the code at 1; `result<handle, error-code>`: 8 bytes, handle (or code) at 4;
`result<descriptor-flags | descriptor-type>`: 2 bytes, the value at 1; `result<descriptor-stat>`: 104 bytes, the stat at 8 (type 0, link-count 8, size 16, then three
`option<datetime>` of 24 bytes at 24, 48, 72, the datetime inside at +8); `result<tuple<list<u8>, bool>>`: 16 bytes, pointer 4, length 8, end-of-file 12; `result<filesize>`: 16 bytes,
value at 8; `result<string>`: 12 bytes, pointer 4, length 8; `result<metadata-hash-value>`: 24 bytes, lower 8, upper 16; `result<option<directory-entry>>`: 20 bytes, option tag at 4,
type 8, name pointer 12, length 16; `option<error-code>`: tag 0, code 1; the code is the index in `ERROR_CODES`.
"""

from __future__ import annotations

import errno
import os
import stat as stat_module
import sys
from collections.abc import Callable

from .wasi_p2_core import (
    KIND_DESCRIPTOR,
    KIND_DIRECTORY_ENTRY_STREAM,
    KIND_ERROR,
    Guest,
    alloc,
    load_bytes,
    store_bytes,
    store_u8,
    store_u32,
    store_u64,
    table_drop,
    table_get,
    table_new,
    trap,
)
from .wasi_p2_io import (
    READ_CAP,
    Error,
    InputStream,
    OutputStream,
    StreamClosed,
    StreamFailed,
    input_stream_new,
    output_stream_new,
)

ERROR_CODES = [
    "access", "would-block", "already", "bad-descriptor", "busy", "deadlock", "quota", "exist", "file-too-large", "illegal-byte-sequence", "in-progress",
    "interrupted", "invalid", "io", "is-directory", "loop", "too-many-links", "message-size", "name-too-long", "no-device", "no-entry", "no-lock",
    "insufficient-memory", "insufficient-space", "not-directory", "not-empty", "not-recoverable", "unsupported", "no-tty", "no-such-device", "overflow",
    "not-permitted", "pipe", "read-only", "invalid-seek", "text-file-busy", "cross-device",
]  # fmt: skip
_CODE = {name: i for i, name in enumerate(ERROR_CODES)}

_ERRNO_NAMES = {
    "EACCES": "access", "EAGAIN": "would-block", "EALREADY": "already", "EBADF": "bad-descriptor", "EBUSY": "busy", "EDEADLK": "deadlock", "EDQUOT": "quota",
    "EEXIST": "exist", "EFBIG": "file-too-large", "EILSEQ": "illegal-byte-sequence", "EINPROGRESS": "in-progress", "EINTR": "interrupted", "EINVAL": "invalid",
    "EIO": "io", "EISDIR": "is-directory", "ELOOP": "loop", "EMLINK": "too-many-links", "EMSGSIZE": "message-size", "ENAMETOOLONG": "name-too-long",
    "ENODEV": "no-device", "ENOENT": "no-entry", "ENOLCK": "no-lock", "ENOMEM": "insufficient-memory", "ENOSPC": "insufficient-space", "ENOTDIR": "not-directory",
    "ENOTEMPTY": "not-empty", "ENOTRECOVERABLE": "not-recoverable", "ENOSYS": "unsupported", "ENOTSUP": "unsupported", "ENOTTY": "no-tty",
    "ENXIO": "no-such-device", "EOVERFLOW": "overflow", "EPERM": "not-permitted", "EPIPE": "pipe", "EROFS": "read-only", "ESPIPE": "invalid-seek",
    "ETXTBSY": "text-file-busy", "EXDEV": "cross-device",
}  # fmt: skip
_ERRNO = {
    getattr(errno, n): name for n, name in _ERRNO_NAMES.items() if hasattr(errno, n)
}  # (some names are not on every system)

# descriptor-flags (a bit for each, in the order of the WIT), path-flags, open-flags
READ, WRITE, FILE_SYNC, DATA_SYNC, REQUESTED_SYNC, MUTATE = 1, 2, 4, 8, 16, 32
SYMLINK_FOLLOW = 1
CREATE, DIRECTORY, EXCLUSIVE, TRUNCATE = 1, 2, 4, 8

# descriptor-type
_TYPE = {
    "unknown": 0,
    "block-device": 1,
    "character-device": 2,
    "directory": 3,
    "fifo": 4,
    "symbolic-link": 5,
    "regular-file": 6,
    "socket": 7,
}

NAME_MAX = 255
MAX_DEPTH = 128
MAX_LINKS = 40
STREAM_CHUNK = 65536


class FsError(Exception):
    """A failure that is an `error-code`. `escape` (only for the adapter of 0.1): the walk of the path led out of the directory. `done`: for a write, the bytes written before the failure."""

    def __init__(self, name: str, *, escape: bool = False) -> None:
        super().__init__(name)
        self.name = name
        self.code = _CODE[name]
        self.escape = escape
        self.done = 0


_WINERROR = {  # the error-code of a Win32 error (`fusedwasi_p2_win_code` in C)
    2: "no-entry", 3: "no-entry", 15: "no-entry", 53: "no-entry", 67: "no-entry", 5: "access", 32: "access", 33: "access", 65: "access", 80: "exist", 183: "exist",
    145: "not-empty", 267: "not-directory", 17: "cross-device", 1314: "not-permitted", 1142: "too-many-links", 112: "insufficient-space", 39: "insufficient-space",
    206: "name-too-long", 8: "insufficient-memory", 14: "insufficient-memory", 6: "bad-descriptor", 19: "read-only", 1921: "loop", 4390: "invalid", 123: "invalid",
    161: "invalid", 87: "invalid", 1113: "invalid", 109: "pipe", 232: "pipe",
}  # fmt: skip


def _oserror(exc: OSError) -> FsError:
    winerror = getattr(exc, "winerror", None)
    if winerror:
        return FsError(_WINERROR.get(winerror, _ERRNO.get(exc.errno or 0, "io")))
    return FsError(_ERRNO.get(exc.errno or 0, "io"))


class Descriptor:
    __slots__ = ("fd", "flags", "is_dir", "path")

    def __init__(self, fd: int, flags: int, is_dir: bool, path: str = "") -> None:
        self.fd, self.flags, self.is_dir = fd, flags, is_dir
        self.path = path  # Windows: where it is (a directory has no fd there)


class DirectoryEntries:
    __slots__ = ("entries", "pos")

    def __init__(self, entries: list[tuple[int, bytes]]) -> None:
        self.entries, self.pos = entries, 0


def _descriptor(guest: Guest, handle: int) -> Descriptor:
    return table_get(guest, handle, KIND_DESCRIPTOR)  # type: ignore[return-value]


def _need(descriptor: Descriptor, flag: int) -> None:
    if not descriptor.flags & flag:
        raise FsError(
            "read-only" if flag == MUTATE else "bad-descriptor"
        )  # `mutate-directory` unset: read-only (the WIT says so)


def _type_of(mode: int) -> int:
    for test, name in (
        (stat_module.S_ISREG, "regular-file"), (stat_module.S_ISDIR, "directory"), (stat_module.S_ISLNK, "symbolic-link"),
        (stat_module.S_ISCHR, "character-device"), (stat_module.S_ISBLK, "block-device"), (stat_module.S_ISFIFO, "fifo"), (stat_module.S_ISSOCK, "socket"),
    ):  # fmt: skip
        if test(mode):
            return _TYPE[name]
    return 0


# What differs between POSIX and Windows. A `Ref` is what the walk stands on: the descriptor of a directory (POSIX) or its path (Windows); `name` is a name below it.

Ref = int | str

if sys.platform == "win32":
    _REPARSE_MOUNT_POINT = 0xA0000003

    def _is_link(st: os.stat_result) -> bool:
        return stat_module.S_ISLNK(st.st_mode) or getattr(st, "st_reparse_tag", 0) == _REPARSE_MOUNT_POINT

    def _join(parent: Ref, name: bytes) -> str:
        try:
            text = os.fsdecode(name)
        except UnicodeDecodeError:  # Windows wants text
            raise FsError("invalid") from None
        if "\\" in text or ":" in text or "/" in text or text in ("", ".", ".."):
            raise FsError("invalid")
        return f"{parent}\\{text}"

    def _target(parent: Ref, name: bytes) -> str:
        """The place a name stands for: the directory itself for `.`, with no trailing slash (a slash after the name of something that is not a directory is `not-directory`, as in POSIX)."""
        bare = name.rstrip(b"/")
        full = str(parent) if bare in (b".", b"") else _join(parent, bare)
        if bare != name:
            try:
                is_dir = stat_module.S_ISDIR(os.stat(full).st_mode)
            except OSError:
                return full
            if not is_dir:
                raise NotADirectoryError(errno.ENOTDIR, "not a directory")
        return full

    def _ref(d: Descriptor) -> Ref:
        return d.path

    def _lstat(parent: Ref, name: bytes) -> os.stat_result:
        return os.lstat(_target(parent, name))

    def _readlink(parent: Ref, name: bytes) -> bytes:
        target = os.readlink(_target(parent, name))
        if target.startswith(("\\\\?\\", "\\??\\")):
            target = target[4:]
        target = target.replace("\\", "/")
        if len(target) > 1 and target[1] == ":" and target[0].isalpha():
            target = "/" + target  # a drive: absolute, so it is refused like a rooted target
        return os.fsencode(target)

    def _enter(parent: Ref, name: bytes) -> tuple[Ref, int | None]:
        full = _join(parent, name)
        st = os.lstat(full)
        if _is_link(st):
            raise OSError(errno.ELOOP, "link")
        if not stat_module.S_ISDIR(st.st_mode):
            raise NotADirectoryError(errno.ENOTDIR, "not a directory")
        return full, None

    def _fstat(d: Descriptor) -> os.stat_result:
        return os.fstat(d.fd) if d.fd >= 0 else os.stat(d.path)

    def _pread(fd: int, n: int, offset: int) -> bytes:
        if fd < 0:
            raise OSError(errno.EISDIR, "a directory")
        os.lseek(fd, offset, os.SEEK_SET)
        return os.read(fd, n)

    def _pwrite(fd: int, data: bytes | memoryview, offset: int) -> int:
        os.lseek(fd, offset, os.SEEK_SET)
        return os.write(fd, data)

    def _mkdir(parent: Ref, name: bytes) -> None:
        os.mkdir(_target(parent, name))

    def _rmdir(parent: Ref, name: bytes) -> None:
        os.rmdir(_target(parent, name))

    def _unlink(parent: Ref, name: bytes) -> None:
        os.unlink(_target(parent, name))

    def _is_plain_dir(path: str) -> bool:
        return os.path.isdir(path) and not os.path.islink(path)

    def _rename(p1: Ref, name1: bytes, p2: Ref, name2: bytes) -> None:
        a, b = _target(p1, name1), _target(p2, name2)
        # POSIX replaces an empty directory by a directory; Windows does not, so it is taken away first (it says not-empty if it is not)
        if _is_plain_dir(a) and _is_plain_dir(b) and os.path.normcase(a) != os.path.normcase(b):
            os.rmdir(b)
        os.replace(a, b)

    def _link(p1: Ref, name1: bytes, p2: Ref, name2: bytes) -> None:
        os.link(_target(p1, name1), _target(p2, name2))

    def _symlink(target: bytes, parent: Ref, name: bytes) -> None:
        text = os.fsdecode(target).replace("/", "\\")
        beside = os.path.normpath(os.path.join(str(parent).removeprefix("\\\\?\\"), text))
        full = _target(parent, name)
        try:
            os.symlink(text, full, target_is_directory=os.path.isdir(beside))
        except OSError:
            if os.path.lexists(full):  # a name that is taken (a directory says access denied)
                raise FileExistsError(errno.EEXIST, "exists") from None
            raise

    def _utime_reparse_point(full: str, ns: tuple[int, int]) -> None:
        """The times of a link itself, for a Python that cannot (before 3.13 on Windows): what `fusedwasi_p2_win_set_times_path` does in C, a handle opened on the
        reparse point (`FILE_FLAG_OPEN_REPARSE_POINT`) and `SetFileTime`. `ctypes` is imported here, so nothing else pays for it."""
        import ctypes
        from ctypes import wintypes

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)  # type: ignore[attr-defined]
        kernel32.CreateFileW.restype = wintypes.HANDLE
        kernel32.CreateFileW.argtypes = [
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.LPVOID,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        ]
        kernel32.SetFileTime.restype = wintypes.BOOL
        kernel32.SetFileTime.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(wintypes.FILETIME),
            ctypes.POINTER(wintypes.FILETIME),
            ctypes.POINTER(wintypes.FILETIME),
        ]
        kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

        def filetime(value: int) -> wintypes.FILETIME:
            ticks = max(value, 0) // 100 + 116444736000000000  # 100 ns since 1601, as `fusedwasi_p2_ns_ft`
            return wintypes.FILETIME(ticks & 0xFFFFFFFF, ticks >> 32)

        file_write_attributes, share_all, open_existing = 0x100, 0x7, 3
        backup_semantics, open_reparse_point = 0x02000000, 0x00200000
        handle = kernel32.CreateFileW(
            full, file_write_attributes, share_all, None, open_existing, backup_semantics | open_reparse_point, None
        )
        if handle is None or handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())  # type: ignore[attr-defined]
        try:
            atime, mtime = filetime(ns[0]), filetime(ns[1])
            if not kernel32.SetFileTime(handle, None, ctypes.byref(atime), ctypes.byref(mtime)):
                raise ctypes.WinError(ctypes.get_last_error())  # type: ignore[attr-defined]
        finally:
            kernel32.CloseHandle(handle)

    def _utime(parent: Ref, name: bytes, ns: tuple[int, int]) -> None:
        full = _target(parent, name)
        try:
            os.utime(full, ns=ns, follow_symlinks=False)  # a link at the end is the one that is touched
        except NotImplementedError:  # before Python 3.13 on Windows: there it would follow the link
            _utime_reparse_point(full, ns)

    def _utime_d(d: Descriptor, ns: tuple[int, int]) -> None:
        os.utime(d.path, ns=ns)

    def _listdir(d: Descriptor) -> list[bytes]:
        return [os.fsencode(n) for n in os.listdir(d.path)]

    def _open_at(parent: Ref, name: bytes, open_flags: int, flags: int) -> Descriptor:
        full = _target(parent, name)
        try:
            st = os.lstat(full)
        except FileNotFoundError:
            st = None
        if st is not None and _is_link(st):
            raise FsError("loop")  # links are not followed here (O_NOFOLLOW)
        if st is not None and stat_module.S_ISDIR(st.st_mode):
            if open_flags & CREATE and open_flags & EXCLUSIVE:
                raise FsError("exist")
            if flags & WRITE or open_flags & (CREATE | TRUNCATE):
                raise FsError("is-directory")
            return Descriptor(-1, flags, True, full)
        if open_flags & DIRECTORY or name.endswith(b"/"):
            raise FsError("not-directory" if st is not None else "no-entry")
        oflags = getattr(os, "O_BINARY", 0)
        oflags |= os.O_RDWR if flags & READ and flags & WRITE else os.O_WRONLY if flags & WRITE else os.O_RDONLY
        if open_flags & CREATE:
            oflags |= os.O_CREAT
        if open_flags & EXCLUSIVE:
            oflags |= os.O_EXCL
        if open_flags & TRUNCATE:
            oflags |= os.O_TRUNC
        return Descriptor(os.open(full, oflags, 0o666), flags, False, full)

    def _open_directory(path: str, flags: int) -> Descriptor:
        real = os.path.realpath(path).removeprefix("\\\\?\\").rstrip("\\/")
        return Descriptor(-1, flags, True, "\\\\?\\" + real)

else:

    def _ref(d: Descriptor) -> Ref:
        return d.fd

    def _lstat(parent: Ref, name: bytes) -> os.stat_result:
        return os.stat(name, dir_fd=int(parent), follow_symlinks=False)

    def _readlink(parent: Ref, name: bytes) -> bytes:
        return os.fsencode(os.readlink(name, dir_fd=int(parent)))

    def _enter(parent: Ref, name: bytes) -> tuple[Ref, int | None]:
        fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC, dir_fd=int(parent))
        return fd, fd

    def _fstat(d: Descriptor) -> os.stat_result:
        return os.fstat(d.fd)

    def _pread(fd: int, n: int, offset: int) -> bytes:
        return os.pread(fd, n, offset)

    def _pwrite(fd: int, data: bytes | memoryview, offset: int) -> int:
        return os.pwrite(fd, data, offset)

    def _mkdir(parent: Ref, name: bytes) -> None:
        os.mkdir(name, 0o777, dir_fd=int(parent))

    def _rmdir(parent: Ref, name: bytes) -> None:
        os.rmdir(name, dir_fd=int(parent))

    def _unlink(parent: Ref, name: bytes) -> None:
        os.unlink(name, dir_fd=int(parent))

    def _rename(p1: Ref, name1: bytes, p2: Ref, name2: bytes) -> None:
        os.rename(name1, name2, src_dir_fd=int(p1), dst_dir_fd=int(p2))

    def _link(p1: Ref, name1: bytes, p2: Ref, name2: bytes) -> None:
        os.link(name1, name2, src_dir_fd=int(p1), dst_dir_fd=int(p2), follow_symlinks=False)

    def _symlink(target: bytes, parent: Ref, name: bytes) -> None:
        os.symlink(target, name, dir_fd=int(parent))

    def _utime(parent: Ref, name: bytes, ns: tuple[int, int]) -> None:
        os.utime(name, ns=ns, dir_fd=int(parent), follow_symlinks=False)

    def _utime_d(d: Descriptor, ns: tuple[int, int]) -> None:
        os.utime(d.fd, ns=ns)

    def _listdir(d: Descriptor) -> list[bytes]:
        return [os.fsencode(n) for n in os.listdir(d.fd)]

    def _open_at(parent: Ref, name: bytes, open_flags: int, flags: int) -> Descriptor:
        oflags = os.O_NOFOLLOW | os.O_CLOEXEC
        oflags |= os.O_RDWR if flags & READ and flags & WRITE else os.O_WRONLY if flags & WRITE else os.O_RDONLY
        if open_flags & CREATE:
            oflags |= os.O_CREAT
        if open_flags & DIRECTORY:
            oflags |= os.O_DIRECTORY
        if open_flags & EXCLUSIVE:
            oflags |= os.O_EXCL
        if open_flags & TRUNCATE:
            oflags |= os.O_TRUNC
        if flags & FILE_SYNC:
            oflags |= os.O_SYNC
        if flags & DATA_SYNC:
            oflags |= os.O_DSYNC
        try:
            fd = os.open(name, oflags, 0o666, dir_fd=int(parent))
        except NotADirectoryError:
            if open_flags & DIRECTORY and not name.endswith(b"/"):
                try:
                    os.readlink(name, dir_fd=int(parent))
                except OSError:
                    pass
                else:
                    raise FsError("loop") from None  # a link is not entered when links are not followed (O_NOFOLLOW)
            raise
        return Descriptor(fd, flags, stat_module.S_ISDIR(os.fstat(fd).st_mode))

    def _open_directory(path: str, flags: int) -> Descriptor:
        return Descriptor(os.open(path, os.O_RDONLY | os.O_DIRECTORY | os.O_CLOEXEC), flags, True)


# the walk of a path


def _walk(base: Ref, path: bytes, follow: bool, link_slash_not_dir: bool = False) -> tuple[Ref, bytes, list[int]]:
    """(directory, name, descriptors to close): the directory that holds the last name of `path` and that name (`.` for none), the walk confined below `base`.

    `link_slash_not_dir`: a link with a slash after it, where links are not followed, is not a directory (WASI 0.1: uvwasi says so); else the walk follows it, below the base (Wasmtime)."""
    if b"\0" in path:
        raise FsError("invalid")
    if not path:
        raise FsError("no-entry")  # an empty name is no name (POSIX; Wasmtime says the same)
    if path.startswith(b"/"):
        raise FsError("not-permitted")
    stack: list[Ref] = [base]
    opened: list[int] = []
    links = 0
    pending = path
    try:
        while True:
            pending = pending.lstrip(b"/")
            if not pending:
                leaf = b"."
                break
            name, sep, rest = pending.partition(b"/")
            rest = rest.lstrip(b"/")
            last = not rest
            slash = last and bool(sep)
            if len(name) > NAME_MAX:
                raise FsError("name-too-long")
            if name == b".":
                if last:
                    leaf = b"."
                    break
                pending = rest
                continue
            if name == b"..":
                if len(stack) == 1:
                    raise FsError("not-permitted")  # out of the directory it was given
                ref = stack.pop()
                if isinstance(ref, int):
                    opened.remove(ref)
                    os.close(ref)
                if last:
                    leaf = b"."
                    break
                pending = rest
                continue
            if last:
                if not follow:
                    is_link = False
                    if slash:
                        # a slash after a name makes the kernel follow a link there whatever O_NOFOLLOW says; the walk follows it itself, below the base
                        try:
                            _readlink(stack[-1], name)
                        except OSError:
                            pass  # not a link
                        else:
                            if link_slash_not_dir:
                                raise FsError("not-directory")
                            is_link = True
                    if not is_link:
                        leaf = name + (b"/" if slash else b"")
                        break
                # a link at the end is read before it is used (below)
            else:
                if len(stack) > MAX_DEPTH:
                    raise FsError("name-too-long")
                try:
                    nxt, entered = _enter(stack[-1], name)
                except OSError as exc:
                    if exc.errno not in (errno.ELOOP, errno.ENOTDIR, errno.EMLINK):
                        raise _oserror(exc) from exc
                    # not a directory we can enter: perhaps a link, which is read below
                else:
                    stack.append(nxt)
                    if entered is not None:
                        opened.append(entered)
                    pending = rest
                    continue
            try:
                target = _readlink(stack[-1], name)
            except OSError as exc:
                if last:  # not a link: this is the last name
                    leaf = name + (b"/" if slash else b"")
                    break
                raise FsError("not-directory") from exc
            links += 1
            if links > MAX_LINKS:
                raise FsError("loop")
            if target.startswith(b"/"):
                raise FsError("not-permitted")
            pending = target + (b"/" + rest if rest else b"/" if slash else b"")
        return stack[-1], leaf, opened
    except BaseException:
        for fd in opened:
            os.close(fd)
        raise


class _Walked:
    """`with _Walked(base, path, follow) as (directory, name):` closes what the walk opened."""

    def __init__(self, base: Ref, path: bytes, follow: bool, link_slash_not_dir: bool = False) -> None:
        self.directory, self.name, self.opened = _walk(base, path, follow, link_slash_not_dir)

    def __enter__(self) -> tuple[Ref, bytes]:
        return self.directory, self.name

    def __exit__(self, *exc: object) -> None:
        for fd in self.opened:
            os.close(fd)


# The system layer that wasi_p1_files still stands on (removed when the adapter of 0.1 moves over the operations below).
sys_ref = _ref
sys_walk = _walk
SysWalked = _Walked
sys_lstat = _lstat
sys_readlink = _readlink
sys_fstat = _fstat
sys_mkdir = _mkdir
sys_rmdir = _rmdir
sys_unlink = _unlink
sys_rename = _rename
sys_link = _link
sys_symlink = _symlink
sys_utime = _utime
sys_utime_fd = _utime_d
sys_listdir = _listdir
sys_open_at = _open_at
sys_open_directory = _open_directory
sys_error_name = ERROR_CODES


# writing results


def _ok(guest: Guest, comp: int, retptr: int) -> None:
    store_u8(guest, comp, retptr, 0)


def _fail(guest: Guest, comp: int, retptr: int, at: int, exc: FsError) -> None:
    store_u8(guest, comp, retptr, 1)
    store_u8(guest, comp, at, exc.code)


def _datetime(guest: Guest, comp: int, ptr: int, ns: int) -> None:
    store_u8(guest, comp, ptr, 1)  # some
    store_u64(guest, comp, ptr + 8, ns // 1_000_000_000)
    store_u32(guest, comp, ptr + 16, ns % 1_000_000_000)


def _write_stat(guest: Guest, comp: int, ptr: int, st: os.stat_result) -> None:
    store_u8(guest, comp, ptr, _type_of(st.st_mode))
    store_u64(guest, comp, ptr + 8, st.st_nlink)
    store_u64(guest, comp, ptr + 16, st.st_size)
    _datetime(guest, comp, ptr + 24, st.st_atime_ns)
    _datetime(guest, comp, ptr + 48, st.st_mtime_ns)
    _datetime(guest, comp, ptr + 72, st.st_ctime_ns)


def _string(guest: Guest, comp: int, data: bytes, at: int) -> None:
    ptr = alloc(guest, comp, 1, len(data))
    store_bytes(guest, comp, ptr, data)
    store_u32(guest, comp, at, ptr)
    store_u32(guest, comp, at + 4, len(data))


# streams over a file


class FileInput(InputStream):
    def __init__(self, fd: int, offset: int) -> None:
        self.fd, self.pos = (os.dup(fd) if fd >= 0 else -1), offset  # (-1: a directory on Windows)

    def read(self, n: int) -> bytes:
        if n == 0:
            return b""
        try:
            data = _pread(self.fd, n, self.pos)
        except OSError as exc:
            raise StreamFailed(exc.strerror or "read failed", _oserror(exc).code) from exc
        if not data:
            raise StreamClosed
        self.pos += len(data)
        return data

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)


class FileOutput(OutputStream):
    def __init__(self, fd: int, offset: int | None) -> None:
        self.fd, self.pos = (os.dup(fd) if fd >= 0 else -1), offset  # offset None: append

    def check_write(self) -> int:
        return STREAM_CHUNK

    def write(self, data: bytes) -> None:
        try:
            if self.pos is None:
                at = os.fstat(self.fd).st_size
            else:
                at = self.pos
            view = memoryview(data)
            done = 0
            while done < len(view):
                done += _pwrite(self.fd, view[done:], at + done)
            if self.pos is not None:
                self.pos += len(data)
        except OSError as exc:
            raise StreamFailed(exc.strerror or "write failed", _oserror(exc).code) from exc

    def flush(self) -> None:
        pass

    def close(self) -> None:
        if self.fd >= 0:
            os.close(self.fd)


# ---- the operations ----------------------------------------------------------------------------------------------------------------------
#
# A function of wasi:filesystem is an operation and the Canonical ABI around it (the twin of the operations of `wasi_p2_filesystem.h`). The operation takes host values (a
# descriptor, a path as bytes, an offset, bytes) and returns a result or raises `FsError`; it never reads the memory of a guest and never traps. The functions of 0.2 below read
# their arguments from the memory of the guest and write the result there; the adapter of WASI 0.1 (`fusedwasi.p1`) calls the same operations with its own, so a rule of 0.2 is
# written once. They are methods of an object that a `State` holds (`State(filesystem=...)`): `SystemFilesystem` is the files of the system, `Filesystem` a host without files, and a
# host that has other files (in memory, say) gives its own, with a descriptor of its own that has `flags` and `is_dir` (it may subclass `Descriptor`).
#
# `p1=True` is for the adapter of 0.1, which keeps rights of its own and has checked them: the flags of the descriptor (read, write, mutate-directory) are not asked, the walk of a
# path is the one of 0.1 (a link with a slash after it, where links are not followed, is not a directory), a way out of the directory is an `FsError` with `escape` set (0.2:
# not-permitted), and the name `.` as the last name of a path answers as 0.1 does (unlink and remove: invalid; rename and link: invalid or exist; make and symlink: exist;
# readlink: invalid).

Times = tuple[
    int, int, int, int
]  # (new-timestamp of the access time: 0 no-change, 1 now, 2 a timestamp; its nanoseconds since 1970; the same for the modification time)


def _new_time(tag: int, ns: int, current_ns: int, now_ns: int) -> int:
    """new-timestamp: 0 no-change, 1 now, 2 timestamp."""
    if tag == 0:
        return current_ns
    if tag == 1:
        return now_ns
    return ns


class Filesystem:
    """The operations of wasi:filesystem on host values. This base answers every one with `unsupported`: a host without files."""

    def open_directory(self, path: str, flags: int) -> Descriptor:
        raise FsError("unsupported")

    def close(self, d: Descriptor) -> int:
        """Closes what the descriptor holds; 0, or the errno of the C library of the close."""
        return 0

    def stat(self, d: Descriptor) -> os.stat_result:
        raise FsError("unsupported")

    def stat_entry(self, d: Descriptor, name: bytes) -> os.stat_result:
        """The stat of a name that is directly in the directory (`.` and `..` too), no link followed, no walk: what `fd_readdir` of 0.1 needs for the inode."""
        raise FsError("unsupported")

    def read(self, d: Descriptor, length: int, offset: int, p1: bool = False) -> bytes:
        raise FsError("unsupported")

    def write(self, d: Descriptor, data: bytes | memoryview, offset: int, p1: bool = False) -> int:
        """Writes all of it or raises (`FsError.done` is what was written before the failure)."""
        raise FsError("unsupported")

    def set_size(self, d: Descriptor, size: int, p1: bool = False) -> None:
        raise FsError("unsupported")

    def sync(self, d: Descriptor, data_only: bool) -> None:
        raise FsError("unsupported")

    def set_times(self, d: Descriptor, times: Times, now: Callable[[], int], p1: bool = False) -> None:
        """`now()` is the wall clock of the state, asked when the file could be read (an exception of it is not an `FsError`: it is the trap of the caller)."""
        raise FsError("unsupported")

    def read_directory(self, d: Descriptor) -> list[tuple[int, bytes]]:
        """The (descriptor-type, name) of what is in a directory, sorted by name, without `.` and `..`."""
        raise FsError("unsupported")

    def stat_at(self, d: Descriptor, path: bytes, path_flags: int, p1: bool = False) -> os.stat_result:
        raise FsError("unsupported")

    def set_times_at(
        self, d: Descriptor, path: bytes, path_flags: int, times: Times, now: Callable[[], int], p1: bool = False
    ) -> None:
        raise FsError("unsupported")

    def link_at(
        self, d: Descriptor, old_flags: int, old_path: bytes, new_d: Descriptor, new_path: bytes, p1: bool = False
    ) -> None:
        raise FsError("unsupported")

    def rename_at(self, d: Descriptor, old_path: bytes, new_d: Descriptor, new_path: bytes, p1: bool = False) -> None:
        raise FsError("unsupported")

    def open_at(
        self, d: Descriptor, path: bytes, path_flags: int, open_flags: int, flags: int, p1: bool = False
    ) -> Descriptor:
        raise FsError("unsupported")

    def readlink_at(self, d: Descriptor, path: bytes, p1: bool = False) -> bytes:
        raise FsError("unsupported")

    def create_directory_at(self, d: Descriptor, path: bytes, p1: bool = False) -> None:
        raise FsError("unsupported")

    def remove_directory_at(self, d: Descriptor, path: bytes, p1: bool = False) -> None:
        raise FsError("unsupported")

    def unlink_file_at(self, d: Descriptor, path: bytes, p1: bool = False) -> None:
        raise FsError("unsupported")

    def symlink_at(self, d: Descriptor, target: bytes, path: bytes, p1: bool = False) -> None:
        """`target` has been looked at by the caller (no NUL, not rooted)."""
        raise FsError("unsupported")

    def input_stream(self, d: Descriptor, offset: int) -> InputStream:
        raise FsError("unsupported")

    def output_stream(self, d: Descriptor, offset: int | None) -> OutputStream:
        """`offset` None: append."""
        raise FsError("unsupported")

    def is_same_object(self, a: Descriptor, b: Descriptor) -> bool:
        return False


def _need_flag(d: Descriptor, flag: int, p1: bool) -> None:
    if not p1:
        _need(d, flag)


def _walk_below(d: Descriptor, path: bytes, follow: bool, p1: bool) -> _Walked:
    """The walk of a path below the directory `d`, to be used as a context."""
    if not d.is_dir:
        raise FsError("not-directory")
    try:
        return _Walked(_ref(d), path, follow, p1)
    except FsError as exc:
        if p1 and exc.name == "not-permitted":
            raise FsError("not-permitted", escape=True) from None
        raise
    except OSError as exc:
        raise _oserror(exc) from exc


def _leaf_slash(parent: Ref, leaf: bytes) -> None:
    """A name to be made that has a slash after it (a link): the error it can only be; nothing when it has none."""
    if not leaf.endswith(b"/"):
        return
    try:
        st = _lstat(parent, leaf[:-1])
    except OSError as exc:
        raise _oserror(exc) from exc
    raise FsError("exist" if stat_module.S_ISDIR(st.st_mode) else "not-directory")


class SystemFilesystem(Filesystem):
    """The files of the system: a directory is a descriptor (a path on Windows), a path is walked from it one name at a time below it."""

    def open_directory(self, path: str, flags: int) -> Descriptor:
        try:
            return _open_directory(path, flags)
        except OSError as exc:
            raise _oserror(exc) from exc

    def close(self, d: Descriptor) -> int:
        fd, d.fd = d.fd, -1
        if fd < 0:
            return 0
        try:
            os.close(fd)
        except OSError as caught:
            return caught.errno or errno.EIO
        return 0

    def stat(self, d: Descriptor) -> os.stat_result:
        try:
            return _fstat(d)
        except OSError as exc:
            raise _oserror(exc) from exc

    def stat_entry(self, d: Descriptor, name: bytes) -> os.stat_result:
        try:
            if name == b".." and sys.platform == "win32":
                return os.stat(os.path.dirname(d.path.rstrip("\\")) or d.path)
            return _lstat(_ref(d), name)
        except OSError as exc:
            raise _oserror(exc) from exc

    def read(self, d: Descriptor, length: int, offset: int, p1: bool = False) -> bytes:
        _need_flag(d, READ, p1)
        try:
            return _pread(d.fd, length, offset)
        except OSError as exc:
            raise _oserror(exc) from exc

    def write(self, d: Descriptor, data: bytes | memoryview, offset: int, p1: bool = False) -> int:
        _need_flag(d, WRITE, p1)
        view = memoryview(data)
        done = 0
        try:
            while done < len(view):
                done += _pwrite(d.fd, view[done:], offset + done)
        except OSError as exc:
            failure = _oserror(exc)
            failure.done = done
            raise failure from exc
        return done

    def set_size(self, d: Descriptor, size: int, p1: bool = False) -> None:
        _need_flag(d, WRITE, p1)
        try:
            os.ftruncate(d.fd, size)
        except OSError as exc:
            raise _oserror(exc) from exc

    def sync(self, d: Descriptor, data_only: bool) -> None:
        try:
            if d.fd >= 0:  # (a directory on Windows has nothing to flush)
                (getattr(os, "fdatasync", os.fsync) if data_only else os.fsync)(d.fd)  # macOS has no fdatasync
        except OSError as exc:
            raise _oserror(exc) from exc

    def set_times(self, d: Descriptor, times: Times, now: Callable[[], int], p1: bool = False) -> None:
        _need_flag(d, MUTATE if d.is_dir else WRITE, p1)
        st = self.stat(d)
        ns = self._times(st, times, now())
        try:
            _utime_d(d, ns)
        except OSError as exc:
            raise _oserror(exc) from exc

    @staticmethod
    def _times(st: os.stat_result, times: Times, now_ns: int) -> tuple[int, int]:
        a_tag, a_ns, m_tag, m_ns = times
        return _new_time(a_tag, a_ns, st.st_atime_ns, now_ns), _new_time(m_tag, m_ns, st.st_mtime_ns, now_ns)

    def read_directory(self, d: Descriptor) -> list[tuple[int, bytes]]:
        if not d.is_dir:
            raise FsError("not-directory")
        entries: list[tuple[int, bytes]] = []
        try:
            for name in sorted(_listdir(d)):
                try:
                    entries.append((_type_of(_lstat(_ref(d), name).st_mode), name))
                except FileNotFoundError:  # gone since the listing
                    continue
        except OSError as exc:
            raise _oserror(exc) from exc
        return entries

    def stat_at(self, d: Descriptor, path: bytes, path_flags: int, p1: bool = False) -> os.stat_result:
        with _walk_below(d, path, bool(path_flags & SYMLINK_FOLLOW), p1) as (parent, name):
            try:
                return _lstat(parent, name)  # a link at the end was expanded by the walk if it was to be
            except OSError as exc:
                raise _oserror(exc) from exc

    def set_times_at(
        self, d: Descriptor, path: bytes, path_flags: int, times: Times, now: Callable[[], int], p1: bool = False
    ) -> None:
        _need_flag(d, MUTATE, p1)
        with _walk_below(d, path, bool(path_flags & SYMLINK_FOLLOW), p1) as (parent, name):
            try:
                current = _lstat(parent, name)
            except OSError as exc:
                raise _oserror(exc) from exc
            ns = self._times(current, times, now())
            try:
                try:
                    _utime(parent, name, ns)  # a link at the end was expanded by the walk
                except NotImplementedError:  # a platform without it (Emscripten): fine for what is not a link; a link itself cannot be touched there
                    if stat_module.S_ISLNK(current.st_mode):
                        raise FsError("unsupported") from None
                    os.utime(os.fsdecode(name), ns=ns, dir_fd=int(parent))
            except OSError as exc:
                raise _oserror(exc) from exc

    @staticmethod
    def _pair(
        d1: Descriptor, path1: bytes, follow: bool, d2: Descriptor, path2: bytes, p1: bool
    ) -> tuple[_Walked, _Walked]:
        """The two directories and names of rename and link, walked; both are to be closed by the caller."""
        _need_flag(d1, MUTATE, p1)
        first = _walk_below(d1, path1, follow, p1)
        try:
            _need_flag(d2, MUTATE, p1)
            second = _walk_below(d2, path2, False, p1)
        except BaseException:
            first.__exit__()
            raise
        if p1 and (first.name == b"." or second.name == b"."):  # a directory itself is neither moved nor made a link
            first.__exit__()
            second.__exit__()
            raise FsError("exist" if second.name == b"." else "invalid")
        return first, second

    def link_at(
        self, d: Descriptor, old_flags: int, old_path: bytes, new_d: Descriptor, new_path: bytes, p1: bool = False
    ) -> None:
        first, second = self._pair(d, old_path, bool(old_flags & SYMLINK_FOLLOW), new_d, new_path, p1)
        with first as (parent1, leaf1), second as (parent2, leaf2):
            if p1:
                _leaf_slash(parent2, leaf2)
            try:
                _link(parent1, leaf1, parent2, leaf2)  # (the walk expanded a link at the end if it was to be followed)
            except OSError as exc:
                raise _oserror(exc) from exc

    def rename_at(self, d: Descriptor, old_path: bytes, new_d: Descriptor, new_path: bytes, p1: bool = False) -> None:
        first, second = self._pair(d, old_path, False, new_d, new_path, p1)
        with first as (parent1, leaf1), second as (parent2, leaf2):
            try:
                _rename(parent1, leaf1, parent2, leaf2)
            except OSError as exc:
                raise _oserror(exc) from exc

    def open_at(
        self, d: Descriptor, path: bytes, path_flags: int, open_flags: int, flags: int, p1: bool = False
    ) -> Descriptor:
        if not d.is_dir:
            raise FsError("not-directory")
        if not p1:
            if flags & (WRITE | MUTATE) and not d.flags & MUTATE:
                raise FsError(
                    "read-only"
                )  # a descriptor cannot hand out more than it has (as the WIT says for open-at)
            if flags & READ and not d.flags & READ:
                raise FsError("not-permitted")
            if open_flags & (CREATE | TRUNCATE):
                _need(d, MUTATE)
        with _walk_below(d, path, bool(path_flags & SYMLINK_FOLLOW), p1) as (parent, name):
            try:
                made = _open_at(parent, name, open_flags, flags)
            except OSError as exc:
                raise _oserror(exc) from exc
        if made.is_dir:  # what a directory may do is what the directory it was opened under may do (as in Wasmtime), and not what was asked for
            made.flags = d.flags & (READ | MUTATE)
        return made

    def readlink_at(self, d: Descriptor, path: bytes, p1: bool = False) -> bytes:
        with _walk_below(d, path, False, p1) as (parent, name):
            if p1 and name == b".":
                raise FsError("invalid")
            try:
                target = _readlink(parent, name)
            except OSError as exc:
                raise _oserror(exc) from exc
        if target.startswith(b"/"):
            raise FsError("not-permitted")  # an absolute or rooted target is not shown
        return target

    def create_directory_at(self, d: Descriptor, path: bytes, p1: bool = False) -> None:
        _need_flag(d, MUTATE, p1)
        with _walk_below(d, path, False, p1) as (parent, name):
            if p1 and name == b".":
                raise FsError("exist")
            try:
                _mkdir(parent, name)
            except OSError as exc:
                raise _oserror(exc) from exc

    def remove_directory_at(self, d: Descriptor, path: bytes, p1: bool = False) -> None:
        _need_flag(d, MUTATE, p1)
        with _walk_below(d, path, False, p1) as (parent, name):
            if p1 and name == b".":
                raise FsError("invalid")
            try:
                _rmdir(parent, name)
            except OSError as exc:
                raise _oserror(exc) from exc

    def unlink_file_at(self, d: Descriptor, path: bytes, p1: bool = False) -> None:
        _need_flag(d, MUTATE, p1)
        with _walk_below(d, path, False, p1) as (parent, name):
            if p1 and name == b".":
                raise FsError("invalid")
            try:
                try:
                    _unlink(parent, name)
                except PermissionError:  # some systems say EPERM (or EACCES) for a directory
                    if stat_module.S_ISDIR(_lstat(parent, name).st_mode):
                        raise FsError("is-directory") from None
                    raise
            except OSError as exc:
                raise _oserror(exc) from exc
            except FsError as exc:
                if (
                    p1 and exc.name == "is-directory"
                ):  # unlink of a directory: the answer of the system (the layer of 0.2 says is-directory for all)
                    if sys.platform == "darwin":
                        raise FsError("not-permitted") from None
                    if sys.platform == "win32":
                        raise FsError("access") from None
                raise

    def symlink_at(self, d: Descriptor, target: bytes, path: bytes, p1: bool = False) -> None:
        _need_flag(d, MUTATE, p1)
        with _walk_below(d, path, False, p1) as (parent, name):
            if p1:
                if name == b".":
                    raise FsError("exist")
                _leaf_slash(parent, name)
            try:
                _symlink(target, parent, name)
            except OSError as exc:
                raise _oserror(exc) from exc

    def input_stream(self, d: Descriptor, offset: int) -> InputStream:
        _need(d, READ)
        try:
            return FileInput(d.fd, offset)
        except OSError as exc:
            raise _oserror(exc) from exc

    def output_stream(self, d: Descriptor, offset: int | None) -> OutputStream:
        _need(d, WRITE)
        try:
            return FileOutput(d.fd, offset)
        except OSError as exc:
            raise _oserror(exc) from exc

    def is_same_object(self, a: Descriptor, b: Descriptor) -> bool:
        try:
            x, y = _fstat(a), _fstat(b)
        except OSError:
            return False
        return (x.st_dev, x.st_ino) == (y.st_dev, y.st_ino)


# ---- the Canonical ABI around them -------------------------------------------------------------------------------------------------------


def _fs(guest: Guest) -> Filesystem:
    return guest.state.filesystem


# preopens


def wasi_filesystem_preopens_get_directories(guest: Guest, comp: int, retptr: int) -> None:
    items = guest.state.preopens
    ptr = alloc(guest, comp, 4, 12 * len(items))
    for i, (path, name, writable) in enumerate(items):
        try:
            made = _fs(guest).open_directory(path, READ | (MUTATE if writable else 0))
        except FsError:
            trap(guest, "cannot open a preopened directory")
        handle = table_new(guest, KIND_DESCRIPTOR, made)
        store_u32(guest, comp, ptr + 12 * i, handle)
        _string(guest, comp, name.encode(), ptr + 12 * i + 4)
    store_u32(guest, comp, retptr, ptr)
    store_u32(guest, comp, retptr + 4, len(items))


# the descriptor


def wasi_filesystem_types_descriptor_drop(guest: Guest, handle: int) -> None:
    descriptor = _descriptor(guest, handle)
    table_drop(guest, handle, KIND_DESCRIPTOR)
    _fs(guest).close(descriptor)


def wasi_filesystem_types_directory_entry_stream_drop(guest: Guest, handle: int) -> None:
    table_drop(guest, handle, KIND_DIRECTORY_ENTRY_STREAM)


def _stream_result(guest: Guest, comp: int, retptr: int, make: Callable[[], int]) -> None:
    try:
        stream = make()
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    store_u32(guest, comp, retptr + 4, stream)


def wasi_filesystem_types_descriptor_read_via_stream(
    guest: Guest, comp: int, self: int, offset: int, retptr: int
) -> None:
    def make() -> int:
        d = _descriptor(guest, self)
        return input_stream_new(guest, _fs(guest).input_stream(d, offset))

    _stream_result(guest, comp, retptr, make)


def wasi_filesystem_types_descriptor_write_via_stream(
    guest: Guest, comp: int, self: int, offset: int, retptr: int
) -> None:
    def make() -> int:
        d = _descriptor(guest, self)
        return output_stream_new(guest, _fs(guest).output_stream(d, offset))

    _stream_result(guest, comp, retptr, make)


def wasi_filesystem_types_descriptor_append_via_stream(guest: Guest, comp: int, self: int, retptr: int) -> None:
    def make() -> int:
        d = _descriptor(guest, self)
        return output_stream_new(guest, _fs(guest).output_stream(d, None))

    _stream_result(guest, comp, retptr, make)


def _status(guest: Guest, comp: int, retptr: int, action: Callable[[], object]) -> None:
    """For a `result<_, error-code>`: runs `action()`, writes ok or the error."""
    try:
        action()
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 1, exc)
        return
    _ok(guest, comp, retptr)


def wasi_filesystem_types_descriptor_advise(
    guest: Guest, comp: int, self: int, offset: int, length: int, advice: int, retptr: int
) -> None:
    _status(guest, comp, retptr, lambda: _descriptor(guest, self))  # advice is only a hint: accepted, and nothing done


def wasi_filesystem_types_descriptor_sync_data(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _status(guest, comp, retptr, lambda: _fs(guest).sync(_descriptor(guest, self), True))


def wasi_filesystem_types_descriptor_sync(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _status(guest, comp, retptr, lambda: _fs(guest).sync(_descriptor(guest, self), False))


def wasi_filesystem_types_descriptor_get_flags(guest: Guest, comp: int, self: int, retptr: int) -> None:
    _ok(guest, comp, retptr)
    store_u8(guest, comp, retptr + 1, _descriptor(guest, self).flags)


def wasi_filesystem_types_descriptor_get_type(guest: Guest, comp: int, self: int, retptr: int) -> None:
    d = _descriptor(guest, self)
    try:
        kind = _type_of(_fs(guest).stat(d).st_mode)
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 1, exc)
        return
    _ok(guest, comp, retptr)
    store_u8(guest, comp, retptr + 1, kind)


def wasi_filesystem_types_descriptor_set_size(guest: Guest, comp: int, self: int, size: int, retptr: int) -> None:
    _status(guest, comp, retptr, lambda: _fs(guest).set_size(_descriptor(guest, self), size))


def _times(a_tag: int, a_seconds: int, a_nanos: int, m_tag: int, m_seconds: int, m_nanos: int) -> Times:
    return a_tag, a_seconds * 1_000_000_000 + a_nanos, m_tag, m_seconds * 1_000_000_000 + m_nanos


def wasi_filesystem_types_descriptor_set_times(
    guest: Guest,
    comp: int,
    self: int,
    a_tag: int,
    a_seconds: int,
    a_nanos: int,
    m_tag: int,
    m_seconds: int,
    m_nanos: int,
    retptr: int,
) -> None:
    times = _times(a_tag, a_seconds, a_nanos, m_tag, m_seconds, m_nanos)
    _status(
        guest, comp, retptr, lambda: _fs(guest).set_times(_descriptor(guest, self), times, lambda: guest.state.clock(0))
    )


def wasi_filesystem_types_descriptor_read(
    guest: Guest, comp: int, self: int, length: int, offset: int, retptr: int
) -> None:
    try:
        d = _descriptor(guest, self)
        data = _fs(guest).read(d, min(length, READ_CAP), offset)
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    ptr = alloc(guest, comp, 1, len(data))
    store_bytes(guest, comp, ptr, data)
    _ok(guest, comp, retptr)
    store_u32(guest, comp, retptr + 4, ptr)
    store_u32(guest, comp, retptr + 8, len(data))
    store_u8(guest, comp, retptr + 12, int(not data))  # end of file is when nothing was read (as in Wasmtime)


def wasi_filesystem_types_descriptor_write(
    guest: Guest, comp: int, self: int, ptr: int, length: int, offset: int, retptr: int
) -> None:
    try:
        d = _descriptor(guest, self)
        _fs(guest).write(d, load_bytes(guest, comp, ptr, length), offset)
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 8, exc)
        return
    _ok(guest, comp, retptr)
    store_u64(guest, comp, retptr + 8, length)


def wasi_filesystem_types_descriptor_read_directory(guest: Guest, comp: int, self: int, retptr: int) -> None:
    def make() -> int:
        d = _descriptor(guest, self)
        return table_new(guest, KIND_DIRECTORY_ENTRY_STREAM, DirectoryEntries(_fs(guest).read_directory(d)))

    _stream_result(guest, comp, retptr, make)


def wasi_filesystem_types_directory_entry_stream_read_directory_entry(
    guest: Guest, comp: int, self: int, retptr: int
) -> None:
    stream: DirectoryEntries = table_get(guest, self, KIND_DIRECTORY_ENTRY_STREAM)  # type: ignore[assignment]
    _ok(guest, comp, retptr)
    if stream.pos >= len(stream.entries):
        store_u8(guest, comp, retptr + 4, 0)  # none
        return
    kind, name = stream.entries[stream.pos]
    stream.pos += 1
    store_u8(guest, comp, retptr + 4, 1)
    store_u8(guest, comp, retptr + 8, kind)
    _string(guest, comp, name, retptr + 12)


def wasi_filesystem_types_descriptor_stat(guest: Guest, comp: int, self: int, retptr: int) -> None:
    d = _descriptor(guest, self)
    try:
        st = _fs(guest).stat(d)
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 8, exc)
        return
    _ok(guest, comp, retptr)
    _write_stat(guest, comp, retptr + 8, st)


def _path(guest: Guest, comp: int, ptr: int, length: int) -> bytes:
    return load_bytes(guest, comp, ptr, length)


def _stat_at(guest: Guest, comp: int, self: int, flags: int, ptr: int, length: int) -> os.stat_result:
    d = _descriptor(guest, self)
    return _fs(guest).stat_at(d, _path(guest, comp, ptr, length), flags)


def wasi_filesystem_types_descriptor_stat_at(
    guest: Guest, comp: int, self: int, path_flags: int, ptr: int, length: int, retptr: int
) -> None:
    try:
        st = _stat_at(guest, comp, self, path_flags, ptr, length)
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 8, exc)
        return
    _ok(guest, comp, retptr)
    _write_stat(guest, comp, retptr + 8, st)


def wasi_filesystem_types_descriptor_set_times_at(
    guest: Guest, comp: int, self: int, path_flags: int, ptr: int, length: int,
    a_tag: int, a_seconds: int, a_nanos: int, m_tag: int, m_seconds: int, m_nanos: int, retptr: int,
) -> None:  # fmt: skip
    times = _times(a_tag, a_seconds, a_nanos, m_tag, m_seconds, m_nanos)

    def action() -> None:
        d = _descriptor(guest, self)
        path = _path(guest, comp, ptr, length)
        _fs(guest).set_times_at(d, path, path_flags, times, lambda: guest.state.clock(0))

    _status(guest, comp, retptr, action)


def wasi_filesystem_types_descriptor_link_at(
    guest: Guest,
    comp: int,
    self: int,
    old_flags: int,
    old_ptr: int,
    old_len: int,
    new_dir: int,
    new_ptr: int,
    new_len: int,
    retptr: int,
) -> None:
    def action() -> None:
        d, n = _descriptor(guest, self), _descriptor(guest, new_dir)
        _fs(guest).link_at(d, old_flags, _path(guest, comp, old_ptr, old_len), n, _path(guest, comp, new_ptr, new_len))

    _status(guest, comp, retptr, action)


def wasi_filesystem_types_descriptor_open_at(
    guest: Guest, comp: int, self: int, path_flags: int, ptr: int, length: int, open_flags: int, flags: int, retptr: int
) -> None:
    try:
        d = _descriptor(guest, self)
        made = _fs(guest).open_at(d, _path(guest, comp, ptr, length), path_flags, open_flags, flags)
        handle = table_new(guest, KIND_DESCRIPTOR, made)
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    store_u32(guest, comp, retptr + 4, handle)


def wasi_filesystem_types_descriptor_readlink_at(
    guest: Guest, comp: int, self: int, ptr: int, length: int, retptr: int
) -> None:
    try:
        d = _descriptor(guest, self)
        target = _fs(guest).readlink_at(d, _path(guest, comp, ptr, length))
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 4, exc)
        return
    _ok(guest, comp, retptr)
    _string(guest, comp, target, retptr + 4)


def wasi_filesystem_types_descriptor_create_directory_at(
    guest: Guest, comp: int, self: int, ptr: int, length: int, retptr: int
) -> None:
    _status(
        guest,
        comp,
        retptr,
        lambda: _fs(guest).create_directory_at(_descriptor(guest, self), _path(guest, comp, ptr, length)),
    )


def wasi_filesystem_types_descriptor_remove_directory_at(
    guest: Guest, comp: int, self: int, ptr: int, length: int, retptr: int
) -> None:
    _status(
        guest,
        comp,
        retptr,
        lambda: _fs(guest).remove_directory_at(_descriptor(guest, self), _path(guest, comp, ptr, length)),
    )


def wasi_filesystem_types_descriptor_unlink_file_at(
    guest: Guest, comp: int, self: int, ptr: int, length: int, retptr: int
) -> None:
    _status(
        guest,
        comp,
        retptr,
        lambda: _fs(guest).unlink_file_at(_descriptor(guest, self), _path(guest, comp, ptr, length)),
    )


def wasi_filesystem_types_descriptor_rename_at(
    guest: Guest,
    comp: int,
    self: int,
    old_ptr: int,
    old_len: int,
    new_dir: int,
    new_ptr: int,
    new_len: int,
    retptr: int,
) -> None:
    def action() -> None:
        d, n = _descriptor(guest, self), _descriptor(guest, new_dir)
        _fs(guest).rename_at(d, _path(guest, comp, old_ptr, old_len), n, _path(guest, comp, new_ptr, new_len))

    _status(guest, comp, retptr, action)


def wasi_filesystem_types_descriptor_symlink_at(
    guest: Guest, comp: int, self: int, old_ptr: int, old_len: int, new_ptr: int, new_len: int, retptr: int
) -> None:
    target = load_bytes(guest, comp, old_ptr, old_len)
    if b"\0" in target or target.startswith(b"/"):
        error = FsError("invalid" if b"\0" in target else "not-permitted")

        def refuse() -> None:
            _descriptor(guest, self)
            raise error

        _status(guest, comp, retptr, refuse)
        return
    _status(
        guest,
        comp,
        retptr,
        lambda: _fs(guest).symlink_at(_descriptor(guest, self), target, _path(guest, comp, new_ptr, new_len)),
    )


def wasi_filesystem_types_descriptor_is_same_object(guest: Guest, self: int, other: int) -> int:
    return int(_fs(guest).is_same_object(_descriptor(guest, self), _descriptor(guest, other)))


def _fnv(*values: int) -> int:
    """FNV-1a 64 over the values, each as 8 bytes little-endian (the C twin does the same)."""
    h = 0xCBF29CE484222325
    for v in values:
        for byte in (v & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "little"):
            h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def _hash(guest: Guest, comp: int, retptr: int, st: os.stat_result) -> None:
    """`upper` names the object (device, inode); `lower` also takes in what changes when the object is modified (size, modification time)."""
    _ok(guest, comp, retptr)
    store_u64(guest, comp, retptr + 8, _fnv(st.st_size, st.st_mtime_ns, st.st_dev, st.st_ino))
    store_u64(guest, comp, retptr + 16, _fnv(st.st_dev, st.st_ino))


def wasi_filesystem_types_descriptor_metadata_hash(guest: Guest, comp: int, self: int, retptr: int) -> None:
    try:
        st = _fs(guest).stat(_descriptor(guest, self))
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 8, exc)
        return
    _hash(guest, comp, retptr, st)


def wasi_filesystem_types_descriptor_metadata_hash_at(
    guest: Guest, comp: int, self: int, path_flags: int, ptr: int, length: int, retptr: int
) -> None:
    try:
        st = _stat_at(guest, comp, self, path_flags, ptr, length)
    except FsError as exc:
        _fail(guest, comp, retptr, retptr + 8, exc)
        return
    _hash(guest, comp, retptr, st)


def wasi_filesystem_types_filesystem_error_code(guest: Guest, comp: int, err: int, retptr: int) -> None:
    error: Error = table_get(guest, err, KIND_ERROR)  # type: ignore[assignment]
    if error.code is None:
        store_u8(guest, comp, retptr, 0)
    else:
        store_u8(guest, comp, retptr, 1)
        store_u8(guest, comp, retptr + 1, error.code)
