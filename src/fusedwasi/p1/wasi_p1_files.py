"""The file backend of a host of WASI 0.1: what the calls on files and paths ask of the system (the twin of the `fusedwasi_p1_be_*` functions of `wasi_p1_core.h`).

`Files` answers every request with ENOSYS; `wasi_p1_files_posix.PosixFiles` is the backend of POSIX (a directory is an open descriptor, a path is walked from it one name at a
time). A host that has no files uses this one. Every method returns the errno of WASI (0 is success) first. A path that is `None` was not in the memory of the module: the answer is FAULT, after the check of the directory.
"""

from __future__ import annotations

from dataclasses import dataclass

from .wasi_p1_core import E_NOSYS, Entry, Stat


@dataclass
class Listing:
    """One name of a directory as `fd_readdir` gives it."""

    name: bytes
    ino: int
    filetype: int


class Files:
    """A host without files."""

    def preopen(self, host: str) -> int:
        """The descriptor of the directory of a preopen; raises `OSError` when it cannot be opened."""
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
