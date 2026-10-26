# fusedwasi against Wasmtime

The results and the verdicts are in [`docs/wasmtime_compare.md`](../../wasmtime_compare.md).

* `harness.py`: generates one guest (WAT, from a table of imports), builds it as a component (WASI 0.2) or a core module (WASI 0.1), and runs it in Wasmtime and over the Python twin of fusedwasi with the same poke / peek / run protocol.
* `scenarios.py`: the steps (cli, clocks, random, filesystem, WASI 0.1), written once for both.
* `run.py`: runs them and prints the steps that differ (`--all` prints all, `--json FILE` keeps them).

Needs `wasmtime` from pip, `wasm-tools` on the PATH and the WIT of WASI 0.2.12 (`WASI_WIT=<dir>`, else the `wasip2` crate of `~/.cargo/registry`). Not run in CI.
