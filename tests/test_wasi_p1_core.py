"""WASI 0.1 in Python (`fusedwasi.p1`) against the C core (`fusedwasi/p1/wasi_p1_core.h`): the same script of calls in Python and in C, the outputs are equal."""

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from cbuild import build
from p1_script import run

DATA = Path(__file__).parent / "data"
P1 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p1"
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
SCRIPTS = sorted(path.stem for path in (DATA / "p1").glob("*.txt") if sys.platform != "win32" or path.stem != "fifo")


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if CC is None:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("p1") / ("driver.exe" if sys.platform == "win32" else "driver")
    build(
        [
            CC,
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(P1),
            "-I",
            str(P1.parent / "p2"),
            str(DATA / "wasi_p1_driver.c"),
            "-o",
            str(binary),
            *(["-lbcrypt"] if sys.platform == "win32" else []),
        ],
    )
    return binary


def make_tree(root: Path) -> None:
    """The tree the scripts work on: a.txt, sub/b.txt, link, loop, dirlink, abslink, esc (out of the root), ro/ro.txt."""
    root.mkdir()
    (root.parent / f"{root.name}-outside").mkdir()
    (root / "a.txt").write_bytes(b"hello file\n")
    (root / "sub").mkdir()
    (root / "sub" / "b.txt").write_bytes(b"bbb")
    (root / "ro").mkdir()
    (root / "ro" / "ro.txt").write_bytes(b"readonly")
    (root / "link").symlink_to("a.txt")
    (root / "loop").symlink_to("loop")
    (root / "dirlink").symlink_to("sub", target_is_directory=True)
    (root / "abslink").symlink_to("/etc")  # an absolute target: path_readlink does not show it
    (root / "esc").symlink_to(f"../{root.name}-outside", target_is_directory=True)
    if sys.platform != "win32":
        os.mkfifo(root / "pipe1")  # not a regular file: fifo.txt


@pytest.mark.parametrize("name", SCRIPTS)
def test_c_and_python_agree(driver: Path, name: str, tmp_path: Path) -> None:
    script = DATA / "p1" / f"{name}.txt"
    make_tree(tmp_path / "c")
    make_tree(tmp_path / "py")
    c = subprocess.run(
        [str(driver), str(tmp_path / "c"), str(script)], capture_output=True, text=True, check=True
    ).stdout.splitlines()
    assert run(script, str(tmp_path / "py")) == c


@pytest.mark.parametrize("name", SCRIPTS)
def test_a_memory_that_is_not_a_buffer_gives_the_same(name: str, tmp_path: Path) -> None:
    """An engine that keeps its memory out of this process gives `read`/`write`/`len`; the calls then behave as with a buffer."""
    make_tree(tmp_path / "a")
    make_tree(tmp_path / "b")
    script = DATA / "p1" / f"{name}.txt"
    assert run(script, str(tmp_path / "a"), remote=True) == run(script, str(tmp_path / "b"))


def test_what_the_first_stage_gives(tmp_path: Path) -> None:
    """A few values read by hand, so that two wrong twins do not agree."""
    lines = run(DATA / "p1" / "core.txt", str(tmp_path))
    assert "sizes 030000000b000000" in lines  # 3 arguments, 11 bytes: "prog\0a b\0c\0"
    assert "argbuf 70726f670061206200630000" in lines
    assert "envsizes 0200000008000000" in lines
    assert "realtime e803000000000000" in lines  # the hook of the clock: 1000
    assert "out 1 68656c6c6f20" in lines and "nwritten 0c000000" in lines
    assert "prestat 0000000005000000" in lines  # a directory, the name "/data" is 5 bytes
    # proc_raise asks the hook `signal` of the guest: 31 is INVAL and 0 is success whatever the hook; with no hook 20, 27 and 9 are NOTSUP (58) and the program goes on
    first = lines.index("proc_raise 28")
    assert lines[first : first + 5] == [
        "proc_raise 28",
        "proc_raise 0",
        "proc_raise 58",
        "proc_raise 58",
        "proc_raise 58",
    ]
    # with the hook: it is told the number; 20 is accepted, 27 is refused (PERM, 63), and 9 ends the program by the trap of the guest, which leaves no code
    assert lines[first + 5 : first + 14] == [
        "proc_raise 28",
        "proc_raise 0",
        "signal 20",
        "proc_raise 0",
        "signal 27",
        "proc_raise 63",
        "signal 9",
        "proc_raise trapped",
        "exit 0 0",
    ]
    assert lines[first + 14] == "proc_raise 58"  # the hook is taken away
    # the end is what a call leaves, not a state of the world: the signal 9 did not stop the program (the script goes on), and the last call, proc_exit, leaves its code
    assert lines[-2:] == [
        "proc_exit trapped",
        "exit 1 7",
    ]  # (the host clears the record before a call; the script does not)


