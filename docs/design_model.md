# The model of fusedwasi (a draft, before any code)

Status: a draft for the owner, 2026-10-27. Nothing here is built. It is written first so that the rework (C and the Python twin together) follows one plan, and so that a change is made once, for every user, and not for the one that asked.

## 1. What fusedwasi is, and what it is not

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

## 3. The model: a state and a session

Today one struct (`fusedwasi_p2_state`, `fusedwasi_p1_state`; in Python `Host` and `State`) holds both the world and the way into the module that calls. That is why a user that shares one world between several instances has to rewrite the memory and a pointer before each call (`user`, "the first member is the instance"): the library has no place for "who is calling now", only the one state.

The draft splits them.

* **State**: the world. Configuration, the tables of descriptors and handles, the policies, the sources of time and random bytes, the end of the program. One for everything that shares files and sockets (a Wasmtime `Store`). Made once, freed once, by the user.
* **Session**: the way into the module that is calling. The memory (or memories, for a module fused from components), the trap, the `cabi_realloc`, and a pointer of the user (`call`, never touched by the library) that the hooks get back. One for each instance, made once, never rewritten per call; many sessions point to one state.

The functions keep their names and their parameters. Only the first one changes its type: it was the state, it is the session (which reaches the state). The table of signatures and the Protocols (`signatures/`) do not change, because they never named that parameter.

What is on which side (draft, to be settled in review):

| State (world) | Session (caller) |
| --- | --- |
| `args`, `env`, `initial_cwd`, `preopens` | memory source (`bind_memory`, `set_memory` stay as the easy way to make one) |
| the tables: descriptors, handles, streams, pollables | `trap` |
| `socket_addr_check`, `allow_ip_name_lookup`, `http_check`, `http_send` | `realloc` (the `cabi_realloc` of the module) |
| `clock`, `resolution_ns`, `sleep`, `wait`, `random_bytes`, `insecure_random_bytes` | `call` (a pointer of the user, passed back to the hooks) |
| `exit` hook, `exited`, `exit_code`, `finished`, `end_value` | |
| `user` (the pointer of the user for the whole world) | |

`wait` (a poll that blocks) is on the border: it blocks a call, so it may want the session. Open (section 7).

## 4. 0.1 and 0.2 over one state

WASI 0.1 and 0.2 are two ways to ask for the same things (files, clocks, random, sockets). Wasmtime implements 0.1 over the state of 0.2: a descriptor of 0.1 is a map to a resource of 0.2, so a file opened by a module of 0.1 is the same resource a module of 0.2 sees in the same store.

The draft follows that: **one State**, with the tables of 0.2 as the ground, and the descriptors of 0.1 as a thin table over it. Whether this is the right size for fusedwasi, and whether the functions of 0.1 are rewritten over the calls of 0.2 or keep their own bodies and only share the table, is the big open decision (section 7). If it is not taken, the honest statement is "0.1 and 0.2 are two independent worlds in one library", and a user that wants one `Host` gets two states.

## 5. Public and private

Written once, kept (section 4 of `interface_review.md` has the list; this draft only adds the rule):

* **Public**: the configuration fields, the hooks, the session's memory and hooks, the end of the program (`exited`, `exit_code`, `finished`, `end_value`, the host clears the flags before the next call), the constants, the functions.
* **Private**: the tables and their layout (handles, slots, entries), every `*_sys_*`, `*_walk_*`, `*_default_*`. A private name carries a mark (a double underscore or a `_private` tail); a comment block "Host API" at the top of each header lists the public ones.

Dead names go: `FUSEDWASI_P1_HOOK_TRAP`; `FUSEDWASI_P1_END_EXIT` is set by `proc_exit` or removed; the macros `MALLOC`, `CALLOC`, `FREE` become private (the caller owns its strings, `create` copies).

## 6. The Python twin

Parity is a rule, not a hope: **every change of section 3 to 5 lands in C and in Python in the same commit, with a test that runs both and compares** (`tests/test_parity_surface.py` already does this for the memory binding).

* `State` is the same world in Python (today: `fusedwasi.p1.wasi_p1_core.State`, `fusedwasi.p2.wasi_p2_core.Host`).
* `Session` is new in Python: `Session(state, memory=..., realloc=..., call=...)`, `memory` a buffer or a function that gives it (as now). Python functions take the session first, as the C functions do.
* Traps are exceptions; the end of a program is the exception `Exit` or `Signal` and also the fields, as now.
* A feature that only Python has (`files=`, booleans for the network) stays Python-only, marked as such in `interface_review.md`; it is not an excuse for a different shape of the common part.

## 7. Open decisions (the owner)

1. **One state for 0.1 and 0.2** (section 4), and how deep: a shared table of descriptors only, or the functions of 0.1 over those of 0.2.
2. **The names**: `session` or another word; whether the first parameter keeps `s`.
3. **The border cases** of section 3 (`wait`, `exit`, `user`).
4. **Typed `std[3]`**: tried and reverted once (a derived struct broke the assignment); with the session split it may be settled differently.
5. **Order**: the doc is reviewed, then the C and the twin of the memory and the session (one change), then the shared state, then the public/private marks and the dead names, then the checklist (`release_checklist.md`: Wasmtime comparison, Windows and macOS, the package test), then `0.1.0a1`.

## 8. What the users do after

Nothing before the doc is agreed. Then each user adapts in its own repository (wasmext: one session for each instance, one state for a `Store`; wasmhost: the same in Python), and the C3 work in wasmext waits for it.

## 9. What the others hold (checked 2026-10-27)

**Wasmtime** (`crates/wasi/src/p1.rs`, read; `ctx.rs` and `view.rs` not read): the world is `WasiCtx` plus `ResourceTable`, kept in the data of the store; `WasiP1Ctx` adds only a map from descriptors of 0.1 to resources of 0.2 (`WasiP1Adapter`) and works over the interfaces of 0.2. The memory of the caller is an argument of each call (`GuestMemory`, built by the glue from the `Caller`). So: one world, per-call memory.

**uvwasi** (`include/uvwasi.h`, `wasi_types.h`, read): `uvwasi_t` holds the world only: the descriptor table (`fds`, hidden), the arguments and environment (`argc`, `argv`, `argv_buf`, `envc`, `env`, `env_buf`), the allocator and the libuv loop; `uvwasi_options_t` gives the table size, the preopens (and preopened sockets), arguments, environment, the three standard descriptors and the allocator. It holds **no guest memory and no trap**: the header has no linear-memory API, and every call takes host pointers (`const uvwasi_ciovec_t *iovs`, `char **argv`). The embedder turns guest addresses into host pointers before the call (the header does not say so; this is the only way the signatures can work, and is how its users call it). Its state has no place for "who is calling".

What this says for fusedwasi:

* Both keep the world apart from the caller. That is the split of section 3 (state and session), so the split is not a wasmext idea.
* uvwasi avoids the session by making the embedder translate every guest address; for `fd_write` the embedder builds an array of host `iovec`s from the guest's, on every call. fusedwasi chose the other way (the library reads the guest memory itself), which is cheaper for an engine like wasm2c where the memory is a flat array already, and is why it needs a session at all. Keeping that choice is deliberate; a user that prefers the uvwasi way can still give a session whose memory is the whole host address space (base 0), but that is not a goal.
* Neither has a hook for `trap` or `realloc`, because neither calls into the guest. fusedwasi does (a list or a string for the guest in 0.2), so the session carries them.
