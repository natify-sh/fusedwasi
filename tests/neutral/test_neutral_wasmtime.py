"""fusedwasi used by a program that knows nothing of wasm2c or any particular host: wasmtime-py runs the modules, `neutral_wasmtime.py` is the user.

Each program is run twice, under the functions of fusedwasi and under Wasmtime's own WASI (the module of 0.1 with `define_wasi`, the command of 0.2 as the component
`p2_command.component.wat`), and what the program can see from outside is compared: what it wrote, its status, the files it left.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

import neutral_wasmtime as user
import pytest
import wasmtime as wt
from neutral_binding import implementation
from neutral_reference import ARGS, ENV, program, wasmtime_p1, wasmtime_p2

from fusedwasi import p1, p2

HERE = Path(__file__).resolve().parent


# ---- the same program under the functions of fusedwasi -----------------------------------------------------------------------


def world_p1(root: Path) -> tuple[p2.State, p2.BufferOutput]:
    out = p2.BufferOutput()
    return p2.State(args=ARGS, env=ENV, stdout=out, preopens=[(str(root), "/")]), out


# ---- WASI 0.1 ----------------------------------------------------------------------------------------------------------------


def test_a_module_of_0_1_does_what_it_does_under_wasmtime(engine: wt.Engine, tmp_path: Path) -> None:
    (tmp_path / "theirs").mkdir()
    (tmp_path / "ours" / "root").mkdir(parents=True)
    theirs, statuses = wasmtime_p1(engine, [program("p1_report.wat")], tmp_path / "theirs")
    state, out = world_p1(tmp_path / "ours" / "root")
    outcome = user.Instance(engine, program("p1_report.wat"), state).start()
    assert outcome == user.Outcome(exit_code=3) and statuses == [3]
    assert bytes(out.data) == theirs and theirs.startswith(b"hello p1\n")
    assert (
        (tmp_path / "ours" / "root" / "f.txt").read_bytes()
        == (tmp_path / "theirs" / "root" / "f.txt").read_bytes()
        == b"data!"
    )


def test_two_instances_over_one_state_share_the_descriptors_and_nothing_else(engine: wt.Engine, tmp_path: Path) -> None:
    (tmp_path / "theirs").mkdir()
    (tmp_path / "ours" / "root").mkdir(parents=True)
    wats = [program("p1_share_a.wat"), program("p1_share_b.wat")]
    theirs, statuses = wasmtime_p1(engine, wats, tmp_path / "theirs")
    state, out = world_p1(tmp_path / "ours" / "root")
    first = user.Instance(engine, wats[0], state)
    second = user.Instance(engine, wats[1], state)
    assert first.guest is not second.guest and first.guest.state is second.guest.state is state
    assert not first.guest.exited and not second.guest.exited
    assert first.start() == user.Outcome(exit_code=7)
    assert (
        first.guest.exited and first.guest.exit_code == 7 and not second.guest.exited
    )  # one instance's exit is its own
    assert (
        second.start() == user.Outcome()
    )  # the other one returns; the descriptor 4 that the first left open is there for it
    assert not second.guest.exited and second.guest.exit_code == 0 and first.guest.exited and first.guest.exit_code == 7
    assert statuses == [7, None]
    assert bytes(out.data) == theirs == b"A: 4\nB: from A\n0\x00\x00\x00\x00"
    assert first.memory()[5000:5004] == b"AAAA" and second.memory()[5000:5004] == b"\0\0\0\0"  # two memories


def test_a_module_of_the_first_snapshot_is_bound_by_the_public_table(engine: wt.Engine, tmp_path: Path) -> None:
    """`find_import("wasi_unstable", ...)` answers: `fd_seek` of this snapshot orders `whence` as cur, end, set; the other calls are the ones of `wasi_snapshot_preview1`."""
    (tmp_path / "root").mkdir()
    state, out = world_p1(tmp_path / "root")
    assert user.Instance(engine, program("p1_unstable.wat"), state).start() == user.Outcome(exit_code=3)
    expected = b"\0\0\0\0" + bytes(8) + b"abc12" + b"\0\0\0\0" + (3).to_bytes(8, "little")
    assert bytes(out.data) == expected and (tmp_path / "root" / "u.txt").read_bytes() == b"abc12"


def test_the_end_of_a_call_is_cleared_by_the_caller(engine: wt.Engine, tmp_path: Path) -> None:
    (tmp_path / "root").mkdir()
    state, _ = world_p1(tmp_path / "root")
    instance = user.Instance(engine, program("p1_share_a.wat"), state)
    assert instance.start() == user.Outcome(exit_code=7)
    assert instance.start() == user.Outcome(exit_code=7) and instance.guest.exit_code == 7


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


def test_proc_raise_without_a_hook_is_what_wasmtime_says(engine: wt.Engine, tmp_path: Path) -> None:
    (tmp_path / "theirs").mkdir()
    theirs, _ = wasmtime_p1(engine, [RAISE], tmp_path / "theirs")
    (tmp_path / "ours").mkdir()
    state, out = world_p1(tmp_path / "ours")
    assert user.Instance(engine, RAISE, state).start() == user.Outcome()
    assert bytes(out.data) == theirs == p1.E_NOTSUP.to_bytes(4, "little")


def test_proc_raise_asks_the_hook_of_the_guest(engine: wt.Engine, tmp_path: Path) -> None:
    seen: list[int] = []
    state, out = world_p1(tmp_path)
    assert (
        user.Instance(engine, RAISE, state, p1_signal=lambda number: seen.append(number) or 0).start() == user.Outcome()
    )
    assert seen == [15] and bytes(out.data) == b"\0\0\0\0"


GROW = """
(module
  (import "wasi_snapshot_preview1" "fd_write" (func $write (param i32 i32 i32 i32) (result i32)))
  (memory (export "memory") 1)
  (data (i32.const 0) "ok")
  (func (export "_start")
    (drop (memory.grow (i32.const 8)))
    (i32.store8 (i32.const 500000) (i32.const 0x6f))
    (i32.store8 (i32.const 500001) (i32.const 0x6b))
    (i32.store (i32.const 500100) (i32.const 500000))
    (i32.store (i32.const 500104) (i32.const 2))
    (drop (call $write (i32.const 1) (i32.const 500100) (i32.const 1) (i32.const 500200)))))
