"""The file backend of a host of WASI 0.1: what the calls on files and paths ask of the system (the twin of the `fusedwasi_p1_be_*` functions of `wasi_p1_core.h`).

`Files` answers every request with ENOSYS (a host that has no files uses it); `SystemFiles` is the backend of the system, over the system layer of WASI 0.2
(`fusedwasi.p2.wasi_p2_filesystem`: the walk of a path confined below a directory, and the calls on a name below it, for POSIX and for Windows). Every method returns the errno
of WASI (0 is success) first. A path that is `None` was not in the memory of the module: the answer is FAULT, after the check of the directory.
"""

from __future__ import annotations

import errno as _errno
import os
import stat as _stat
import sys
import time
from dataclasses import dataclass

from ..p2 import wasi_p2_filesystem as fs
from .wasi_p1_core import (
    E_ACCES,
    E_BADF,
    E_EXIST,
    E_FAULT,
    E_FBIG,
    E_INVAL,
    E_IO,
    E_ISDIR,
    E_LOOP,
    E_MFILE,
    E_MLINK,
    E_NAMETOOLONG,
    E_NOENT,
    E_NOMEM,
    E_NOSPC,
    E_NOSYS,
    E_NOTCAPABLE,
    E_NOTDIR,
    E_NOTEMPTY,
    E_NOTSUP,
    E_PERM,
    E_PIPE,
    E_ROFS,
    E_SPIPE,
    E_SUCCESS,
    E_XDEV,
    FT_REGULAR,
    K_DIR,
    K_FILE,
    O_CREAT,
    O_DIRECTORY,
    O_EXCL,
    O_TRUNC,
    R_FD_ALLOCATE,
    R_FD_DATASYNC,
    R_FD_FILESTAT_SET_SIZE,
    R_FD_READ,
    R_FD_WRITE,
    R_OF_DIRECTORY,
    T_ATIM,
    T_ATIM_NOW,
    T_MTIM,
    T_MTIM_NOW,
    Entry,
    Stat,
    errno_of,
    filetype_of,
)

if sys.platform != "win32":
    import fcntl

INT64_MAX = (1 << 63) - 1
PATH_MAX = 4096
LINK_NAMES = {  # the errno of WASI for a code of 0.2 (the host of Wasmtime does the same for 0.1 over 0.2); the walk's not-permitted is a way out, which is not a capability
    "access": E_ACCES, "bad-descriptor": E_BADF, "exist": E_EXIST, "file-too-large": E_FBIG, "invalid": E_INVAL, "io": E_IO, "is-directory": E_ISDIR,
    "loop": E_LOOP, "name-too-long": E_NAMETOOLONG, "too-many-links": E_MLINK, "no-entry": E_NOENT, "insufficient-memory": E_NOMEM, "insufficient-space": E_NOSPC, "not-directory": E_NOTDIR,
    "not-empty": E_NOTEMPTY, "pipe": E_PIPE, "read-only": E_ROFS, "invalid-seek": E_SPIPE, "cross-device": E_XDEV, "unsupported": E_NOSYS,
}  # fmt: skip
_WINERROR = {  # the error of Windows, where there is one, before the errno of the C library
    2: E_NOENT, 3: E_NOENT, 15: E_NOENT, 53: E_NOENT, 67: E_NOENT, 5: E_ACCES, 32: E_ACCES, 33: E_ACCES, 65: E_ACCES, 80: E_EXIST, 183: E_EXIST,
    145: E_NOTEMPTY, 267: E_NOTDIR, 17: E_XDEV, 1314: E_PERM, 1142: E_MLINK, 4: E_MFILE, 112: E_NOSPC, 39: E_NOSPC, 206: E_NAMETOOLONG, 8: E_NOMEM, 14: E_NOMEM, 6: E_BADF,
    19: E_ROFS, 1921: E_LOOP, 4390: E_INVAL, 123: E_INVAL, 161: E_INVAL, 87: E_INVAL, 1113: E_INVAL, 109: E_PIPE, 232: E_PIPE,
}  # fmt: skip


