"""fusedwasi used by a Python program over pywasm3 (the binding of wasm3): the programs of the other neutral users, compared with Wasmtime's own WASI. Skipped where pywasm3 is not installed."""

from __future__ import annotations

from pathlib import Path

import pytest
import wasmtime as wt

pytest.importorskip("wasm3")
import neutral_wasm3py as user
from neutral_reference import ARGS, ENV, program, wasmtime_p1, wasmtime_p2

from fusedwasi import p2


def module(text: str) -> bytes:
    return bytes(wt.wat2wasm(text))


def world_p1(root: Path) -> tuple[p2.State, p2.BufferOutput]:
    out = p2.BufferOutput()
    return p2.State(args=ARGS, env=ENV, stdout=out, preopens=[(str(root), "/")]), out


def test_a_module_of_0_1_does_what_it_does_under_wasmtime(engine: wt.Engine, tmp_path: Path) -> None:
    (tmp_path / "theirs").mkdir()
    (tmp_path / "ours" / "root").mkdir(parents=True)
    theirs, statuses = wasmtime_p1(engine, [program("p1_report.wat")], tmp_path / "theirs")
    state, out = world_p1(tmp_path / "ours" / "root")
    assert user.Instance(module(program("p1_report.wat")), state).start() == (3, None) and statuses == [3]
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
    first, second = (user.Instance(module(w), state) for w in wats)
    assert first.start() == (7, None) and not second.guest.exited
    assert second.start() == (None, None) and first.guest.exit_code == 7
    assert statuses == [7, None] and bytes(out.data) == theirs == b"A: 4\nB: from A\n0\x00\x00\x00\x00"


def test_a_command_of_0_2_does_what_it_does_under_wasmtime(engine: wt.Engine, tmp_path: Path) -> None:
    theirs, status = wasmtime_p2(engine, tmp_path)
    out = p2.BufferOutput()
    state = p2.State(args=ARGS, env=ENV, stdout=out)
    assert user.Instance(module(program("p2_command.wat")), state).start("run") == (1, None) and status == 1
    assert bytes(out.data) == theirs == b"prog\na\nb\nK=V\nX=\nt\nbye\n"


def test_a_trap_in_one_instance_leaves_the_state_to_the_next() -> None:
    out = p2.BufferOutput()
    state = p2.State(args=ARGS, env=ENV, stdout=out)
    trapped = user.Instance(module(program("p2_trap.wat")), state)
    code, trap = trapped.start("run")
    assert code is None and trap is not None and "bad handle" in trap and not trapped.guest.exited
    assert user.Instance(module(program("p2_command.wat")), state).start("run") == (1, None)


def test_a_memory_that_moves_in_the_middle_of_a_call_is_followed() -> None:
    out = p2.BufferOutput()
    state = p2.State(args=ARGS, env=ENV, stdout=out)
    assert (
        user.Instance(module(program("p2_grow.wat")), state).start("run") == (None, None)
        and bytes(out.data) == b"progab"
    )
