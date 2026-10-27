# Signatures of the WASI imports

One table, inside fusedwasi, of the signature of every function that a core module imports from WASI: `wasi_snapshot_preview1` (WASI 0.1) and `wasi:*@0.2.12` (WASI 0.2), with the names of the parameters of the specification, one line of documentation each, and a ready-made `typing.Protocol` per function. It is data: fusedwasi does not use it to run anything, and it knows nothing of the programs that will read it.

## The files

All in `src/fusedwasi/signatures/` (a Python package, so they travel with the wheel; CMake installs the data files to `share/fusedwasi/signatures`):

| File | What it is |
| --- | --- |
| `wasi_p1.json` | 46 functions of `wasi_snapshot_preview1` |
| `wasi_p2.json` | 204 functions of `wasi:*@0.2.12`: the 200 imports of the command world and of `wasi:http` that a host must answer (137 + 63, the number `tools/wasi_p2_audit` counts), and 4 functions that the WIT marks `@unstable` (`stability: "unstable"` with its `feature`) |
| `wasi_p1_protocols.pyi`, `wasi_p2_protocols.pyi` | one `class <CamelName>(Protocol)` per function |
| `__init__.py` | `load("p1")`, `load("p2")`, `find_import(module, name)` and `get_signatures_path()` (also `fusedwasi.get_signatures_path()`, the sibling of `get_include()`) |

The header of each JSON has `source` (the repository, the tag, the commit and the sha256 of every source file read), `count` and `api`. Each function has:

- `module`, `name`: as a core module imports it. For 0.2 the canonical lowered import: module `wasi:io/streams@0.2.12`, name `[method]input-stream.read`, `[static]...`, `[constructor]...`, `[resource-drop]...` or the plain function name.
- `official_name`: the name in the specification (`read`; for 0.1 the same as `name`; `constructor` and `resource-drop` for those two kinds). For 0.2 also `package`, `interface`, `kind` (`func`, `method`, `static`, `constructor`, `resource-drop`), `resource`, `stability`, `feature`.
- `protocol`: the name of the class in the `.pyi` (0.1: `FdRead`; 0.2: package, interface, resource, function, e.g. `IoStreamsInputStreamRead`; unique, the generator stops if two clash).
- `doc`: the first sentence of the comment of the specification (empty for the 44 imports of 0.2 that have none: the drops and a few constructors).
- `c_name`, `python_module`, `python_name`, `takes_comp` (added in round 2, schema still 1: the fields are new, none is changed): where fusedwasi has the function. `c_name` is the C symbol (`fusedwasi_p1_<name>`; `fusedwasi_wasi_<package>_<interface>[_<resource>]_<function>`), `python_module` and `python_name` the module of the twin (`fusedwasi.p1.wasi_p1`, `fusedwasi.p2.wasi_p2_<package>`) and the function in it, `takes_comp` whether a function of 0.2 takes the number of the memory after the guest (in C and in Python alike; always false for 0.1). The generator reads the headers and the Python files as text and stops if the two twins and the table do not agree; the three functions that fusedwasi leaves out on purpose have `null` in all four. `Function.python_function()` imports and returns the Python function.
- `params`: the flat core parameters as the core module sees them, each `{name, type, role, source_type}`; `results`: the flat results (same shape).

**Types and roles.** `type` is `i32`, `i64`, `f32` or `f64`. `role` is `pointer`, `length` (the second half of a string or a list, or a parameter of the type `size` in 0.1), `handle` (a descriptor of 0.1, an `own` or `borrow` of 0.2), `value`, `tag` (the discriminant of a variant, an option or a result), `retptr` (a pointer to memory where the result is written) or `errno` (the result of a function of 0.1).

**0.1** follows the witx ABI: a `string` or a list is a pointer and a length (`path`, `path_len`); a result `expected T (error errno)` is the `errno` returned, and each value of `T` (a `tuple` has several) is written through an extra pointer parameter at the end, named `retptr` (`retptr0`, `retptr1` for two). `proc_exit` has no result.

