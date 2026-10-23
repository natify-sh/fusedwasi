"""WASI 0.1 in Python (`fusedwasi.p1`) against the C core (`fusedwasi/p1/wasi_p1_core.h`): the same script of calls in Python and in C, the outputs are equal."""

import shutil
import subprocess
import sys
from pathlib import Path

import pytest
from p1_script import run

DATA = Path(__file__).parent / "data"
P1 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p1"
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
SCRIPTS = sorted(path.stem for path in (DATA / "p1").glob("*.txt"))


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if CC is None:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("p1") / ("driver.exe" if sys.platform == "win32" else "driver")
    subprocess.run(
        [
            CC,
            "-std=gnu11",
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Werror",
            "-I",
            str(P1),
            str(DATA / "wasi_p1_driver.c"),
            "-o",
            str(binary),
            *(["-lbcrypt"] if sys.platform == "win32" else []),
        ],
        check=True,
    )
    return binary


def make_tree(root: Path) -> None:
    """The tree the scripts work on: a.txt, sub/b.txt, link, loop, dirlink, esc (out of the root), ro/ro.txt."""
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
    (root / "esc").symlink_to(f"../{root.name}-outside", target_is_directory=True)


@pytest.mark.parametrize("name", SCRIPTS)
def test_c_and_python_agree(driver: Path, name: str, tmp_path: Path) -> None:
    script = DATA / "p1" / f"{name}.txt"
    make_tree(tmp_path / "c")
    make_tree(tmp_path / "py")
    c = subprocess.run(
        [str(driver), str(tmp_path / "c"), str(script)], capture_output=True, text=True, check=True
    ).stdout.splitlines()
    assert run(script, str(tmp_path / "py")) == c


def test_what_the_first_stage_gives(tmp_path: Path) -> None:
    """A few values read by hand, so that two wrong twins do not agree."""
    lines = run(DATA / "p1" / "core.txt", str(tmp_path))
    assert "sizes 030000000b000000" in lines  # 3 arguments, 11 bytes: "prog\0a b\0c\0"
    assert "argbuf 70726f670061206200630000" in lines
    assert "envsizes 0200000008000000" in lines
    assert "realtime e803000000000000" in lines  # the hook of the clock: 1000
    assert "out 1 68656c6c6f20" in lines and "nwritten 0c000000" in lines
    assert "prestat 0000000005000000" in lines  # a directory, the name "/data" is 5 bytes
    assert lines[-4:] == ["end 1 7", "proc_exit trapped"] or "end 1 7" in lines
    assert "proc_raise trapped" in lines and "end 2 9" in lines


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
    assert "fdstat_append 0400040000000000ffffff1f00000000ffffff1f00000000" in lines


_NATIVE_POLL = """
import struct, sys
from fusedwasi.p1 import wasi_p1
from fusedwasi.p1.wasi_p1_core import State

memory = bytearray(4096)
s = State(memory)
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
