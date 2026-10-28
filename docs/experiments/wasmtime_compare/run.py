"""Runs the scenarios on Wasmtime and on fusedwasi's Python twin and prints the steps that differ.

    [HOST=python|c|all] python docs/experiments/wasmtime_compare/run.py [--all] [--json FILE] [p1|p2 cli|p2 clocks|p2 random|p2 filesystem ...]

Needs the `wasmtime` package (pip), `wasm-tools` on the PATH, and the WIT of WASI 0.2.12 (see harness.find_wit). HOST=c or all also needs a C compiler and WASM3_SRC (see harness.build_c_driver). Linux or macOS: the scenarios use symbolic links.
Not part of the tests (CI has none of that).
"""

from __future__ import annotations

import json
import os
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


def hosts_wanted() -> list[str]:
    """`HOST=python` (default): Wasmtime and the Python twin; `HOST=c`: Wasmtime and the C headers; `HOST=all`: the three."""
    host = os.environ.get("HOST", "python")
    if host not in ("python", "c", "all"):
        raise SystemExit("HOST is python, c or all")
    return [
        "wasmtime",
        *(["fusedwasi"] if host in ("python", "all") else []),
        *(["fusedwasi-c"] if host in ("c", "all") else []),
    ]


def run_all(selected: list[str], whos: list[str]) -> dict[str, dict[str, list[tuple[str, str]] | str]]:
    work = Path(tempfile.mkdtemp(prefix="fusedwasi-cmp-"))
    try:
        component, core2 = h.build_p2(work)
        core1 = h.build_p1(work)
        exe = h.build_c_driver(work) if "fusedwasi-c" in whos else None
        (work / "p2.core.wasm").write_bytes(core2)
        makers = {
            "p2": {
                "wasmtime": lambda p: h.WasmtimeP2(component, p, ARGS, ENV),
                "fusedwasi": lambda p: h.FusedP2(core2, p, ARGS, ENV),
                "fusedwasi-c": lambda p: h.CHostP2(exe, work / "p2.core.wasm", p, ARGS, ENV),
            },
            "p1": {
                "wasmtime": lambda p: h.WasmtimeP1(core1, p, ARGS, ENV),
                "fusedwasi": lambda p: h.FusedP1(core1, p, ARGS, ENV),
                "fusedwasi-c": lambda p: h.CHostP1(exe, work / "p1.wasm", p, ARGS, ENV),
            },
        }
        results: dict[str, dict[str, list[tuple[str, str]] | str]] = {}
        for family, table in (("p2", sc.SCENARIOS_P2), ("p1", sc.SCENARIOS_P1)):
            for name, scenario in table.items():
                if selected and name not in selected:
                    continue
                results[name] = {}
                for who in whos:
                    base = Path(tempfile.mkdtemp(prefix="tree-", dir=work))
                    root = sc.make_tree(base)
                    maker = makers[family][who]
                    started: list[h.Rt] = []
                    preopens = [(str(root), "/")]

                    def fresh(maker=maker, started=started, preopens=preopens) -> h.Rt:
                        rt = maker(preopens)
                        started.append(rt)
                        return rt

                    try:
                        results[name][who] = scenario(fresh(), fresh)
                    except Exception:  # noqa: BLE001
                        results[name][who] = traceback.format_exc()
                    for rt in started:
                        if isinstance(rt, h.CHost):
                            rt.close()
        return results
    finally:
        shutil.rmtree(work, ignore_errors=True)


def compare(results: dict, left: str, right: str, show_all: bool) -> tuple[int, int]:
    """Prints the steps where `left` and `right` differ; returns (steps, steps that agree)."""
    total = same = 0
    for name, runs in results.items():
        a, b = runs.get(left), runs.get(right)
        print(f"== {name}  [{left} / {right}]")
        if isinstance(a, str) or isinstance(b, str):
            print("   a run failed:")
            for who, r in runs.items():
                if isinstance(r, str) and who in (left, right):
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
                print(f"   DIFF  {label}\n         {left}: {x}\n         {right}: {y}")
        if len(a) != len(b):
            print(f"   different number of steps: {len(a)} / {len(b)}")
    print(f"\n{left} / {right}: {same} of {total} steps agree\n")
    return total, same


def main() -> None:
    argv = sys.argv[1:]
    show_all = "--all" in argv
    out = None
    if "--json" in argv:
        out = argv[argv.index("--json") + 1]
    selected = [a for a in argv if not a.startswith("--") and a != out]
    whos = hosts_wanted()
    results = run_all(selected, whos)
    for left, right in (("wasmtime", "fusedwasi"), ("wasmtime", "fusedwasi-c"), ("fusedwasi", "fusedwasi-c")):
        if left in whos and right in whos:
            compare(results, left, right, show_all)
    if h.CHost.sanitizer_output:
        print(f"the C host wrote to its standard error ({len(h.CHost.sanitizer_output)} instances):")
        for text in h.CHost.sanitizer_output[:3]:
            print(text[:1500])
    elif "fusedwasi-c" in whos:
        print("the sanitizers (address, undefined) of the C host reported nothing")
    if out:
        Path(out).write_text(json.dumps(results, indent=1))


if __name__ == "__main__":
    main()
