"""Runs a WASI 0.1 command module on wasmtime-py with the functions of fusedwasi, as a program of the command line.

    python neutral_wasmtime_run.py [--env NAME=VALUE]... [--dir HOST::GUEST]... module.wasm [argument]...

The standard streams are those of the process. The status of the process is the code of `proc_exit` (0 when `_start` returns); a trap writes its message to the standard error and is status 1.
It is what `wasi_testsuite_adapter.py` hands to the official wasi-testsuite as a runtime.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import wasmtime as wt
from neutral_wasmtime import Instance

from fusedwasi import p1, p2


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="neutral_wasmtime_run")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--dir", action="append", default=[], metavar="HOST::GUEST")
    parser.add_argument("module")
    parser.add_argument("arguments", nargs="*")
    options = parser.parse_args(argv)
    environment = [tuple(item.split("=", 1)) for item in options.env]
    preopens = [tuple(item.split("::", 1)) for item in options.dir]
    world = p2.State(args=[options.module, *options.arguments], env=[(k, v) for k, v in environment])
    state = p1.State(core=world, preopens=[(host, name) for host, name in preopens])
    instance = Instance(wt.Engine(), Path(options.module).read_bytes(), state)
    outcome = instance.start("_start")
    if outcome.trap is not None:
        print(f"trap: {outcome.trap}", file=sys.stderr)
    sys.stdout.flush()
    return outcome.code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
