"""The same programs under Wasmtime's own WASI: what a program can be seen to do from outside (what it wrote, its status, the files it left) with no fusedwasi in it.

Shared by the neutral users, which compare themselves with it. It is a reference, not a user: it uses wasmtime-py and nothing of fusedwasi.
"""

from __future__ import annotations

from pathlib import Path

import wasmtime as wt
import wasmtime.component as wtc

PROGRAMS = Path(__file__).resolve().parent / "programs"
ARGS = ["prog", "a", "b"]
ENV = [("K", "V"), ("X", "")]


def program(name: str) -> str:
    return (PROGRAMS / name).read_text()


def wasmtime_config(stdout: Path, preopen: Path | None = None) -> wt.WasiConfig:
    config = wt.WasiConfig()
    config.argv = ARGS
    config.env = ENV
    config.stdout_file = str(stdout)
    if preopen is not None:
        config.preopen_dir(str(preopen), "/")
    return config


def wasmtime_p1(engine: wt.Engine, wats: list[str], directory: Path) -> tuple[bytes, list[int | None]]:
    """The modules, one after the other in one store (so one world): what they wrote to standard output and the status of each (None when it returned)."""
    root = directory / "root"
    root.mkdir()
    store = wt.Store(engine)
    store.set_wasi(wasmtime_config(directory / "out", root))
    linker = wt.Linker(engine)
    linker.define_wasi()
    statuses: list[int | None] = []
    for wat in wats:
        instance = linker.instantiate(store, wt.Module(engine, wat))
        start = instance.exports(store)["_start"]
        assert isinstance(start, wt.Func)
        try:
            start(store)
            statuses.append(None)
        except wt.ExitTrap as caught:
            statuses.append(caught.code)
    return (directory / "out").read_bytes(), statuses


def wasmtime_p2(engine: wt.Engine, directory: Path) -> tuple[bytes, int | None]:
    component = wtc.Component(engine, program("p2_command.component.wat"))
    linker = wtc.Linker(engine)
    linker.add_wasip2()
    store = wt.Store(engine)
    store.set_wasi(wasmtime_config(directory / "out"))
    instance = linker.instantiate(store, component)
    outer = instance.get_export_index(store, "wasi:cli/run@0.2.6")
    assert outer is not None
    index = instance.get_export_index(store, "run", outer)
    assert index is not None
    run = instance.get_func(store, index)
    assert run is not None
    status: int | None = None
    try:
        run(store)
    except wt.ExitTrap as caught:
        status = caught.code
    return (directory / "out").read_bytes(), status