def failure(caught: BaseException, walking: bool = False) -> int:
    """The errno of WASI of a failure: a code of 0.2 by its name, an error of Windows, or the errno of the C library. A way out of the directory (not-permitted) is NOTCAPABLE
    when it comes from a walk and EPERM when it comes from the system."""
    if isinstance(caught, fs.FsError):
        name = fs.sys_error_name[caught.code]
        return (
            E_NOTCAPABLE
            if name == "not-permitted" and walking
            else E_PERM
            if name == "not-permitted"
            else LINK_NAMES.get(name, E_IO)
        )
    if isinstance(caught, OSError):
        winerror = getattr(caught, "winerror", None)
        if winerror:
            return _WINERROR.get(winerror, errno_of(caught.errno) if caught.errno else E_IO)
        return errno_of(caught.errno)
    raise caught


@dataclass
class Listing:
    """One name of a directory as `fd_readdir` gives it."""

    name: bytes
    ino: int
    filetype: int


class Files:
    """A host without files."""

    def preopen(self, host: str, entry: Entry) -> None:
        """Opens the directory of a preopen into the entry; raises `OSError` when it cannot be opened."""
        raise OSError(38, "no files", host)

    def stat_fd(self, entry: Entry) -> tuple[int, Stat | None]:
        return E_NOSYS, None

    def stat_path(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, Stat | None]:
        return E_NOSYS, None

    def open(
        self, directory: Entry, path: bytes | None, follow: bool, oflags: int, base: int, fdflags: int, entry: Entry
    ) -> int:
        return E_NOSYS

    def mkdir(self, directory: Entry, path: bytes | None) -> int:
        return E_NOSYS

    def remove(self, directory: Entry, path: bytes | None, is_directory: bool) -> int:
        return E_NOSYS

    def times_fd(self, entry: Entry, atim: int, mtim: int, flags: int) -> int:
        return E_NOSYS

    def times_path(self, directory: Entry, path: bytes | None, follow: bool, atim: int, mtim: int, flags: int) -> int:
        return E_NOSYS

    def rename(self, source: Entry, old: bytes | None, target: Entry, new: bytes | None) -> int:
        return E_NOSYS

    def link(self, source: Entry, old: bytes | None, follow: bool, target: Entry, new: bytes | None) -> int:
        return E_NOSYS

    def symlink(self, target: bytes, directory: Entry, path: bytes | None) -> int:
        return E_NOSYS

    def readlink(self, directory: Entry, path: bytes | None, capacity: int) -> tuple[int, bytes]:
        return E_NOSYS, b""

    def list(self, entry: Entry) -> tuple[int, list[Listing]]:
        return E_NOSYS, []

    # the calls on an open file descriptor (a file kind of entry)

    def read(self, entry: Entry, length: int) -> tuple[int, bytes]:
        return E_NOSYS, b""

    def write(self, entry: Entry, data: bytes) -> tuple[int, int]:
        return E_NOSYS, 0

    def read_at(self, entry: Entry, length: int, offset: int) -> tuple[int, bytes]:
        return E_NOSYS, b""

    def write_at(self, entry: Entry, data: bytes, offset: int) -> tuple[int, int]:
        return E_NOSYS, 0

    def is_regular(self, entry: Entry) -> bool:
        return True

    def seek(self, entry: Entry, offset: int, whence: int) -> tuple[int, int]:
        return E_NOSYS, 0

    def sync(self, entry: Entry, data_only: bool) -> int:
        return E_NOSYS

    def set_size(self, entry: Entry, size: int) -> int:
        return E_NOSYS

    def allocate(self, entry: Entry, offset: int, length: int) -> int:
        return E_NOSYS

    def set_flags(self, entry: Entry, flags: int) -> int:
        return E_NOSYS

    def close(self, entry: Entry) -> int:
        """Closes the descriptor the entry owns; the errno of the C library of the close, or 0."""
        return 0


def _ns(nanoseconds: int) -> int:
    """A time of a file in nanoseconds since 1970 as WASI has it (unsigned): before 1970 is 0, and past the year 2262 it saturates."""
    if nanoseconds < 0:
        return 0
    if nanoseconds // 1_000_000_000 > INT64_MAX // 1_000_000_000 - 1:
        return INT64_MAX
    return nanoseconds