**0.2** follows the Canonical ABI as a module imports it (lowering): at most 16 flat parameters (more become one pointer `args`); a flat result of at most one value is returned; otherwise the result is written through the extra last parameter `retptr` and there is no flat result. A parameter that flattens to several values gets the names `name_len` (string, list), `name_<field>` (record), `name_<n>` (tuple), `name_tag` and `name_val0`, `name_val1`, ... (variant, option, result; the values are joined across the cases as the Canonical ABI says, and a joined value has a role only if all the cases agree). Hyphens of the WIT become underscores. The first parameter of a method is `self`.

**The stubs.** `def __call__(self, <names>: int, /) -> int | None`. The parameters are positional-only, so the names document and do not constrain: a function with other names is accepted, one with a wrong number of parameters is not (`tests/test_signatures.py` proves both with pyright). Names that are not valid in Python get a trailing underscore: keywords (`in` of `poll_oneoff` becomes `in_`) and `self` (`self_`, since `self` is the first parameter of `__call__`). A float is `float`, everything else `int`. The docstring has the documentation, the import, the errno meaning (0.1) or where the result goes.

## Python

```python
from fusedwasi import signatures

p2 = signatures.load("p2")  # dataclasses: Signatures, Function, Param
f = p2.get("wasi:io/streams@0.2.12", "[method]input-stream.read")
[
    (p.name, p.type, p.role) for p in f.params
]  # [('self', 'i32', 'handle'), ('len', 'i64', 'value'), ('retptr', 'i32', 'retptr')]
signatures.get_signatures_path()  # the directory with the files
```

## Binding an import (a host)

`fusedwasi.signatures.find_import(module, name)` takes what a core module imports and gives the `Function`, or None: `("wasi_snapshot_preview1", "fd_write")`, or `("wasi:io/streams@0.2.6", "[method]output-stream.write")`. A package of 0.2 is matched whatever the patch version is (the table is of `0.2.12`; `@0.2.0` to `@0.2.99` find the same row; another major.minor finds nothing; `wasi_unstable` is not in the table and gives None). The row has the flat parameters and results (to link the import with the right types), the C symbol, the Python function (`python_function()`) and `takes_comp`. Nothing else is needed to bind an import: `tests/neutral/` binds the imports of a module with this call alone, in Python over wasmtime-py and pywasm3, and in C over wasm3 (the glue is generated from the table).

## Rule for the consumer (a stub generator)

Copy the Protocols a module needs into its own `.pyi` (with `from typing import Protocol`). **Do not import fusedwasi in a `.pyi`**: the stub must work where fusedwasi is not installed. `tests/test_signatures.py` checks that the stubs here import nothing but `typing.Protocol`, so each class can be copied alone. To find the Protocols a module needs, look up each of its imports as `(module, name)` with `Signatures.get`. The version is part of the key (`wasi:io/streams@0.2.12`); the table is of 0.2.12 only, and `find_import` (above) matches any `@0.2.N` to it.

## Regenerating

```
python tools/make_signatures.py            # writes the four files
python tools/make_signatures.py --check    # compares with the files on disk, exit 1 on a difference
```

Standard library only, offline. The generator reads `tools/spec_sources/` as text and runs nothing from it. It has its own small parser for the witx and for the subset of WIT that WASI 0.2 uses, and the Canonical ABI flattening (the rules of `tools/wasi_p2_audit/audit_sigs.py`, which needs `wasm-tools`, extended to flags of more than 32 labels; WASI 0.2 has none). `tests/test_signatures.py` regenerates in memory and compares byte for byte with the committed files, so a change of the generator or of a source that is not followed by a regeneration fails the tests. The `.pyi` files are written in the form `ruff format` leaves them (a test runs `ruff format --check` on them).

## Sources

Fetched from `https://github.com/WebAssembly/WASI` with a clone over HTTPS and copied byte for byte (never executed), with the licence (`LICENSE.md`), into `tools/spec_sources/`; `sources.json` there is the pin, and the sha256 of every file is in the header of each JSON.

