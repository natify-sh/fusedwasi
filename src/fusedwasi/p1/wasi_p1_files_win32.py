"""The file backend of Windows (the twin of `wasi_p1_files_win32.h`): a directory is a path (`root`, the real path of the directory it is under, and `rel`, its place below it,
with '/' between names), a file is an open descriptor of the C library with its path kept for the calls that need one. A name is resolved by hand below the directory (no
"..", no absolute name), and a link that points out of the directory is refused by comparing the real paths of what is used and of the directory. The check and the use
are two steps, so another process that changes the directory in between is not stopped: what this backend cannot do and `openat` can.
"""

from __future__ import annotations

import os
import stat as _stat
import sys
import time

if sys.platform == "win32":
    from .wasi_p1_core import (
        E_ACCES,
        E_BADF,
        E_EXIST,
        E_FAULT,
        E_FBIG,
        E_INVAL,
        E_ISDIR,
        E_LOOP,
        E_MFILE,
        E_MLINK,
        E_NAMETOOLONG,
        E_NOENT,
        E_NOMEM,
        E_NOSPC,
        E_NOTCAPABLE,
        E_NOTDIR,
        E_NOTEMPTY,
        E_PERM,
        E_PIPE,
        E_ROFS,
        E_SUCCESS,
        E_XDEV,
        FT_DIR,
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
    from .wasi_p1_files import Files, Listing

    NAME_MAX = 255
    INT64_MAX = (1 << 63) - 1
    _FIRST_DIRECTORY = 1 << 30  # the numbers of directories, which are not descriptors of the C library

    _WINERROR = {
        2: E_NOENT,
        3: E_NOENT,
        15: E_NOENT,
        53: E_NOENT,
        67: E_NOENT,
        5: E_ACCES,
        32: E_ACCES,
        33: E_ACCES,
        65: E_ACCES,
        80: E_EXIST,
        183: E_EXIST,
        145: E_NOTEMPTY,
        267: E_NOTDIR,
        17: E_XDEV,
        1314: E_PERM,
        1142: E_MLINK,
        4: E_MFILE,
        112: E_NOSPC,
        39: E_NOSPC,
        206: E_NAMETOOLONG,
        8: E_NOMEM,
        14: E_NOMEM,
        6: E_BADF,
        19: E_ROFS,
        1921: E_LOOP,
        4390: E_INVAL,
        123: E_INVAL,
        161: E_INVAL,
        87: E_INVAL,
        1113: E_INVAL,
        109: E_PIPE,
        232: E_PIPE,
    }

    def failure(caught: OSError) -> int:
        """The errno of WASI of an error: the error of Windows when there is one, else the errno of the C library."""
        winerror = getattr(caught, "winerror", None)
        if winerror:
            return _WINERROR.get(winerror, errno_of(caught.errno) if caught.errno else 29)
        return errno_of(caught.errno)

    def _ns(nanoseconds: int) -> int:
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

    def stat_at(full: str, follow: bool) -> tuple[int, os.stat_result | None]:
        try:
            return E_SUCCESS, os.stat(full) if follow else os.lstat(full)
        except OSError as caught:
            return failure(caught), None

    def _inside(root: str, real: str) -> bool:
        """Whether `real` is `root` or below it; Windows does not tell the case of letters apart."""
        a, b = os.path.normcase(root).rstrip("\\"), os.path.normcase(real)
        return b == a or b.startswith(a + "\\")

    def contained(root: str, rel: str, follow: bool) -> bool:
        """Whether what `rel` names below `root` is inside it, links followed (`follow`: the last name too). A name that does not exist yet is judged by the directory that
        holds it; a link that leads nowhere leads nowhere out: what follows it fails, and open() will not create through it."""
        try:
            real_root = os.path.realpath(root, strict=True)
        except OSError:
            return False
        names = [n for n in rel.split("/") if n]
        if not follow and names:
            names.pop()
        while True:
            path = os.path.join(root, *names) if names else root
            try:
                return _inside(real_root, os.path.realpath(path, strict=True))
            except FileNotFoundError:
                if os.path.islink(path) or not names:
                    return True
                names.pop()
            except OSError:
                return False

    def _times(current: os.stat_result, atim: int, mtim: int, flags: int) -> tuple[int, int]:
        now = time.time_ns()
        a = atim if flags & T_ATIM else now if flags & T_ATIM_NOW else current.st_atime_ns
        m = mtim if flags & T_MTIM else now if flags & T_MTIM_NOW else current.st_mtime_ns
        return a, m

    class Win32Files(Files):
        def __init__(self) -> None:
            self._next = _FIRST_DIRECTORY
            self._dirs: dict[int, tuple[str, str]] = {}  # number of a directory -> (root, rel)
            self._paths: dict[int, str] = {}  # descriptor of a file -> its path

        def _new_directory(self, root: str, rel: str) -> int:
            self._next += 1
            self._dirs[self._next] = (root, rel)
            return self._next

        def _full(self, entry: Entry) -> str:
            root, rel = self._dirs[entry.fd] if entry.kind == K_DIR else (self._paths[entry.fd], "")
            return os.path.join(root, *[n for n in rel.split("/") if n]) if rel else root

        def preopen(self, host: str) -> int:
            if not os.path.isdir(host):
                raise OSError(2, "not a directory", host)
            return self._new_directory(os.path.realpath(host), "")

        def close(self, entry: Entry) -> int:
            if entry.kind == K_DIR:
                self._dirs.pop(entry.fd, None)
                return 0
            self._paths.pop(entry.fd, None)
            try:
                os.close(entry.fd)
            except OSError as caught:
                return caught.errno or 5
            return 0

        # ---- names

        def resolve(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, str, str, bool]:
            """The path the program gave, below the directory: (errno, its place in the real file system, its place below the root, whether it ended with a slash)."""
            if directory.kind != K_DIR:
                return E_NOTDIR, "", "", False
            if path is None:
                return E_FAULT, "", "", False
            if len(path) == 0:
                return E_NOENT, "", "", False
            if b"\0" in path:
                return E_INVAL, "", "", False
            if path[:1] in (b"/", b"\\") or (len(path) >= 2 and path[1:2] == b":"):
                return E_NOTCAPABLE, "", "", False
            try:
                text = path.decode("utf-8")
            except UnicodeDecodeError:
                return E_INVAL, "", "", False
            root, base = self._dirs[directory.fd]
            joined = (base + "/" + text.replace("\\", "/")) if base else text.replace("\\", "/")
            names: list[str] = []
            for name in joined.split("/"):
                if name == "..":
                    if not names:
                        return E_NOTCAPABLE, "", "", False
                    names.pop()
                elif name and name != ".":
                    if len(name.encode("utf-8")) > NAME_MAX:
                        return E_NAMETOOLONG, "", "", False
                    names.append(name)
            rel = "/".join(names)
            if names and not contained(root, rel, follow):
                return E_NOTCAPABLE, "", "", False
            full = os.path.join(root, *names) if names else root
            return E_SUCCESS, full, rel, bool(names) and text.endswith(("/", "\\"))

        def _name(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, str, bool]:
            """A name to be changed: (errno, its path, whether it ended with a slash); the directory itself is INVAL."""
            number, full, rel, slash = self.resolve(directory, path, follow)
            if number:
                return number, "", False
            return (E_SUCCESS if rel else E_INVAL), full, slash

        # ---- stat

        def stat_fd(self, entry: Entry) -> tuple[int, Stat | None]:
            try:
                if entry.kind == K_DIR:
                    return E_SUCCESS, stat_of(os.stat(self._full(entry)))
                return E_SUCCESS, stat_of(os.fstat(entry.fd))
            except OSError as caught:
                return failure(caught), None

        def stat_path(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, Stat | None]:
            number, full, _rel, slash = self.resolve(directory, path, follow)
            if number:
                return number, None
            number, st = stat_at(full, follow)
            if number or st is None:
                return number, None
            if slash and not _stat.S_ISDIR(st.st_mode):
                return E_NOTDIR, None  # a name with a slash after it is a directory
            return E_SUCCESS, stat_of(st)

        # ---- open

        def open(
            self, directory: Entry, path: bytes | None, follow: bool, oflags: int, base: int, fdflags: int, entry: Entry
        ) -> int:
            number, full, _rel, slash = self.resolve(directory, path, follow)
            if number:
                return number
            number, info = stat_at(full, follow)
            exists = True
            if number == E_NOENT and oflags & O_CREAT:
                if follow:
                    again, link = stat_at(full, False)
                    if again == E_SUCCESS and link is not None and _stat.S_ISLNK(link.st_mode):
                        return E_NOTCAPABLE  # a file would be made where the link leads, which is not known to be inside the directory
                exists = False
                number = E_SUCCESS
            if number:
                return number
            writable = bool(base & (R_FD_WRITE | R_FD_DATASYNC | R_FD_ALLOCATE | R_FD_FILESTAT_SET_SIZE)) or bool(
                oflags & O_TRUNC
            )
            flags = (os.O_RDWR if base & R_FD_READ else os.O_WRONLY) if writable else os.O_RDONLY
            if oflags & O_CREAT:
                flags |= os.O_CREAT
            if oflags & O_EXCL:
                flags |= os.O_EXCL
            if oflags & O_TRUNC:
                flags |= os.O_TRUNC
            if exists and info is not None:
                if not follow and _stat.S_ISLNK(info.st_mode):
                    return E_LOOP  # what O_NOFOLLOW says
                if oflags & O_CREAT and oflags & O_EXCL:
                    return E_EXIST
                if slash and not _stat.S_ISDIR(info.st_mode):
                    return E_NOTDIR
                if _stat.S_ISDIR(
                    info.st_mode
                ):  # a directory is its own root: it cannot reach what is above it, as with openat
                    as_directory = bool(base & R_OF_DIRECTORY) and not oflags & (O_CREAT | O_TRUNC)
                    if not as_directory and (flags & (os.O_WRONLY | os.O_RDWR | os.O_TRUNC) or oflags & O_CREAT):
                        return E_ISDIR
                    entry.filetype = FT_DIR
                    entry.kind = K_DIR
                    entry.fd = self._new_directory(full, "")
                    entry.owns_fd = True
                    return E_SUCCESS
            elif slash:
                return E_ISDIR  # a file cannot be made with a slash after its name
            if oflags & O_DIRECTORY:
                return E_NOTDIR
            try:
                fd = os.open(full, flags | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOINHERIT", 0), 0o666)
            except OSError as caught:
                return failure(caught)
            self._paths[fd] = full
            entry.filetype = FT_REGULAR if not exists or info is None else filetype_of(info.st_mode)
            entry.kind = K_FILE
            entry.fd = fd
            entry.owns_fd = True
            return E_SUCCESS

        # ---- changing the directory

        def mkdir(self, directory: Entry, path: bytes | None) -> int:
            number, full, _slash = self._name(directory, path, False)
            if number:
                return E_EXIST if number == E_INVAL else number
            try:
                os.mkdir(full)
                return E_SUCCESS
            except OSError as caught:
                return failure(caught)

        def remove(self, directory: Entry, path: bytes | None, is_directory: bool) -> int:
            number, full, slash = self._name(directory, path, False)
            if number:
                return number
            number, info = stat_at(full, False)
            if number or info is None:
                return number
            try:
                if is_directory:  # rmdir: a directory and nothing else (a link to one is a link)
                    if not _stat.S_ISDIR(info.st_mode):
                        return E_NOTDIR
                    os.rmdir(full)
                else:  # unlink: the systems differ in what they say to a directory
                    if _stat.S_ISDIR(info.st_mode):
                        return E_ISDIR
                    if slash:
                        return E_NOTDIR
                    if _stat.S_ISLNK(info.st_mode) and os.path.isdir(full):
                        os.rmdir(full)  # a link to a directory is removed as one
                    else:
                        os.unlink(full)
                return E_SUCCESS
            except OSError as caught:
                return failure(caught)

        # ---- times

        def _set_times(self, full: str, follow: bool, atim: int, mtim: int, flags: int) -> int:
            try:
                current = os.stat(full) if follow else os.lstat(full)
                os.utime(full, ns=_times(current, atim, mtim, flags))
                return E_SUCCESS
            except NotImplementedError:
                return E_INVAL
            except OSError as caught:
                return failure(caught)

        def times_fd(self, entry: Entry, atim: int, mtim: int, flags: int) -> int:
            return self._set_times(self._full(entry), True, atim, mtim, flags)

        def times_path(
            self, directory: Entry, path: bytes | None, follow: bool, atim: int, mtim: int, flags: int
        ) -> int:
            number, full, _rel, _slash = self.resolve(directory, path, follow)
            return number or self._set_times(full, follow, atim, mtim, flags)

        # ---- rename, link, symlink, readlink

        def _two(
            self, source: Entry, old: bytes | None, follow: bool, target: Entry, new: bytes | None
        ) -> tuple[int, str, str, bool, bool]:
            number, first, slash1 = self._name(source, old, follow)
            if number:
                return number, "", "", False, False
            number, second, slash2 = self._name(target, new, False)
            if number:
                return number, "", "", False, False
            return E_SUCCESS, first, second, slash1, slash2

        def rename(self, source: Entry, old: bytes | None, target: Entry, new: bytes | None) -> int:
            number, first, second, slash1, slash2 = self._two(source, old, False, target, new)
            if number:
                return number
            have_from, src = stat_at(first, False)
            _, dst = stat_at(second, False)
            if have_from or src is None:
                return have_from
            from_dir = _stat.S_ISDIR(src.st_mode)
            to_dir = dst is not None and _stat.S_ISDIR(dst.st_mode)
            if (slash1 or slash2) and not from_dir:
                return E_NOTDIR  # a slash after the name asks for a directory
            if dst is not None:
                if slash2 and not to_dir:
                    return E_NOTDIR
                if from_dir and not to_dir:
                    return E_NOTDIR
                if not from_dir and to_dir:
                    return E_ISDIR
            try:
                # POSIX replaces an empty directory by a directory; Windows does not, so it is taken away first (it says NOTEMPTY if it is not)
                if dst is not None and to_dir and from_dir and (src.st_dev, src.st_ino) != (dst.st_dev, dst.st_ino):
                    os.rmdir(second)
                os.replace(first, second)
                return E_SUCCESS
            except OSError as caught:
                return failure(caught)

        def link(self, source: Entry, old: bytes | None, follow: bool, target: Entry, new: bytes | None) -> int:
            number, first, second, _slash1, slash2 = self._two(source, old, follow, target, new)
            if number:
                return number
            number, info = stat_at(first, follow)  # a link at the end is followed: it has to lead somewhere
            if number or info is None:
                return number
            if _stat.S_ISDIR(info.st_mode):
                return E_PERM  # a hard link to a directory is not made
            if slash2:
                number, there = stat_at(second, False)
                return number if number or there is None else (E_EXIST if _stat.S_ISDIR(there.st_mode) else E_NOTDIR)
            try:
                os.link(first, second)
                return E_SUCCESS
            except OSError as caught:
                return failure(caught)

        def symlink(self, target: bytes, directory: Entry, path: bytes | None) -> int:
            number, full, slash = self._name(directory, path, False)
            if number:
                return E_EXIST if number == E_INVAL else number
            number, info = stat_at(full, False)
            if number == E_SUCCESS and info is not None:  # the name is taken
                return (E_EXIST if _stat.S_ISDIR(info.st_mode) else E_NOTDIR) if slash else E_EXIST
            if slash or len(target) == 0:
                return E_NOENT
            try:
                text = target.decode("utf-8")
            except UnicodeDecodeError:
                return E_INVAL
            text = text.replace("/", "\\")  # a link made with a slash does not work on Windows
            # a link to a directory has to be made as one: the target is looked for from the directory of the link
            is_directory = not target.startswith(b"/") and os.path.isdir(os.path.join(os.path.dirname(full), text))
            try:
                os.symlink(text, full, target_is_directory=is_directory)
                return E_SUCCESS
            except OSError as caught:
                return failure(caught)

        def readlink(self, directory: Entry, path: bytes | None, capacity: int) -> tuple[int, bytes]:
            number, full, _slash = self._name(directory, path, False)
            if number:
                return number, b""
            try:
                text = os.readlink(full)
            except OSError as caught:
                return failure(caught), b""
            return E_SUCCESS, text.replace("\\", "/").encode("utf-8")[:capacity]

        def list(self, entry: Entry) -> tuple[int, list[Listing]]:
            full = self._full(entry)
            items: list[Listing] = []
            try:
                items.append(Listing(b".", os.stat(full).st_ino, 3))
                up = os.path.dirname(full)
                items.append(Listing(b"..", os.stat(up or full).st_ino, 3))
                with os.scandir(full) as names:
                    for item in names:
                        try:
                            st = item.stat(follow_symlinks=False)
                        except OSError:
                            continue  # gone since the listing was made: it is left out
                        items.append(Listing(item.name.encode("utf-8"), st.st_ino, filetype_of(st.st_mode)))
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
            try:  # Windows has no pread: seek, do it, seek back
                saved = os.lseek(entry.fd, 0, os.SEEK_CUR)
                os.lseek(entry.fd, offset, os.SEEK_SET)
                try:
                    return E_SUCCESS, os.read(entry.fd, length)
                finally:
                    os.lseek(entry.fd, saved, os.SEEK_SET)
            except OSError as caught:
                return failure(caught), b""

        def write_at(self, entry: Entry, data: bytes, offset: int) -> tuple[int, int]:
            try:
                saved = os.lseek(entry.fd, 0, os.SEEK_CUR)
                os.lseek(entry.fd, offset, os.SEEK_SET)
                try:
                    return E_SUCCESS, os.write(entry.fd, data)
                finally:
                    os.lseek(entry.fd, saved, os.SEEK_SET)
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
            number, st = self.stat_fd(entry)
            if number or st is None:
                return number
            return E_SUCCESS if st.size >= offset + length else self.set_size(entry, offset + length)

        def set_flags(self, entry: Entry, flags: int) -> int:
            return E_SUCCESS  # append is done by fd_write (the C library of Windows cannot take it off again)
