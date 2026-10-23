"""The file backend of POSIX (the twin of the POSIX part of `wasi_p1_core.h`): a directory is an open descriptor, a path is walked from it one name at a time, and a symbolic
link met on the way is read and its target put in front of the rest of the path (never an absolute one): nothing can lead out of the preopen, and there is no gap between a
check and a use.
"""

from __future__ import annotations

import errno as _errno
import os
import stat as _stat
import sys
import time

if sys.platform != "win32":
    import fcntl

    from .wasi_p1_core import (
        E_BADF,
        E_EXIST,
        E_FAULT,
        E_FBIG,
        E_INVAL,
        E_ISDIR,
        E_LOOP,
        E_NAMETOOLONG,
        E_NOENT,
        E_NOTCAPABLE,
        E_NOTDIR,
        E_SPIPE,
        E_SUCCESS,
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

    PATH_MAX = 4096
    NAME_MAX = 255
    MAX_DEPTH = 64
    MAX_LINKS = 40
    INT64_MAX = (1 << 63) - 1
    _OPEN_DIR = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | getattr(os, "O_CLOEXEC", 0)

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

    def open_flags(base: int, oflags: int, fdflags: int) -> int:
        """The flags of open(2) that a path_open asks for: writing is what the rights say (to write, to sync the data, to allocate, to set the size)."""
        writable = bool(base & (R_FD_WRITE | R_FD_DATASYNC | R_FD_ALLOCATE | R_FD_FILESTAT_SET_SIZE)) or bool(
            oflags & O_TRUNC
        )
        readable = bool(base & R_FD_READ)
        flags = (os.O_RDWR if readable else os.O_WRONLY) if writable else os.O_RDONLY
        if oflags & O_CREAT:
            flags |= os.O_CREAT
        if oflags & O_EXCL:
            flags |= os.O_EXCL
        if oflags & O_TRUNC:
            flags |= os.O_TRUNC
        return flags | native_fdflags(fdflags)

    def walk(base: int, given: bytes, follow: bool) -> tuple[int, int, bool, bytes]:
        """Walks `given` from the directory `base`: (errno, the directory that holds the last name, whether it is a new descriptor to close, that name).

        The name is "." for a path that names a directory itself. When `follow` is set a last name that is a link is expanded too."""
        if len(given) == 0:
            return E_NOENT, -1, False, b""
        if len(given) >= PATH_MAX:
            return E_NAMETOOLONG, -1, False, b""
        if b"\0" in given:
            return E_INVAL, -1, False, b""
        if given[:1] == b"/":
            return E_NOTCAPABLE, -1, False, b""
        work = given
        fds = [base]
        links = 0
        cur = 0
        leaf = b"."

        def done(number: int) -> tuple[int, int, bool, bytes]:
            for fd in fds[1:]:
                os.close(fd)
            return number, -1, False, b""

        while True:
            while cur < len(work) and work[cur : cur + 1] == b"/":
                cur += 1
            if cur >= len(work):  # nothing left: the path named a directory (it ended with a slash)
                leaf = b"."
                break
            end = work.find(b"/", cur)
            clen = (end if end >= 0 else len(work)) - cur
            rest = cur + clen
            while rest < len(work) and work[rest : rest + 1] == b"/":
                rest += 1
            last = rest >= len(work)
            slash = last and end >= 0  # the path ended with a slash: what it names has to be a directory
            if clen > NAME_MAX:
                return done(E_NAMETOOLONG)
            name = work[cur : cur + clen]
            if name == b".":
                if last:
                    leaf = b"."
                    break
                cur = rest
                continue
            if name == b"..":
                if len(fds) == 1:  # out of the directory it was given
                    return done(E_NOTCAPABLE)
                os.close(fds.pop())
                if last:
                    leaf = b"."
                    break
                cur = rest
                continue
            expand = False
            if last:
                if not follow:
                    # A slash after a name makes the kernel follow a link there whatever O_NOFOLLOW says, and the walk would hand over a way out of the preopen:
                    # a link with a slash and no symlink_follow is not a directory.
                    if slash:
                        try:
                            os.readlink(name, dir_fd=fds[-1])
                        except OSError:
                            pass
                        else:
                            return done(E_NOTDIR)
                    leaf = name + (b"/" if slash else b"")
                    break
                expand = True  # a link at the end is read before it is used
            else:
                if len(fds) - 1 >= MAX_DEPTH:
                    return done(E_NAMETOOLONG)
                try:
                    fds.append(os.open(name, _OPEN_DIR, dir_fd=fds[-1]))
                    cur = rest
                    continue
                except OSError as caught:
                    if caught.errno not in (_errno.ELOOP, _errno.ENOTDIR, _errno.EMLINK):
                        return done(errno_of(caught.errno))
                expand = True  # not a directory we can enter: perhaps a link
            if expand:
                try:
                    target = os.readlink(name, dir_fd=fds[-1])[: PATH_MAX - 1]
                except OSError:
                    if last:  # not a link: this is the last name
                        leaf = name + (b"/" if slash else b"")
                        break
                    return done(E_NOTDIR)  # a name in the middle that is not a directory, nor a link
                links += 1
                if links > MAX_LINKS:
                    return done(E_LOOP)
                if target[:1] == b"/":
                    return done(E_NOTCAPABLE)
                tail = work[rest:]
                if len(target) + 1 + len(tail) >= PATH_MAX:
                    return done(E_NAMETOOLONG)
                work = target + (b"/" + tail if tail or slash else b"")  # a slash only where the path had one
                cur = 0
        for fd in fds[1:-1]:
            os.close(fd)
        return E_SUCCESS, fds[-1], len(fds) > 1, leaf

    class PosixFiles(Files):
        def preopen(self, host: str) -> int:
            return os.open(host, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_CLOEXEC", 0))

        def stat_fd(self, entry: Entry) -> tuple[int, Stat | None]:
            try:
                return E_SUCCESS, stat_of(os.fstat(entry.fd))
            except OSError as caught:
                return errno_of(caught.errno), None

        def _parent(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, int, bool, bytes, bool]:
            """(errno, parent, owned, leaf, whether the path names the directory itself)."""
            if directory.kind != K_DIR:
                return E_NOTDIR, -1, False, b"", False
            if path is None:
                return E_FAULT, -1, False, b"", False
            number, parent, owned, leaf = walk(directory.fd, path, follow)
            return number, parent, owned, leaf, number == E_SUCCESS and leaf == b"."

        def stat_path(self, directory: Entry, path: bytes | None, follow: bool) -> tuple[int, Stat | None]:
            if directory.kind != K_DIR:
                return E_NOTDIR, None
            if path is None:
                return E_FAULT, None
            number, parent, owned, leaf = walk(directory.fd, path, follow)
            if number:
                return number, None
            try:
                return E_SUCCESS, stat_of(
                    os.stat(leaf, dir_fd=parent, follow_symlinks=False)
                )  # a link at the end was expanded by the walk if it was to be
            except OSError as caught:
                return errno_of(caught.errno), None
            finally:
                if owned:
                    os.close(parent)

        def open(
            self, directory: Entry, path: bytes | None, follow: bool, oflags: int, base: int, fdflags: int, entry: Entry
        ) -> int:
            if directory.kind != K_DIR:
                return E_NOTDIR
            if path is None:
                return E_FAULT
            number, parent, owned, leaf = walk(directory.fd, path, follow)
            if number:
                return number
            flags = open_flags(base, oflags, fdflags) | os.O_NOFOLLOW | getattr(os, "O_CLOEXEC", 0)
            if oflags & O_DIRECTORY:
                flags |= os.O_DIRECTORY
            try:
                try:
                    fd = os.open(leaf, flags, 0o666, dir_fd=parent)
                except OSError as caught:
                    if caught.errno == _errno.EISDIR and base & R_OF_DIRECTORY and not oflags & (O_CREAT | O_TRUNC):
                        fd = os.open(leaf, _OPEN_DIR, dir_fd=parent)  # it was a directory, and asked for as one
                    else:
                        raise
            except OSError as caught:
                return errno_of(caught.errno)
            finally:
                if owned:
                    os.close(parent)
            try:
                mode = os.fstat(fd).st_mode
            except OSError as caught:
                os.close(fd)
                return errno_of(caught.errno)
            entry.filetype = filetype_of(mode)
            entry.kind = K_DIR if entry.filetype == FT_DIR else K_FILE
            entry.fd = fd
            entry.owns_fd = True
            return E_SUCCESS

        def mkdir(self, directory: Entry, path: bytes | None) -> int:
            number, parent, owned, leaf, itself = self._parent(directory, path, False)
            if number:
                return number
            try:
                if itself:
                    return E_EXIST
                os.mkdir(leaf, 0o777, dir_fd=parent)
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)
            finally:
                if owned:
                    os.close(parent)

        def remove(self, directory: Entry, path: bytes | None, is_directory: bool) -> int:
            number, parent, owned, leaf, itself = self._parent(directory, path, False)
            if number:
                return number
            try:
                if itself:
                    return E_INVAL
                if is_directory:
                    os.rmdir(leaf, dir_fd=parent)
                else:
                    os.unlink(leaf, dir_fd=parent)
                return E_SUCCESS
            except OSError as caught:
                return errno_of(
                    caught.errno
                )  # unlink of a directory: EISDIR on Linux, EPERM on macOS; the system's own answer is what is given
            finally:
                if owned:
                    os.close(parent)

        @staticmethod
        def _times(current: os.stat_result, atim: int, mtim: int, flags: int) -> tuple[int, int]:
            """The two times to set: a value, now, or what the file has (a time that is not to be set)."""
            now = time.time_ns()
            a = atim if flags & T_ATIM else now if flags & T_ATIM_NOW else current.st_atime_ns
            m = mtim if flags & T_MTIM else now if flags & T_MTIM_NOW else current.st_mtime_ns
            return a, m

        def times_fd(self, entry: Entry, atim: int, mtim: int, flags: int) -> int:
            try:
                os.utime(entry.fd, ns=self._times(os.fstat(entry.fd), atim, mtim, flags))
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)

        def times_path(
            self, directory: Entry, path: bytes | None, follow: bool, atim: int, mtim: int, flags: int
        ) -> int:
            if directory.kind != K_DIR:
                return E_NOTDIR
            if path is None:
                return E_FAULT
            number, parent, owned, leaf = walk(directory.fd, path, follow)
            if number:
                return number
            try:
                current = os.stat(leaf, dir_fd=parent, follow_symlinks=False)
                os.utime(
                    leaf, ns=self._times(current, atim, mtim, flags), dir_fd=parent, follow_symlinks=False
                )  # a link at the end was expanded by the walk
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)
            finally:
                if owned:
                    os.close(parent)

        def _pair(
            self, source: Entry, old: bytes | None, follow: bool, target: Entry, new: bytes | None
        ) -> tuple[int, int, bool, bytes, int, bool, bytes]:
            number, parent1, owned1, leaf1, itself1 = self._parent(source, old, follow)
            if number:
                return number, -1, False, b"", -1, False, b""
            number, parent2, owned2, leaf2, itself2 = self._parent(target, new, False)
            if number:
                if owned1:
                    os.close(parent1)
                return number, -1, False, b"", -1, False, b""
            if itself1 or itself2:  # a directory itself is neither moved nor made a link
                if owned1:
                    os.close(parent1)
                if owned2:
                    os.close(parent2)
                return (E_EXIST if itself2 else E_INVAL), -1, False, b"", -1, False, b""
            return E_SUCCESS, parent1, owned1, leaf1, parent2, owned2, leaf2

        def rename(self, source: Entry, old: bytes | None, target: Entry, new: bytes | None) -> int:
            number, parent1, owned1, leaf1, parent2, owned2, leaf2 = self._pair(source, old, False, target, new)
            if number:
                return number
            try:
                os.rename(leaf1, leaf2, src_dir_fd=parent1, dst_dir_fd=parent2)
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)
            finally:
                if owned1:
                    os.close(parent1)
                if owned2:
                    os.close(parent2)

        @staticmethod
        def _leaf_slash(parent: int, leaf: bytes) -> int:
            """A name to be made that has a slash after it (a link): the errno it can only be, or 0 when it has none."""
            if not leaf.endswith(b"/"):
                return 0
            bare = leaf[:-1]
            try:
                st = os.stat(bare, dir_fd=parent, follow_symlinks=False)
            except OSError as caught:
                return errno_of(caught.errno)
            return E_EXIST if _stat.S_ISDIR(st.st_mode) else E_NOTDIR

        def link(self, source: Entry, old: bytes | None, follow: bool, target: Entry, new: bytes | None) -> int:
            number, parent1, owned1, leaf1, parent2, owned2, leaf2 = self._pair(source, old, follow, target, new)
            if number:
                return number
            try:
                bad = self._leaf_slash(parent2, leaf2)
                if bad:
                    return bad
                os.link(
                    leaf1, leaf2, src_dir_fd=parent1, dst_dir_fd=parent2, follow_symlinks=False
                )  # (the walk expanded a link at the end if it was to be followed)
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)
            finally:
                if owned1:
                    os.close(parent1)
                if owned2:
                    os.close(parent2)

        def symlink(self, target: bytes, directory: Entry, path: bytes | None) -> int:
            if len(target) >= PATH_MAX:
                return E_NAMETOOLONG
            number, parent, owned, leaf, itself = self._parent(directory, path, False)
            if number:
                return number
            try:
                if itself:
                    return E_EXIST
                bad = self._leaf_slash(parent, leaf)
                if bad:
                    return bad
                os.symlink(target, leaf, dir_fd=parent)
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)
            finally:
                if owned:
                    os.close(parent)

        def readlink(self, directory: Entry, path: bytes | None, capacity: int) -> tuple[int, bytes]:
            number, parent, owned, leaf, itself = self._parent(directory, path, False)
            if number:
                return number, b""
            try:
                if itself:
                    return E_INVAL, b""
                text = os.readlink(leaf, dir_fd=parent)
                return E_SUCCESS, text[:capacity]  # a buffer of 0 bytes asked with 1 still says whether this is a link
            except OSError as caught:
                return errno_of(caught.errno), b""
            finally:
                if owned:
                    os.close(parent)

        def list(self, entry: Entry) -> tuple[int, list[Listing]]:
            items: list[Listing] = []
            try:
                items.append(Listing(b".", os.fstat(entry.fd).st_ino, 3))
                items.append(Listing(b"..", os.stat("..", dir_fd=entry.fd).st_ino, 3))
                with os.scandir(entry.fd) as names:
                    for item in names:
                        try:
                            kind = filetype_of(item.stat(follow_symlinks=False).st_mode)
                        except OSError:
                            kind = 0
                        items.append(Listing(os.fsencode(item.name), item.inode(), kind))
            except OSError as caught:
                return errno_of(caught.errno), []
            return E_SUCCESS, items

        # ---- the calls on an open file descriptor

        def read(self, entry: Entry, length: int) -> tuple[int, bytes]:
            try:
                return E_SUCCESS, os.read(entry.fd, length)
            except OSError as caught:
                return errno_of(caught.errno), b""

        def write(self, entry: Entry, data: bytes) -> tuple[int, int]:
            try:
                return E_SUCCESS, os.write(entry.fd, data)
            except OSError as caught:
                return errno_of(caught.errno), 0

        def read_at(self, entry: Entry, length: int, offset: int) -> tuple[int, bytes]:
            try:
                return E_SUCCESS, os.pread(entry.fd, length, offset)
            except OSError as caught:
                return errno_of(caught.errno), b""

        def write_at(self, entry: Entry, data: bytes, offset: int) -> tuple[int, int]:
            try:
                return E_SUCCESS, os.pwrite(entry.fd, data, offset)
            except OSError as caught:
                return errno_of(caught.errno), 0

        def is_regular(self, entry: Entry) -> bool:
            try:
                return _stat.S_ISREG(os.fstat(entry.fd).st_mode)
            except OSError:
                return False

        def seek(self, entry: Entry, offset: int, whence: int) -> tuple[int, int]:
            try:
                return E_SUCCESS, os.lseek(entry.fd, offset, (os.SEEK_SET, os.SEEK_CUR, os.SEEK_END)[whence])
            except OSError as caught:
                return errno_of(caught.errno), 0

        def sync(self, entry: Entry, data_only: bool) -> int:
            try:
                if data_only and sys.platform == "linux":
                    os.fdatasync(entry.fd)
                else:
                    os.fsync(entry.fd)
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)

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
                return errno_of(caught.errno)

        def allocate(self, entry: Entry, offset: int, length: int) -> int:
            if (
                sys.platform != "darwin"
            ):  # space is really reserved (and can run out: NOSPC), and a descriptor that is not a file says so (SPIPE)
                try:
                    os.posix_fallocate(entry.fd, offset, length)
                    return E_SUCCESS
                except OSError as caught:
                    if caught.errno not in (_errno.EINVAL, _errno.EOPNOTSUPP, _errno.ENODEV):
                        return errno_of(caught.errno)  # a file system that cannot do it gets the old way
            number, st = self.stat_fd(entry)
            if number or st is None:
                return number
            if st.filetype != FT_REGULAR:
                return E_SPIPE  # a pipe: macOS would truncate it without a word
            return E_SUCCESS if st.size >= offset + length else self.set_size(entry, offset + length)

        def set_flags(self, entry: Entry, flags: int) -> int:
            try:
                current = fcntl.fcntl(entry.fd, fcntl.F_GETFL)
                every = native_fdflags(1 | 2 | 4 | 8 | 16)
                fcntl.fcntl(entry.fd, fcntl.F_SETFL, (current & ~every) | native_fdflags(flags))
                return E_SUCCESS
            except OSError as caught:
                return errno_of(caught.errno)