| | Tag | Commit | Files |
| --- | --- | --- | --- |
| WASI 0.1 | `v0.2.9` | `34c0258951fbd31b2e7591c5d4cdc5ebc760bcfc` | `legacy/preview1/witx/typenames.witx`, `wasi_snapshot_preview1.witx` (to `wasi_p1/`) |
| WASI 0.2 | `v0.2.12` | `281ba75fafcd50961ef55f9e52747afcc9b71ede` | `proposals/{cli,clocks,filesystem,http,io,random,sockets}/wit/*.wit` (to `wasi_p2/<package>/`) |

The witx files are not in the tag `v0.2.12` (nor in `main`): they were moved to the `wasi-0.1` branch after `v0.2.9`, the last tag that carries them. The two tags are two different sources, not two versions of one: 0.1 and 0.2 are described in different languages (witx and WIT). The interface of `wasi_snapshot_preview1` is stable, but the **text** of its witx was not frozen: see "Checked against other copies" below. The first snapshot, `wasi_unstable`, has no table of its own: its functions have the names and the number of parameters of `wasi_snapshot_preview1` (five differ in the layout of what they write: `fd_seek`, `fd_fdstat_get`, `fd_filestat_get`, `path_filestat_get`, `poll_oneoff`; `sock_accept` is not in it), so the table of `p1` serves both.

To move to another version: replace the files of `tools/spec_sources/`, change `sources.json`, run the generator and the tests.

Left out on purpose: the two exports (`wasi:cli/run#run`, `wasi:http/incoming-handler#handle`), which are not imports; worlds; `[resource-new]`/`[resource-rep]` (a host imports only `[resource-drop]`).

Checked once by hand, not part of the tests: the output for 0.2 was compared with `wasm-tools component wit --json` of the same files and the flattening of `audit_sigs.py`. Same 200 stable functions and the same flat types for each (the first run differed in the result of the four `[constructor]`s of `wasi:http`, which return the resource; fixed).

## Checked against other copies (2026-10-27)

The committed witx of 0.1 (`v0.2.9`) was compared by hand, outside the tests, with two other copies and with a header generated from the same files:

- **The text changed between versions.** `snapshot-01` (`phases/snapshot/witx/`, the original place) and `v0.2.0` (`legacy/preview1/witx/`) are not byte-equal to `v0.2.9`: the syntax of witx was modernised (`(enum (@witx tag u32)`, `(expected ...)` for results), the comments were extended, and `$sock_accept` was added to the rights of a descriptor. The functions of the interface and their parameters are the same apart from that; the claim that the text of 0.1 is frozen was wrong, and the generator reads `v0.2.9` only.
- **`wasi/api.h` of wasi-libc** (`wasi-sdk-25`, written from the same witx and used by real compilers) agrees with the table: the same functions (45 of the 46; see below), 20 functions with identical parameter names, 15 that differ only in `retptr` (the table) against `retptr0` (libc), and the `path_*` functions, where libc takes a C string `path` and the table, as the core module sees it, takes `path` and `path_len` (10 functions: this is the lowering of a string, not a difference).
- **`proc_raise`** is the one function the table has and the header of libc does not declare. It is in the witx of all three versions (`snapshot-01`, `v0.2.0`, `v0.2.9`) and is part of `wasi_snapshot_preview1`; libc simply has no wrapper for it in `api.h` (it has `proc_exit` and `sched_yield`).

Not compared: the byte text of the three versions function by function (their syntax differs), and the types of the parameters against libc (only names and counts).

## What the tests compare (`tests/test_signatures.py`)

- The twins and the table have the same functions. Python of 0.1: the 46, plus three helpers that are not imports (`errno_of`, `filestat_of_fd`, `filestat_of_path`). C of 0.1: the 46 and the five `fusedwasi_p1_unstable_*`. 0.2: Python and C have the same 201 functions; the table has three more, all `@unstable` in the WIT and left out on purpose (`wasi:clocks/timezone` `display` and `utc-offset`, `[method]response-outparam.send-informational`); `network-error-code`, the fourth unstable one, is implemented. The three are listed with their reasons in `P2_NOT_IMPLEMENTED` in the test.
- The number of parameters of each Python and each C function equals the table's (for 0.2 without the `comp` of the call; for 0.1 without the state `s` and the C `trapped`). All 46 and all 201 agree.
- A Python function of 0.2 returns a value exactly when the table has a flat result.

