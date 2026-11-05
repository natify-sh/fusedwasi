# The feature matrix (`docs/features.json`)

Status: written 2026-11-05 against the branch `wasip2-gap-review` at `16c7824`. Facts are marked **[read]** (read in a file of the repository), **[run]** (run by `tools/check_features.py` or a test of the repository) or **[not checked]**.

## What it is

A plain JSON file that says, for each thing a user asks about (an interface of WASI, a limit, a piece of the library), whether the C twin and the Python twin have it. The data is `docs/features.json`; `tools/check_features.py` checks it against the real code of both twins, so it cannot drift without a failing test (`tests/test_features.py`).

It is **our own format, on the example of the `features.json` of the WebAssembly website** (a JSON matrix with a status for each implementation and a note for a partial). We do not use the original because it does not fit: it lists WebAssembly proposals for each engine, `null` there is "not reported" (it cannot say "not there"), it has no level of modules, and it has no idea of two implementations that must be at parity **[read: the original, as the file of `natify-sh/waboros` copies it: top level `$source` and `runtimes`, each with `url`, `category`, `features`; the values are `true`, `null` or `["partial", note]`]**.

## The structure

```
{ "format": "fusedwasi-features", "version": 1, "wasi": "0.2.12", "$comment": "...",
  "twins": {"c": {"files": "src/fusedwasi/include/fusedwasi/"}, "python": {"files": "src/fusedwasi/"}},
  "modules": { "wasi:filesystem": { "title": "...", "files": {"c": "p2/wasi_p2_filesystem.h", "python": "p2/wasi_p2_filesystem.py"},
      "imports": 32,
      "features": { "types": {"covers": ["types"], "imports": 31, "c": true, "python": true} } } } }
```

* `wasi` is the version of WASI that the tables of signatures are counted against (0.2.12, `src/fusedwasi/signatures/wasi_p2.json`, `docs/host_scope.md`).
* A **module** is a unit a user thinks in: a package of WASI 0.2 (`wasi:cli`, `wasi:clocks`, `wasi:filesystem`, `wasi:http`, `wasi:io`, `wasi:random`, `wasi:sockets`), a snapshot of WASI 0.1 (`wasi_snapshot_preview1`, `wasi_unstable`), or a piece of the library with no imports (`host`: the state and the guest; `p1OverP2`: the layer that puts 0.1 over 0.2). It names its file in each twin (a string or a list) and the number of its `imports`; `stability` is `unstable` for sockets and http (`docs/host_scope.md`, Known limits).
* A **feature** is an interface (or a group of calls of 0.1), or a behaviour that the documents list as a limit (`ipv6`, `https`, `streamingBodies`, `serverSide`, ...). Names are camelCase. `imports` is how many imports it has (0 for a behaviour).
* The status of a twin, `c` and `python`, is `true` (every function of the feature is there and the documents name no limit), `false` (not there) or `["partial", "<note>"]` (the documents name a limit; the note says which and in which file, for example `docs/host_scope.md, Known limits`). There is no `null`. A `note` (a string, or an object with `c` and `python`) may go with any status.
* `covers` says which imports belong to the feature: an interface of the module (`"types"`), `"interface#name"` for one import of it, a call of 0.1 or a pattern (`"fd_*"`), `"@differs"` (the five calls of `wasi_unstable` that differ, `unstable.differs` of `wasi_p1.json`) and `"@rest"` (what no other feature claims). The strongest claim wins; every import of the tables must have exactly one owner.
* `symbols` (optional) lists what a feature without imports stands on, for each twin. In C: an identifier of the library (`fusedwasi_...`, `FUSEDWASI_...`: a function or a type), or **`Struct.field`** for a field of a struct (`fusedwasi_p2_state.tls`), which is looked for in the body of that struct and nowhere else; a bare word such as `tls` is refused, because it would pass for any identifier of any header **[run: `tests/test_features.py`]**. In Python: `module:Name`, `module:Class.attr` or `module:Class(param)`.
* `limits` (required) maps **every limit** that `docs/host_scope.md` lists as a bullet in "Known limits" to the features that stand for it (`["wasi:sockets/ipv6", ...]`) or to `{"not a feature": "why, and a file"}`. Its keys are the titles of the bullets (without links, parentheses and the full stop), no more and no fewer: a limit that is added to the document and not here, or dropped from the document and left here, is an error, and so is a feature that does not exist **[run: `tests/test_features.py`]**. The checker proves the *list* of limits this way; that each claim is true is what the statuses, the symbols and `tests` are for.
* `tests` (optional) lists the tests that stand behind a feature: an object with `c`, `python` and `both` (a test that runs both twins, such as the scenarios compared by `test_c_and_python_agree`), each a list of `tests/file.py::test_name`. The checker looks that each one is a `def test_...` in that file, so a test that is renamed or deleted breaks the matrix. **A feature whose note names a file of `tests/` must have `tests`, and each twin must be covered**, by its own list or by `both`: a limit that is pinned for one twin only is a difference of the twins that nobody noticed. The checker does not *run* the tests: the CI does, with the whole suite.

