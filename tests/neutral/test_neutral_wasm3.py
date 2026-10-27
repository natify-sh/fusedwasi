"""fusedwasi used from C by a program that knows nothing of wasm2c or any particular host: wasm3 runs the modules, `c_wasm3/neutral_wasm3.c` is the user.

The driver is built here from the public headers and from the glue that `c_wasm3/gen_wasm3_glue.py` writes out of the public table of signatures; wasm3 is not in this repository: set
WASM3_SRC to a checkout of https://github.com/wasm3/wasm3 (the CI job uses the tag v0.5.0, commit 6b8bcb1e07bf26ebef09a7211b0a37a446eafd52). Without it these tests are skipped.
The programs are the ones of `test_neutral_wasmtime.py`, assembled with wasmtime-py (only to turn the text into a module), and are compared with Wasmtime's own WASI.
"""

from __future__ import annotations

import importlib.util
import os
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

import pytest
import wasmtime as wt
from neutral_reference import ARGS, ENV, program, wasmtime_p1, wasmtime_p2

import fusedwasi

HERE = Path(__file__).resolve().parent / "c_wasm3"
WASM3_SOURCES = (
    "m3_bind",
    "m3_code",
    "m3_compile",
    "m3_core",
    "m3_emit",
    "m3_env",
    "m3_exec",
    "m3_function",
    "m3_info",
    "m3_module",
    "m3_parse",
)
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")


@dataclass
class Result:
    exited: bool
    code: int
    trap: str | None


@pytest.fixture(scope="session")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if "WASM3_SRC" not in os.environ:
        pytest.skip("WASM3_SRC is not set: it is a checkout of https://github.com/wasm3/wasm3 (tag v0.5.0)")
    if CC is None:
        pytest.skip("no C compiler")
    source = Path(os.environ["WASM3_SRC"]) / "source"
    work = tmp_path_factory.mktemp("wasm3")
    include = Path(fusedwasi.get_include())
    spec = importlib.util.spec_from_file_location("gen_wasm3_glue", HERE / "gen_wasm3_glue.py")
    assert spec is not None and spec.loader is not None
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    generator.main(str(work / "neutral_glue.h"))
    objects: list[str] = []
    for name in WASM3_SOURCES:  # the engine as it is, with its own warnings
        subprocess.run(
            [
                CC,
                "-O1",
                "-w",
                "-fno-sanitize=all",
                "-c",
                f"-I{source}",
                str(source / f"{name}.c"),
                "-o",
                str(work / f"{name}.o"),
            ],
            check=True,
        )
        objects.append(str(work / f"{name}.o"))
    # the user, with the warnings on and as errors, optimised (a warning of the headers can appear only when the compiler optimises)
    flags = ["-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror"]
    paths = [f"-I{source}", f"-I{include}", f"-I{work}"]
    out = work / "neutral_wasm3"
    subprocess.run([CC, *flags, *paths, str(HERE / "neutral_wasm3.c"), *objects, "-lm", "-o", str(out)], check=True)
    return out


@pytest.mark.parametrize("level", ["-O1", "-O2", "-O3", "-Os"])
@pytest.mark.parametrize("compiler", [c for c in ("gcc", "clang", "x86_64-w64-mingw32-gcc") if shutil.which(c)])
def test_the_user_and_its_glue_build_without_a_warning(compiler: str, level: str, tmp_path: Path) -> None:
    """The C of the neutral user and the glue generated for it, optimised, with the warnings as errors: a user's build of the headers (no `-Wno` flag)."""
    if "WASM3_SRC" not in os.environ:
        pytest.skip("WASM3_SRC is not set: it is a checkout of https://github.com/wasm3/wasm3 (tag v0.5.0)")
    spec = importlib.util.spec_from_file_location("gen_wasm3_glue", HERE / "gen_wasm3_glue.py")
    assert spec is not None and spec.loader is not None
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    generator.main(str(tmp_path / "neutral_glue.h"))
    command = [
        compiler,
        "-std=gnu11",
        level,
        "-Wall",
        "-Wextra",
        "-Werror",
        "-c",
        f"-I{Path(os.environ['WASM3_SRC']) / 'source'}",
    ]
    command += [
        f"-I{fusedwasi.get_include()}",
        f"-I{tmp_path}",
        str(HERE / "neutral_wasm3.c"),
        "-o",
        str(tmp_path / "neutral.o"),
    ]
    done = subprocess.run(command, capture_output=True, text=True, check=False)
    assert done.returncode == 0, done.stderr


def assemble(tmp_path: Path, name: str, text: str | None = None) -> Path:
    path = tmp_path / (name + ".wasm")
    path.write_bytes(wt.wat2wasm(text if text is not None else program(name + ".wat")))
    return path


def run(
    driver: Path, world: str, modules: list[Path], *, root: Path | None = None, entry: str | None = None
) -> tuple[bytes, list[Result], int]:
    command = [str(driver), "--world", world]
    for argument in ARGS:
        command += ["--arg", argument]
    for name, value in ENV:
        command += ["--env", f"{name}={value}"]
    if root is not None:
        command += ["--dir", f"{root}::/"]
    if entry is not None:
        command += ["--entry", entry]
    done = subprocess.run([*command, *map(str, modules)], capture_output=True, check=False)
    results = [
        Result(m[2] == "1", int(m[3]), None if m[4] == "-" else m[4])
        for m in re.finditer(r"^result (\d+) exited=(\d) code=(\d+) trap=(.*)$", done.stderr.decode(), re.MULTILINE)
    ]
    return done.stdout, results, done.returncode


