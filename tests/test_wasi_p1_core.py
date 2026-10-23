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


@pytest.mark.parametrize("name", SCRIPTS)
def test_c_and_python_agree(driver: Path, name: str, tmp_path: Path) -> None:
    script = DATA / "p1" / f"{name}.txt"
    c = subprocess.run(
        [str(driver), str(tmp_path), str(script)], capture_output=True, text=True, check=True
    ).stdout.splitlines()
    assert run(script, str(tmp_path)) == c


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
