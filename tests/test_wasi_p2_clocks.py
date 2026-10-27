"""wasi:clocks of the host of WASI 0.2 (`fusedwasi.p2.wasi_p2_clocks`, `include/fusedwasi/p2/wasi_p2_clocks.h`): the same scenario in Python and in C, the outputs are equal.

No wasm2c and no module: a fake memory and the flat arguments, as the core module gives them.
"""

import shutil
import subprocess
from pathlib import Path

import pytest
from cbuild import build
from guests import guest_of

from fusedwasi.p2 import wasi_p2_clocks as clocks
from fusedwasi.p2.wasi_p2_core import KIND_POLLABLE, Trap, _table_drop, _table_get

C_P2 = Path(__file__).parent.parent / "src" / "fusedwasi" / "include" / "fusedwasi" / "p2"
DRIVER = Path(__file__).parent / "data" / "wasi_p2_clocks_driver.c"
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")

SCENARIOS = ["main", "double_drop", "outside", "no_memory"]


def python_output(scenario: str) -> str:
    memory = bytearray(64)
    host = guest_of(
        [memory],
        clock=lambda ident: 1700000000123456789 if ident == 0 else 5000,
        resolution_ns=[1000, 7, 1, 1],
    )
    lines: list[str] = []
    try:
        if scenario == "main":
            clocks.wasi_clocks_wall_clock_now(host, 0, 0)
            clocks.wasi_clocks_wall_clock_resolution(host, 0, 16)
            lines.append(bytes(memory[:32]).hex())
            lines.append(
                f"{clocks.wasi_clocks_monotonic_clock_now(host)} {clocks.wasi_clocks_monotonic_clock_resolution(host)}"
            )
            a = clocks.wasi_clocks_monotonic_clock_subscribe_duration(host, 100)
            b = clocks.wasi_clocks_monotonic_clock_subscribe_instant(host, 9000)
            c = clocks.wasi_clocks_monotonic_clock_subscribe_duration(host, 2**64 - 1)
            lines.append(f"{a} {b} {c}")
            lines.append(" ".join(str(_table_get(host, h, KIND_POLLABLE).deadline) for h in (a, b, c)))  # type: ignore[attr-defined]
            _table_drop(host, a, KIND_POLLABLE)
            lines.append(str(clocks.wasi_clocks_monotonic_clock_subscribe_instant(host, 1)))
        elif scenario == "double_drop":
            a = clocks.wasi_clocks_monotonic_clock_subscribe_instant(host, 1)
            _table_drop(host, a, KIND_POLLABLE)
            _table_drop(host, a, KIND_POLLABLE)
        elif scenario == "outside":
            clocks.wasi_clocks_wall_clock_now(host, 0, 60)
        elif scenario == "no_memory":
            clocks.wasi_clocks_wall_clock_now(host, 1, 0)
    except Trap:
        lines.append("TRAP")
    return "\n".join(lines) + "\n"


def c_output(scenario: str, binary: Path) -> str:
    out = subprocess.run([str(binary), scenario], capture_output=True, text=True, check=True).stdout
    return "\n".join("TRAP" if line.startswith("TRAP") else line for line in out.splitlines()) + "\n"


@pytest.fixture(scope="module")
def driver(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if CC is None:
        pytest.skip("no C compiler")
    binary = tmp_path_factory.mktemp("p2") / "driver"
    build([CC, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-I", str(C_P2), str(DRIVER), "-o", str(binary)])
    return binary


@pytest.mark.parametrize("scenario", SCENARIOS)
def test_c_and_python_agree(driver: Path, scenario: str) -> None:
    assert python_output(scenario) == c_output(scenario, driver)


def test_values() -> None:
    lines = python_output("main").splitlines()
    # datetime {seconds: u64, nanoseconds: u32} little-endian, then the resolution of 1000 ns
    assert lines[0] == "00f1536500000000" + "15cd5b07" + "00000000" + "0000000000000000" + "e8030000" + "00000000"
    assert lines[1] == "5000 7"
    assert lines[3] == "5100 9000 18446744073709551615"  # a duration that overflows is the largest instant
    assert lines[2].split()[0] != lines[4]  # the slot of the dropped handle is used again with another generation


@pytest.mark.parametrize("scenario", ["double_drop", "outside", "no_memory"])
def test_traps(scenario: str) -> None:
    assert python_output(scenario).strip() == "TRAP"