## How to run it

```
python tools/check_features.py             # check; exit 0, or 1 and a line "MISMATCH ..." for each problem
python tools/check_features.py --list      # the modules and the features as a table
python tools/check_features.py --json      # what the code has, for each feature and twin: present, of, missing
python tools/check_features.py --doc FILE  # check another file
```

Standard library only, no network, about a tenth of a second **[run]**. For both twins in the same way: the C name of each function (`c_name` of the tables) must be defined in a header of `include/fusedwasi/p1/` or `p2/` (a regular expression, the idea of `tests/test_known_limits.py::test_the_c_and_the_python_have_a_function_for_the_same_imports`); the Python name must be a function of its module (imported, `getattr`). `true` needs all of them; `false` needs none; a `partial` of functions needs some, and its note must say what is missing. A partial or false note must name a file of the repository that exists. A `true` with nothing to check is refused. The script also checks that the modules and the counts equal the tables (204 imports of 0.2, 46 calls of the snapshot, 45 of `wasi_unstable`), that no function of the twins is missing from the tables, and the `format` and `version`.

## The names

Module `wasi:<package>`: a feature `types` is the interface `wasi:<package>/types`; `environment`, `exit`, `monotonicClock`, `ipNameLookup`, `tcpCreateSocket` and the like are the interface of that name in camelCase (`stdio` is `stdin`, `stdout` and `stderr`; `terminals` the five `terminal-*`). In `wasi:http`, `types` is the 62 stable imports of `types` and `informationalOutboundResponses` is the one unstable import, `[method]response-outparam.send-informational`. `wasi:clocks/timezone` (2 imports) and that import are unstable and have no function in either twin **[read: `wasi_p2.json`: no `c_name`, no `python_name`]**; `wasi:sockets/network` counts `network-error-code`, which is unstable and which both twins have. In 0.1 the features are groups of calls by name (`fd_*`, `path_*`, `sock_*`, ...).

## Where each partial comes from

| Feature | Twin | Source |
| --- | --- | --- |
| `wasi:http` `types` | both | `docs/host_scope.md`, Known limits, "Bodies of http are whole" (256 MiB, `http-error-code` always `none`) |
| `wasi:http` `outgoingHandler` | both | same file, "No HTTP/2, no redirects, no proxies" |
| `https` | C | same file, "`https` in C needs a TLS hook or `http_send`"; `docs/specs/S0001.md`; `docs/tls_hook.md` (Python has TLS: `true`) |
| `serverSide` | both | `docs/public_api.md`, "The transport and the server side of wasi:http (unstable in the alpha)" |
| `wasi_snapshot_preview1` and `wasi_unstable` `sockets` | both | same file, "An import with no function" (the four calls answer `NOTSOCK` or `BADF`) |

`false` with a note: `tlsHook` in Python is **deliberate** (`docs/host_scope.md`, "The Python twin has TLS and no way to set it"; `docs/specs/S0001.md` 3.3) and `tlsHook` in C is `true` (`state->tls`); `streamingBodies`, `http2`, `redirects`, `proxies` are the limits of the same bullets; `timezone` and `informationalOutboundResponses` are unstable items without a function.

**The twins are held at parity:** where the statuses of the two twins differ, the checker requires that **each side** names a file of the repository that says why (today `https` and `tlsHook`, both deliberate: `docs/host_scope.md`, Known limits; `docs/specs/S0001.md`). A third difference that nobody wrote down is an error **[run: `tests/test_features.py`]**.

**The rule for `partial`:** it is a gap of the library, or a claim that nobody has checked. A behaviour that the system or the specification defines is not one: a socket of the family IPv6 on a machine with no IPv6 is `not-supported`, which is the answer that the WIT gives for it, so `tcpCreateSocket`, `udpCreateSocket` and `ipv6` are `true` (the checks of addresses are run in both twins on any machine, `tests/test_wasi_p2_address_checks.py`, and a whole session over `::1` is compared where there is IPv6). A `partial` says its own note: the note of the feature does not stand in for it.

What is *not* a partial here: the sockets and http being "unstable in the alpha" (a promise about names, not a missing function; it is the `stability` of the module); the differences from the letter of the WIT that `docs/host_scope.md` ("Checked against the WIT") keeps on purpose (`blocking-write-and-flush` over 4096 bytes, `get-random-bytes` over 64 MiB, `sync` on a read-only descriptor); and the limits that were fixed (accepted sockets now get the options of the listener, `BCryptGenRandom` is run in the CI on Windows, IPv6 sessions are tested). **[not checked]**: that every limit of the documents is in the file; the list was taken from "Known limits" and `docs/release_checklist.md`, section 3, by reading.

## What it does not cover

`docs/public_api.md` is not repeated here: the public names are kept by `tests/test_public_api.py`. The pieces `host` and `p1OverP2` list a few public names, not all.
