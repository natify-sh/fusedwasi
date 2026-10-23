"""The first stage of WASI 0.1 (`fusedwasi.p1`, `fusedwasi/p1/wasi_p1_core.h`): the same script of calls in Python and in C, the outputs are equal."""

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

pytestmark = pytest.mark.skipif(
    sys.platform == "win32", reason="POSIX for now: the files of Windows are not in the twin yet"
)


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if CC is None:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("p1") / "driver"
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
    (root / "dirlink").symlink_to("sub")
    (root / "esc").symlink_to(f"../{root.name}-outside")


@pytest.mark.parametrize("name", SCRIPTS)
def test_c_and_python_agree(driver: Path, name: str, tmp_path: Path) -> None:
    script = DATA / "p1" / f"{name}.txt"
    make_tree(tmp_path / "c")
    make_tree(tmp_path / "py")
    c = subprocess.run(
        [str(driver), str(tmp_path / "c"), str(script)], capture_output=True, text=True, check=True
    ).stdout.splitlines()
    assert run(script, str(tmp_path / "py")) == c


def test_what_the_first_stage_gives() -> None:
    """A few values read by hand, so that two wrong twins do not agree."""
    lines = run(DATA / "p1" / "core.txt", "/tmp")
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