The C types of every function of both snapshots are compared with the table by `test_p1_c_parameter_types_are_those_of_the_table` and `test_p2_c_parameter_types_are_those_of_the_table` (i32 is `uint32_t`, i64 is `uint64_t`). The one difference found by hand before the test, `fusedwasi_wasi_http_types_response_outparam_set` (`a1`...`a7` were all `uint64_t`, the Canonical ABI has `i32` for all but `a3`, an `i64`), is fixed (2026-10-27).

## Names that differ between the table and the implementations

The implementations were **not** renamed. Positions agree everywhere, and so do the wasm types apart from the width above. The causes:

- A pointer and its length are `name`, `name_len` in the table; the implementations say `ptr`/`length`, `path`/`length`/`len`, `name_ptr`/`name_len`.
- A pointer for a result is `retptr` (`retptr0`, `retptr1`) in the table; the implementations name it by what it receives (`nread`, `nwritten`, `used`, `out`, `ptr`, `result`).
- The first parameter of a method of 0.2 is `self`; the drops call it `handle` in Python and in C.
- A variant, option or result is flattened to `name_tag`, `name_val0`, ...; the implementations use short names of their own (`a_tag`, `a_seconds`, `opt_tag`, `tag`).
- Names of the witx that the implementations shorten or replace: `id` (`ident` in Python), `rval` (`code`), `len` (`length`), `buf_len` (`size`), `fs_rights_base`/`fs_rights_inheriting` (`base`/`inheriting`), `fst_flags` (`flags`), `ri_data` (`iovs`), `iovs_len` (`count`), `to` (`target`), `old_path` (`old`), `old_fd` (`fd`).
- Reserved words: Python writes `in_`; C writes `in`, `environ_` and `new_`. The table keeps `in`, `environ`; the stubs write `in_`.

Of the 46 functions of 0.1, 39 have at least one name that differs (in Python or in C); of the 201 functions of 0.2 that the twins have, 66. In the tables the columns are the table, the Python and, when it is not the same as the Python, the C.

### 0.1 (`wasi_snapshot_preview1`)

