# Before the first release

fusedwasi is an alpha and nothing is published. It comes out when its interface is frozen **and shown neutral** (section 5), not when a particular user is ready. The users that exist today pin it by commit, so waiting costs them nothing; none of them is a condition of the release.

This file is a list of what to settle first. It is written 2026-10-26 from the code of the three repositories and from `host_scope.md`; an item marked **[check]** was not verified when this was written.

## 1. The public names

**Done 2026-10-27 (step 5).** The public names are listed in [`public_api.md`](public_api.md), made from `src/fusedwasi/public_api.json` (C and Python, a line of purpose each, the fields of the public structs; wasi:http and wasi:sockets marked unstable). Every other name is private and carries a mark (a double underscore after the prefix in C, a leading underscore in Python; `design_model.md`, section 5), and `tests/test_public_api.py` fails when a name is neither listed nor marked, when a listed name does not exist, and when a neutral user (`tests/neutral/`) uses a private name. The names that changed are in [`private_names.md`](private_names.md). The function of each import (`fusedwasi_p1_<name>`, `fusedwasi_wasi_<package>_<interface>_<name>`) is bound by name by the generators; those 247 names are public by being in the tables of signatures.

What the owner still has to confirm (they were chosen, not decided): the helpers of the canonical ABI (`mem`, `load_*`, `store_*`, `alloc`, `string`, `table_get`) are private; the kinds `FUSEDWASI_P2_KIND_*` are public; the transport and the server side of http are public and unstable.

## 2. Behaviour not yet checked against a real host

- 0.1 is held by the official wasi-testsuite (72 tests), which **runs today only in the CI of a user**. It has to run in the CI of fusedwasi itself, before the step that puts 0.1 over 0.2 (section 5). 0.2 is compared with the text of the WIT and between the C and the Python (`host_scope.md`, "Checked against the WIT"), and **with Wasmtime 50.0.1: `wasi:filesystem` (262 operations) and `wasi:http` (23 requests), in Python and in C, run on 2026-10-27 and again after the rewrite of 0.1 over 0.2 (same lines, 0 differences that are not marked as known; `wasmtime_compare.md`, "Re-run after the 0.1-over-0.2 rewrite"); cli, clocks, random and WASI 0.1 as flat calls in Python only (301 of 365 steps agree, unchanged by the rewrite)**. **Not compared with a real host:** sockets, terminals, a real stdin, Windows, macOS, MSVC; and the C host for cli, clocks, random and io (only against the Python twin). None of this runs in the CI (it needs Wasmtime and the Rust target).
- The deliberate differences from the letter of the WIT are listed in `host_scope.md`; each should be kept on purpose or removed.

## 3. Known gaps

Written down 2026-10-26 as "Known limits" in `host_scope.md`, with tests in `tests/test_known_limits.py`. Settled 2026-10-27 (step 5): each row is fixed, or marked in writing as deferred or unstable in the alpha, with where it is tested. Sockets and http are marked unstable in the README feature list and in `host_scope.md`, "Known limits", too.

| Gap | Where | Status in the alpha | Tested |
| --- | --- | --- | --- |
| `utime` without following a link on Python <= 3.12, Windows | `p2/wasi_p2_filesystem.py` | **fixed** (the Python touches the link itself on Windows before 3.13) | `tests/test_wasi_p2_filesystem.py` (for the Windows runners; not run on Windows by hand) |
| IPv6 sockets | `wasi_p2_sockets` | **unstable in the alpha** | the checks of addresses, `test_ipv6_sockets_follow_the_system` (`test_wasi_p2_sockets.py`, `test_known_limits.py`); a whole session over `::1` is not tested |
| Listener properties are not copied to accepted sockets | sockets | **unstable in the alpha**; the system decides | not tested (the result is the system's) |
| `https` in C | `wasi_p2_http.h` | **deferred**: `internal-error` unless the embedder gives `http_send`; the optional TLS backend is not in the alpha | `test_http_the_c_has_no_tls` (`test_known_limits.py`) |
| Streaming of http bodies, HTTP/2, redirects, proxies | http | **unstable in the alpha**: whole body, at most 256 MiB, HTTP/1.1 | `test_known_limits.py` (the limit, the redirect, the proxy variables, one connection each) |
| `BCryptGenRandom` on Windows | random | **deferred to step 6** (a run on Windows): written; the scenario `default_source` runs the default source on every system with a C compiler | `tests/test_wasi_p2_random.py`; whether the Windows runner ran it is read from the CI log, below |
| What an import with no function answers | the user that links the imports | **not the library's to decide**: it has a function for every import; recommended default `ENOSYS` for 0.1, a trap for 0.2 | `host_scope.md`, "Known limits" |
| Sockets and http (63 and 58 imports), `https` in C, IPv6 | | **unstable in the alpha**: no promise on their behaviour or names until a real program uses them | as above |

## 4. Packaging

The name `fusedwasi` is already taken on PyPI by the owner (2026-10-26) and is ready for a publication, so the first release is a pre-release there (`0.1.0a1`), which the users can then install with `--pre` instead of a commit. What the distribution carries: the Python twin, the C headers, `get_include()`, `CMakeLists.txt` and `fusedwasi-config.cmake`. **[check]** before the upload: a test that builds the sdist and the wheel, installs them in a clean environment and compiles a C file against `get_include()` (`tests/test_package.py` and `test_wasi_p1_headers.py` cover parts of it); the licence and the notices of what the headers derive from (the WIT and the witx) are in the sdist; the upload is a trusted publisher of the CI, not a token in a file.

## 5. The order

1. The state and the guest (done, `3c3924b`, `b4aa2db`): the model of `design_model.md`.
2. The official wasi-testsuite (0.1) and the neutral user in Python (the twin over `wasmtime-py`) in the CI of fusedwasi. This is the net under step 3 and the first half of the proof of neutrality. **Written** (`tests/neutral/`, job `python-wasmtime` of `neutral.yml`; 72 tests of the wasi-testsuite pass locally; see `design_model.md`, section 11).
3. 0.1 over 0.2 (`design_model.md`, section 4). **Done** (stage B: one state and one guest for both families, the descriptors of 0.1 are resources of the table of 0.2, the functions of 0.1 call the operations of 0.2; the 72 tests of the wasi-testsuite, the three neutral users and the C/Python parity tests pass; see `design_model.md`, section 4 and `host_scope.md`, "Changes of 0.1 that 0.2 forced").
4. The C user over an engine that is not `wasm2c` (`wasm3` first; a hand-made guest memory as the fallback), the second half of the proof of neutrality (`design_model.md`, section 11). **Written** (`tests/neutral/c_wasm3/`, job `c-wasm3` of `neutral.yml`; passes locally on Linux). A third, small Python user over `pywasm3` is in `tests/neutral/` (job `python-wasm3`).
5. Public/private marks, the dead names, the gaps of section 3 closed or deferred in writing. **Done 2026-10-27** (`design_model.md` section 5; `public_api.md`; `private_names.md`; `tests/test_public_api.py`). The public documents (`README.md`, the headers, the docstrings) say nothing about how a particular user works.
6. The remaining checks: Windows and macOS runs, the package test (section 4).
7. Tag and publish `0.1.0a1`. A stable `0.1.0` waits for use, not for a feature of a user: the states are opaque across artifacts (the layout does not cross), so no user's versioned record is a condition.
