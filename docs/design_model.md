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

## 4. 0.1 and 0.2 over one state (decided by the owner, 2026-10-27)

WASI 0.1 and 0.2 are two ways to ask for the same things (files, clocks, random, sockets). **Decided: as in Wasmtime, 0.1 is implemented over 0.2.** There is one State; its ground is the state of 0.2 (the tables of resources, the configuration, the policies); a descriptor of 0.1 is a map to a resource of 0.2 (Wasmtime: `WasiP1Adapter` over `WasiCtx` and `ResourceTable`), and the functions of 0.1 do their work through the operations of 0.2. A file opened by a module of 0.1 is the same resource that a module of 0.2 sees in the same state.

Why not "a shared table only": a descriptor of 0.2 is a resource with its own position, rights and system descriptor. A function of 0.1 can work on the same file only through the operations of 0.2, so the shared table already is "0.1 over 0.2".

What it costs: the 46 functions of 0.1 are rewritten over 0.2, in C and in Python. What guards it: the official wasi-testsuite (72 tests for 0.1, run in the CI of wasmext), `tests/test_parity_surface.py`, and the drivers that compare the C with the Python.

Order: it is done after the split of section 3 (the guest), on the finished guest, not together with it. Until then 0.1 keeps its own state.

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
5. **Order**: the doc is reviewed, then the C and the twin of the memory and the guest (one change), then the shared state, then the public/private marks and the dead names, then the checklist (`release_checklist.md`: Wasmtime comparison, Windows and macOS, the package test), then `0.1.0a1`.

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
