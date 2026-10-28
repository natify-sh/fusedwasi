# fusedwasi against Wasmtime

The results and the verdicts are in [`docs/wasmtime_compare.md`](../../wasmtime_compare.md).

* `harness.py`: generates one guest (WAT, from a table of imports), builds it as a component (WASI 0.2) or a core module (WASI 0.1), and runs it in Wasmtime, over the Python twin of fusedwasi and over the C headers of fusedwasi (through wasm3), with the same poke / peek / run protocol.
* `cmp_wasm3.c`: the C host, a `main` over the neutral user `tests/neutral/c_wasm3/neutral_wasm3.c`; it serves poke / peek / run on a pipe. The glue to the headers is generated from `fusedwasi.signatures` by `tests/neutral/c_wasm3/gen_wasm3_glue.py`; `harness.build_c_driver` builds it (with the address and undefined sanitizers).
* `scenarios.py`: the steps (cli, clocks, random, filesystem, WASI 0.1), written once for both.
* `run.py`: runs them and prints the steps that differ (`--all` prints all, `--json FILE` keeps them). `HOST=python` (default) compares Wasmtime with the Python twin, `HOST=c` Wasmtime with the C headers, `HOST=all` the three (and C against Python).

Needs `wasmtime` from pip, `wasm-tools` on the PATH and the WIT of WASI 0.2.12 (`WASI_WIT=<dir>`, else the `wasip2` crate of `~/.cargo/registry`). `HOST=c` and `HOST=all` also need a C compiler and `WASM3_SRC` (a checkout of wasm3 v0.5.0). Not run in CI.

**Runs on the current API** (one `State`, one `Guest`; checked 2026-10-28: `HOST=all python run.py` prints "301 of 365 steps agree" for Wasmtime against the Python twin, "301 of 365" for Wasmtime against the C headers and "365 of 365" for the Python twin against the C headers, the figures of `wasmtime_compare.md`). Still not covered: sockets, http, terminals, a stdin with data, Windows and macOS.
