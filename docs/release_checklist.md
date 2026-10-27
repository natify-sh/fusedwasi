# Before the first release

fusedwasi is an alpha and nothing is published. It comes out when its interface is frozen **and shown neutral** (section 5), not when a particular user is ready. The users that exist today pin it by commit, so waiting costs them nothing; none of them is a condition of the release.

This file is a list of what to settle first. It is written 2026-10-26 from the code of the three repositories and from `host_scope.md`; an item marked **[check]** was not verified when this was written.

## 1. The public names

The names below are the interface of the library as it stands (read from what the two users in existence call). They are read once, kept or renamed on purpose, and then frozen. A name is public because any user needs it, not because one user calls it.

C:

- State of 0.2 (the world): `fusedwasi_p2_state` (fields read or written from outside: `user`, `std[]`, `args`, `env_*`, `preopens`, `clock`, `random_bytes`, `sleep`, `socket_addr_check`, `http_check`), `fusedwasi_p2_init`, `fusedwasi_p2_free`, `fusedwasi_p2_stream` with `FUSEDWASI_P2_STREAM_OK / CLOSED / FAILED`, `fusedwasi_p2_preopen`.
- Guest of 0.2 (the caller): `fusedwasi_p2_guest` (fields: `state`, `user`, `trap`, `realloc`, `wait`, `exit`, `exited`, `exit_code`), `fusedwasi_p2_guest_init`, `fusedwasi_p2_bind_memory` (primary), `fusedwasi_p2_set_memory` (convenience).
- State of 0.1: `fusedwasi_p1_state` (fields: `core`, `user`), `fusedwasi_p1_config`, `fusedwasi_p1_preopen`, `fusedwasi_p1_create`, `fusedwasi_p1_destroy`, `fusedwasi_p1_errno`, `fusedwasi_p1_copy`, and the macros `FUSEDWASI_P1_MALLOC / CALLOC / FREE / WRITE`.
- Guest of 0.1: `fusedwasi_p1_guest` (fields: `core` (a guest of 0.2: memory, `trap`, `wait`, `exited`, `exit_code`, `user`), `state`, `signal`), `fusedwasi_p1_guest_init`, `fusedwasi_p1_bind`.
- The function of each import (`fusedwasi_p1_<name>`, `fusedwasi_p2_<package>_<name>`) is bound by the generator by name, so these names are an interface too.

Python: `fusedwasi.p1.wasi_p1`, `wasi_p1_unstable`, `wasi_p1_core.State`, `Guest` and `Exit`, `fusedwasi.p2.wasi_p2_core.State` and `Guest`, `wasi_p2_io.InputStream / OutputStream / StreamClosed / StreamFailed`.

To decide: which fields are public (and read-only for the host), which are private. The end of a call is read from the guest (`exited`, `exit_code`; for 0.1 `g->core.exited`); that is fine if it is written down as the contract.

## 2. Behaviour not yet checked against a real host

- 0.1 is held by the official wasi-testsuite (72 tests), which **runs today only in the CI of a user**. It has to run in the CI of fusedwasi itself, before the step that puts 0.1 over 0.2 (section 5). 0.2 is compared only with the text of the WIT and between the C and the Python (`host_scope.md`, "Checked against the WIT"); **it was compared with Wasmtime 50.0.1 on 2026-10-27 (filesystem and http, in Python and in C; cli, clocks and random in Python only; no sockets, terminals, Windows or macOS; see `wasmtime_compare.md`)**. The sockets, terminals, a real stdin, Windows and macOS were not compared, and the C host was not compared for cli, clocks, random and io (only in Python).
- The deliberate differences from the letter of the WIT are listed in `host_scope.md`; each should be kept on purpose or removed.

## 3. Known gaps

Done 2026-10-26: the first row is fixed (the Python touches the link itself on Windows before 3.13; the test is written for the Windows runners and was not run on Windows) and the others are written down as "Known limits" in `host_scope.md`, with tests in `tests/test_known_limits.py`. The comparison of section 2 is in `wasmtime_compare.md`; the review of section 1 is in `interface_review.md`.

| Gap | Where | Note |
| --- | --- | --- |
| `utime` without following a link on Python <= 3.12, Windows | `p2/wasi_p2_filesystem.py` | the Python follows the link; the C does not |
| IPv6 sockets | `wasi_p2_sockets` | tested only for the checks of addresses |
| Listener properties are not copied to accepted sockets | sockets | the system decides |
| `https` in C | `wasi_p2_http.h` | `internal-error` unless the embedder gives `http_send`; an optional TLS backend was deferred |
| Streaming of http bodies, HTTP/2, redirects, proxies | http | whole body, at most 256 MiB |
| `BCryptGenRandom` on Windows | random | written, not tested here **[check]** |
| What an import with no function answers | the user that links the imports | not the library's to decide (it has a function for every import); recommended default: `ENOSYS` for 0.1, a trap for 0.2 |
| Sockets and http (63 and 58 imports), `https` in C, IPv6 | | **unstable in the alpha**: no promise on their behaviour or names until they are used by a real program |

## 4. Packaging

The name `fusedwasi` is already taken on PyPI by the owner (2026-10-26) and is ready for a publication, so the first release is a pre-release there (`0.1.0a1`), which the users can then install with `--pre` instead of a commit. What the distribution carries: the Python twin, the C headers, `get_include()`, `CMakeLists.txt` and `fusedwasi-config.cmake`. **[check]** before the upload: a test that builds the sdist and the wheel, installs them in a clean environment and compiles a C file against `get_include()` (`tests/test_package.py` and `test_wasi_p1_headers.py` cover parts of it); the licence and the notices of what the headers derive from (the WIT and the witx) are in the sdist; the upload is a trusted publisher of the CI, not a token in a file.

## 5. The order

1. The state and the guest (done, `3c3924b`, `b4aa2db`): the model of `design_model.md`.
2. The official wasi-testsuite (0.1) and the neutral user in Python (the twin over `wasmtime-py`) in the CI of fusedwasi. This is the net under step 3 and the first half of the proof of neutrality.
3. 0.1 over 0.2 (`design_model.md`, section 4).
4. The C user over an engine that is not `wasm2c` (`wasm3` first; a hand-made guest memory as the fallback), the second half of the proof of neutrality (`design_model.md`, section 11).
5. Public/private marks, the dead names, the gaps of section 3 closed or deferred in writing; the public documents (`README.md`, the headers, the docstrings) say nothing about how a particular user works.
6. The remaining checks: Windows and macOS runs, the package test (section 4).
7. Tag and publish `0.1.0a1`. A stable `0.1.0` waits for use, not for a feature of a user: the states are opaque across artifacts (the layout does not cross), so no user's versioned record is a condition.
