# Before the first release

fusedwasi is an alpha and nothing is published. It should come out when `wasmext` and `wasmhost` work on it and what they depend on is not going to change, not before. Until then they pin it by commit (`wasmhost` in `uv.lock`; `wasmext` takes the headers through `fusedwasi.get_include()`), so waiting costs them nothing.

This file is a list of what to settle first. It is written 2026-10-26 from the code of the three repositories and from `host_scope.md`; an item marked **[check]** was not verified when this was written.

## 1. The names that the two users depend on

What `wasmext` (C) and `wasmhost` (Python) use today is, in effect, the public interface. It should be read once, kept or renamed on purpose, and then frozen.

C (counted in `wasmext/src`, by use):

- State of 0.2: `fusedwasi_p2_state` (fields read or written from outside: `user`, `trap`, `realloc`, `exited`, `exit_code`, `std[]`, `args`, `env_*`, `preopens`, `clock`, `random_bytes`, `sleep`, `wait`, `socket_addr_check`, `http_check`), `fusedwasi_p2_init`, `fusedwasi_p2_free`, `fusedwasi_p2_set_memory`, `fusedwasi_p2_stream` with `FUSEDWASI_P2_STREAM_OK / CLOSED / FAILED`, `fusedwasi_p2_preopen`.
- State of 0.1: `fusedwasi_p1_state` (fields: `core`, `user`, `finished`, `end_value`, `memory_data`, `memory_size`), `fusedwasi_p1_config`, `fusedwasi_p1_preopen`, `fusedwasi_p1_create`, `fusedwasi_p1_destroy`, `fusedwasi_p1_bind`, `fusedwasi_p1_errno`, `fusedwasi_p1_copy`, `FUSEDWASI_P1_END_EXIT / END_SIGNAL`, and the macros `FUSEDWASI_P1_MALLOC / CALLOC / FREE / WRITE`.
- The function of each import (`fusedwasi_p1_<name>`, `fusedwasi_p2_<package>_<name>`) is bound by the generator by name, so these names are an interface too.

Python (`wasmhost`): `fusedwasi.p1.wasi_p1`, `wasi_p1_unstable`, `wasi_p1_core.State` and `Exit`, `fusedwasi.p2.wasi_p2_core.Host`, `wasi_p2_io.InputStream / OutputStream / StreamClosed / StreamFailed`.

To decide: which fields are public (and read-only for the host), which are private. Today the end of a program is read straight from `core->exited` and `finished`; that is fine if it is written down as the contract.

## 2. Behaviour not yet checked against a real host

- 0.1 is held by the official wasi-testsuite (72 tests, run in the CI of `wasmext`). 0.2 is compared only with the text of the WIT and between the C and the Python (`host_scope.md`, "Checked against the WIT"); **it has not been compared with Wasmtime**. A script like `wasmext/docs/experiments/wasmtime_exit/` (the `wasmtime` package from pip) for `cli`, `filesystem`, `clocks` and `io` would show differences of behaviour.
- The deliberate differences from the letter of the WIT are listed in `host_scope.md`; each should be kept on purpose or removed.

## 3. Known gaps

| Gap | Where | Note |
| --- | --- | --- |
| `utime` without following a link on Python <= 3.12, Windows | `p2/wasi_p2_filesystem.py` | the Python follows the link; the C does not |
| IPv6 sockets | `wasi_p2_sockets` | tested only for the checks of addresses |
| Listener properties are not copied to accepted sockets | sockets | the system decides |
| `https` in C | `wasi_p2_http.h` | `internal-error` unless the embedder gives `http_send`; an optional TLS backend was deferred |
| Streaming of http bodies, HTTP/2, redirects, proxies | http | whole body, at most 256 MiB |
| `BCryptGenRandom` on Windows | random | written, not tested here **[check]** |
| What an import with no function answers | generator side | an error code or a trap: not decided |

## 4. Packaging

- Name and version: **[check]** that `fusedwasi` is free on PyPI; the first version is a pre-release (`0.1.0a1`).
- What the distribution carries: the Python twin, the C headers, `get-include`, `CMakeLists.txt` and `fusedwasi-config.cmake`; `tests/test_package.py` and `test_wasi_p1_headers.py` check parts of it. A test that builds the sdist and the wheel, installs them in a clean environment and compiles a C file against `get_include()` is **[check]** whether it exists.
- Licence and notices: the headers derived from the WIT and the witx of the specifications (`NOTICE`) **[check]**.

## 5. The order

1. `wasmext`: C2a to C2e (the public `Store`, the calls rewritten); `wasmhost`: stage 7. They exercise the interface of section 1.
2. Read and freeze section 1; close or defer the gaps of section 3 in writing.
3. The comparison with Wasmtime of section 2 for the packages a program on WASI uses most.
4. Release `0.1.0a1`. A stable `0.1.0` after the versioned store record (C7) has shown that the layouts shared between artifacts need no change.