def stat_of(st: os.stat_result) -> Stat:
    return Stat(
        dev=st.st_dev & 0xFFFFFFFFFFFFFFFF,
        ino=st.st_ino & 0xFFFFFFFFFFFFFFFF,
        nlink=st.st_nlink,
        size=st.st_size,
        atim=_ns(st.st_atime_ns),
        mtim=_ns(st.st_mtime_ns),
        ctim=_ns(st.st_ctime_ns),
        filetype=filetype_of(st.st_mode),
    )


def native_fdflags(fdflags: int) -> int:
    """The fdflags as flags of the system (the ones it has)."""
    flags = 0
    for bit, name in ((1, "O_APPEND"), (2, "O_DSYNC"), (8, "O_RSYNC"), (16, "O_SYNC"), (4, "O_NONBLOCK")):
        if fdflags & bit:
            flags |= getattr(os, name, 0)
    return flags


def _descriptor(entry: Entry) -> fs.Descriptor:
    return fs.Descriptor(entry.fd, 0, entry.kind == K_DIR, entry.path or "")


class SystemFiles(Files):
    """The files of the system: a directory is a descriptor (a path on Windows), a path is walked from it one name at a time by the system layer of WASI 0.2."""

    def preopen(self, host: str, entry: Entry) -> None:
        if not _stat.S_ISDIR(os.stat(host).st_mode):
            raise NotADirectoryError(_errno.ENOTDIR, "not a directory", host)
        made = fs.sys_open_directory(os.path.realpath(host) if sys.platform == "win32" else host, 0)
        entry.fd, entry.path = made.fd, made.path or None

    def close(self, entry: Entry) -> int:
        entry.path = None
        if entry.fd < 0:
            return 0
        try:
            os.close(entry.fd)
        except OSError as caught:
            return caught.errno or _errno.EIO
        return 0

    def stat_fd(self, entry: Entry) -> tuple[int, Stat | None]:
        try:
            return E_SUCCESS, stat_of(fs.sys_fstat(_descriptor(entry)))
        except OSError as caught:
            return failure(caught), None

    def _walked(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, fs.SysWalked | None]:
        """(errno, the walk of the path below the directory, to be used as a context)."""
        if directory.kind != K_DIR:
            return E_NOTDIR, None
        if path is None:
            return E_FAULT, None
        try:
            return E_SUCCESS, fs.SysWalked(fs.sys_ref(_descriptor(directory)), path, follow, True)
        except (OSError, fs.FsError) as caught:
            return failure(caught, walking=True), None

    def stat_path(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, Stat | None]:
        number, walked = self._walked(directory, path, follow)
        if walked is None:
            return number, None
        with walked as (parent, leaf):
            try:
                return E_SUCCESS, stat_of(
                    fs.sys_lstat(parent, leaf)
                )  # a link at the end was expanded by the walk if it was to be
            except OSError as caught:
                return failure(caught), None

    def open(
        self, directory: Entry, path: bytes | None, follow: bool, oflags: int, base: int, fdflags: int, entry: Entry
    ) -> int:
        number, walked = self._walked(directory, path, follow)
        if walked is None:
            return number
        # writing is what the rights say: to write, to sync the data, to allocate, to set the size
        writable = bool(base & (R_FD_WRITE | R_FD_DATASYNC | R_FD_ALLOCATE | R_FD_FILESTAT_SET_SIZE)) or bool(
            oflags & O_TRUNC
        )
        readable = bool(base & R_FD_READ)
        flags = (fs.WRITE if writable else 0) | (fs.READ if readable or not writable else 0)
        if fdflags & (8 | 16):
            flags |= fs.FILE_SYNC
        if fdflags & 2:
            flags |= fs.DATA_SYNC
        open_flags = (
            (fs.CREATE if oflags & O_CREAT else 0)
            | (fs.DIRECTORY if oflags & O_DIRECTORY else 0)
            | (fs.EXCLUSIVE if oflags & O_EXCL else 0)
            | (fs.TRUNCATE if oflags & O_TRUNC else 0)
        )
        with walked as (parent, leaf):
            try:
                try:
                    made = fs.sys_open_at(parent, leaf, open_flags, flags)
                except (OSError, fs.FsError) as caught:
                    if failure(caught) == E_ISDIR and base & R_OF_DIRECTORY and not oflags & (O_CREAT | O_TRUNC):
                        made = fs.sys_open_at(
                            parent, leaf, fs.DIRECTORY, fs.READ
                        )  # it was a directory, and asked for as one
                    else:
                        raise
            except (OSError, fs.FsError) as caught:
                return failure(caught)
        try:
            mode = fs.sys_fstat(made).st_mode
        except OSError as caught:
            if made.fd >= 0:
                os.close(made.fd)
            return failure(caught)
        if sys.platform != "win32" and fdflags & (1 | 4):  # what the open of 0.2 does not take: appending, not blocking
            try:
                current = fcntl.fcntl(made.fd, fcntl.F_GETFL)
                fcntl.fcntl(made.fd, fcntl.F_SETFL, current | native_fdflags(fdflags & (1 | 4)))
            except OSError as caught:
                os.close(made.fd)
                return failure(caught)
        entry.filetype = filetype_of(mode)
        entry.kind = K_DIR if made.is_dir else K_FILE
        entry.fd = made.fd
        entry.path = made.path or None
        entry.owns_fd = True
        return E_SUCCESS

    def _parent(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, fs.SysWalked | None]:
        return self._walked(directory, path, follow)

    def mkdir(self, directory: Entry, path: bytes | None) -> int:
        number, walked = self._walked(directory, path, False)
        if walked is None:
            return number
        with walked as (parent, leaf):
            try:
                if leaf == b".":
                    return E_EXIST
                fs.sys_mkdir(parent, leaf)
                return E_SUCCESS
            except (OSError, fs.FsError) as caught:
                return failure(caught)

    def remove(self, directory: Entry, path: bytes | None, is_directory: bool) -> int:
        number, walked = self._walked(directory, path, False)
        if walked is None:
            return number
        with walked as (parent, leaf):
            try:
                if leaf == b".":
                    return E_INVAL
                if is_directory:
                    fs.sys_rmdir(parent, leaf)
                else:
                    fs.sys_unlink(parent, leaf)
                return E_SUCCESS
            except (OSError, fs.FsError) as caught:
                return failure(
                    caught
                )  # unlink of a directory: EISDIR on Linux, EPERM on macOS; the system's own answer is what is given

    @staticmethod
    def _times(current: os.stat_result, atim: int, mtim: int, flags: int) -> tuple[int, int]:
        """The two times to set: a value, now, or what the file has (a time that is not to be set)."""
        now = time.time_ns()
        a = atim if flags & T_ATIM else now if flags & T_ATIM_NOW else current.st_atime_ns
        m = mtim if flags & T_MTIM else now if flags & T_MTIM_NOW else current.st_mtime_ns
        return a, m

    def times_fd(self, entry: Entry, atim: int, mtim: int, flags: int) -> int:
        made = _descriptor(entry)
        try:
            fs.sys_utime_fd(made, self._times(fs.sys_fstat(made), atim, mtim, flags))
            return E_SUCCESS
        except OSError as caught:
            return failure(caught)

    def times_path(self, directory: Entry, path: bytes | None, follow: bool, atim: int, mtim: int, flags: int) -> int:
        number, walked = self._walked(directory, path, follow)
        if walked is None:
            return number
        with walked as (parent, leaf):
            try:
                current = fs.sys_lstat(parent, leaf)
                times = self._times(current, atim, mtim, flags)
                try:
                    fs.sys_utime(parent, leaf, times)  # a link at the end was expanded by the walk
                except NotImplementedError:  # a platform without it (Emscripten): fine for what is not a link; a link itself cannot be touched there
                    if _stat.S_ISLNK(current.st_mode):
                        return E_NOTSUP
                    os.utime(os.fsdecode(leaf), ns=times, dir_fd=int(parent))
                return E_SUCCESS
            except (OSError, fs.FsError) as caught:
                return failure(caught)

    def _pair(
        self, source: Entry, old: bytes | None, follow: bool, target: Entry, new: bytes | None
    ) -> tuple[int, fs.SysWalked | None, fs.SysWalked | None]:
        number, first = self._walked(source, old, follow)
        if first is None:
            return number, None, None
        number, second = self._walked(target, new, False)
        if second is None:
            first.__exit__()
            return number, None, None
        if first.name == b"." or second.name == b".":  # a directory itself is neither moved nor made a link
            first.__exit__()
            second.__exit__()
            return (E_EXIST if second.name == b"." else E_INVAL), None, None
        return E_SUCCESS, first, second

    def rename(self, source: Entry, old: bytes | None, target: Entry, new: bytes | None) -> int:
        number, first, second = self._pair(source, old, False, target, new)
        if first is None or second is None:
            return number
        with first as (parent1, leaf1), second as (parent2, leaf2):
            try:
                fs.sys_rename(parent1, leaf1, parent2, leaf2)
                return E_SUCCESS
            except (OSError, fs.FsError) as caught:
                return failure(caught)

    @staticmethod
    def _leaf_slash(parent: fs.Ref, leaf: bytes) -> int:
        """A name to be made that has a slash after it (a link): the errno it can only be, or 0 when it has none."""
        if not leaf.endswith(b"/"):
            return 0
        try:
            st = fs.sys_lstat(parent, leaf[:-1])
        except OSError as caught:
            return failure(caught)
        return E_EXIST if _stat.S_ISDIR(st.st_mode) else E_NOTDIR

    def link(self, source: Entry, old: bytes | None, follow: bool, target: Entry, new: bytes | None) -> int:
        number, first, second = self._pair(source, old, follow, target, new)
        if first is None or second is None:
            return number
        with first as (parent1, leaf1), second as (parent2, leaf2):
            bad = self._leaf_slash(parent2, leaf2)
            if bad:
                return bad
            try:
                fs.sys_link(
                    parent1, leaf1, parent2, leaf2
                )  # (the walk expanded a link at the end if it was to be followed)
                return E_SUCCESS
            except (OSError, fs.FsError) as caught:
                return failure(caught)

    def symlink(self, target: bytes, directory: Entry, path: bytes | None) -> int:
        if len(target) >= PATH_MAX:
            return E_NAMETOOLONG
        number, walked = self._walked(directory, path, False)
        if walked is None:
            return number
        with walked as (parent, leaf):
            if leaf == b".":
                return E_EXIST
            bad = self._leaf_slash(parent, leaf)
            if bad:
                return bad
            try:
                fs.sys_symlink(target, parent, leaf)
                return E_SUCCESS
            except (OSError, fs.FsError) as caught:
                return failure(caught)

    def readlink(self, directory: Entry, path: bytes | None, capacity: int) -> tuple[int, bytes]:
        number, walked = self._walked(directory, path, False)
        if walked is None:
            return number, b""
        with walked as (parent, leaf):
            if leaf == b".":
                return E_INVAL, b""
            try:
                return E_SUCCESS, fs.sys_readlink(parent, leaf)[
                    :capacity
                ]  # a buffer of 0 bytes asked with 1 still says whether this is a link
            except OSError as caught:
                return failure(caught), b""

    def list(self, entry: Entry) -> tuple[int, list[Listing]]:
        made = _descriptor(entry)
        ref = fs.sys_ref(made)
        items: list[Listing] = []
        try:
            above = (
                os.stat("..", dir_fd=entry.fd)
                if sys.platform != "win32"
                else os.stat(os.path.dirname(made.path.rstrip("\\")) or made.path)
            )
            items.append(Listing(b".", fs.sys_fstat(made).st_ino, 3))
            items.append(Listing(b"..", above.st_ino, 3))
            for name in fs.sys_listdir(made):
                try:
                    st = fs.sys_lstat(ref, name)
                    items.append(Listing(name, st.st_ino, filetype_of(st.st_mode)))
                except OSError:  # gone since the listing
                    items.append(Listing(name, 0, 0))
        except OSError as caught:
            return failure(caught), []
        return E_SUCCESS, items

    # ---- the calls on an open file descriptor

    def read(self, entry: Entry, length: int) -> tuple[int, bytes]:
        try:
            return E_SUCCESS, os.read(entry.fd, length)
        except OSError as caught:
            return failure(caught), b""

    def write(self, entry: Entry, data: bytes) -> tuple[int, int]:
        try:
            return E_SUCCESS, os.write(entry.fd, data)
        except OSError as caught:
            return failure(caught), 0

    def read_at(self, entry: Entry, length: int, offset: int) -> tuple[int, bytes]:
        try:
            if sys.platform == "win32":  # Windows has no pread: seek, do it, seek back
                saved = os.lseek(entry.fd, 0, os.SEEK_CUR)
                os.lseek(entry.fd, offset, os.SEEK_SET)
                try:
                    return E_SUCCESS, os.read(entry.fd, length)
                finally:
                    os.lseek(entry.fd, saved, os.SEEK_SET)
            return E_SUCCESS, os.pread(entry.fd, length, offset)
        except OSError as caught:
            return failure(caught), b""

    def write_at(self, entry: Entry, data: bytes, offset: int) -> tuple[int, int]:
        try:
            if sys.platform == "win32":
                saved = os.lseek(entry.fd, 0, os.SEEK_CUR)
                os.lseek(entry.fd, offset, os.SEEK_SET)
                try:
                    return E_SUCCESS, os.write(entry.fd, data)
                finally:
                    os.lseek(entry.fd, saved, os.SEEK_SET)
            return E_SUCCESS, os.pwrite(entry.fd, data, offset)
        except OSError as caught:
            return failure(caught), 0

    def is_regular(self, entry: Entry) -> bool:
        try:
            return _stat.S_ISREG(os.fstat(entry.fd).st_mode)
        except OSError:
            return False

    def seek(self, entry: Entry, offset: int, whence: int) -> tuple[int, int]:
        try:
            return E_SUCCESS, os.lseek(entry.fd, offset, (os.SEEK_SET, os.SEEK_CUR, os.SEEK_END)[whence])
        except OSError as caught:
            return failure(caught), 0

    def sync(self, entry: Entry, data_only: bool) -> int:
        try:
            if data_only and sys.platform == "linux":
                os.fdatasync(entry.fd)
            else:
                os.fsync(entry.fd)
            return E_SUCCESS
        except OSError as caught:
            return failure(caught)

    def set_size(self, entry: Entry, size: int) -> int:
        if entry.kind == K_DIR:
            return E_ISDIR
        if entry.kind != K_FILE:
            return E_BADF
        if size > INT64_MAX:
            return E_FBIG
        try:
            os.ftruncate(entry.fd, size)
            return E_SUCCESS
        except OSError as caught:
            return failure(caught)

    def allocate(self, entry: Entry, offset: int, length: int) -> int:
        if sys.platform != "darwin" and hasattr(os, "posix_fallocate"):
            try:  # space is really reserved (and can run out: NOSPC), and a descriptor that is not a file says so (SPIPE)
                os.posix_fallocate(entry.fd, offset, length)
                return E_SUCCESS
            except OSError as caught:
                if caught.errno not in (_errno.EINVAL, _errno.EOPNOTSUPP, _errno.ENODEV):
                    return failure(caught)  # a file system that cannot do it gets the old way
        number, st = self.stat_fd(entry)
        if number or st is None:
            return number
        if sys.platform != "win32" and st.filetype != FT_REGULAR:
            return E_SPIPE  # a pipe: macOS would truncate it without a word
        return E_SUCCESS if st.size >= offset + length else self.set_size(entry, offset + length)

    def set_flags(self, entry: Entry, flags: int) -> int:
        if sys.platform == "win32":
            return E_SUCCESS  # append is done by fd_write (the C library of Windows cannot take it off again)
        try:
            current = fcntl.fcntl(entry.fd, fcntl.F_GETFL)
            every = native_fdflags(1 | 2 | 4 | 8 | 16)
            fcntl.fcntl(entry.fd, fcntl.F_SETFL, (current & ~every) | native_fdflags(flags))
            return E_SUCCESS
        except OSError as caught:
            return failure(caught)