"""


def test_a_memory_that_grows_is_followed(engine: wt.Engine, tmp_path: Path) -> None:
    state, out = world_p1(tmp_path)
    instance = user.Instance(engine, GROW, state)
    assert len(instance.memory()) == 65536
    assert instance.start() == user.Outcome() and bytes(out.data) == b"ok" and len(instance.memory()) == 9 * 65536


# ---- WASI 0.2 ----------------------------------------------------------------------------------------------------------------


def world_p2() -> tuple[p2.State, p2.BufferOutput]:
    out = p2.BufferOutput()
    return p2.State(args=ARGS, env=ENV, stdout=out), out


def test_a_command_of_0_2_does_what_it_does_under_wasmtime(engine: wt.Engine, tmp_path: Path) -> None:
    theirs, status = wasmtime_p2(engine, tmp_path)
    state, out = world_p2()
    instance = user.Instance(engine, program("p2_command.wat"), state)
    assert instance.start("run") == user.Outcome(exit_code=1) and status == 1
    assert bytes(out.data) == theirs == b"prog\na\nb\nK=V\nX=\nt\nbye\n"


def test_the_imports_are_found_by_name_whatever_the_version_of_the_module() -> None:
    function, row = implementation("wasi:io/streams@0.2.0", "[method]output-stream.blocking-write-and-flush")
    assert function.__name__ == "wasi_io_streams_output_stream_blocking_write_and_flush" and row.takes_comp
    function, row = implementation("wasi:io/streams@0.2.6", "[resource-drop]output-stream")
    assert function.__name__ == "wasi_io_streams_output_stream_drop" and not row.takes_comp


def test_two_commands_over_one_state_and_a_trap_in_a_third(engine: wt.Engine) -> None:
    state, out = world_p2()
    traps: list[str] = []
    first = user.Instance(engine, program("p2_command.wat"), state)
    second = user.Instance(engine, program("p2_command.wat"), state)
    third = user.Instance(engine, program("p2_trap.wat"), state, traps=traps)
    assert first.start("run") == user.Outcome(exit_code=1)
    assert (
        third.start("run").trap is not None and traps == ["bad handle 999"] and not third.guest.exited
    )  # a trap is not an exit
    assert second.start("run") == user.Outcome(exit_code=1)  # the world is as it was
    assert first.guest.exited and second.guest.exited and first.guest is not second.guest
    assert bytes(out.data) == 2 * b"prog\na\nb\nK=V\nX=\nt\nbye\n"
    assert first.start("run").exit_code == 1  # the same instance again: the flags were cleared before the call


def test_a_memory_that_moves_in_the_middle_of_a_call_is_followed(engine: wt.Engine) -> None:
    """`cabi_realloc` grows the memory while a function of fusedwasi is writing a list and its strings into it."""
    state, out = world_p2()
    assert user.Instance(engine, program("p2_grow.wat"), state).start("run") == user.Outcome()
    assert bytes(out.data) == b"progab"


# ---- the official testsuite --------------------------------------------------------------------------------------------------


@pytest.mark.skipif(
    "WASI_TESTSUITE" not in os.environ,
    reason="WASI_TESTSUITE is not set: it is the checkout of WebAssembly/wasi-testsuite (branch prod/testsuite-base)",
)
def test_the_official_testsuite_of_0_1(tmp_path: Path) -> None:
    import json

    suite = Path(os.environ["WASI_TESTSUITE"])
    report = tmp_path / "report.json"
    subprocess.run(
        [
            sys.executable,
            str(suite / "run-tests"),
            "--disable-colors",
            "--runtime-adapter",
            str(HERE / "wasi_testsuite_adapter.py"),
            "--json-output-location",
            str(report),
        ],
        check=False,
        capture_output=True,
        cwd=suite,
    )
    results = json.loads(report.read_text())["results"]
    passed = sum(r["passed"] for r in results)
    failed = [t["name"] for r in results for t in r["tests"] if t["outcome"] not in ("pass", "skip")]
    assert failed == [] and passed >= 72, f"{passed} passed, failed: {failed}"