def test_append_by_hand(tmp_path: Path) -> None:
    """Read by hand: a write and a pwrite on a descriptor opened for appending go to the end (the system's rule for pwrite, kept when 0.1 went over 0.2); with the flag off they go where told."""
    lines = run(DATA / "p1" / "append.txt", str(tmp_path))
    assert "tell_after_write 0300000000000000" in lines  # "abc" written: the position is 3
    if (
        sys.platform == "win32"
    ):  # there pwrite is a seek, a write and a seek back, as it always was: it writes where it is told
        assert "content 5a5a630000000000" in lines
        return
    assert "content 6162635a5a000000" in lines  # the pwrite of "ZZ" at 0 went to the end
    assert "content2 5a5a635a5a61626300000000" in lines  # flag off: pwrite at 0 overwrote "ab"


@pytest.mark.skipif(sys.platform == "win32", reason="no FIFO")
def test_a_fifo_by_hand(tmp_path: Path) -> None:
    """A FIFO opened by a path is not a regular file: read and written at the position of the system, it cannot seek, and polling it says what is waiting."""
    make_tree(tmp_path / "t")
    lines = run(DATA / "p1" / "fifo.txt", str(tmp_path / "t"))
    assert "written 03000000" in lines and "content 6162630000000000" in lines  # "abc" in, "abc" out
    assert "fd_tell 70" in lines and "fd_seek 70" in lines and "fd_pread 70" in lines  # SPIPE
    assert any(
        line.startswith("ev 0900000000000000000001") for line in lines
    )  # userdata 9, no error, fd_read: ready (the bytes waiting are the system's to count)


def test_proc_exit_is_the_end_of_wasi_02(tmp_path: Path) -> None:
    """The code of proc_exit is where a host of 0.2 reads an exit: `exited` and `exit_code` of the state of 0.2, in C and in Python."""
    assert run(DATA / "p1" / "exit.txt", str(tmp_path)) == ["proc_exit trapped", "exit 1 7"]


def test_what_the_second_stage_gives(tmp_path: Path) -> None:
    """Values read by hand from the files: the content, the times, the truncation, the links."""
    make_tree(tmp_path / "py")
    lines = run(DATA / "p1" / "files.txt", str(tmp_path / "py"))
    assert "part1 68656c6c6f" in lines and "part2 2066696c650a" in lines  # "hello", " file\n"
    assert "seek_end 0800000000000000" in lines  # 11 - 3
    assert "times_ns 00f2052a0100000000863ba101000000" in lines  # 5 s and 7 s, in nanoseconds
    assert "size_after_set_size 0400000000000000" in lines and "size_after_trunc 0000000000000000" in lines
    assert "nlink 0200000000000000" in lines  # a hard link
    assert "readlink_data 68617264" in lines  # "hard"
    assert (
        lines.count("path_readlink 63") == 1
    )  # PERM: the link to "/etc" is not shown (readlink-at of 0.2 says not-permitted, and Wasmtime PERM)
    assert lines[lines.index("path_readlink 63") + 1] == "path_readlink 0"  # a relative one after it is read
    assert "fdstat_append 0400040000000000ffffff1f00000000ffffff1f00000000" in lines


_NATIVE_POLL = """
import struct, sys
from fusedwasi.p1 import wasi_p1
from fusedwasi.p1.wasi_p1_core import Guest, State
from fusedwasi.p2.wasi_p2_cli import FdInput

memory = bytearray(4096)
s = Guest(State(stdin=FdInput(0)), memory)  # the descriptor of the process, not the Python stream
struct.pack_into("<QBxxxxxxxI", memory, 0, 1, 1, 0)  # fd_read of fd 0
struct.pack_into("<QBxxxxxxxIxxxxQQHxxxxxx", memory, 48, 2, 0, 1, 20_000_000, 0, 0)  # a clock of 20 ms
rc = wasi_p1.poll_oneoff(s, 0, 1000, 2, 900)
n = struct.unpack_from("<I", memory, 900)[0]
events = [struct.unpack_from("<QHBxxxxxQH", memory, 1000 + 32 * i) for i in range(n)]
print(rc, events)
"""


def _poll_stdin(data: bytes | None) -> str:
    """poll_oneoff on the real standard input, a pipe: ready (with the bytes waiting) or, with nothing in it, the clock of 20 ms ends the wait."""
    proc = subprocess.Popen(
        [sys.executable, "-c", _NATIVE_POLL], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True
    )
    assert proc.stdin is not None and proc.stdout is not None
    if data is not None:
        proc.stdin.write(data.decode())
        proc.stdin.flush()
    out = proc.stdout.read()  # the pipe stays open until the child has answered
    proc.stdin.close()
    proc.wait()
    return out.strip()


@pytest.mark.skipif(sys.platform == "win32", reason="no select.poll")
def test_native_poll_on_a_pipe() -> None:
    assert _poll_stdin(b"hello") == "0 [(1, 0, 1, 5, 0)]"  # userdata 1, no error, fd_read, 5 bytes waiting
    assert _poll_stdin(None) == "0 [(2, 0, 0, 0, 0)]"  # nothing to read: the clock
