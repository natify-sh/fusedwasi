"""Runs the scenarios on Wasmtime and on fusedwasi's Python twin and prints the steps that differ.

    python docs/experiments/wasmtime_compare/run.py [--all] [--json FILE] [p1|p2 cli|p2 clocks|p2 random|p2 filesystem ...]

Needs the `wasmtime` package (pip), `wasm-tools` on the PATH, and the WIT of WASI 0.2.12 (see harness.find_wit). Linux or macOS: the scenarios use symbolic links.
Not part of the tests (CI has none of that).
"""

from __future__ import annotations

import json
import shutil
import sys
import tempfile
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import harness as h
import scenarios as sc

ARGS = ["prog", "a b", "ü", ""]
ENV = [("FOO", "bar"), ("EMPTY", ""), ("EQ", "a=b")]


def run_all(selected: list[str]) -> dict[str, dict[str, list[tuple[str, str]] | str]]:
    work = Path(tempfile.mkdtemp(prefix="fusedwasi-cmp-"))
    try:
        component, core2 = h.build_p2(work)
        core1 = h.build_p1(work)
        results: dict[str, dict[str, list[tuple[str, str]] | str]] = {}
        for family, table, makers in (
            (
                "p2",
                sc.SCENARIOS_P2,
                (lambda p: h.WasmtimeP2(component, p, ARGS, ENV), lambda p: h.FusedP2(core2, p, ARGS, ENV)),
            ),
            (
                "p1",
                sc.SCENARIOS_P1,
                (lambda p: h.WasmtimeP1(core1, p, ARGS, ENV), lambda p: h.FusedP1(core1, p, ARGS, ENV)),
            ),
        ):
            for name, scenario in table.items():
                if selected and name not in selected:
                    continue
                results[name] = {}
                for maker in makers:
                    base = Path(tempfile.mkdtemp(prefix="tree-", dir=work))
                    root = sc.make_tree(base)
                    who = "wasmtime" if maker is makers[0] else "fusedwasi"
                    try:
                        preopens = [(str(root), "/")]
                        rt = maker(preopens)
                        results[name][who] = scenario(rt, lambda maker=maker, preopens=preopens: maker(preopens))
                    except Exception:  # noqa: BLE001
                        results[name][who] = traceback.format_exc()
        return results
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main() -> None:
    argv = sys.argv[1:]
    show_all = "--all" in argv
    out = None
    if "--json" in argv:
        out = argv[argv.index("--json") + 1]
    selected = [a for a in argv if not a.startswith("--") and a != out]
    results = run_all(selected)
    total = same = 0
    for name, both in results.items():
        a, b = both.get("wasmtime"), both.get("fusedwasi")
        print(f"== {name}")
        if isinstance(a, str) or isinstance(b, str):
            print("   a run failed:")
            for who, r in both.items():
                if isinstance(r, str):
                    print(f"   [{who}] {r}")
            continue
        assert a is not None and b is not None
        for (label, x), (label2, y) in zip(a, b, strict=False):
            total += 1
            if label != label2:
                print(f"   steps are out of line at {label!r} / {label2!r}")
                break
            if x == y:
                same += 1
                if show_all:
                    print(f"   same  {label}: {x}")
            else:
                print(f"   DIFF  {label}\n         wasmtime : {x}\n         fusedwasi: {y}")
        if len(a) != len(b):
            print(f"   different number of steps: {len(a)} / {len(b)}")
    print(f"\n{same} of {total} steps agree")
    if out:
        Path(out).write_text(json.dumps(results, indent=1))


if __name__ == "__main__":
    main()
