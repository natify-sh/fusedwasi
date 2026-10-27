# Interface review (checklist section 1)

Written 2026-10-26 on the branch `wasi-p2`, at `3736d20`. Nothing in the library was changed for it: this is a report.

**Owner's requirement:** C and Python at parity, and nothing tailored to one runtime.

**How a fact is known.** **[read]** means I read the code at the place given. **[unverified]** means I did not run or could not see it. Nothing here was run against a real C compiler or engine except what is said.

**Users read.** `py-wasmext` (branch `dev`, 84e0cf2): `src/wasmext/_gen.py`, `_shared.py`, `_exe.py`, `_natify.py`, `c/wasi_python.h`, `c/wasi2_python.h`, `tools/make_wasi_p2_table.py`, `_wasi_p2_table.py`. `py-wasmhost` (branch `fusedwasi-p1`, af6224a): `src/wasmhost/wasi/preview1.py` is the only file that imports `fusedwasi`.

**The list in `release_checklist.md` section 1 is not exact** (see 1.4): the name scheme of the per-import functions it gives is wrong; `FUSEDWASI_P1_WRITE` is not used by `wasmext`; `fusedwasi_p2_preopen` is a `struct` tag, not a typedef; and the users also depend on several things it does not list.

## 1. Names the users use, and a verdict

Verdicts: **general** = means the same for any engine or host; **leaks** = carries knowledge of `wasmext` or `wasm2c` (or of Python as a host) that another engine would have to copy; **ambiguous** = can be read either way, or the contract is not written.

### 1.1 C, state of 0.2 (`wasi_p2_core.h`)

