# The model of fusedwasi (a draft, before any code)

Status: a draft for the owner, 2026-10-27. Nothing here is built. It is written first so that the rework (C and the Python twin together) follows one plan, and so that a change is made once, for every user, and not for the one that asked.

## 1. What fusedwasi is, and what it is not

**Why it exists.** The WASI of the existing runtimes is not usable outside them: in Wasmtime it is built on its own types (`WasiCtx` lives in the `Store<T>`, `ResourceTable` is `wasmtime::component`'s, the functions are generated for its `Linker`); uvwasi has WASI 0.1 only, takes host pointers, and carries a libuv loop (`uv_loop_t *loop`) in its state. fusedwasi was started because none of them could be taken by another engine. That is the test of every decision here: *could an engine that nobody here has heard of use this?* Section 11 is how it is checked.

fusedwasi is **a set of ready, cross-platform functions of WASI 0.1 and 0.2**, in C (headers) and in Python (the twin), with the state those functions need. It is not a host and it is not an engine. It knows nothing of the runtime that calls it (`wasm2c`, Wasmtime, a browser) and nothing of the user that wraps it (`wasmext`, `wasmhost`, a new one). A user depends on fusedwasi; fusedwasi never depends on a user, in code, in names, or in comments that describe how a user works.

The rule for a change: it is made when it is right for **any** user. "It is convenient for wasmext" is not a reason; "two users would otherwise write the same workaround" is.

## 2. Who owns what

| Thing | Owner | Why |
| --- | --- | --- |
| What a descriptor, a handle, a stream, a pollable *is*; the tables that hold them; the rules (rights, order, errors) | **fusedwasi** | the functions cannot work without them: `open` returns a handle that has to mean something later |
| The configuration the functions read (arguments, environment, preopens, policies for network and http, clock and random sources) | fusedwasi defines the shape, the **user fills it** | the values come from the user |
| Where the state lives, how many there are, who frees it, how it is shared between instances | **the user** | allocation and lifetime are the user's (a `Store`, a pool, a plain variable) |
| The memory of the guest: where it is, how it is read when it grows | **the user** gives it, fusedwasi reads it | an engine owns its memory |
| How a trap leaves a function, how `cabi_realloc` of the guest is called | **the user** gives it | an engine decides (a `longjmp`, an exception, a returned code) |
| What happens at the end of a program (`exit`, a signal), what the caller sees | fusedwasi **records** it, the user reads it | the contract of section 5 |

## 3. The model: a state and a guest

Today one struct (`fusedwasi_p2_state`, `fusedwasi_p1_state`; in Python `Host` and `State`) holds both the world and the way into the module that calls. That is why a user that shares one world between several instances has to rewrite the memory and a pointer before each call (`user`, "the first member is the instance"): the library has no place for "who is calling now", only the one state.

The draft splits them.

* **State**: the world. Configuration, the tables of descriptors and handles, the policies, the sources of time and random bytes, the end of the program. One for everything that shares files and sockets (a Wasmtime `Store`). Made once, freed once, by the user.
* **Guest**: the way into the module that is calling. The memory (or memories, for a module fused from components), the trap, the `cabi_realloc`, and a pointer of the user (`call`, never touched by the library) that the hooks get back. One for each instance, made once, never rewritten per call; many guests point to one state.

The functions keep their names and their parameters. Only the first one changes its type: it was the state, it is the guest (which reaches the state). The table of signatures and the Protocols (`signatures/`) do not change, because they never named that parameter.

A guest is what Wasmtime gives a call as `GuestMemory` and `Caller`: the memory of the caller and the way back into it. Wasmtime builds it for each call; a user of fusedwasi may do the same (a small struct on the stack, filled by the glue) or make it once for each instance and pass a pointer. The library asks for neither: it only reads the guest it is given.

What is on which side (settled 2026-10-27 after reading Wasmtime, section 9):

| State (world) | Guest (caller) |
| --- | --- |
| `args`, `env`, `initial_cwd`, `preopens` | the memory source (`bind_memory`, `set_memory` stay as the easy way to make one) |
| the tables: descriptors, handles, streams, pollables | `trap` |
| the standard streams `std[3]` (typed `fusedwasi_p2_stream *`; `std_default` is private) and `terminals` | `realloc` (the `cabi_realloc` of the module) |
| `socket_addr_check`, `allow_ip_name_lookup`, `http_check`, `http_send` | `wait` (it can trap: it is the point where the caller may be interrupted) |
| `clock`, `resolution_ns`, `sleep`, `random_bytes`, `insecure_random_bytes` | the end of the call: `exit` (hook), `exited`, `exit_code`, `finished`, `end_value` |
| `user`, the pointer of whoever made the state, given back to the hooks of the world | `user`, the pointer of whoever made the guest, given back to the hooks of the guest |

The name is **Guest**. The hooks of the world take the state; the hooks of the guest take the guest.

**The rule for the objects of the world** (decided 2026-10-27): a stream, a pollable, a slot `destroy` and every constructor of a resource belong to the world. They take the **state** (or nothing), never a guest, and **never trap**: a failure is a return value (a stream: `FUSEDWASI_P2_STREAM_FAILED` with its `failure`; a pollable: `ready` gives -1 with `failure`; a constructor: 0 or NULL). Only the WASI function, which has the guest, turns that into a trap through `g->trap`, with the message it always had. So a stream can be made and given to a state (`std[i]`) while the state is configured, before any guest exists, and the author of a stream does not need to know what a guest is. This is how Wasmtime does it: a stream operation returns a `Result` and the engine decides the trap.

## 4. 0.1 and 0.2 over one state (decided by the owner, 2026-10-27; stage B of step 3, written before the code)

WASI 0.1 and 0.2 are two ways to ask for the same things (files, clocks, random, sockets). **Decided: as in Wasmtime, 0.1 is implemented over 0.2.** There is one State; its ground is the state of 0.2 (the tables of resources, the configuration, the policies, the streams, the clocks, the random source); a descriptor of 0.1 is a map to a resource of 0.2 plus what 0.1 keeps for each descriptor (Wasmtime: `WasiP1Adapter` over `WasiCtx` and `ResourceTable`), and the functions of 0.1 do their work through the operations of 0.2. A file opened by a module of 0.1 is the same resource that a module of 0.2 sees in the same state (it is in the same table, and it is closed by the same `free`).

Why not "a shared table only": a descriptor of 0.2 is a resource with its own system descriptor and rights; a function of 0.1 can work on the same file only through the operations of 0.2, so the shared table already is "0.1 over 0.2".

What guards it: the official wasi-testsuite (72 tests for 0.1, run through `tests/neutral`), the three neutral users, `tests/test_parity_surface.py`, the scripts of `tests/data/p1` that the C driver and the Python twin both run (their output was also kept from the code before this change and compared with, line by line), and the tests of 0.2, which do not change.

### 4.1 One State and one Guest (C and Python)

* **C.** `fusedwasi_p2_state` and `fusedwasi_p2_guest` are the only two types, for both families. The names stay: the "p2" is the ground of the model (the state is the one of 0.2), not a second world. Gone: `fusedwasi_p1_state`, `fusedwasi_p1_guest` (and its `core`), `fusedwasi_p1_config`, `fusedwasi_p1_preopen`, `fusedwasi_p1_create`, `fusedwasi_p1_destroy`, `fusedwasi_p1_guest_init`, `fusedwasi_p1_bind`, `FUSEDWASI_P1_GUEST`, `FUSEDWASI_P1_MEM`, the `void *vg` and the `int *trapped`. A function of 0.1 is `uint32_t fusedwasi_p1_<name>(fusedwasi_p2_guest *g, <parameters>)` (`proc_exit` returns nothing), the same `g` as a function of 0.2; the memory of an instance of 0.1 is memory 0 of its guest (`fusedwasi_p2_bind_memory(g, 0, ...)` or `fusedwasi_p2_set_memory`). The guest gains one hook, `signal` (`proc_raise`; NULL answers NOTSUP).
* **The configuration of 0.1 is the fields of the state:** `args`, `env_*`, `std[3]` and `terminals`, `preopens`, `clock`, `resolution_ns`, `sleep`, `random_bytes`. A preopen is `struct fusedwasi_p2_preopen {path, name, writable}` for both.
* **Python.** `fusedwasi.p1.State is fusedwasi.p2.State`, `fusedwasi.p1.Guest is fusedwasi.p2.Guest`, `fusedwasi.p1.Exit is fusedwasi.p2.Exit`: one class each. `Guest(state, memories=(), *, realloc, wait, trap, exit, signal, user)`; `memories` is a list of memories as now, or one memory (a buffer, a function that gives it, or an object with `read`/`write`/`len`) for the common case of one instance. The functions of 0.1 take the `guest` first, and trap through `trap(guest, message)` like those of 0.2.
* **The end of a call** is the same for both families: `exited` and `exit_code` of the guest. `proc_exit` is `wasi:cli/exit`: it tells the hook `exit` of the guest, whose default records the code and traps. `proc_raise` asks the hook `signal`.
* **One way to trap.** A function of 0.1 traps with `g->trap(g, message)` (it does not return in C; in Python `trap(guest, message)` tells the hook and raises `Trap`). What 0.1 used to report in `*trapped` is now: `proc_exit` (the exit hook), and a hook of the guest or of the state that traps by itself (`wait`, `signal`, a stream, the clock). The signature table and the Protocols describe the guest-facing core signature and do not change; the C parameter lists lose `int *trapped` and `void *vg` becomes `fusedwasi_p2_guest *g`, so `tools/make_signatures.py`, `docs/signatures.md` and the tests that compare the C parameters with the table are updated.

### 4.2 The descriptor map of 0.1

A private table in the state (a slot of the table of resources of kind `P1_FDS`, so that `fusedwasi_p2_free` frees it with the rest; Python: an attribute of the state that `close_all` empties). It is made by the first call of a function of 0.1 on the state, or earlier by `fusedwasi_p1_open_descriptors(state, &err, &bad)` (C) / `fusedwasi.p1.open_descriptors(state)` (Python, raises `PreopenError(.index)`), which tell a directory of `preopens` that cannot be opened with its errno and its place in the list. All the guests of 0.1 of the state share it (as the guests of 0.1 shared the descriptors of their state until now). An entry:

| Field | Meaning |
| --- | --- |
| `kind` | none (free), standard stream, file, directory |
| `handle` | the resource of 0.2: the handle of a `descriptor` in the table of the state (file or directory); for a standard stream the index 0, 1 or 2 of `std[]` |
| `filetype` | the filetype of 0.1 of what the descriptor was opened on |
| `rights`, `inheriting` | the rights of 0.1: a concept of 0.1 that 0.2 does not have; the adapter checks them (NOTCAPABLE) before the operation of 0.2 |
| `flags` | the fdflags (append, dsync, nonblock, rsync, sync) |
| `position` | the offset of the next `fd_read` / `fd_write` (0.2 has no position in a descriptor: `read`, `write` and the streams take an offset) |
| `preopen` | the name the program sees, for the entries 3, 4, ... that `preopens` made in order |
| `listing` | the snapshot of the names of a directory, made at cookie 0 by `fd_readdir` |

Descriptors 0, 1 and 2 are the standard streams of the state (`std[0..2]`, else the descriptors of the process). Numbers 3 and up are the preopens, in the order of `preopens`, then what `path_open` opens, always the lowest free number. `fd_renumber` and `fd_close` change the map; closing a file or a directory drops the resource, closing a standard stream only frees the number. Sockets: a 0.1 descriptor is never a socket (NOTSOCK, or BADF for a number not in the map), as now.

### 4.3 The operations of 0.2 that 0.1 uses

The filesystem functions of 0.2 are split into the operation and the Canonical ABI around it: the operation takes host values (a descriptor, a path as bytes and a length, an offset, a buffer) and returns an error-code or a result; the function of 0.2 reads its arguments from the memory of the guest and writes the result there, and the adapter of 0.1 reads its own. So a rule of 0.2 (what a read-only preopen allows, the walk of a path below a directory, which error a missing right is) is written once. C: `fusedwasi_p2_fs_<operation>(...)` in `wasi_p2_filesystem.h`; Python: the same names as methods of `fusedwasi.p2.wasi_p2_filesystem.SystemFilesystem` (see 4.5). The few places where 0.1 differs from 0.2 are one option, `FUSEDWASI_P2_FS_P1` (Python `p1=True`), of the operations that take a path: a link with a slash after it is not a directory (the walk of 0.1 follows uvwasi, 0.2 follows Wasmtime), the name `.` as the last name (`unlink` and `rmdir`: INVAL; `rename`, `link`, `symlink`: INVAL or EXIST; `mkdir`: EXIST), and a path that leads out of the directory is told apart from an error of the system (it is `NOTCAPABLE` in 0.1, `not-permitted` in 0.2). Streams are used as they are: the standard streams are `std[]` of the state (`read`, `write`), and the readiness of a descriptor is a pollable.

### 4.4 The 46 functions

`fd`: the map of 4.2 (a number that is not in it is BADF). "Rights" = the adapter checks the right of 0.1 first (NOTCAPABLE).

| Function of 0.1 | Operations of 0.2 | What the adapter keeps or does |
| --- | --- | --- |
| `args_sizes_get`, `args_get` | `wasi:cli/environment.get-arguments`: `state->args` | the layout of 0.1 in the memory (pointers, NUL-terminated strings) |
| `environ_sizes_get`, `environ_get` | `get-environment`: `state->env_*` | `NAME=value` strings |
| `clock_res_get` | `monotonic-clock.resolution`, `wall-clock.resolution`: `resolution_ns[id]` | ids 0 to 3; INVAL above |
| `clock_time_get` | `monotonic-clock.now`, `wall-clock.now`: `state->clock` | a clock that cannot be read is NOTSUP (0.2 traps) |
| `random_get` | `wasi:random/random.get-random-bytes`: `state->random_bytes` | IO where 0.2 traps |
| `sched_yield` | none | SUCCESS |
| `proc_exit` | `wasi:cli/exit`: the hook `exit` of the guest | nothing; `exited` and `exit_code` are the guest's, set by the hook (the default records and traps) |
| `proc_raise` | none | the hook `signal` of the guest; NOTSUP without it; INVAL above 30; 0 is success |
| `fd_write` | stdout/stderr: `output-stream.write` on `std[1|2]`; file: `descriptor.write` at an offset | the position; append (the write goes to the end of the file); the right; all the buffers looked at before one is used; a short write |
| `fd_read` | stdin: `input-stream.read` on `std[0]`; file: `descriptor.read` at an offset | the position; waits (`g->wait`) for a byte or the end on a stream; the first buffer that is not empty for a stream |
| `fd_pread`, `fd_pwrite` | `descriptor.read`, `descriptor.write` at the offset given | the right; the position does not move |
| `fd_seek`, `fd_tell` | `descriptor.stat` (size, for the end) | the position is the adapter's; the right (a seek by 0 from the current position is a tell); a descriptor that cannot seek: SPIPE |
| `fd_close` | `descriptor` drop | a standard stream: only the number is freed |
| `fd_renumber` | none | the map |
| `fd_advise` | `descriptor.advise` | the right; the advice is one of six |
| `fd_allocate` | `descriptor.stat`, `descriptor.set-size` | the right; the room is reserved where the system has `posix_fallocate` (no operation of 0.2 reserves), else the file is made as long |
| `fd_datasync`, `fd_sync` | `descriptor.sync-data`, `descriptor.sync` | the right; a standard stream: SUCCESS |
| `fd_fdstat_get` | `descriptor.stat` (the type) | filetype, rights, fdflags; the rights of the standard streams are fixed |
| `fd_fdstat_set_flags` | none in 0.2 | the right; the flags are the adapter's (append is done by `fd_write`); the sync and non-blocking flags are set on the system descriptor |
| `fd_fdstat_set_rights` | none | rights can only be given up |
| `fd_filestat_get` | `descriptor.stat` | the layout of 0.1 (and of `wasi_unstable`); a standard stream is a character device |
| `fd_filestat_set_size` | `descriptor.set-size` | the right |
| `fd_filestat_set_times` | `descriptor.set-times` | the right; the flags of 0.1 (value, now, none) become the `new-timestamp` of 0.2 |
| `fd_readdir` | `descriptor.read-directory`, `directory-entry-stream.read-directory-entry`, `descriptor.stat-at` (the inode) | `.` and `..` first, the names in order, the cookie, an entry cut by the end of the buffer |
| `fd_prestat_get`, `fd_prestat_dir_name` | `wasi:filesystem/preopens.get-directories`: `state->preopens` | the entries of the map that `preopens` made |
| `path_open` | `descriptor.open-at` | the rights and `inheriting` given to the new entry (never more than the directory has); the oflags and fdflags; the rights decide the flags of 0.2 (read, write); a directory asked for as one |
| `path_create_directory` | `descriptor.create-directory-at` | the right |
| `path_remove_directory` | `descriptor.remove-directory-at` | the right |
| `path_unlink_file` | `descriptor.unlink-file-at` | the right; the answer of the system for a directory |
| `path_rename` | `descriptor.rename-at` | the two rights |
| `path_link` | `descriptor.link-at` | the two rights; follow |
| `path_symlink` | `descriptor.symlink-at` | the right; an empty target, a NUL, a rooted target |
| `path_readlink` | `descriptor.readlink-at` | the right; the target cut to the buffer; a rooted target is PERM |
| `path_filestat_get` | `descriptor.stat-at` | the right; follow |
| `path_filestat_set_times` | `descriptor.set-times-at` | the right; follow; the flags |
| `poll_oneoff` | `wasi:io/poll`: the pollable of `input-stream` / `output-stream.subscribe` (a clock of a subscription is the arithmetic of `state->clock`; a wait only for clocks is `state->sleep`) | the subscription and event layouts (and `wasi_unstable`); a descriptor that is a pipe or a FIFO is asked of the system; the number of bytes; the hangup flag |
| `sock_accept`, `sock_recv`, `sock_send`, `sock_shutdown` | none | NOTSOCK for a descriptor of the map, BADF otherwise |

### 4.5 The Python-only features of the old state of 0.1

What they were (read in `wasi_p1_files.py` and `State`): **`files=`**, a backend for the files with the interface `Files` (`preopen`, `stat_*`, `open`, `mkdir`, `remove`, `times_*`, `rename`, `link`, `symlink`, `readlink`, `list`, `read`, `write`, `seek`, `sync`, ..., on an `Entry`), `SystemFiles` (the default, over the system layer of 0.2) and `Files()` (a host without files); the data classes `Stat`, `Entry`, `Listing`; `PreopenError(.index)`; `WasiError` (an errno from a hook; nothing in the library raises it any more). The booleans `network=` and `http=`, `stdin=`, `stdout=`, `stderr=` are on the state of 0.2 and stay as they are.

The choice: **a filesystem backend for 0.2, which both families use.** The operations of 4.3 are methods of `fusedwasi.p2.wasi_p2_filesystem.SystemFilesystem` (the system's files, the default); `State(filesystem=...)` takes another object with the same methods (a base class `Filesystem` answers every operation with `unsupported`: a host without files). A descriptor of 0.2 or of 0.1 is whatever that object gives. So the feature moves one layer down, to the interface that is common to both families, and is not a layer of 0.1: the old `files=` and `Files` (an interface on `Entry`, a thing that does not exist in the model any more) are replaced by `filesystem=` and `Filesystem`. This is a change of the public Python API; it is listed in `interface_review.md` and in the report. In C the host still writes functions (the operations are `static inline` over the system layer), as before: the gap with Python is the one `interface_review.md` already records.

### 4.6 Behaviours that may change

Only where 0.1 on top of 0.2 forces it. Each is listed in `host_scope.md` when it is made, and in the report of the change. Expected from the design, to be confirmed by the code:

1. `proc_exit` is the exit of 0.2: a hook `exit` that replaces the default also receives `proc_exit`, and is the one that records `exited` (the default does).
2. `poll_oneoff` that waits for a descriptor asks the pollables between two `wait`s of the guest (1 ms by default), instead of blocking in `poll(2)`; a wait only for clocks is the hook `sleep`, as before.
3. A preopen that cannot be opened is told by `open_descriptors` (and `PreopenError`); the first call of a function of 0.1 that makes the map answers with the errno instead (the state is not created by 0.1 any more).
4. The files and directories of 0.1 are freed by `fusedwasi_p2_free(state)` (there is no `fusedwasi_p1_destroy`).
5. The position of a file is the adapter's: an `fd_read` / `fd_write` is a `pread` / `pwrite` at it. A file that cannot be read at an offset (a FIFO, a terminal opened by a path) is read and written at the system's own position, as before.

Anything else that shows up while the code is written is added here before it is pushed.

### 4.7 Order of the work

(1) this document; (2) the operations of 0.2 for the filesystem, in C and in Python, with the functions of 0.2 unchanged and their tests green; (3) one State and one Guest, the map, and arguments, environment, clocks, random, standard streams, preopens and `proc_*` on them; (4) files and directories over the operations; (5) `poll_oneoff` and the rest; (6) the old implementation of 0.1 removed (it was kept working until the new one passed everything); (7) README, `interface_review.md`, `signatures.md`, `release_checklist.md` section 5, the neutral users.

## 5. Public and private

Written once, kept (section 4 of `interface_review.md` has the list; this draft only adds the rule):

* **Public**: the configuration fields, the hooks, the guest's memory and hooks, the end of the program (`exited`, `exit_code`, `finished`, `end_value`, the host clears the flags before the next call), the constants, the functions.
* **Private**: the tables and their layout (handles, slots, entries), every `*_sys_*`, `*_walk_*`, `*_default_*`. A private name carries a mark (a double underscore or a `_private` tail); a comment block "Host API" at the top of each header lists the public ones.

Dead names go: `FUSEDWASI_P1_HOOK_TRAP`; `FUSEDWASI_P1_END_EXIT` is set by `proc_exit` or removed; the macros `MALLOC`, `CALLOC`, `FREE` become private (the caller owns its strings, `create` copies).

## 6. The Python twin

Parity is a rule, not a hope: **every change of section 3 to 5 lands in C and in Python in the same commit, with a test that runs both and compares** (`tests/test_parity_surface.py` already does this for the memory binding).

* `State` is the same world in Python (today: `fusedwasi.p1.wasi_p1_core.State`, `fusedwasi.p2.wasi_p2_core.Host`).
* `Guest` is new in Python: `Guest(state, memory=..., realloc=..., call=...)`, `memory` a buffer or a function that gives it (as now). Python functions take the guest first, as the C functions do.
* Traps are exceptions; the end of a program is the exception `Exit` or `Signal` and also the fields, as now.
* A feature that only Python has (`files=`, booleans for the network) stays Python-only, marked as such in `interface_review.md`; it is not an excuse for a different shape of the common part.

## 7. Open decisions (the owner)

1. ~~One state for 0.1 and 0.2~~ decided: 0.1 over 0.2 (section 4).
2. ~~The name~~ decided: Guest (first Session, changed 2026-10-27: a Session sounds like Wasmtime's WasiCtx, which is the State here). The first parameter of a function is the guest: `g` in C, `guest` in Python (not `s`, which would read as the state); the state is `g->state.
3. ~~The border cases~~ settled (section 3). `proc_raise` decided (owner, 2026-10-27): a hook `signal` on the guest; the default answers `NOTSUP` as Wasmtime does; the library no longer ends the program by itself (section 10).
4. ~~Typed `std[3]`~~ decided with the split: `fusedwasi_p2_stream *std[3]` (a forward declaration), the one-line casts of derived structs in the drivers are fixed in the same change.
5. **Order**: the doc is reviewed, then the C and the twin of the memory and the guest (one change; **step 1 done**, see "Step 1: what was learned" below), then the shared state (**step 3, stage B: in progress**, the design is in section 4), then the public/private marks and the dead names, then the checklist (`release_checklist.md`: Wasmtime comparison, Windows and macOS, the package test), then `0.1.0a1`.

## 8. What the users do after

Nothing before the doc is agreed. Then each user adapts in its own repository (wasmext: one guest for each instance, one state for a `Store`; wasmhost: the same in Python), and the C3 work in wasmext waits for it.

## 9. What the others hold (checked 2026-10-27)

**Wasmtime** (`crates/wasi/src/p1.rs`, read; `ctx.rs` and `view.rs` not read): the world is `WasiCtx` plus `ResourceTable`, kept in the data of the store; `WasiP1Ctx` adds only a map from descriptors of 0.1 to resources of 0.2 (`WasiP1Adapter`) and works over the interfaces of 0.2. The memory of the caller is an argument of each call (`GuestMemory`, built by the glue from the `Caller`). So: one world, per-call memory.

**uvwasi** (`include/uvwasi.h`, `wasi_types.h`, read): `uvwasi_t` holds the world only: the descriptor table (`fds`, hidden), the arguments and environment (`argc`, `argv`, `argv_buf`, `envc`, `env`, `env_buf`), the allocator and the libuv loop; `uvwasi_options_t` gives the table size, the preopens (and preopened sockets), arguments, environment, the three standard descriptors and the allocator. It holds **no guest memory and no trap**: the header has no linear-memory API, and every call takes host pointers (`const uvwasi_ciovec_t *iovs`, `char **argv`). The embedder turns guest addresses into host pointers before the call (the header does not say so; this is the only way the signatures can work, and is how its users call it). Its state has no place for "who is calling".

What this says for fusedwasi:

* Both keep the world apart from the caller. That is the split of section 3 (state and guest), so the split is not a wasmext idea.
* uvwasi avoids the guest by making the embedder translate every guest address; for `fd_write` the embedder builds an array of host `iovec`s from the guest's, on every call. fusedwasi chose the other way (the library reads the guest memory itself), which is cheaper for an engine like wasm2c where the memory is a flat array already, and is why it needs a guest at all. Keeping that choice is deliberate; a user that prefers the uvwasi way can still give a guest whose memory is the whole host address space (base 0), but that is not a goal.
* Neither has a hook for `trap` or `realloc`, because neither calls into the guest. fusedwasi does (a list or a string for the guest in 0.2), so the guest carries them.

### Wasmtime on the border cases (read 2026-10-27: `ctx.rs`, `p1.rs`; `cli.rs` did not show `I32Exit` or `wasi:cli/exit`)

* `WasiCtx` holds the stdio (`StdinStream` and `StdoutStream` objects), the clocks, the random sources, the policies of the sockets, the environment, the arguments and the preopens. It holds **no memory, no trap, no realloc and no exit status**. This is the same cut as the table above.
* **The end of the program** leaves the call as its error: `proc_exit` returns `I32Exit(status)` (status of 126 and more is refused with an error); nothing is stored in `WasiP1Ctx` or `WasiCtx`. So the end is the result of one call, which is why `exited` and the others belong to the guest.
* `proc_raise` answers `NOTSUP` in Wasmtime. fusedwasi ends the program with the signal (`finished`, `end_value`). That is a difference from Wasmtime, not from the WASI text; it came with the code moved from wasmext (`8f35972`, `5058d6a`), and wasmhost answers `ENOSYS` and does not end the program (its README). Decided in section 10.
* **Waiting**: Wasmtime has no `wait` hook. A blocking `poll_oneoff` is an `await` point of the caller's task, or, when `allow_blocking_current_thread` is set and there is one relative clock, a `thread::sleep`. The wait belongs to the caller (it is where the caller can yield or be interrupted); fusedwasi is synchronous, so the hook stands for the `await`, and it is on the guest.
* **`user`**: the data of the user is the `T` of `Store<T>`; `WasiCtx` has no pointer of the user, because Rust closures carry their own data. C has no closures, so each side has a `user`.

## 10. `proc_raise` (decided by the owner, 2026-10-27)

What a signal does to a program is the host's decision, not WASI's: the guest only asks. So the library does not end the program on its own.

* The guest has a hook `signal(guest, number)`. It returns an errno-like result for the call, or does what the host wants (ends the program with its own means: a trap, an exception).
* The default (no hook): answers `NOTSUP`, as Wasmtime does. The program goes on.
* `sig > 30` stays `INVAL`; `0` stays success (no signal).
* `finished`, `end_value`, `FUSEDWASI_P1_END_SIGNAL` and `FUSEDWASI_P1_END_NONE`/`END_EXIT` go away from the state: what remains of the end of a call is `exited` and `exit_code`, on the guest (section 3). Python: `WasiSignal` and `Signal` go away; the hook is a callable of the `Guest`.
* The user that wants today's behaviour of wasmext (a signal that kills ends the program, with the number and the name) writes it as the hook; the table of which signals end a process moves to that user. The test `tests/test_wasi_p1_core.py` (`proc_raise trapped`, `end 2 9`) is rewritten for the hook.

## 11. The proof of neutrality (accepted by the owner, 2026-10-27)

"fusedwasi does not care who uses it" is shown, not claimed. The rework is not done until fusedwasi has, **inside this repository**, two small users that have nothing to do with `wasm2c`, `wasmext` or `wasmhost`, and that use only the public API of this document:

1. **A Python user over an engine that is not ours**: the Python twin driven by `wasmtime-py` (the `State` and a `Guest` per instance, the memory as a function that gives the memory of the instance in call, `trap` and `exit` as exceptions), running a small WASI 0.1 program and a small WASI 0.2 command (as a core module) and checking the result against the same program under Wasmtime's own WASI. Lives in `examples/` or `tests/neutral/`, with its own test.
2. **A C user over an engine that is not `wasm2c`**: a small C driver (a tiny interpreter or an existing small engine such as `wasm3` or `WAMR`, whichever can be had in CI without a heavy dependency) with its own `Guest`, memory source, `trap` and `realloc`. If no engine is practical, the driver runs the C functions on a hand-made guest memory, as the existing C tests do, and says plainly that this proves the C interface but not an engine.

Rules for both: they include only the public headers and import only the public Python names; they live in the repository of fusedwasi; they run in its CI; a change of the public API that breaks one of them is a change that is reviewed, not worked around.

Where it fits in the order (section 7, item 5): after the split (step 1) and 0.1 over 0.2 (step 2), before the public/private marks are frozen and before `0.1.0a1`. The first user (Python over `wasmtime-py`) can start as soon as the split is pushed and is the quickest check that the split is neutral.

### What runs (2026-10-27)

All in `tests/neutral/`, run by `.github/workflows/neutral.yml` (ubuntu-latest). Each user has its own commit, its own tests and its own job; none of them is imported by the library or by another user (the reference run under Wasmtime's own WASI, `neutral_reference.py`, is shared by the two test files and has no fusedwasi in it).

| User | Engine | What it does | Where |
| --- | --- | --- | --- |
| Python over `wasmtime-py` (50.0.1) | Wasmtime | A `State` and a `Guest` per instance. Runs WASI 0.1 core modules and a WASI 0.2 command given as a core module with `cabi_realloc`; compares what the program wrote, its status and its files with the same program under Wasmtime's own WASI (the module with `define_wasi`, the command as a component, `programs/p2_command.component.wat`). Two instances over one state (0.1 and 0.2), one instance's exit or trap leaves the other alone. Runs the official wasi-testsuite for 0.1 as the runtime of the runner | `neutral_wasmtime.py` (the user), `neutral_wasmtime_run.py` (command line), `wasi_testsuite_adapter.py`, `test_neutral_wasmtime.py`, `programs/` |
| C over `wasm3` (tag v0.5.0, commit `6b8bcb1e07bf26ebef09a7211b0a37a446eafd52`, fetched by the CI and by `WASM3_SRC`, not vendored) | wasm3 | The public headers only. A `fusedwasi_p1_guest` or `fusedwasi_p2_guest` for each instance and one state for all; the memory by pointer to a cell that refreshes from `m3_GetMemory`; `trap` as a `longjmp`; `realloc` as `m3_Call` of `cabi_realloc` from inside the import. A generator writes a wasm3 raw function for every import (all 46 of 0.1 and the 201 of 0.2 that have a function) out of the public table and the headers; the driver links the ones a module imports. The same programs as the Python user, compared with Wasmtime's own WASI; two instances over one state, a trap in a third, a memory that moves in the middle of a call | `c_wasm3/neutral_wasm3.c` (the user), `c_wasm3/gen_wasm3_glue.py`, `test_neutral_wasm3.py`, `neutral_reference.py` (the Wasmtime reference both share) |
| Python over `pywasm3` (0.5.0, built from its source distribution; the binding of wasm3 v0.5.0) | wasm3 | The smaller sibling of the first: same public names, a `Guest` per instance over a shared `State`; the imports are read from the binary (pywasm3 lists none), linked by signature string; the memory is `Runtime.get_memory`, asked at each access; `cabi_realloc` is a call of the export from inside the import. 0.1 report, two instances over one state, a 0.2 command against Wasmtime's own WASI, a trap, a memory that moves | `neutral_wasm3py.py`, `test_neutral_wasm3py.py`, `neutral_binding.py` (the two Python users share the lookup of the function of an import) |

The testsuite is `WebAssembly/wasi-testsuite`, branch `prod/testsuite-base`, pinned by commit `9dfd4867f12498bd6411a57739c24b4880668683` in `neutral.yml`: 72 tests of `wasm32-wasip1` pass (AssemblyScript 12, Rust 46, C 14), 0 fail; the suites for `wasm32-wasip3` are skipped by the runner because the adapter says it answers for 0.1 only. It runs in the CI job and locally with `WASI_TESTSUITE=<checkout> pytest tests/neutral`.

How the engine fits the model, as the user wrote it: the memory of the guest is a function (`Instance.memory`, a view of `Memory.data_ptr`, built at each access, so `memory.grow` is followed); `realloc` is a call of the export `cabi_realloc` through the `Caller` of the call in progress; a trap is an exception (wasmtime-py unwinds and raises the same exception out of the call); the end of a program is read from `guest.exited` and `guest.exit_code`, which the user clears before each call.

What the Python user needed that the API does not give (each is a leak or a gap, reported, none worked around inside `src/`):

1. **The name of the function for an import was not public** (closed, round 2): the signature table now carries `c_name`, `python_module`, `python_name` and `takes_comp` for every function, generated by `tools/make_signatures.py` from the headers and the Python files (it stops if the twins and the table disagree), and `fusedwasi.signatures.find_import(module, name)` gives the row. The three users bind with it; the C glue generator no longer reads the headers.
2. **Whether a function of 0.2 takes `comp` was not in the table** (closed, round 2: `takes_comp`, tested against the headers and `inspect`).
3. **The Python memory had to be a `memoryview` of format `B`** (closed, round 1a): a view of a `ctypes` array has the format `<B` and `Guest.store` / `store_bytes` failed on it. Both twins' `Guest` now see any writable buffer (a `bytearray`, a `memoryview`, a `ctypes` array or its view, an `array` of wider items, an `mmap`) as flat bytes (`wasi_p2_core.flat_view`, at each access, also for a function that gives the buffer); the user no longer casts. `tests/test_memory_forms.py`.
4. **Integers are unsigned, and that was not written for Python** (closed, round 1b: written in the docstrings of `wasi_p2_core` and `wasi_p1`, in `wasi_p2_core.h` and in the README, with what an engine that passes signed values does: mask arguments by their width, re-sign results). Decided not to mask in the twin's public entries.
5. **`fusedwasi.p1` and `fusedwasi.p2` exported nothing** (closed, round 2): each family package now exports its own `State`, `Guest`, `Exit` (and for 0.2 `Trap`, `Pollable`, `flat_view`, the streams and their exceptions, the constants; for 0.1 `WasiError`, `PreopenError`, `errno_of`, the constants) with `__all__`; the top-level `fusedwasi` has only `get_include`, `get_signatures_path` and `__version__` (the 0.2 `State`, `Guest`, `Exit`, `Trap` it re-exported are gone). `tests/test_exports.py`.
6. **A State of 0.1 cannot be given the arguments, the environment or the standard streams**: they are the `core` State of 0.2 (`wasi_p1_core.State(core=wasi_p2_core.State(args=..., env=..., stdout=...), preopens=...)`), so a user of 0.1 builds two objects. Step 2 of the order (0.1 over 0.2) is expected to change it.
7. **The Python `Guest` of 0.1 has no `trap` and no `realloc` hook** (a function of 0.1 answers an errno; only the C guest of 0.1 carries those of its embedded guest of 0.2). Not a problem for this user; noted because a user that wants to see the messages of traps in 0.1 cannot.
8. **Versions of the imports** (closed, round 2): `find_import` matches a package of 0.2 of any patch version.

**What the C user over wasm3 needed** (wasm3 was built at `-O0`; 7 programs-and-checks pass locally, see the table; Windows and macOS not tried, `neutral.yml` runs it on ubuntu only):

9. **The published include path was not enough for 0.1** (fixed in a separate commit, `dbb55e2`, with a test): `wasi_p1_core.h` included the headers of 0.2 by bare name, so `#include <fusedwasi/p1/wasi_p1.h>` with only `-I$(python -m fusedwasi)` (or the CMake target `fusedwasi::headers`) failed. Closed in round 1c: the header of 0.1 gave `-Wunused-parameter` in `fusedwasi_p1_be_list`, `fusedwasi_p1_subscription_wait` and the four socket calls (now written out, with the placeholders named like the Python twin's parameters and unused on purpose), and GCC 13 gave about forty `-Wmaybe-uninitialized` at `-O1` and above (`fusedwasi_p2_stat` after `sys_fstat`/`sys_lstat`: the structs are now zeroed first, in the POSIX and Windows layers; `n` of the http reader and two `itself` flags are initialised). The headers now build with no warning at `-Wall -Wextra`, GCC 13 and clang, `-O0` `-O1` `-O2` `-O3` `-Os` `-Og`, through the wasm3 driver (which uses every function); the repository's own builds no longer pass `-Wno-unused-parameter`, and the driver is built `-O2 -Wall -Wextra -Werror`. Windows (MSVC) not tried for the zeroing.
10. **The memory source is a pointer to a cell, not a function.** wasm3 has `m3_GetMemory(runtime, &size, 0)` and keeps its base nowhere the user can point at, so the user keeps a cell in the instance and writes it at every entry of an import and after every call back into the module (`cabi_realloc` may have grown, and moved, the memory; a test fails if that refresh is removed). Python takes a function and needs none of this. An engine that stores its base in a field (wasm2c) is the case the interface was shaped for.
11. **Two ways to trap.** A function of 0.2 traps through `g->trap`, which must not return; a function of 0.1 reports through its `int *trapped` and also uses `core.trap` for what it does through 0.2. The user needs both: a `longjmp` for the hook and a check of `trapped` after each call of 0.1. `proc_exit` of 0.1 is a `trapped`; `wasi:cli/exit` is the default `exit` hook, which records and calls `trap`, so the user reads `exited` before deciding that a returned text is a trap.
12. **0.1 and 0.2 guests are two types with different paths.** The p1 functions take `void *vg`, the p2 functions `fusedwasi_p2_guest *`; the user, the trap, `exited` and `exit_code` of an instance of 0.1 are at `g1.core.*`. A driver for both writes two paths (step 2 of the order is expected to remove the second).
13. **Which C function an import is, and whether it takes `comp`, is again not in the table** (items 1 and 2): the generator takes the name from the rule written again and the arity from a regular expression over the prototypes in the headers.
14. **`realloc`** fits: wasm3 lets an import call back into the module (`m3_Call` from inside a raw function; it moves its stack to the caller's `_sp`), with one rule for the user, copy the arguments of the import out of that stack first, since the call reuses it. The library re-reads the memory after every `realloc` (it reads through the pointer at each access), which the growing module checks. wasm3 sizes memory in a `uint32_t`, so a memory of exactly 4 GiB cannot be given; nothing in fusedwasi's interface asks for more.
15. **Nothing in the C interface failed to fit wasm3** beyond item 10; the library was not bent. The standard output of the programs is the process's descriptor 1 (no capture without a stream of the user's own; not tried).

**What the Python user over pywasm3 needed**: nothing more than the first Python user (items 1 to 8 again; the lookup of the function and the test of `comp` are now written once, in `neutral_binding.py`, for both). pywasm3 has no types and no stubs, so the user reaches it through `importlib` as `Any` (pyright strict). `pywasm3` is a source distribution only (2021): it builds on Python 3.13 and 3.14 here, with a C compiler, and is a separate dependency group (`neutral-wasm3`) so that nothing else needs it. The job installs it and fails if it cannot, rather than skipping.

Choices the document did not fix and the user made: one `Store` for each instance (the shared thing is the `State`, not the engine's store); a trap hook that only records the message; the status of a process is the code of the exit, 0 for a return, 1 for a trap.

## 11. Step 1: what was learned (done 2026-10-27)

The split is made in C and in Python in one change: `fusedwasi_p2_state` / `fusedwasi_p2_guest` (Python `State` / `Guest` of `fusedwasi.p2.wasi_p2_core`) and `fusedwasi_p1_state` / `fusedwasi_p1_guest` (Python `State` / `Guest` of `fusedwasi.p1.wasi_p1_core`). The 0.1 and 0.2 states are still two; the 0.1 guest is built so that step 2 can drop it. Things the doc did not say:

* **The guest of 0.1 contains a guest of 0.2** (`fusedwasi_p1_guest.core`, the first member). The calls of 0.1 go through 0.2 for the standard streams, the clock that traps, the listing of a directory and `wait`; all of them can trap or wait, so they need a guest of 0.2. Embedding it means a host makes one object for an instance of 0.1 (`fusedwasi_p1_guest_init`), the memory of 0.1 is memory 0 of that guest, and `trap`, `wait`, `exited`, `exit_code` and `user` of the guest are the ones of the embedded guest. In step 2 `fusedwasi_p1_guest` becomes `fusedwasi_p2_guest` and `core.` goes away. `signal` is on the guest of 0.1 only (0.2 has no signal).
* **Callbacks of an object (`fusedwasi_p2_stream.read/write/...`, `fusedwasi_p2_pollable.ready`, `destroy`) take the state and never trap** (first built with the guest, so that they could trap on out of memory; reversed by the owner the same day, see the rule in section 3). An allocation that fails in a stream is `STREAM_FAILED` with `failure` "out of memory" (`errno` ENOMEM); in a pollable it is `ready` = -1 with `failure`; in a constructor it is 0 or NULL. `fusedwasi_p2_table_add(state, ...)` is the table without a guest (0 when full); `fusedwasi_p2_table_new(g, ...)` and `fusedwasi_p2_handle_or_trap(g, handle)` are for the WASI function.
* `proc_exit` of 0.1 still records the code and reports `*trapped`; it does not call the `exit` hook (0.1 never had one). Python: `proc_exit` sets `exited` and `exit_code` of the guest and raises `Exit`.
* A hook of the guest in Python takes no guest argument (a closure carries its data), as the hooks of the state already did: `trap(message)`, `exit(code)`, `signal(number)`, `wait()`, `realloc(comp, ...)`. In C they take the guest, which is how a hook finds `user`.
* The constructors of a resource (`fusedwasi_p2_memory_input_new`, `fusedwasi_p2_fd_input_new`, `fusedwasi_p2_file_stream_new`, ...) take the state and give 0 or NULL on failure. The helpers that are bodies of WASI functions (`fusedwasi_p2_table_new`, `fusedwasi_p2_error_new`, `fusedwasi_p2_std`, `fusedwasi_p2_stream_pollable`, `fusedwasi_p2_deadline_pollable`) keep the guest and are thin wrappers of the `*_add` / `*_get` ones of the state. `fusedwasi_p2_sys_list` no longer traps: no memory is `FUSEDWASI_P2_E_INSUFFICIENT_MEMORY`, which the guest sees as a returned error (an allocation failure in the world is a returned error; the guest decides what it means).
* `FUSEDWASI_P1_HOOK_TRAP`, `FUSEDWASI_P1_END_*`, `finished`, `end_value`, Python `Signal`/`WasiSignal` and `END_*` are gone. `proc_raise` asks the hook for every number from 1 to 30; the table of signals that are ignored moved to whoever writes the hook.