| Function | Table (witx) | Python | C |
| --- | --- | --- | --- |
| `args_get` | argv, argv_buf | argv, buf | same |
| `args_sizes_get` | retptr0, retptr1 | argc, size | same |
| `environ_get` | environ, environ_buf | environ, buf | environ_, buf |
| `environ_sizes_get` | retptr0, retptr1 | count, size | same |
| `clock_res_get` | id, retptr | ident, ptr | id, ptr |
| `clock_time_get` | id, precision, retptr | ident, precision, ptr | id, precision, ptr |
| `fd_advise` | fd, offset, len, advice | fd, offset, length, advice | same |
| `fd_allocate` | fd, offset, len | fd, offset, length | same |
| `fd_fdstat_get` | fd, retptr | fd, ptr | same |
| `fd_fdstat_set_rights` | fd, fs_rights_base, fs_rights_inheriting | fd, base, inheriting | same |
| `fd_filestat_get` | fd, retptr | fd, ptr | same |
| `fd_filestat_set_times` | fd, atim, mtim, fst_flags | fd, atim, mtim, flags | same |
| `fd_pread` | fd, iovs, iovs_len, offset, retptr | fd, iovs, count, offset, nread | same |
| `fd_prestat_get` | fd, retptr | fd, ptr | same |
| `fd_prestat_dir_name` | fd, path, path_len | fd, path, size | same |
| `fd_pwrite` | fd, iovs, iovs_len, offset, retptr | fd, iovs, count, offset, nwritten | same |
| `fd_read` | fd, iovs, iovs_len, retptr | fd, iovs, count, nread | same |
| `fd_readdir` | fd, buf, buf_len, cookie, retptr | fd, buf, size, cookie, used | same |
| `fd_renumber` | fd, to | source, target | from, to |
| `fd_seek` | fd, offset, whence, retptr | fd, offset, whence, ptr | same |
| `fd_tell` | fd, retptr | fd, ptr | same |
| `fd_write` | fd, iovs, iovs_len, retptr | fd, iovs, count, nwritten | same |
| `path_create_directory` | fd, path, path_len | fd, path, length | fd, path, len |
| `path_filestat_get` | fd, flags, path, path_len, retptr | fd, flags, path, length, ptr | fd, flags, path, len, ptr |
| `path_filestat_set_times` | fd, flags, path, path_len, atim, mtim, fst_flags | fd, lookup, path, length, atim, mtim, flags | fd, lookup, path, len, atim, mtim, flags |
| `path_link` | old_fd, old_flags, old_path, old_path_len, new_fd, new_path, new_path_len | fd, flags, old, old_len, new_fd, new, new_len | fd, flags, old, old_len, new_fd, new_, new_len |
| `path_open` | fd, dirflags, path, path_len, oflags, fs_rights_base, fs_rights_inheriting, fdflags, retptr | fd, dirflags, path, length, oflags, base, inheriting, fdflags, out | fd, dirflags, path, len, oflags, base, inheriting, fdflags, out |
| `path_readlink` | fd, path, path_len, buf, buf_len, retptr | fd, path, length, buf, buf_len, used | fd, path, len, buf, buf_len, used |
| `path_remove_directory` | fd, path, path_len | fd, path, length | fd, path, len |
| `path_rename` | fd, old_path, old_path_len, new_fd, new_path, new_path_len | fd, old, old_len, new_fd, new, new_len | fd, old, old_len, new_fd, new_, new_len |
| `path_symlink` | old_path, old_path_len, fd, new_path, new_path_len | target, target_len, fd, path, length | target, target_len, fd, path, len |
| `path_unlink_file` | fd, path, path_len | fd, path, length | fd, path, len |
| `poll_oneoff` | in, out, nsubscriptions, retptr | in_, out, count, nevents | in, out, count, nevents |
| `proc_exit` | rval | code | same |
| `random_get` | buf, buf_len | buf, size | same |
| `sock_accept` | fd, flags, retptr | fd, flags, result | same |
| `sock_recv` | fd, ri_data, ri_data_len, ri_flags, retptr0, retptr1 | fd, iovs, count, flags, nread, roflags | same |
| `sock_send` | fd, si_data, si_data_len, si_flags, retptr | fd, iovs, count, flags, nsent | same |
| `sock_shutdown` | fd, how | fd, how | same |

### 0.2 (`wasi:*@0.2.12`)