| Name | Used by | Verdict | Why |
| --- | --- | --- | --- |
| `fusedwasi_p2_state` (the struct, by value inside the host's own struct) | wasmext everywhere | general | one state, no globals |
| `fusedwasi_p2_init`, `fusedwasi_p2_free` | wasmext `_gen.py` 1993/2029/1482, `_exe.py` | general | |
| `fusedwasi_p2_set_memory(s, comp, base, size)` | wasmext `_gen.py` 1974 | **ambiguous** | by value: a host must call it again after every call that may grow the memory (wasmext does so in `_p2_refresh` and after each `realloc`). The 0.1 state binds by pointer instead (1.2). Two models in one library. |
| `fusedwasi_p2_memory` (`uint8_t *base; uint64_t size`) | not named by the users | general, but see 3.1 | |
| field `user` | wasmext reads it in every callback | **leaks** (as used) / general (as declared) | `void *user` is general. wasmext puts a convention on it: "the first member of what `user` points to is the instance" (`*(Instance **)s->user`, `_gen.py` 1963/1984, `_shared.py` `API_M`). That convention is wasmext's, not fusedwasi's. See 3.2. |
| field `trap` (`void (*)(state*, const char*)`) | wasmext sets it to a function that calls `wasm_rt_trap` | general | the header comment names `wasm_rt_trap` (a comment only, `wasi_p2_core.h:101`). It must not return: a contract that is written only in that comment. |
| field `realloc` (`(s, comp, old_ptr, old_size, align, new_size) -> uint32_t`) | wasmext | general | it is the `cabi_realloc` of the component; 32-bit pointers (see 3.4) |
| field `wait` | wasmext (`wasi2_wait`, `api_p2_wait`) | general | |
| fields `args`, `arg_count`, `env_names`, `env_values`, `env_count`, `initial_cwd` | wasmext, `_exe.py` | general | owned by the caller, must outlive the state: written in the header |
| fields `preopens`, `preopen_count`; `struct fusedwasi_p2_preopen {path, name, writable}` | wasmext | general | but see 2.4 (the 0.1 twin is `{name, name_len, host, readonly}`) |
| fields `socket_addr_check`, `allow_ip_name_lookup`, `http_check` | wasmext | general | `use` is an `int`; the names for it are in Python only (2.2) |
| `FUSEDWASI_P2_USE_*` | wasmext (`UDP_BIND` to tell TCP from UDP by `>=`) | **ambiguous** | the order of the enum is relied on (`use >= UDP_BIND`) and is not documented as a contract |
| field `http_send` | not used by wasmext (the check `_exe.py` sets only `http_check`) | general | |
| field `std[3]` | wasmext sets `s->std[i] = &stream` | general | `void *`, not `fusedwasi_p2_stream *`: the type is lost (see 3.5) |
| `std_default[3]`, `terminals`, `FUSEDWASI_P2_TERMINAL_*` | no user | general | internal, or not yet used by anyone |
| fields `clock`, `random_bytes`, `insecure_random_bytes`, `sleep` | wasmext | general | the clock `id` 0..3 is documented. `resolution_ns[4]` is public and no user sets it. |
| field `exit` (hook) | no user overrides it | general | default records and traps |
| fields `exited`, `exit_code` | wasmext reads both after a call, and clears `exited` before the next (`_gen.py` 2085, `_shared.py` 912) | general, **contract not written** | see 4 |
| `fusedwasi_p2_default_*` | no user | general | internal |
| `FUSEDWASI_P2_FN` | wasmext's table tool matches it in a regular expression (1.3) | general | the macro is an interface only through that regular expression |

### 1.2 C, state of 0.1 (`wasi_p1_core.h`)

| Name | Used by | Verdict | Why |
| --- | --- | --- | --- |
| `fusedwasi_p1_state` (opaque pointer, but fields read) | wasmext | see fields | |
| field `core` | wasmext (indirectly, through `create`) | general | |
| field `user` | wasmext sets it through `cfg.user`, reads it in hooks | ambiguous | `never freed by the core`: written. Its use is wasmext's (the `wasmext_wasi_py`). |
| fields `finished`, `end_value` | wasmext reads, and clears `finished` before each call | general, **contract not written** | see 4 |
| fields `memory_data`, `memory_size` (`uint8_t **`, `uint64_t *`) | not touched; set through `fusedwasi_p1_bind` | **leaks** | the shape is the shape of `wasm_rt_memory_t` (`uint8_t *data; ... uint64_t size`): see 3.1 |
| `fusedwasi_p1_bind(w, uint8_t **data, uint64_t *size)` | wasmext, 4 places, with `&m_->data, &m_->size` | **leaks** | same |
| `fusedwasi_p1_config {preopens, npreopens, user}`, `fusedwasi_p1_preopen {name, name_len, host, readonly}` | wasmext | general | but differs from the 0.2 preopen (2.4) |
| `fusedwasi_p1_create(cfg, core, &err, &bad)`, `fusedwasi_p1_destroy` | wasmext | general | `err` is an `errno` of the C library, `bad` an index: written |
| `fusedwasi_p1_copy`, `FUSEDWASI_P1_MALLOC / CALLOC / FREE` | wasmext uses them for buffers it later hands to `create` and frees itself | **ambiguous** | they are `#define`d without `#ifndef`, so a host cannot replace them; they are public only because the host frees what the library allocated (`fusedwasi_p1_copy`). `FUSEDWASI_P1_REALLOC` is the same, `FUSEDWASI_P1_WRITE` is a private alias of `write`/`_write` that no user uses. |
| `fusedwasi_p1_errno(int)` | wasmext (`_shared.py` 174) | general | wasmext needs the inverse (WASI errno to C errno) and builds it by trying 1..255; the library has no such function |
| `FUSEDWASI_P1_HOOK_TRAP` | wasmext (`wasi_python.h`, 5 uses) | **leaks / vestigial** | defined in fusedwasi, **used nowhere in fusedwasi** [read: `grep` finds only the definition and its comment]. The hooks it described are gone since 0.1 went over 0.2. It is now a private constant of the host. |
| `FUSEDWASI_P1_END_EXIT`, `FUSEDWASI_P1_END_SIGNAL`, `FUSEDWASI_P1_END_NONE` | wasmext `wasi_end` returns them, and `raise_end(how, ...)` | **ambiguous** | `END_SIGNAL` is set by the library. `END_EXIT` is **never set** by the library or the Python twin [read: `grep END_EXIT` finds only the enum]; wasmext uses the value as its own return code for "ended by exit". |
| `fusedwasi_p1_<name>(void *s, ..., int *trapped)` (46 functions), `fusedwasi_p1_unstable_<name>` | wasmext binds by name (`_gen.py` 2048, 2455) | general | the name is the WASI name. `trapped` is an out parameter, not a return. |
| `FUSEDWASI_P1_STATE(s)` | internal | not public |

### 1.3 C, streams and packages

| Name | Used by | Verdict |
| --- | --- | --- |
| `fusedwasi_p2_stream` with members `read, check_write, write, flush, ready, ctx, failure, failure_errno, failure_network_code` | wasmext implements its own streams over Python objects and callbacks | general. But `failure_network_code = -1` (what wasmext sets, `wasi2_python.h`) has no effect: the field is read only when `failure_has_network` is set [read: `wasi_p2_io.h:209`]. Three overlapping ways of saying why a stream failed: `failure_has_code/failure_code`, `failure_has_network/failure_network_code`, `failure_errno`. **ambiguous** |
| `FUSEDWASI_P2_STREAM_OK / CLOSED / FAILED` | wasmext | general |
| the status contract of `read`: `CLOSED` means the end of the input and `got` must be 0 | wasmext relies on it | general, written in the header as "the end" only |
| `fusedwasi_wasi_<package>_<interface>_<name>` (200 functions, e.g. `fusedwasi_wasi_io_streams_output_stream_write`) | wasmext binds them by name through a table it makes by parsing the headers with a regular expression (`tools/make_wasi_p2_table.py`) | general. **The checklist says `fusedwasi_p2_<package>_<name>`: that is wrong.** The README has it right. The function takes `(state, [comp,] flat args...)`, `comp` only where the function touches memory. |
| the header names `wasi_p2_core.h`, `wasi_p2_io.h`, `wasi_p2_cli.h`, `wasi_p2_filesystem.h`, ... and what each includes | wasmext (`_p2_headers`, `INCLUDES` of its table) | general; but `fs_posix.h` and `fs_win32.h` are special-cased (copied, not included) |

**The formatting of the headers is an interface.** `make_wasi_p2_table.py` reads `FUSEDWASI_P2_FN <ret> fusedwasi_wasi_... ( <params> )` with a regular expression, and wants the second parameter to be named `comp`, and the types to be `uint32_t` or `uint64_t`. A change of the style (a line break in the middle of the parameters is allowed; a macro instead of `FUSEDWASI_P2_FN` is not) breaks the user without any name changing. **ambiguous**; it should be written down or replaced by a table that fusedwasi ships (the checklist and `host_scope.md` already suggest one, "the table of the signatures made once from the WIT").

### 1.4 Python (`wasmhost`, the only Python user; `wasmext` uses none)

| Name | Used by | Verdict | Why |
| --- | --- | --- | --- |
| `fusedwasi.p1.wasi_p1` (module of 46 functions `name(state, *ints) -> int`) | wasmhost looks them up with `getattr(table, name)` | general | |
| `fusedwasi.p1.wasi_p1_unstable` | same | general | |
| `wasi_p1_core.State(memory, *, core=, preopens=, files=)` | wasmhost, with a **callable** for `memory` that returns the memory as the call in progress sees it, a view or an object with `read/write/__len__` | general | this is the engine-neutral memory the C lacks (see 3.1) |
| `State.close_all()` | wasmhost | general | not in the C list (`destroy` is the twin) |
| `wasi_p1_core.Exit` (attribute `code`) | wasmhost catches it and raises its own `WasiExit` | general | |
| `wasi_p2_core.Host(args=, env=, stdin=, stdout=, stderr=)` | wasmhost | general | `env` is a list of pairs here, `args` a list |
| `wasi_p2_io.InputStream`, `OutputStream` (subclassed: `read(n)`, `check_write()`, `write(data)`) | wasmhost | general | the Python base classes have more methods (`ready`, `flush`, `close`) with defaults |
| `StreamClosed`, `StreamFailed(message, errno=)` | wasmhost | general | the `errno` keyword is how 0.1 gets the error of the system |
| not used by wasmhost, though in the checklist | `Signal` (wasmhost overrides `proc_raise` itself), `Trap` | | wasmhost's README says `proc_raise` does not end the program there. In the C and in this Python it does. A difference between the two users, not between C and Python. |

### 1.5 Used but not in the checklist

`fusedwasi_p2_preopen` (listed), `fusedwasi_p2_stream` fields, `FUSEDWASI_P2_USE_*`, `fusedwasi_p2_default_*` (no), `FUSEDWASI_P1_HOOK_TRAP` (listed with the macros of the memory), the **function names and the formatting of the headers** (1.3), `fusedwasi.get_include()` and the layout `include/fusedwasi/p{1,2}/*.h` (wasmext `_natify.py` 56-63 assumes `p1/` is tried before `p2/` for a bare file name), and `fusedwasi_p1_unstable_<name>` (only for the five calls that differ).

## 2. Parity: C against Python

Same name, same meaning, unless stated. "gap" is a place where a host written against one cannot be written the same way against the other.

### 2.1 Names of the per-import functions

| C | Python | Parity |
| --- | --- | --- |
| `fusedwasi_wasi_<pkg>_<iface>_<name>(state, [comp,] ...)` | `wasi_p2_<pkg>.wasi_<pkg>_<iface>_<name>(host, [comp,] ...)` | same. `tools/wasi_p2_audit/audit_sigs.py` checks the signatures (0 differences, per `host_scope.md`; I did not re-run it) |
| `fusedwasi_p1_<name>(void *s, ..., int *trapped)` | `wasi_p1.<name>(state, ...)` | same, except how a trap is told (2.3) |
| `fusedwasi_p1_unstable_<name>` (5 functions) | `wasi_p1_unstable.<name>` | same [read: not compared call by call] |

### 2.2 The state of 0.2

| C field | Python `Host` | Parity |
| --- | --- | --- |
| `memory[8]`, `memories` (pointers to base and size; `fusedwasi_p2_bind_memory` binds them, `fusedwasi_p2_set_memory` writes cells of the state and binds those) | `memories` list of any writable buffer or a function of no arguments that gives it | same model since the bind by pointer (as 0.1's `fusedwasi_p1_bind` and `memory=`). Python has no limit of 8. |
| `trap` hook | none: a bad handle raises `Trap` | **gap (by design)**: Python has no hook to replace; an exception is the trap. Neither is wrong, but a Python host cannot turn a trap into something else without catching. |
| `realloc(s, comp, ...)` | `realloc(comp, ...)` | same |
| `wait(s)` | `wait()` | same |
| `args`, `arg_count` | `args` list | same |
| `env_names`, `env_values`, `env_count` | `env` list of `(name, value)` | same |
| `initial_cwd` | `initial_cwd` | same |
| `preopens {path,name,writable}` | `preopens` tuples `(path, name[, writable])` | same |
| `socket_addr_check(s, ip, port, int use)` | `socket_addr_check(ip, port, str use)` | **gap**: `use` is an int in C and a string (`"tcp-bind"`, ...) in Python. wasmext's `wasi2_uses[]` is its own copy of the names. |
| `allow_ip_name_lookup` | `allow_ip_name_lookup` (default: `network`) | same; Python has also `network=` and `http=` booleans, C has none (the host writes a function that returns 1) |
| `http_check(s, scheme, authority)`, `http_send` | `http_check`, `http_send` | same |
| `std[3]` (`void *`), `std_default[3]` | `std` dict by name | same meaning |
| `terminals` (bit mask) | `terminals` (set of names) | same meaning; **gap**: different representation |
| `exit` hook | none: `Exit` is raised | **gap (by design)** |
| `exited`, `exit_code` | `exited`, `exit_code` | same |
| `random_bytes(s, buf, n) -> int`, `insecure_random_bytes` | `random_bytes(n) -> bytes` | same meaning |
| `clock(s, id, &ns) -> int` | `clock(id) -> int` (raise on failure) | same meaning |
| `resolution_ns[4]`, `sleep(s, ns) -> int` | `resolution_ns`, `sleep(ns)` | same |
| `user` | **none declared** | **gap**: Python has no `user` slot. `Host` has no `__slots__`, so `host.user = x` works, but it is not in the signature and `pyright --strict` (CI) would object in a host that did it. The C host needs `user` because callbacks are plain functions. In Python a subclass or closure does it. Document as "not needed in Python", or add the attribute for symmetry. |
| `slots`, `slot_count`, `slot_capacity` | `slots` | internal, both |

### 2.3 The state of 0.1

| C | Python | Parity |
| --- | --- | --- |
| `fusedwasi_p1_state.core` | `State.core` | same |
| `user` | none | gap as above |
| `finished`, `end_value` | `finished`, `end_value` | same, plus Python raises `Signal`/`Exit` and C sets `*trapped` |
| `memory_data` / `memory_size` (by pointer) | `memory` (buffer, callable, or object with `read/write/len`) | **gap**: the Python is the more general. Only the C is tied to one layout. |
| trap | `*trapped = 1` and the host knows why | Python raises |
| `entries`, `nentries` | `entries` | internal |
| `fusedwasi_p1_create(&cfg, core, &err, &bad)` | `State(..., preopens=...)` raising `PreopenError(.index)` | same |
| `fusedwasi_p1_config.preopens {name, name_len, host, readonly}` | `preopens=[(host, name[, writable])]` | same meaning, **order and polarity differ** (name first and `readonly` in C; host first and `writable` in Python) |
| (no file backend) | `files=Files()` / `SystemFiles()` | **gap**: Python can give a host without files or its own; the C `fusedwasi_p1_be_*` are fixed `static inline` functions over the system layer. |
| `fusedwasi_p1_destroy` | `State.close_all()` | same |
| `fusedwasi_p1_errno(int)` | `errno_of(int)` | same. Neither has the inverse. |
| `FUSEDWASI_P1_E_*`, `_FT_*`, `_R_*`, `_O_*`, `_T_*`, `_FD_*`, `_LOOKUP_FOLLOW` | `E_*`, `FT_*`, `R_*`, `O_*`, `T_*`, `FD_*`, `LOOKUP_FOLLOW` | same. **Gap:** Python has `FT_SOCKET_DGRAM` (5) and the right names `R_PATH_LINK_SOURCE` etc. for every bit; the C enum has no `FT_SOCKET_DGRAM` and names only some rights. |
| `FUSEDWASI_P1_END_*` | `END_*` | same, both never set `END_EXIT` |
| `FUSEDWASI_P1_HOOK_TRAP`, `FUSEDWASI_P1_MALLOC/...`, `FUSEDWASI_P1_LINK_MAX`, `FUSEDWASI_P1_DIRS_ARE_PATHS`, `FUSEDWASI_P1_NATIVE_POLL` | none | C-only, rightly (they are about C) except `HOOK_TRAP`, which is dead in the library |
| (none) | `WasiError`, `Stat`, `Entry`, `Listing`, `Files`, `SystemFiles`, `Signal`, `PreopenError` | Python-only; `WasiError` is the Python way to give an errno from a hook, with no C twin in the library (wasmext's `HOOK_TRAP` convention is its analogue) |

### 2.4 Parity inside C

* The two preopen structs (0.1 and 0.2) have different field names and polarity (`host`/`path`, `readonly`/`writable`, `name_len`). A host that fills both (wasmext does) writes the same list twice.
* 0.1 and 0.2 both bind the memory by pointer (`fusedwasi_p1_bind`, `fusedwasi_p2_bind_memory`); `fusedwasi_p2_set_memory` is the by-value convenience (2.2).
* `fusedwasi_p1_*` functions take `void *s`; `fusedwasi_wasi_*` take `fusedwasi_p2_state *`. This is deliberate in 0.1 (a glue binds all functions with one pointer type) but is not said.

### 2.5 Behaviour parity

The tests compare C and Python output scenario by scenario (`tests/test_wasi_p2_*.py`, `tests/p1_script.py`, `tests/data/p1/*.txt`); I did not re-run the C side in this review. Known behaviour gaps are in `release_checklist.md` section 3 (the `utime` of a link on Windows with Python 3.12 and earlier) and are handled in the work that follows this review.

## 3. What assumes one engine

### 3.1 Memory as `uint8_t **` and `uint64_t *` (0.1) [leaks]

`fusedwasi_p1_bind(w, uint8_t **data, uint64_t *size)` is `&m->data, &m->size` of a `wasm_rt_memory_t` [read: `wasm-rt.h` of wabt, `uint8_t* data` and a `uint64_t` `size` field; the `size` line itself was cut in my read]. Any engine that keeps the base and the size as two plain fields could use it; one that keeps them as one object (a `wasmtime::Memory`, a WAMR instance, a JavaScript `ArrayBuffer`) cannot, and an engine whose base is not a `uint8_t *` (**shared** memory in wasm-rt is `_Atomic volatile uint8_t *`, `wasm-rt.h` near line 513 [unverified: I did not compile it]) needs a cast.

The Python twin already shows the neutral form: a *function that gives the memory of the call in progress*. A proposal (not done):

```c
typedef struct fusedwasi_memory_view {          /* one per memory; the host fills it before each call */
    uint8_t *base;
    uint64_t size;
} fusedwasi_memory_view;

typedef struct fusedwasi_memory_source {
    /* called by the library before it touches memory; returns the current base and size */
    void (*get)(void *ctx, uint32_t comp, fusedwasi_memory_view *out);
    void *ctx;
} fusedwasi_memory_source;
```

The same type serves 0.1 (comp 0) and 0.2 (comp n), `fusedwasi_p2_set_memory` stays as a convenience that installs a source of constant values, `fusedwasi_p1_bind` takes `(w, source)`. wasm2c would give `get` that reads `&m->data, m->size` itself. The cost: one indirect call per import call (the library may cache the view for the length of one call).

### 3.2 `user` and "the first member of `user` is the instance" [leaks, in the users]

fusedwasi itself never reads `user`. The convention (`*(Instance **)s->user`) lives in wasmext (`_gen.py` 1963, 1984, `_shared.py` `API_M`, `_exe.py` 255-262). What is wrong is that the library gives **no way to find the instance** from a callback other than `user`, and `trap(s, msg)` / `realloc(s, comp, ...)` do not carry a per-call context. So a store shared by several instances (wasmext C2a) must rewrite `*s->user` before every call (`_p2_refresh` does exactly that) because the state, not the call, is the only carrier.

Proposal: keep `user` as "the host's pointer, never touched by the library" and write that in the header; add an optional **call context** that the host sets once per entry into the module and the library passes back unchanged to hooks:

```c
void *call;   /* set by the host on entry to the module; the library passes it to hooks as `s->call` */
```

or, simply, pass `void *user` as an extra argument to `trap`, `realloc`, `wait`, `clock`, `sleep`, `random_bytes`, `exit`. The first is a smaller change. The layout rule ("first member") then disappears from the users.

### 3.3 `wasm_rt` and `wasm2c` in the library

None in code [read: `grep -i 'wasm_rt|wasm2c|wasmext'` over `include/` and `src/**/*.py` finds only comments]:

* `wasi_p2_core.h:101` "in a module it is `wasm_rt_trap`" (comment);
* `wasi_p1_core.h:3` "A module needs nothing of wasmext", `:10` "The host that has Python is wasi_python.h" (a file that is not in this repository), `:6` "There is no Python in this file", `:24` and `:64` (the `_GNU_SOURCE` set "for what the headers of Python set");
* `README.md` and `pyproject.toml` description: "for core modules fused from components (wasm2c and other C hosts)"; README "It reads the memory ... so it knows nothing of `wasm2c`" and "The C is the core that `wasmext` builds into every module".
* Python: `wasi_p2_cli.py:8` "as in the WASI 0.1 host of wasmext"; `wasi_p1_files.py` refers to uvwasi and Wasmtime (fine).

Proposal: rewrite those comments to say what a host must do ("a function that does not return") without naming the engine, and leave the examples in the README. The words `wasmext` and `wasmhost` in a README "Who uses it" section are fine.

**The one real leak in the data model** is the MALLOC/CALLOC/FREE trio (1.2): they are not configurable and exist so that `wasmext` can free what `fusedwasi_p1_copy` allocated. Proposal: `fusedwasi_p1_preopen` fields owned by the caller (as `args` and `preopens` of 0.2 already are, "owned by the caller, they must outlive the state") and `create` copies; then `fusedwasi_p1_copy` and the macros become private. (`create` already copies, per its comment.)

### 3.4 32-bit memory

Every pointer in the API is `uint32_t` (`fusedwasi_p2_mem(s, comp, uint32_t ptr, ...)`, `realloc` returns `uint32_t`). `memory.size` is `uint64_t`. A memory64 module cannot be served. WASI 0.2 and 0.1 are wasm32 only, so this is a **limit of both families, not a leak**; say so in the README (nothing about memory64 is written there; [read]).

### 3.5 `std[3]` is `void *`

The user must know it is a `fusedwasi_p2_stream *`; the header comment says so. A typed array would make the contract visible and cost nothing. The type is declared in `wasi_p2_io.h`, which `wasi_p2_core.h` does not include, which is why it is `void *`; a forward declaration (`struct fusedwasi_p2_stream;`) would do.

### 3.6 Names containing a runtime

* Only `README.md` and `pyproject.toml` say `wasm2c`; no public identifier contains `wasmext`, `wasm_rt`, `wasm2c`, `py` or `cpython` [read: `grep -o 'fusedwasi_[a-z0-9_]*'` over the headers].
* `FUSEDWASI_P1_HOOK_TRAP` is a name from a Python-host design, dead in the library. Remove from fusedwasi, define in `wasmext`.
* The python twin file layout (`wasi_p2_*.py`) is a mirror of the C names; fine.

### 3.7 Other one-host assumptions

* **`trap` must not return** is true for wasm2c (longjmp) and for a Python exception. An engine that unwinds by returning an error code cannot use `trap` as is, because the library calls it from the middle of a function (`s->trap(s, ...)` in the table code, with no `return` after). The Python twin raises, so it is consistent. **[read]**
* **Exit is a trap with a flag.** In C `exit` hook records and traps, the host then reads `exited`. In Python, `Exit` is raised and `exited` is also set. Both need an engine where a host function can abort the call.
* **The streams of the standard descriptors** (`std_default`, `fusedwasi_p2_fd_input_new`) use the process descriptors 0, 1, 2 directly. Fine for a CLI; wasmext replaces them with Python streams; a browser host replaces them with its own.
* **Nothing is allowed by default for sockets and http**: a policy, not an engine assumption.

## 4. The contract for the host: public and private (decided 2026-10-27, step 5)

This section was a proposal; it is now a record of what was decided. The list itself is `src/fusedwasi/public_api.json` (`docs/public_api.md` is made from it), the rule is `design_model.md` section 5, and the test is `tests/test_public_api.py`.

* **Public**: the configuration fields the host sets (`args`, `env_*`, `initial_cwd`, `preopens`, `socket_addr_check`, `allow_ip_name_lookup`, `http_check`, `http_send`, `std[3]`, `terminals`, `random_bytes`, `insecure_random_bytes`, `clock`, `resolution_ns`, `sleep`, `user`); the hooks of the guest (`trap`, `realloc`, `wait`, `exit`, `signal`, `user`); the end of a call (`exited`, `exit_code`; the host clears `exited` before the next call; for 0.1 too, since `proc_exit` is `wasi:cli/exit`); the memory by `fusedwasi_p2_bind_memory` / `set_memory`; the constants (`USE_*`, `TERMINAL_*`, `STREAM_*`, `KIND_*`, the errno, rights and flags of 0.1, the error-codes of the filesystem and the sockets); the stream types and their callbacks; `fusedwasi_p2_init / free / guest_init`, the `*_add` constructors, `std_get`, `use_name`, `fusedwasi_p1_open_descriptors`, `fusedwasi_p1_errno`; the preopen struct; the functions of the tables of signatures. In Python: the classes and constants of `fusedwasi.p1` and `fusedwasi.p2`, the streams of `wasi_p2_io`, `Filesystem` / `SystemFilesystem` / `FsError` / `Descriptor` / `Times` (Python only), `fusedwasi.signatures`.
* **Private**: `slots`, `slot_count`, `slot_capacity`, `memory`, `memory_cell`, `memories`, `std_default`, `p1_table` (now `x__`), the slot and the pollable layout, `INDEX_BITS`, `INDEX_MASK`, `GENERATION_MASK`, `FUSEDWASI_P1_K_*`, every `sys_*`, `walk_*`, `default_*` and `fs_*`, the helpers of the memory (`mem`, `load_*`, `store_*`, `alloc`, `string`), the table functions that take a guest, `FUSEDWASI_P1_READ/WRITE/...`: all of them carry the mark. The table is in `private_names.md`.
* **The questions of the proposal**: `END_EXIT`, `HOOK_TRAP`, `MALLOC/CALLOC/FREE` and `finished` / `end_value` are gone (step 1 and stage B); the inverse of `fusedwasi_p1_errno` is not offered (private `fusedwasi_p1__errno_of_code` exists); `fusedwasi_p2_use_name` and a typed `std` exist.
* **Provisional, to be confirmed by the owner**: the helpers of the canonical ABI (`fusedwasi_p2__mem`, `__load_*`, `__store_*`, `__alloc`, `__string`, `__table_get`) are private because no user and no neutral user needs them; the kinds `FUSEDWASI_P2_KIND_*` are public (Python exports `KIND_*`, and `table_add` takes one); the transport and the server side of `wasi:http` are public but marked unstable.

## 5. Short list for the owner

1. `FUSEDWASI_P1_HOOK_TRAP`: dead in the library; move to `wasmext`.
2. `FUSEDWASI_P1_END_EXIT`: never set; set it or drop it.
3. `fusedwasi_p1_bind` and `fusedwasi_p2_set_memory`: two models; unify (3.1).
4. `user`: write that the library never touches it; consider a per-call context (3.2).
5. Python `Host`/`State` lack `user`; C lacks a file backend and `Host(network=True)`-style booleans (2.2, 2.3).
6. `socket_addr_check`: int in C, string in Python.
7. The 0.1 and 0.2 preopen structs differ in field names and polarity.
8. The format of the headers is read by `make_wasi_p2_table.py` with a regular expression; ship a table or document the format.
9. `release_checklist.md` section 1 has the wrong function-name scheme; correct it.
10. (Done 2026-10-27: `tools/wasi_p2_conformance/` runs again, with the paths fixed; filesystem and http were compared with Wasmtime 50.0.1, see `wasmtime_compare.md`.)

## 6. What was done about the gaps of section 2 (task 4, 2026-10-26)

The owner decided that a difference between the C and the Python twins is to be fixed. The library is not published, so the changes below break the users' code where they say so (see "Changes for the users"). Tests: `tests/test_parity_surface.py` (each test fails on the tree before the change; the pairs build a C program and compare it with the Python), and the existing tests changed with their drivers.

| Gap (section) | Done |
| --- | --- |
| `user` (2.2, 2.3) | `Host(user=...)` and `State(user=...)` exist and are never touched by the library, as `fusedwasi_p2_state.user` and `fusedwasi_p1_state.user` (copied from `fusedwasi_p1_config.user`). The C headers say that the library never reads, writes or frees it. Test: both languages. |
| `socket_addr_check` `use` (2.2) | **Both pass the number.** C: `FUSEDWASI_P2_USE_*` (0 to 4) and the new `fusedwasi_p2_use_name(int)`. Python: `USE_TCP_BIND` ... `USE_UDP_OUTGOING` (the same numbers) and `use_name(int)` in `wasi_p2_core`; the callable of a `Host` is now `f(ip, port, use: int)`. Why this way: the one user of the C policy (wasmext) already gets the number and keeps a private table of the five names (`wasi2_uses[]` in `wasi2_python.h`, `_USES` in `_ctypes_gen.py`), which `fusedwasi_p2_use_name` replaces; the other direction (strings in C) would make every C policy do `strcmp`. The only Python callable of fusedwasi (`Host(socket_addr_check=)`) has no user (wasmhost does not use it; wasmext's `sockets=` callable is its own, fed with names by its own code). `http_check(scheme, authority)` has no `use` and is the same in both. |
| `terminals` (2.2) | **A mask in both**: `FUSEDWASI_P2_TERMINAL_STDIN/STDOUT/STDERR` (1, 2, 4) in C and `TERMINAL_STDIN/STDOUT/STDERR` with the same values in Python; `Host(terminals=mask)` replaces the set of names. Neither user sets it. |
| the preopen of 0.1 and 0.2 (2.3, 2.4) | **One struct**: `fusedwasi_p1_preopen` is now `struct fusedwasi_p2_preopen`, `{path, name, writable}` (a typedef of the same type; the Python tuples were already `(path, name, writable)` in `Host` and in `State`). `name_len` is gone: it was never needed (the name is a C string, as in 0.2, and every user computed it with `strlen`; a name with a NUL is not a path). The user can now give `s->preopens` to `fusedwasi_p1_config.preopens` and drop its second array. |
| the constants of 0.1 (2.3) | The C had no `FUSEDWASI_P1_FT_SOCKET_DGRAM` and lacked the rights `R_PATH_LINK_SOURCE`, `R_PATH_LINK_TARGET`, `R_PATH_READLINK`, `R_PATH_RENAME_SOURCE`, `R_PATH_RENAME_TARGET`, `R_PATH_SYMLINK`, `R_SOCK_SHUTDOWN`: added. A test now compares **every** constant of `wasi_p1_core.py` (errno, file types, rights, oflags, fdflags, times, kinds, ends) with the C macro of the same name: 100+ values. |
| `trap` and `exit` hooks (2.2) | **Not a gap to close, deliberate**: in C a hook that does not return is the only way out of a function; in Python an exception does the same job, and `Exit` and `Trap` are the exceptions. A Python `Host` that wanted to turn a trap into something else catches the exception. Adding `trap`/`exit` callables to the Python `Host` would only duplicate `try`/`except`. |
| trap told by `*trapped` (2.1) | Deliberate for the same reason. |
| the memory of 0.1 by pointer and of 0.2 by value (2.4) | **Not changed**: it is the change of 3.1 (a memory source), which touches every call that reads memory and both users' glue. It is a decision for the owner, not a mismatch between C and Python (the Python takes the memory as a buffer or a function in both states). |
| a file backend for 0.1 (`files=`), `network=`, `http=` (2.2, 2.3) | Python-only features (a `Files` object, two booleans), deliberate: in C the host writes a function. |
| `END_EXIT` never set, `FUSEDWASI_P1_HOOK_TRAP` dead (1.2) | The same in both; not a C/Python difference. Left for the owner (section 5, items 1 and 2). |
| `std[3]` is `void *` (3.5) | Tried and **reverted**: a typed `fusedwasi_p2_stream *std[3]` breaks every assignment of a derived struct (the cli driver does it) for no gain in parity. |

## Changes for the users

One line for each name that a user has to change. `wasmext` is `/home/user/py-wasmext` (C), `wasmhost` is `/home/user/py-wasmhost` (Python). Everything else added (`Host.user`, `State.user`, `fusedwasi_p2_use_name`, `use_name`, the constants) touches nobody.

* `fusedwasi_p1_preopen.host` -> `.path`: wasmext (`c/wasi_python.h` 298, 445, `c/wasi2_python.h` 331 and 380, `_shared.py` 382, `_exe.py` 193).
* `fusedwasi_p1_preopen.readonly` (set when read-only) -> `.writable` (set when writable, the opposite): wasmext (`c/wasi_python.h` where the item is made, `c/wasi2_python.h` 333, `_shared.py` 385, `_exe.py` 196).
* `fusedwasi_p1_preopen.name_len` -> removed (use `strlen(name)`; the name has no NUL): wasmext (`c/wasi_python.h` 444, `_shared.py` 384, `_exe.py` 195).
* The order of the fields of `fusedwasi_p1_preopen` was `{name, name_len, host, readonly}` -> `{path, name, writable}`: wasmext, only where it initialises the struct by position (none found; it assigns by field). Optional simplification: pass `s->preopens` and `s->preopen_count` as `cfg.preopens` / `cfg.npreopens` and delete the second array (`_shared.py` 376-389, `_exe.py` 188-200, `c/wasi2_python.h` `wasmext_wasi_over`).
* `Host(socket_addr_check=f)`: `f(ip, port, use: str)` -> `f(ip, port, use: int)`, with `fusedwasi.p2.wasi_p2_core.use_name(use)` for the old text: nobody (wasmhost does not set it; wasmext's `sockets=` callable is fed by `wasi2_uses[]` in C and needs no change; it can replace that table, and `_USES` in `_ctypes_gen.py`, with `fusedwasi_p2_use_name`).
* `Host(terminals=["stdout"])` (names) -> `Host(terminals=TERMINAL_STDOUT)` (mask): nobody (grep of both repositories finds no use).
* `fusedwasi_p1_unstable_*`, `fusedwasi_p1_<name>`, `fusedwasi_wasi_*`, `wasi_p1.<name>`, `State(...)`, `Exit`, the streams: **unchanged**.

### Step 1 of the rework (the split of the state and the guest, 2026-10-27)

The types are `fusedwasi_p2_state` + `fusedwasi_p2_guest`, `fusedwasi_p1_state` + `fusedwasi_p1_guest` (C) and `State` + `Guest` in `fusedwasi.p2` and `fusedwasi.p1` (Python; the classes are defined in `wasi_p2_core` and `wasi_p1_core`, and each family package exports its own `State`, `Guest`, `Exit`; the top level `fusedwasi` no longer has the `State`, `Guest`, `Exit` and `Trap` of 0.2 that it had re-exported, round 2). The first argument of every function is the guest (`g`, `guest`); the names of the functions and their other arguments are unchanged. The sections above describe the code before this change.

* C 0.2: `fusedwasi_p2_state s; fusedwasi_p2_init(&s)` -> `fusedwasi_p2_state state; fusedwasi_p2_init(&state); fusedwasi_p2_guest g; fusedwasi_p2_guest_init(&g, &state)` (new). Every `fusedwasi_wasi_*(&s, ...)` takes `&g`: wasmext (`c/wasi2_python.h`, `_gen.py`, `_exe.py`).
* C 0.2, fields that moved to the guest (`g.`): `memory`, `memory_cell`, `memories`, `trap`, `realloc`, `wait`, `exit`, `exited`, `exit_code`; and `user` now has two owners, `state.user` (given back to the hooks of the state) and `g.user` (given back to the hooks of the guest). wasmext reads the instance from `*(Instance **)s->user`: that becomes `g->user`, one per instance, and the rewrite before each call (`_p2_refresh`) is no longer needed.
* C 0.2, fields that stay on the state: `slots*`, `args`, `arg_count`, `env_names`, `env_values`, `env_count`, `initial_cwd`, `preopens`, `preopen_count`, `socket_addr_check`, `allow_ip_name_lookup`, `http_check`, `http_send`, `std`, `std_default` (private), `terminals`, `random_bytes`, `insecure_random_bytes`, `clock`, `resolution_ns`, `sleep`, `user`.
* `fusedwasi_p2_bind_memory(&s, ...)`, `fusedwasi_p2_set_memory(&s, ...)` -> `(&g, ...)` (same arguments).
* Hooks: `trap`, `realloc`, `wait`, `exit` now take `fusedwasi_p2_guest *`; `clock`, `sleep`, `random_bytes`, `insecure_random_bytes`, `socket_addr_check`, `http_check`, `http_send` still take the state (`fusedwasi_p2_state *`). **The objects of the world take the state and never trap**: the callbacks of `fusedwasi_p2_stream` (`read`, `check_write`, `write`, `flush`, `ready`), `fusedwasi_p2_pollable.ready`, and every `destroy` take `fusedwasi_p2_state *`. A stream tells a failure with `FUSEDWASI_P2_STREAM_FAILED` and `failure` (an allocation that fails: "out of memory", `failure_errno` ENOMEM, where it used to trap); `fusedwasi_p2_pollable.ready` returns 1, 0, or -1 with the reason in the new field `failure` (the WASI function that asked traps with it). `fusedwasi_p2_default_trap/wait/exit` take the guest; `fusedwasi_p2_default_clock/sleep/random_bytes` take the state.
* Constructors that take the state now (were the guest) and give 0 or NULL instead of trapping: `fusedwasi_p2_memory_input_new` (handle), `fusedwasi_p2_buffer_output_new` (pointer, handle by `*handle`), `fusedwasi_p2_fd_input_new`, `fusedwasi_p2_fd_output_new`, `fusedwasi_p2_file_stream_new`, `fusedwasi_p2_descriptor_new`, `fusedwasi_p2_sock_stream_new`, `fusedwasi_p2_sock_pollable`, `fusedwasi_p2_dgram_stream_new`, `fusedwasi_p2_http_incoming_request_new`, `fusedwasi_p2_http_outparam_new`; `fusedwasi_p2_tcp_make(family, fd, state)` has no state argument. New, with the state: `fusedwasi_p2_table_add`, `fusedwasi_p2_input_stream_add`, `fusedwasi_p2_output_stream_add`, `fusedwasi_p2_error_add`, `fusedwasi_p2_deadline_pollable_add`, `fusedwasi_p2_stream_pollable_add`, `fusedwasi_p2_std_get`, `fusedwasi_p2_http_fields_register`. New, for a function that has the guest: `fusedwasi_p2_handle_or_trap(g, handle)`. `fusedwasi_p2_sys_list` lost its last argument; no memory is the error-code `FUSEDWASI_P2_E_INSUFFICIENT_MEMORY` (the guest sees `insufficient-memory`, 0.1 `NOMEM`, not a trap). `fusedwasi_p2_http_future_advance(state, f)` returns -1 for no memory.
* Python: `Pollable.ready(host)` -> `ready(state)` (a `State`); `FutureIncomingResponse.advance(guest)` -> `advance(state)`; new `wasi_p2_core.table_add(state, kind, obj)` (0 when full), `wasi_p2_io.error_add`, `input_stream_add`, `output_stream_add` (state, no guest). The stream methods never received a guest and still do not.
* Helpers that take a guest now (were the state): `fusedwasi_p2_table_new/get/take/alive/drop`, `fusedwasi_p2_mem`, `fusedwasi_p2_load_*`, `fusedwasi_p2_store_*`, `fusedwasi_p2_alloc`, `fusedwasi_p2_string`, `fusedwasi_p2_clock_ns`, `fusedwasi_p2_std`, `fusedwasi_p2_error_new*`, `fusedwasi_p2_input_stream_new`, `fusedwasi_p2_output_stream_new` (the last four are wrappers of the `*_add` ones of the state that trap). `fusedwasi_p2_http_default_send` takes the state (it is the default of `http_send`). The slot `destroy` functions and `fusedwasi_p2_free_obj` take the state.
* `fusedwasi_p2_state.std[3]` is `fusedwasi_p2_stream *` (was `void *`): an assignment of a derived struct needs `&derived->base` (or a cast). `std_default` is private.
* C 0.1: `fusedwasi_p1_create(&cfg, &core, &err, &bad)` and `fusedwasi_p1_destroy(w)` are unchanged and give the state (`fusedwasi_p1_state`, fields `core`, `user`). New: `fusedwasi_p1_guest g; fusedwasi_p1_guest_init(&g, w)`. Every `fusedwasi_p1_<name>(w, ...)` and `fusedwasi_p1_unstable_<name>(w, ...)` takes `&g` (a `void *` as before). `fusedwasi_p1_bind(w, &data, &size)` -> `fusedwasi_p1_bind(&g, &data, &size)`.
* C 0.1, removed: `fusedwasi_p1_state.finished`, `.end_value`, `.memory_data`, `.memory_size`, `FUSEDWASI_P1_END_NONE / END_EXIT / END_SIGNAL`, `FUSEDWASI_P1_HOOK_TRAP`, `FUSEDWASI_P1_STATE(s)` (private), `FUSEDWASI_P1_MEM(w)` now takes the guest. `proc_exit` leaves its code in `g.core.exited` and `g.core.exit_code` (was `core->exited`, `core->exit_code` of the state of 0.2); wasmext's `wasi_end` reads them there.
* C 0.1, `proc_raise`: it no longer ends the program. New hook `fusedwasi_p1_guest.signal` (`uint32_t (*)(fusedwasi_p1_guest *g, uint32_t number)`, returns the errno of the call); without it the answer is NOTSUP; 31 and above is INVAL, 0 is success. wasmext writes today's behaviour (a signal that ends a process traps with the number and the name; 16 to 22 and 27 are answered with success) as the hook.
* Python 0.2: `Host(memories, **everything)` -> `State(**world)` and `Guest(state, memories, realloc=, wait=, trap=, exit=, user=)`. Keywords of `Host` that go to `State`: `clock`, `resolution_ns`, `sleep`, `random_bytes`, `insecure_random_bytes`, `args`, `env`, `initial_cwd`, `stdin`, `stdout`, `stderr`, `terminals`, `preopens`, `network`, `socket_addr_check`, `allow_ip_name_lookup`, `http`, `http_check`, `http_send`, `user`. Every function of `wasi_p2_*` takes the guest first (the same position). `Host.exited`, `exit_code` -> `Guest.exited`, `exit_code`. `Pollable.ready(host)` -> `ready(guest)`; `wasi_p2_cli.std(host, name)` -> `std(state, name)`. New: `wasi_p2_core.trap(guest, message)`. `fusedwasi.Host` -> `fusedwasi.State` and `fusedwasi.Guest`.
* Python 0.1: `State(memory, core=Host(...), preopens=, files=, user=)` -> `State(core=<p2 State>, preopens=, files=, user=)` and `Guest(state, memory, wait=, signal=, user=)`. The memory methods (`put_u32`, `read_bytes`, `in_memory`, ...) are on the `Guest`; `entries`, `files`, `core`, `close_all` stay on the `State`. `State.finished`, `State.end_value`, `END_NONE / END_EXIT / END_SIGNAL` and `Signal` are removed; `proc_exit` sets `Guest.exited`, `Guest.exit_code` (was `core.exited`) and raises `Exit`; `proc_raise` calls `Guest.signal(number)` (a callable returning the errno, or raising), NOTSUP when there is none. wasmhost's `WasiSignal` goes: the table of signals that end a process is its hook.
* Python-only, unchanged: `files=`, `network=`, `http=` (on the `State`).

### Step 3, stage B: WASI 0.1 over WASI 0.2, one State and one Guest (2026-10-27)

The sections above describe the code before this change. There is now one state and one guest for both families (`docs/design_model.md`, section 4).

* C: the types `fusedwasi_p1_state`, `fusedwasi_p1_guest` (and its `core` member), `fusedwasi_p1_config` and `fusedwasi_p1_preopen` are gone; so are `fusedwasi_p1_create`, `fusedwasi_p1_destroy`, `fusedwasi_p1_guest_init`, `fusedwasi_p1_bind`, `fusedwasi_p1_copy` (kept as a private helper), the macros `FUSEDWASI_P1_MALLOC / CALLOC / REALLOC / FREE`, `FUSEDWASI_P1_GUEST`, `FUSEDWASI_P1_MEM`. A function of 0.1 is `uint32_t fusedwasi_p1_<name>(fusedwasi_p2_guest *g, ...)` (the `void *vg` and the `int *trapped` are gone; `proc_exit` returns nothing and goes through the hook `exit` of the guest). Use `fusedwasi_p2_state` with `args`, `env_*`, `std[]`, `preopens` (`struct fusedwasi_p2_preopen {path, name, writable}`), `clock`, `random_bytes`, `sleep` set on it; `fusedwasi_p2_guest_init(&g, &state)`; `fusedwasi_p2_bind_memory(&g, 0, &data, &size)` for the memory of an instance of 0.1; `g.user`, `g.trap`, `g.wait`, `g.exited`, `g.exit_code` where `g.core.*` was; the new hook `g.signal` where `fusedwasi_p1_guest.signal` was. `fusedwasi_p2_free(&state)` frees what `fusedwasi_p1_destroy` freed. New: `fusedwasi_p1_open_descriptors(&state, &err, &bad)` (what `fusedwasi_p1_create` told of a preopen that cannot be opened); `FUSEDWASI_P1_FN` (the marker of a call of the specification); the operations `fusedwasi_p2_fs_*` and the option `FUSEDWASI_P2_FS_P1` in `wasi_p2_filesystem.h`; `fusedwasi_p2_table_find`, `fusedwasi_p2_descriptor_find` (no trap); the guest member `signal`; the state member `p1_table` (private).
* C: `fusedwasi_p1_entry` changed (private); `fusedwasi_p1_be_*`, `fusedwasi_p1_walk`-style helpers, `fusedwasi_p1_release(e)` (now `(state, e)`), `fusedwasi_p1_new_fd(w)` (now `(fds)`) are private.
* Python: `fusedwasi.p1.State`, `Guest` and `Exit` are the classes of `fusedwasi.p2` (`fusedwasi.p1.State is fusedwasi.p2.State`, so `State(core=<p2 State>, preopens=, files=, user=)` becomes `State(preopens=..., <everything of the p2 State>)`: build one `State` with the arguments, the environment, the streams, the clocks and the preopens). `Guest(state, memory, signal=, wait=, trap=, exit=, realloc=, user=)` takes one memory (buffer, function, or object with `read`/`write`/`len`) or a list of memories; the 0.1 `Guest` gained `trap`, `exit` and `realloc`, the 0.2 `Guest` gained `signal`. The memory methods of the old 0.1 `Guest` (`put_u32`, `get_u32`, `read_bytes`, `in_memory`, `load`, `store`, `iov`, `path_at`, `iovs_valid`, ...) are functions of `fusedwasi.p1.wasi_p1_core` that take the guest first. `state.entries`, `State.entry_of`, `new_fd`, `entry_with`, `dir_with`, `release`, `hooked`, `std_stream`, `std_is_real`, `std_filetype`, `stream_status`, `clock_ns` are gone from the state (the map is private: `state.p1`); `State.close_all()` stays and closes everything the state holds. `PreopenError` is raised by `fusedwasi.p1.open_descriptors(state)` (new), not by the constructor. `proc_exit` goes through the hook `exit` of the guest and raises the one `Exit`.
* Python, the Python-only features of the old state of 0.1 (`docs/design_model.md` 4.5): `State(files=Files()/SystemFiles())` is replaced by `State(filesystem=Filesystem()/SystemFilesystem())` (`fusedwasi.p2.wasi_p2_filesystem`), the files behind both families; `Files`, `SystemFiles`, `Entry` (now the private entry of the map), `Listing` (private), `wasi_p1_files` and the `sys_*` aliases of `wasi_p2_filesystem` are gone. Kept: `WasiError` (exported; nothing in the library raises it, as before), `PreopenError`, `Stat` (private), `network=`, `http=`.
* Python 0.2: `FsError` has `name`, `escape` and `done`; the functions of `wasi_p2_filesystem` call the operations (methods of `State.filesystem`).

### Step 5: public and private marks (2026-10-27)

The names of this file up to here are those of their day. Every name that was not public got a mark (`docs/private_names.md` has all 551 C names and fields and the 128 Python ones, one line each). What a user has to change, by group:

* C: `fusedwasi_p2_sys_*`, `_walk_*`, `_default_*`, `_fs_*`, `_http_*` (except the ones of `public_api.md`), `fusedwasi_p1_<helper>` (not the 46 functions), the table (`fusedwasi_p2_table_new / get / take / alive / drop / find`), the memory helpers (`mem`, `load_*`, `store_*`, `alloc`, `string`), `FUSEDWASI_P2_INDEX_*`, `GENERATION_MASK`, `FUSEDWASI_P1_K_*`, the include guards: `fusedwasi_p2_x` -> `fusedwasi_p2__x`, `FUSEDWASI_P2_X` -> `FUSEDWASI_P2__X` (a name of the library with no `p1` / `p2`: `fusedwasi__x`). Calling one still works with the new name, and is not promised.
* C fields: `state.slots`, `slot_count`, `slot_capacity`, `std_default`, `p1_table`; `guest.memory`, `memory_cell`, `memories`; `stream.dead` get two underscores at the end. Use `fusedwasi_p2_bind_memory` / `set_memory` for the memory.
* C, removed: `fusedwasi_p2_http_fields_find`, `fusedwasi_p2_walk_path` (use `fusedwasi_p2__walk_path_with`), `fusedwasi_p2_error_new`, `fusedwasi_p2_error_new_code` (use `fusedwasi_p2_error_add` with the state, or the private `fusedwasi_p2__error_new_codes` with the guest).
* Python: the helpers of `wasi_p1_core` (`put_u32`, `get_u32`, `load`, `store`, `iov`, `path_at`, `read_bytes`, `Stat`, `Entry`, `Fds`, ...), of `wasi_p2_core` (`table_new / get / alive / drop`, `load_*`, `store_*`, `alloc`, `trap`, `Slot`, `INDEX_*`), of `wasi_p2_io` (`Error`, `error_new`, `input_stream_new`, `output_stream_new`), of `wasi_p2_cli` (`FdInput`, `FdOutput`, `SysInput`, `SysOutput`), the classes and constants of `wasi_p2_sockets`, the internal classes of `wasi_p2_http` and `wasi_p2_filesystem` get a leading underscore; `State.slots` -> `_slots`, `State.p1` -> `_p1`, `Guest.memories` -> `_memories`; `wasi_p1.filestat_of_fd` / `_path`, `PATH_MAX`, `LINK_MAX`, `INT64_MAX` too. `fusedwasi.signatures.P1_MODULE` -> `_P1_MODULE`; `Snapshot` is public (it was missing from `__all__`).