# ---- WASI 0.1 ----------------------------------------------------------------------------------------------------------------


def test_a_module_of_0_1_does_what_it_does_under_wasmtime(driver: Path, engine: wt.Engine, tmp_path: Path) -> None:
    (tmp_path / "theirs").mkdir()
    (tmp_path / "ours" / "root").mkdir(parents=True)
    theirs, statuses = wasmtime_p1(engine, [program("p1_report.wat")], tmp_path / "theirs")
    out, results, _ = run(driver, "p1", [assemble(tmp_path, "p1_report")], root=tmp_path / "ours" / "root")
    assert results == [Result(True, 3, None)] and statuses == [3]
    assert out == theirs and theirs.startswith(b"hello p1\n")
    assert (
        (tmp_path / "ours" / "root" / "f.txt").read_bytes()
        == (tmp_path / "theirs" / "root" / "f.txt").read_bytes()
        == b"data!"
    )


def test_two_instances_over_one_state_share_the_descriptors_and_nothing_else(
    driver: Path, engine: wt.Engine, tmp_path: Path
) -> None:
    (tmp_path / "theirs").mkdir()
    (tmp_path / "ours" / "root").mkdir(parents=True)
    wats = [program("p1_share_a.wat"), program("p1_share_b.wat")]
    theirs, statuses = wasmtime_p1(engine, wats, tmp_path / "theirs")
    out, results, _ = run(
        driver,
        "p1",
        [assemble(tmp_path, "p1_share_a"), assemble(tmp_path, "p1_share_b")],
        root=tmp_path / "ours" / "root",
    )
    assert results == [Result(True, 7, None), Result(False, 0, None)] and statuses == [
        7,
        None,
    ]  # one instance's exit is its own
    assert (
        out == theirs == b"A: 4\nB: from A\n0\x00\x00\x00\x00"
    )  # the descriptor 4 of the first is there for the second; the memory 5000 of the first is not


RAISE = """
(module
  (import "wasi_snapshot_preview1" "proc_raise" (func $raise (param i32) (result i32)))
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "_start")
    (i32.store (i32.const 16) (call $raise (i32.const 15)))
    (i32.store (i32.const 0) (i32.const 16))
    (i32.store (i32.const 4) (i32.const 4))
    (drop (call $write (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 8)))))
"""


def test_proc_raise_without_a_hook_is_what_wasmtime_says(driver: Path, engine: wt.Engine, tmp_path: Path) -> None:
    (tmp_path / "theirs").mkdir()
    theirs, _ = wasmtime_p1(engine, [RAISE], tmp_path / "theirs")
    out, results, _ = run(driver, "p1", [assemble(tmp_path, "raise", RAISE)])
    assert results == [Result(False, 0, None)] and out == theirs == (58).to_bytes(4, "little")


GROW = """
(module
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "_start")
    (drop (memory.grow (i32.const 8)))
    (i32.store8 (i32.const 500000) (i32.const 0x6f))
    (i32.store8 (i32.const 500001) (i32.const 0x6b))
    (i32.store (i32.const 500100) (i32.const 500000))
    (i32.store (i32.const 500104) (i32.const 2))
    (drop (call $write (i32.const 1) (i32.const 500100) (i32.const 1) (i32.const 500200)))))
"""


def test_a_memory_that_grew_before_the_call_is_followed(driver: Path, tmp_path: Path) -> None:
    out, results, _ = run(driver, "p1", [assemble(tmp_path, "grow", GROW)])
    assert results == [Result(False, 0, None)] and out == b"ok"


# ---- WASI 0.2 ----------------------------------------------------------------------------------------------------------------


def test_a_command_of_0_2_does_what_it_does_under_wasmtime(driver: Path, engine: wt.Engine, tmp_path: Path) -> None:
    theirs, status = wasmtime_p2(engine, tmp_path)
    out, results, _ = run(driver, "p2", [assemble(tmp_path, "p2_command")])
    assert results == [Result(True, 1, None)] and status == 1
    assert out == theirs == b"prog\na\nb\nK=V\nX=\nt\nbye\n"


def test_two_commands_over_one_state_and_a_trap_in_a_third(driver: Path, tmp_path: Path) -> None:
    command, trap = assemble(tmp_path, "p2_command"), assemble(tmp_path, "p2_trap")
    out, results, status = run(driver, "p2", [command, trap, command])
    assert (
        results == [Result(True, 1, None), Result(False, 0, "bad handle"), Result(True, 1, None)] and status == 1
    )  # a trap is not an exit
    assert out == 2 * b"prog\na\nb\nK=V\nX=\nt\nbye\n"


def test_a_memory_that_moves_in_the_middle_of_a_call_is_followed(driver: Path, tmp_path: Path) -> None:
    """`cabi_realloc` grows the memory (wasm3 may move it) while a function of fusedwasi is writing a list and its strings into it."""
    out, results, _ = run(driver, "p2", [assemble(tmp_path, "p2_grow")])
    assert results == [Result(False, 0, None)] and out == b"progab"
