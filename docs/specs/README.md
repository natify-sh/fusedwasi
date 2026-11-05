# Specs

A spec is a short design document for a change that needs a decision before the code is written. One file for each, numbered in order: `S0001.md`, `S0002.md`, and so on. A number is never reused.

Each spec starts with its **Status**: `draft` (a proposal, nothing decided), `accepted` (the owner decided; the decisions are in the text), `implemented` (done; the text says where), or `rejected` / `superseded` (and by what). The facts in a spec are marked as in the other documents of `docs/`: **[read]** read in the code or the source, **[run]** run, **[not checked]** said from memory or not checked.

| Spec | Title | Status |
| --- | --- | --- |
| [S0001](S0001.md) | A TLS hook for the C transport of `wasi:http` (Python keeps `ssl`) | accepted |
| [S0002](S0002.md) | A ready TLS backend for the C transport of `wasi:http`, over Mbed TLS | draft, deferred |
| [S0003](S0003.md) | Streaming of `wasi:http` bodies (the answer is read whole today) | draft |
| [S0004](S0004.md) | Redirects and proxies for `wasi:http` | draft |
| [S0005](S0005.md) | HTTP/2 for `wasi:http` | draft |
| [S0006](S0006.md) | `tls_wrap` in the Python twin (a setting of TLS for `wasi:http`) | draft |
| [S0007](S0007.md) | The error of a path that leaves the preopen in WASI 0.1 (row E1) | draft |
| [S0008](S0008.md) | The comparison with Wasmtime in the CI | draft |
| [S0009](S0009.md) | The Python twin on PyPy and on Pyodide (state of 2026-11-05, nothing fixed) | draft |
| [S0010](S0010.md) | Android in the CI: what the job is and what it is not (state of 2026-11-05) | draft |
