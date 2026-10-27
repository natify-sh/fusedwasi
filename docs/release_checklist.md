# Before the first release

fusedwasi is an alpha and nothing is published. It comes out when its interface is frozen **and shown neutral** (section 5), not when a particular user is ready. The users that exist today pin it by commit, so waiting costs them nothing; none of them is a condition of the release.

This file is a list of what to settle first. It is written 2026-10-26 from the code of the three repositories and from `host_scope.md`; an item marked **[check]** was not verified when this was written.

## 1. The public names

The names below are the interface of the library as it stands (read from what the two users in existence call). They are read once, kept or renamed on purpose, and then frozen. A name is public because any user needs it, not because one user calls it.

C:

- State of 0.2 (the world): `fusedwasi_p2_state` (fields read or written from outside: `user`, `std[]`, `args`, `env_*`, `preopens`, `clock`, `random_bytes`, `sleep`, `socket_addr_check`, `http_check`), `fusedwasi_p2_init`, `fusedwasi_p2_free`, `fusedwasi_p2_stream` with `FUSEDWASI_P2_STREAM_OK / CLOSED / FAILED`, `fusedwasi_p2_preopen`.
- Guest of 0.2 (the caller): `fusedwasi_p2_guest` (fields: `state`, `user`, `trap`, `realloc`, `wait`, `exit`, `exited`, `exit_code`), `fusedwasi_p2_guest_init`, `fusedwasi_p2_bind_memory` (primary), `fusedwasi_p2_set_memory` (convenience).
- WASI 0.1 has no state and no guest of its own (stage B of step 3, done): it uses the two above, with `fusedwasi_p2_guest.signal` (the hook of `proc_raise`), `fusedwasi_p1_open_descriptors(&state, &err, &bad)` and `fusedwasi_p1_errno`. Memory 0 of the guest is the memory of an instance of 0.1.
- The function of each import (`fusedwasi_p1_<name>`, `fusedwasi_p2_<package>_<name>`) is bound by the generator by name, so these names are an interface too.

Python: `fusedwasi.p1.wasi_p1`, `wasi_p1_unstable`, `fusedwasi.p2.wasi_p2_core.State`, `Guest` and `Exit` (the same classes as `fusedwasi.p1.State`, `Guest`, `Exit`), `fusedwasi.p1.open_descriptors`, `fusedwasi.p2.wasi_p2_filesystem.Filesystem` and `SystemFilesystem` (`State(filesystem=...)`), `wasi_p2_io.InputStream / OutputStream / StreamClosed / StreamFailed`.

To decide: which fields are public (and read-only for the host), which are private. The end of a call is read from the guest (`exited`, `exit_code`; for 0.1 `g->core.exited`); that is fine if it is written down as the contract (for both families: `proc_exit` of 0.1 leaves its code there too).

## 2. Behaviour not yet checked against a real host

- 0.1 is held by the official wasi-testsuite (72 tests), which **runs today only in the CI of a user**. It has to run in the CI of fusedwasi itself, before the step that puts 0.1 over 0.2 (section 5). 0.2 is compared with the text of the WIT and between the C and the Python (`host_scope.md`, "Checked against the WIT"), and **with Wasmtime 50.0.1: `wasi:filesystem` (262 operations) and `wasi:http` (23 requests), in Python and in C, run on 2026-10-27 and again after the rewrite of 0.1 over 0.2 (same lines, 0 differences that are not marked as known; `wasmtime_compare.md`, "Re-run after the 0.1-over-0.2 rewrite"); cli, clocks, random and WASI 0.1 as flat calls in Python only (301 of 365 steps agree, unchanged by the rewrite)**. **Not compared with a real host:** sockets, terminals, a real stdin, Windows, macOS, MSVC; and the C host for cli, clocks, random and io (only against the Python twin). None of this runs in the CI (it needs Wasmtime and the Rust target).
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
2. The official wasi-testsuite (0.1) and the neutral user in Python (the twin over `wasmtime-py`) in the CI of fusedwasi. This is the net under step 3 and the first half of the proof of neutrality. **Written** (`tests/neutral/`, job `python-wasmtime` of `neutral.yml`; 72 tests of the wasi-testsuite pass locally; see `design_model.md`, section 11).
3. 0.1 over 0.2 (`design_model.md`, section 4). **Done** (stage B: one state and one guest for both families, the descriptors of 0.1 are resources of the table of 0.2, the functions of 0.1 call the operations of 0.2; the 72 tests of the wasi-testsuite, the three neutral users and the C/Python parity tests pass; see `design_model.md`, section 4 and `host_scope.md`, "Changes of 0.1 that 0.2 forced").
4. The C user over an engine that is not `wasm2c` (`wasm3` first; a hand-made guest memory as the fallback), the second half of the proof of neutrality (`design_model.md`, section 11). **Written** (`tests/neutral/c_wasm3/`, job `c-wasm3` of `neutral.yml`; passes locally on Linux). A third, small Python user over `pywasm3` is in `tests/neutral/` (job `python-wasm3`).
5. Public/private marks, the dead names, the gaps of section 3 closed or deferred in writing; the public documents (`README.md`, the headers, the docstrings) say nothing about how a particular user works.
6. The remaining checks: Windows and macOS runs, the package test (section 4).
7. Tag and publish `0.1.0a1`. A stable `0.1.0` waits for use, not for a feature of a user: the states are opaque across artifacts (the layout does not cross), so no user's versioned record is a condition.