| Import | Table (WIT) | Python | C |
| --- | --- | --- | --- |
| `cli/exit` `exit` | status_tag | status | same |
| `cli/terminal-input` `[resource-drop]terminal-input` | self | handle | same |
| `cli/terminal-output` `[resource-drop]terminal-output` | self | handle | same |
| `filesystem/types` `[method]descriptor.set-times` | self, data_access_timestamp_tag, data_access_timestamp_val0, data_access_timestamp_val1, data_modification_timestamp_tag, data_modification_timestamp_val0, data_modification_timestamp_val1, retptr | self, a_tag, a_seconds, a_nanos, m_tag, m_seconds, m_nanos, retptr | same |
| `filesystem/types` `[method]descriptor.write` | self, buffer, buffer_len, offset, retptr | self, ptr, length, offset, retptr | same |
| `filesystem/types` `[method]descriptor.create-directory-at` | self, path, path_len, retptr | self, ptr, length, retptr | same |
| `filesystem/types` `[method]descriptor.stat-at` | self, path_flags, path, path_len, retptr | self, path_flags, ptr, length, retptr | same |
| `filesystem/types` `[method]descriptor.set-times-at` | self, path_flags, path, path_len, data_access_timestamp_tag, data_access_timestamp_val0, data_access_timestamp_val1, data_modification_timestamp_tag, data_modification_timestamp_val0, data_modification_timestamp_val1, retptr | self, path_flags, ptr, length, a_tag, a_seconds, a_nanos, m_tag, m_seconds, m_nanos, retptr | same |
| `filesystem/types` `[method]descriptor.link-at` | self, old_path_flags, old_path, old_path_len, new_descriptor, new_path, new_path_len, retptr | self, old_flags, old_ptr, old_len, new_dir, new_ptr, new_len, retptr | same |
| `filesystem/types` `[method]descriptor.open-at` | self, path_flags, path, path_len, open_flags, flags, retptr | self, path_flags, ptr, length, open_flags, flags, retptr | same |
| `filesystem/types` `[method]descriptor.readlink-at` | self, path, path_len, retptr | self, ptr, length, retptr | same |
| `filesystem/types` `[method]descriptor.remove-directory-at` | self, path, path_len, retptr | self, ptr, length, retptr | same |
| `filesystem/types` `[method]descriptor.rename-at` | self, old_path, old_path_len, new_descriptor, new_path, new_path_len, retptr | self, old_ptr, old_len, new_dir, new_ptr, new_len, retptr | same |
| `filesystem/types` `[method]descriptor.symlink-at` | self, old_path, old_path_len, new_path, new_path_len, retptr | self, old_ptr, old_len, new_ptr, new_len, retptr | same |
| `filesystem/types` `[method]descriptor.unlink-file-at` | self, path, path_len, retptr | self, ptr, length, retptr | same |
| `filesystem/types` `[method]descriptor.metadata-hash-at` | self, path_flags, path, path_len, retptr | self, path_flags, ptr, length, retptr | same |
| `filesystem/types` `[resource-drop]descriptor` | self | handle | same |
| `filesystem/types` `[resource-drop]directory-entry-stream` | self | handle | same |
| `http/outgoing-handler` `handle` | request, options_tag, options_val0, retptr | request, opt_tag, opt_handle, retptr | same |
| `http/types` `[static]fields.from-list` | entries, entries_len, retptr | ptr, len, retptr | same |
| `http/types` `[method]fields.get` | self, name, name_len, retptr | self, name_ptr, name_len, retptr | same |
| `http/types` `[method]fields.has` | self, name, name_len | self, name_ptr, name_len | same |
| `http/types` `[method]fields.set` | self, name, name_len, value, value_len, retptr | self, name_ptr, name_len, value_ptr, value_len, retptr | same |
| `http/types` `[method]fields.delete` | self, name, name_len, retptr | self, name_ptr, name_len, retptr | same |
| `http/types` `[method]fields.append` | self, name, name_len, value, value_len, retptr | self, name_ptr, name_len, value_ptr, value_len, retptr | same |
| `http/types` `[resource-drop]fields` | self | handle | same |
| `http/types` `[resource-drop]incoming-request` | self | handle | same |
| `http/types` `[method]outgoing-request.set-method` | self, method_tag, method_val0, method_val1 | self, tag, ptr, len | same |
| `http/types` `[method]outgoing-request.set-path-with-query` | self, path_with_query_tag, path_with_query_val0, path_with_query_val1 | self, tag, ptr, len | same |
| `http/types` `[method]outgoing-request.set-scheme` | self, scheme_tag, scheme_val0, scheme_val1, scheme_val2 | self, tag, scheme_tag, ptr, len | same |
| `http/types` `[method]outgoing-request.set-authority` | self, authority_tag, authority_val0, authority_val1 | self, tag, ptr, len | same |
| `http/types` `[resource-drop]outgoing-request` | self | handle | same |
| `http/types` `[method]request-options.set-connect-timeout` | self, duration_tag, duration_val0 | self, tag, value | same |
| `http/types` `[method]request-options.set-first-byte-timeout` | self, duration_tag, duration_val0 | self, tag, value | same |
| `http/types` `[method]request-options.set-between-bytes-timeout` | self, duration_tag, duration_val0 | self, tag, value | same |
| `http/types` `[resource-drop]request-options` | self | handle | same |
| `http/types` `[static]response-outparam.set` | param, response_tag, response_val0, response_val1, response_val2, response_val3, response_val4, response_val5, response_val6 | param, tag, a1, a2, a3, a4, a5, a6, a7 | same |
| `http/types` `[resource-drop]response-outparam` | self | handle | same |
| `http/types` `[resource-drop]incoming-response` | self | handle | same |
| `http/types` `[static]incoming-body.finish` | this | this | this_ |
| `http/types` `[resource-drop]incoming-body` | self | handle | same |
| `http/types` `[resource-drop]future-trailers` | self | handle | same |
| `http/types` `[method]outgoing-response.set-status-code` | self, status_code | self, status | same |
| `http/types` `[resource-drop]outgoing-response` | self | handle | same |
| `http/types` `[static]outgoing-body.finish` | this, trailers_tag, trailers_val0, retptr | this, tag, trailers, retptr | this_, tag, trailers, retptr |
| `http/types` `[resource-drop]outgoing-body` | self | handle | same |
| `http/types` `[resource-drop]future-incoming-response` | self | handle | same |
| `io/error` `[resource-drop]error` | self | handle | same |
| `io/poll` `[resource-drop]pollable` | self | handle | same |
| `io/poll` `poll` | in, in_len, retptr | in_ptr, in_len, retptr | same |
| `io/streams` `[resource-drop]input-stream` | self | handle | same |
| `io/streams` `[method]output-stream.write` | self, contents, contents_len, retptr | self, ptr, len, retptr | same |
| `io/streams` `[method]output-stream.blocking-write-and-flush` | self, contents, contents_len, retptr | self, ptr, len, retptr | same |
| `io/streams` `[resource-drop]output-stream` | self | handle | same |
| `sockets/ip-name-lookup` `resolve-addresses` | network, name, name_len, retptr | network, ptr, length, retptr | same |
| `sockets/ip-name-lookup` `[resource-drop]resolve-address-stream` | self | handle | h |
| `sockets/network` `[resource-drop]network` | self | handle | h |
| `sockets/tcp` `[method]tcp-socket.start-bind` | self, network, local_address_tag, local_address_val0, local_address_val1, local_address_val2, local_address_val3, local_address_val4, local_address_val5, local_address_val6, local_address_val7, local_address_val8, local_address_val9, local_address_val10, retptr | self, network, tag, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, retptr | same |
| `sockets/tcp` `[method]tcp-socket.start-connect` | self, network, remote_address_tag, remote_address_val0, remote_address_val1, remote_address_val2, remote_address_val3, remote_address_val4, remote_address_val5, remote_address_val6, remote_address_val7, remote_address_val8, remote_address_val9, remote_address_val10, retptr | self, network, tag, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, retptr | same |
| `sockets/tcp` `[resource-drop]tcp-socket` | self | handle | h |
| `sockets/udp` `[method]udp-socket.start-bind` | self, network, local_address_tag, local_address_val0, local_address_val1, local_address_val2, local_address_val3, local_address_val4, local_address_val5, local_address_val6, local_address_val7, local_address_val8, local_address_val9, local_address_val10, retptr | self, network, tag, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, retptr | same |
| `sockets/udp` `[method]udp-socket.stream` | self, remote_address_tag, remote_address_val0, remote_address_val1, remote_address_val2, remote_address_val3, remote_address_val4, remote_address_val5, remote_address_val6, remote_address_val7, remote_address_val8, remote_address_val9, remote_address_val10, remote_address_val11, retptr | self, remote_tag, tag, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11, retptr | same |
| `sockets/udp` `[resource-drop]udp-socket` | self | handle | h |
| `sockets/udp` `[resource-drop]incoming-datagram-stream` | self | handle | h |
| `sockets/udp` `[method]outgoing-datagram-stream.send` | self, datagrams, datagrams_len, retptr | self, ptr, length, retptr | same |
| `sockets/udp` `[resource-drop]outgoing-datagram-stream` | self | handle | h |
