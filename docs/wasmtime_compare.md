# fusedwasi against Wasmtime (checklist section 2)

Run on 2026-10-26 (the checklist is dated the same day) from `docs/experiments/wasmtime_compare/`, at fusedwasi `6c5c655` plus the scripts. **Everything in this file was run** unless it says otherwise; a difference that was not seen in a run is not listed.

* Wasmtime: the `wasmtime` package 50.0.1 from pip (a Wasmtime 50 library), Linux x86-64, Python 3.14 in a scratch venv. (The older note in `release_checklist.md` and the `wasmtime_exit` experiment of wasmext name 49.0.0; this run used what pip gave today.)
* fusedwasi: the Python twin (`fusedwasi.p1.wasi_p1`, `fusedwasi.p2.wasi_p2_*`). **The C side was not run here.** The tests of the repository keep the C and the Python equal, so a result of the Python is taken to stand for the C; that is the repository's claim, not something this comparison measured.
* The WIT: `0.2.12`, the files of the `wasip2` crate that was already in the cargo registry of the machine; `wasm-tools` 1.262.0 and `wat2wasm` were installed; no binary was downloaded.

## How the comparison is made

One guest, generated as WAT from a table (`harness.py`), is run in both. It imports the flat core form of the WASI functions and has a dispatcher: a host writes the arguments and a blob (a path) into its memory, calls `run(op)`, and reads the scalar result and the return area. The guest is

* for WASI 0.2: a core module turned into a **component** with `wasm-tools component embed` + `new` and run in Wasmtime with `add_wasip2` (so Wasmtime's own implementation of `wasi:*` answers, through the canonical ABI); and the **same core module** run in Wasmtime's core engine with every import bound to the function of the same name in `fusedwasi.p2`, over the memory of the instance;
* for WASI 0.1: the core module, in Wasmtime with `define_wasi`, and the same module with the imports bound to `fusedwasi.p1.wasi_p1` (the way `wasmhost` does).

The same steps are run on both, on two fresh copies of one tree (symbolic links of every kind: to a directory, to a file, dangling, a loop, absolute, leading out of the preopen). The scenarios decode the result record and print a line per step, so what is compared is what a guest would see. 365 steps: **300 agree, 65 differ** (`python docs/experiments/wasmtime_compare/run.py --all`).

| Scenario | Steps | Differ |
| --- | ---: | ---: |
| `wasi:cli` (arguments, environment, cwd, stdout, exit) | 16 | 2 |
| `wasi:clocks` | 10 | 3 |
| `wasi:random` | 11 | 1 |
| `wasi:filesystem` | 153 | 6 (5 steps, 4 causes; the sixth is `readdir /`, a result of two of them) |
| WASI 0.1 | 175 | 53 (30 of them one cause) |

What the steps cover that the task asked for: arguments, environment, `initial-cwd`, exit codes (`exit(ok)`, `exit(err)`, `exit-with-code(0/7/255)`, `proc_exit(0/3)`), the clocks and their pollables, shapes of `wasi:random`, and for the filesystem open / read / write / readdir / stat / rename / unlink / rmdir / mkdir / readlink / symlink / link with a missing file, a directory unlinked, a path that escapes (`..`, an absolute path, a link that leads out, `sub/../..`), links followed or not, loops, dangling links, trailing slashes, an empty path, a name of 300 bytes, a NUL in the name. The same cases for WASI 0.1 (errno).

## What agrees (the important ones, all run)

Both give the same answer, in WASI 0.2 (error-code) and 0.1 (errno), for: a missing file (`no-entry`/`NOENT`); **unlinking a directory** (`is-directory`/`ISDIR`, also for `.` and `sub/..` in 0.2, and for a directory with files); `rmdir` of a file (`not-directory`), of a non-empty directory (`not-empty`), of `.` (`invalid`), of `..` or of a directory outside (`not-permitted`); a **path that escapes the preopen** by `..`, by an absolute name, by `sub/../..`, by a link to a directory outside (followed or not), by a link to a file outside, by a link to `/etc`, in `open`, `stat`, `unlink`, `mkdir`, `rename` (both ends), `hard link` (both ends), `symlink` (name) (`not-permitted` in 0.2; in 0.1 the *code* differs, see D8); **symbolic links**: `open` of a link without `symlink-follow` is `loop`, of a dangling link `no-entry` when followed and `loop` when not, a loop is `loop`, a link in the middle of a path is followed, `stat-at` shows `symbolic-link` and the link's own size, `rename` of a link moves the link, `unlink` of a link to a directory removes the link, `readlink` of a file or a directory is `invalid`; `rename` of a file over a directory is `is-directory`, of a directory over a file `not-directory`, into itself `invalid`, through a file `not-directory`; `symlink` over an existing name `exist`, with an absolute target `not-permitted`; hard link of a directory `not-permitted` (the text of the WIT); `read` at and past the end (`eof` only when nothing was read), `write` past the end fills with zeros, `set-size`, a write to a directory descriptor; the arguments (a space, a non-ASCII name and an empty argument), the environment (an empty value and `=` in a value), `initial-cwd` is `none`; the bytes written to stdout; `exit(ok)` is code 0, `exit(err)` is code 1 and `exit-with-code` gives its code (0, 7, 255) back; the realtime clock within seconds of the host's; monotonic clocks advance; `block` on a 50 ms pollable waits at least 45 ms; the lengths of `get-random-bytes` for 0, 1, 3, 16, 1000 and 100000, successive values differ, **128 MiB traps in both** (the WIT sets no limit; both hosts chose one).

## The differences

"Spec" says whether the text of the WIT (0.2.12) or of the specification of 0.1 decides. "Verdict" is: **bug** (fusedwasi is wrong), **deliberate** (fusedwasi chose it; `host_scope.md` or a comment in the code says so, or it is the repository's design), **Wasmtime-specific** (Wasmtime is stricter, looser or different, and fusedwasi follows the text). Where I could not decide, it says **open**.

### WASI 0.2

| # | Scenario | Wasmtime | fusedwasi | Verdict | Spec decides? |
| --- | --- | --- | --- | --- | --- |
| D1 | `output-stream.blocking-write-and-flush` of 5000 bytes | traps (`Buffer too large ... expected at most 4096`) | writes all 5000 | **deliberate** (`host_scope.md`: "takes a list longer than 4096 bytes and writes it in pieces") | The WIT says "Perform a write of up to 4096 bytes" and, for `write`, that a length above `check-write` traps; for `blocking-write-and-flush` the trap is not stated. So partly: the text allows either. |
| D2 | `monotonic-clock.subscribe-duration(0)` and `subscribe-instant(past)`, then `pollable.ready` at once | `false` | `true` | **Wasmtime-specific** (the pollable of the host is a future that is polled only when waited for) | No. The WIT says the pollable "will resolve" once the instant has occurred / after the duration; `ready` "never blocks". It does not say a pollable whose time has passed is ready at the first call. In the `poll` step (D3) the same pollables are reported ready by both. |
| D3 | `poll` with `[far, past, done]` | ready indexes `[2, 1]` | `[1, 2]` | **Wasmtime-specific** (order of the answer) | Yes: the WIT says "contains one or more indices", no order. A guest must not rely on one. |
| D4 | `insecure-seed` called twice | the same pair both times | a different pair each time | **deliberate** | Yes: "may even be entirely deterministic ... intended to be called only once". Neither is wrong. |
| D5 | `set-size` on a descriptor opened for reading only | `invalid` | `bad-descriptor` | **deliberate** (the conformance notes of `tools/wasi_p2_conformance/README.md` list the same family: errors for a descriptor without `write`) | The WIT has `bad-descriptor` among the errors but does not say for which condition. **open** |
| D6 | `read` of a directory descriptor | `bad-descriptor` | `is-directory` | **deliberate** (same README) | The WIT lists `is-directory` as an error code; `read` is "similar to `pread`", which gives EISDIR on a directory. Leans to fusedwasi, does not decide. |
| D7 | `remove-directory-at` with a trailing slash: `'trail/'` (empty directory), `'full/'` (non-empty) | `invalid` both | `ok` / `not-empty` | **Wasmtime-specific** (POSIX `unlinkat(AT_REMOVEDIR)` accepts `dir/`; the WIT calls it "similar to unlinkat") | No (the WIT does not mention trailing slashes). |
| D8 | `link-at` with `symlink-follow` on a symbolic link | `invalid` | `ok` (a hard link to the target) | **Wasmtime-specific**; the README of `tools/wasi_p2_conformance` lists it too | Partly: the WIT gives `old-path-flags` with `symlink-follow` to `link-at`, so the flag is meaningful. Wasmtime refuses it. |

(The `readdir /` step also differs; it is the result of D7 and D8: Wasmtime kept `trail` and made no `hl7`.)

### WASI 0.1

| # | Scenario | Wasmtime | fusedwasi | Verdict | Spec decides? |
| --- | --- | --- | --- | --- | --- |
| E1 | any path that leaves the preopen: `..`, `/etc/passwd`, `sub/../..`, a link out (followed or not), a link to `/etc`, in `path_open`, `path_filestat_get`, `path_unlink_file`, `path_remove_directory`, `path_create_directory`, `path_rename` (both ends), `path_symlink` (name), `path_link` (both ends): 30 steps | `PERM` | `NOTCAPABLE` | **deliberate** (`wasi_p1_files.py`: "A way out of the directory is NOTCAPABLE when it comes from a walk and EPERM when it comes from the system"; the repository holds 0.1 to the official wasi-testsuite, per the checklist, which I did not run) | The spec of 0.1 names `ENOTCAPABLE` ("capabilities insufficient") but does not say which error an escape gets. **open** |
| E2 | `clock_time_get` and `clock_res_get` for clock 2 (process CPU time) and 3 (thread CPU time) | `BADF` (and no time) | `SUCCESS` | **Wasmtime-specific** (it does not implement them) | The spec lists the four clocks. fusedwasi follows it. |
| E3 | the same calls with an id of 4 or 5 (not a clock) | a **trap** (`Invalid enum value Clockid`) | `INVAL` | **Wasmtime-specific**: Wasmtime rejects an invalid enum while lifting it | The witx type is an enum; the spec has `EINVAL` for bad arguments in general. |
| E4 | a pointer outside the memory in `random_get`, `args_sizes_get`, `fd_write` (iovec) | a **trap** (`Pointer out of bounds`) | `FAULT` | **Wasmtime-specific** | The spec has `EFAULT` ("bad address"), so fusedwasi's answer is allowed. |
| E5 | `fd_write` and `fd_read` on a directory descriptor (the preopen) | `BADF` | `ISDIR` | **deliberate** (the same direction as D6) | No. |
| E6 | `fd_fdstat_get` of stdout when the host gave a callback for it | filetype `unknown` | `char` | **deliberate** (the C comment: "a stream that goes through a hook has no descriptor to ask and stays a character device") | No. |
| E7 | `fd_readdir` on stdout | `BADF` | `NOTDIR` | **deliberate** / **open** | No. |
| E8 | `path_open` of `.`, `sub/..` and a followed link to a directory with **all rights** and no `directory` flag | `ISDIR` (the adapter opens it for writing) | `SUCCESS` | **deliberate**: `FUSEDWASI_P1_R_OF_DIRECTORY` and its comment: "a program that asks for one of them along with the right to write (wasi-libc asks for all the rights there are ...) may mean a directory". **With the rights wasi-libc asks for a directory and `O_DIRECTORY`, all 9 variants agree** (`.`, `sub/..`, a link followed or not, `sub`, `..`, `/`...), apart from the E1 code. | No. |
| E9 | `path_unlink_file` of `.` | `ISDIR` | `INVAL` | **open**. In 0.2 both say `is-directory` for `.` | No. |
| E10 | `path_remove_directory` with a trailing slash | `INVAL` (also for an empty directory) | `NOTEMPTY` / `SUCCESS` | **Wasmtime-specific** (same as D7) | No. |
| E11 | `path_readlink` of a link whose target is absolute (`/etc`) | `PERM` | `PERM` | **fixed 2026-10-26** (the owner decided): 0.1 now refuses it as `readlink-at` of 0.2 does (`not-permitted`), in C and in Python; `path_symlink` with an absolute target was already refused (`NOTCAPABLE`). Pinned in `tests/data/p1/files.txt`. The official wasi-testsuite is run by `wasmext`; run it with this commit before a release. | The 0.2 text says so for 0.2 only; Wasmtime agrees. |
| E12 | `path_link` with the follow flag | `INVAL` | `SUCCESS` | **Wasmtime-specific** (same as D8) | The `lookupflags` of `path_link` is in the spec of 0.1. |

## Not compared, and why

* **Sockets, http**: `wasmtime-py` has no way to allow the network from `WasiConfig` (the same reason as in `tools/wasi_p2_conformance/README.md`); http is covered by that tool, not here.
* **Terminals, `get-terminal-*`**: not run.
* **Reading from stdin**: only an empty stdin (both say end of input).
* **Windows, macOS**: not run (no such machine here). Two codes depend on the system by design: unlinking a directory is `is-directory` in 0.2 and the answer of the system in 0.1 (`wasi_p1_files._UNLINK_OF_DIRECTORY`), which this Linux run cannot show.
* **The C implementation.** See the top. A C driver for these scenarios does not exist; the harness would need a `ctypes` or a C front-end for every function. The parity of C and Python is the subject of the tests, not of this document.
* `tools/wasi_p2_conformance/` already compares the filesystem (262 operations) and http (23 requests) with a Rust guest; it needs `rustup target add wasm32-wasip2`. **I did not run it**; its README is where D5, D6 and D8 were first written down. `release_checklist.md` section 2 says that 0.2 "has not been compared with Wasmtime": that is out of date for the filesystem and http by that tool, and is now also out of date for cli, clocks, random and the filesystem by this one.

## What this changed in the library

Nothing: every difference found is either Wasmtime's own behaviour or listed as deliberate. **E11** is the one candidate for a change; it needs the owner (it touches the 0.1 behaviour that the wasi-testsuite holds). Nothing was fixed in this task.

## Run it

```
uv venv /tmp/cmp && uv pip install --python /tmp/cmp/bin/python wasmtime
WASI_WIT=<dir with cli.wit, clocks.wit, filesystem.wit, io.wit, random.wit ...> /tmp/cmp/bin/python docs/experiments/wasmtime_compare/run.py --all
```

`wasm-tools` must be on the `PATH`. Without `WASI_WIT` the script looks for the `wasip2` crate in `~/.cargo/registry`. About a minute.

## Run of 2026-10-27

Run at the tip of `wasi-p2` (`6a45104`) in a clean worktree. **Everything below was run in this session unless it says "not run".** Nothing under `src/` or `tests/` was touched; nothing was pushed.

### Toolchain (all of it could be had)

* Rust: `cargo 1.97.0`, `rustup` stable with the target `wasm32-wasip2` already installed. The guest (`tools/wasi_p2_conformance/guest`, crate `wasi 0.14.7`, the interfaces of `0.2.4`) built in 11 s with `cargo build --release --target wasm32-wasip2` (the target directory was outside the tree; `Cargo.lock` unchanged), giving `conf.wasm` and `http.wasm`.
* Wasmtime: the `wasmtime` package **50.0.1** from pip, Python 3.14.6 in a scratch venv (`uv`, through the proxy of the environment, TLS verification on). `pytest` was also needed, because `server.py` imports `tests/test_wasi_p2_http.py`.
* C: `gcc`; the C host is header-only (`include/fusedwasi/p2/*.h`), so it is driven by a C program built for each run (see below). Built with `-fsanitize=address,undefined`; the filesystem run printed nothing from either sanitizer (checked by hand); the http run exited 0 (its stderr was not inspected for UBSan lines).
* `wasm-tools` and `wat2wasm` were on the machine, so the older experiment of `docs/experiments/wasmtime_compare/` was run again too.

### What was found broken in the tools themselves (fixed in `tools/`, not in `src/`)

The tools could not run as they stood: they were moved from `docs/experiments/wasi_p2_conformance/` and the paths were not updated.

* `runner.py` put `HERE.parents[2] / "src"` on the path (a directory outside the repository); it is `parents[1]` now. Its usage line and the README say `tools/wasi_p2_conformance/`.
* `server.py` took `tests/` from `parents[3]` (outside the tree) and did not put `src/` on the path; it is `parents[2]` now, plus `src`. Without that the http guest panicked on an empty port, which looks like a Wasmtime failure but is the server not starting.
* New: `ours_c.c` and `ours_http_c.c` (the C host driven by the same two scripts, ports of `ours.py` and `ours_http.py`), and in `runner.py` the environment variable `HOST=c` (build the C driver into the temp directory with `cc` and run it instead of the Python twin) and `SHOW_KNOWN=1` (print the differences marked `#~` as well, which is how the lists below were made).

```
rustup target add wasm32-wasip2
(cd tools/wasi_p2_conformance/guest && cargo build --release --target wasm32-wasip2)
python tools/wasi_p2_conformance/runner.py                                              # filesystem, Python twin
HOST=c python tools/wasi_p2_conformance/runner.py                                       # filesystem, C host
python tools/wasi_p2_conformance/runner.py tools/wasi_p2_conformance/script_http.txt    # http (HOST=c for C)
```

### Results

| Part | Host | Operations | Differ (not marked `#~`) | Marked `#~` and differ | Marked `#~` and agree |
| --- | --- | ---: | ---: | ---: | ---: |
| `wasi:filesystem` (`script_fs.txt`) | Python | 262 | 1 | 24 | 8 |
| `wasi:filesystem` | **C** | 262 | 1 | 24 | 8 |
| `wasi:http` (`script_http.txt`) | Python | 23 | 0 | 4 | 0 |
| `wasi:http` | **C** | 23 | 0 | 4 | 0 |

**C and Python gave the same line for each of the 262 and the 23 operations**: the text of the differences against Wasmtime is identical in both (the output of the runner was compared with `diff`), and every other line equals Wasmtime's, so on those the C equals Python too. The http error-codes of the C driver are numbers that the runner turns into names with the table of the Python twin (an assumption of the comparison). `pytest tests/test_wasi_p2_*.py` (with `-o pythonpath=src`): 74 passed, 1 skipped.

Also run, Python twin only: `docs/experiments/wasmtime_compare/run.py` (cli, clocks, random, filesystem as flat core-module calls, and WASI 0.1): **301 of 365 steps agree**, 64 differ (the run of 2026-10-26 said 300 and 65). The 12 differences of WASI 0.2 are exactly D1 to D8 above (cli 2 steps, clocks 3, random 1, filesystem 6), so those verdicts stand. The 52 of WASI 0.1 are the E-rows above; I did not find which step makes the difference of one against the earlier count. **Not run: the C host for cli, clocks, random, io, or for this older experiment** (it binds the imports to the Python functions; there is no C driver for it), so for those packages the C is covered only by the C-against-Python tests of the repository.

### Every difference of `script_fs.txt` (Wasmtime against fusedwasi; C and Python the same)

| # | Operations (numbers of the output lines) | Wasmtime | fusedwasi | Verdict |
| --- | --- | --- | --- | --- |
| F1 | 12 operations on a **read-only preopen** (`open` for write, create or truncate, `mkdir`, `rmdir`, `unlink`, `symlink`, `rename` out of and into it, `link` into it): 184, 185, 194, 205-212, 217 | `not-permitted` | `read-only` | **Deliberate** (the WIT lists `read-only`; README of the tool). |
| F2 | `write-via-stream` (19, 224) and `append-via-stream` (225) on a descriptor opened without `write` | the stream is made and fails later (`stream-failed`) | `bad-descriptor` at once | **Deliberate** (README). |
| F3 | `write` (213) and `set-size` (35, 218) on a descriptor that cannot write | `write`: `not-permitted`; `set-size`: `invalid` (35) and `not-permitted` (218) | `bad-descriptor` | **Deliberate** for fusedwasi; **Wasmtime quirk** that its own two cases of `set-size` differ. Same family as D5. |
| F4 | `read` of a directory descriptor (54) | `bad-descriptor` | `is-directory` | **Deliberate** (D6). |
| F5 | `blocking-read` of 0 bytes from a file stream (223) | `stream-closed` | an empty list | **Deliberate** (README). |
| F6 | a **FIFO**: `readdir` (61) and `stat-at` (233) | type `unknown` | type `fifo` | **Wasmtime quirk** (the WIT has `fifo`). |
| F7 | `link-at` with `symlink-follow` (191) | `invalid` | `no-entry` here (see below) | **Wasmtime quirk** (D8). |
| F8 | `get-flags` of a directory opened with `mutate-directory` (243, marked) and **of a directory opened with `read` only (252, not marked)** | `r` | `rm` | see F9. **The only line of the script that differs without a `#~`.** |
| F9 | 8 operations marked "mutate-directory unset on a directory" (253-259, 261: `mkdir`, `unlink`, `rmdir`, `rename`, `symlink`, `open` for write, through a directory opened with `read` only) | succeed, or the same error as fusedwasi | **the same as Wasmtime: they agree now** | Not a difference any more; see below. |

Details that matter:

* **F9 / F8 (documentation drift, and a behaviour to decide on).** `wasi_p2_filesystem.py` says in `open_at`: "what a directory may do is what the directory it was opened under may do (as in Wasmtime), and not what was asked for"; the C run shows the same. So a directory opened below a descriptor that has `mutate-directory` gets `read | mutate-directory` whatever flags the guest asked for: a directory opened with `read` only can be changed through, as in Wasmtime, and `get-flags` reports a `mutate-directory` that the guest did not ask for (Wasmtime reports `read` only and never reports `mutate-directory`). The README of the tool says the opposite ("This host follows the WIT"; the 8 `#~` lines of the `nm` group still claim a difference that is gone), and `host_scope.md` ("a change of a directory without `mutate-directory` ... is `read-only`, as the WIT says") is true only for a descriptor whose parent lacks the flag (a read-only preopen). The line `flags nm` has no `#~` mark although it is the same Wasmtime quirk as `flags mm`, which has one: mark it, and remove the `#~` from the 8 lines that now agree. **Verdict:** deliberate (it is in the code, with a comment), but a decision to confirm and to write down: it makes a handle opened with `read` as powerful as one opened with `read, mutate-directory`.
* **F7 and the script.** The line `link root f ln root lnlink` is not a good test: by that line `ln` has been unlinked, so fusedwasi answers `no-entry`, not "done" as the comment says. With a probe on a fresh tree (a scratch script, not committed): Wasmtime answers `invalid` to **every** `link-at` with `symlink-follow` (a link to a file or to a directory, dangling, a missing name, leading out, absolute); fusedwasi follows: a link to a file and a regular file are **done** (a hard link to the target), a link to a directory is `not-permitted`, a dangling link or a missing name `no-entry`, a link out or an absolute link `not-permitted`. C and Python gave the same answers on the probe.
* **The numbers of the README are old.** It says 39 of 236 differed on 2026-10-23 and 32 operations are marked. Today: 262 operations, 32 marked, 24 of them differ, 8 agree (F9), and 1 differs unmarked (F8). Of the four mistakes of that first run (`read` end-of-file, empty name, slash after a link, `directory` on a link) none came back.

### Every difference of `script_http.txt` (C and Python the same)

19 of 23 requests agree (status, fields without `date` and `server`, body, trailers, and the `error-code` for a refused connection, a missing name, a silent server, a bad status line). The four marked ones differ as the README says, and nothing else:

| Request | Wasmtime | fusedwasi | Verdict |
| --- | --- | --- | --- |
| `/truncated` (the body ends before `Content-Length`) | answers 200 with 10 bytes, the body stream fails at the end | the request fails: `HTTP-response-incomplete` | **Deliberate** (the answer is read whole). |
| `/badchunk` | answers 200, the body stream fails (empty body) | `HTTP-protocol-error` | **Deliberate** (same). |
| `/te` (unknown transfer coding) | 200, the body is read to the end (`xx`) | `HTTP-response-transfer-coding` | **Deliberate**; the WIT has the case, hyper does not use it. |
| `/bighead` (a head of 70 000 bytes) | 200 with the field | `HTTP-response-header-section-size` | **Deliberate** (64 KiB limit). |

### Not compared

Sockets (no way to allow the network in `wasmtime-py`), terminals, a real stdin, Windows and macOS; the C host for cli, clocks, random and io (see above); streaming of big bodies, and `Content-Length` against chunks for a request body (the README says Wasmtime always chunks; no step of the script looks at it). Everything here was measured on Linux x86-64.

### What this changes

Nothing in `src/`. **F9/F8 decided by the owner (2026-10-27):** a directory opened below one that has `mutate-directory` inherits the rights of its parent, as in Wasmtime; `host_scope.md`, the README of the tool and the `#~` marks of `script_fs.txt` were corrected in the same commit as this section (the 8 lines that now agree lost their mark; `flags nm` got one). The marks were not re-run after the edit. The C drivers `ours_c.c` and `ours_http_c.c` (`HOST=c` of `runner.py`) are **not in the tree yet**: they were written for the API before the State/Guest split of `design_model.md` and are adapted after it; the pre-split versions are in commit `98a7243` of the local branch `worktree-agent-a227be3ce8558457a` of the repository of this session, and the table above holds for them. The stale paths in `tools/` are fixed. Cli, clocks and random were compared in Python only.

## Re-run after the 0.1-over-0.2 rewrite

The run of 2026-10-27 above was made before the filesystem of 0.2 was split into operations plus ABI wrappers (`500cfb4`), before WASI 0.1 was put over 0.2 and before State and Guest were merged (`14829c4`). This section is the same comparison after them, at `f06e34b` plus the drivers below. **Everything here was run in this session unless it says "not run".** Nothing under `src/` or `tests/` was changed.

### What was ported (tools only, public API only)

* `ours.py`, `ours_http.py`: `Host(...)` became `Guest(State(preopens=...) or State(http=True), [memory], realloc=...)`; the functions of `wasi_p2_*` are called as before, with the guest first.
* `ours_c.c`, `ours_http_c.c` (taken from `98a7243`, which is not in this branch): one `fusedwasi_p2_state` (`fusedwasi_p2_init`, `preopens`, `http_check`) and one `fusedwasi_p2_guest` (`fusedwasi_p2_guest_init`, `trap`, `realloc`, `fusedwasi_p2_set_memory`); every call is `fusedwasi_wasi_<package>_<name>(&guest, ...)`. Built by `HOST=c` of `runner.py` with `cc -O1 -g -fsanitize=address,undefined`. `runner.py` already had `HOST=c` and `SHOW_KNOWN=1`, unchanged. The sanitizers printed nothing on stderr and both programs exited 0 (this time stderr was read, which the run above did not do for http).
* `docs/experiments/wasmtime_compare/harness.py`: `Host` and the 0.1 `State(memory, core=...)` became one `State` and one `Guest` (`fusedwasi.p2`, the same classes as `fusedwasi.p1`); the functions of `wasi_p1` take the guest.

Toolchain: Wasmtime (`wasmtime` from pip) **50.0.1**, Python 3.13.16 in a scratch venv with `pytest`, `cargo` with the target `wasm32-wasip2` (the guest built in 27 s, `Cargo.lock` unchanged), `gcc 13.3.0`, `wasm-tools`, the WIT of `wasip2-*+wasi-0.2.12` in the cargo registry. Linux x86-64.

### Results

| Part | Host | Operations | Differ (not marked `#~`) | Marked `#~` and differ | Marked `#~` and agree |
| --- | --- | ---: | ---: | ---: | ---: |
| `wasi:filesystem` (`script_fs.txt`) | Python | 262 | **0** | 25 | 0 |
| `wasi:filesystem` | **C** | 262 | **0** | 25 | 0 |
| `wasi:http` (`script_http.txt`) | Python | 23 | 0 | 4 | 0 |
| `wasi:http` | **C** | 23 | 0 | 4 | 0 |

Against the run before: the filesystem had 1 unmarked difference (`flags nm`), 24 marked that differ and 8 marked that agree. The marks were edited after that run (`flags nm` marked, the 8 `nm` lines unmarked), and now the 25 marked lines are the earlier 24 and `flags nm`, none of them agrees, and nothing is unmarked: **the expectation held**. http is unchanged (0 and 4). C and Python gave the same line for all 262 and all 23 operations (the lists of lines were compared as lists, not by eye), and every unmarked line equals Wasmtime's.

A second check, stronger than the counts: the lines of the Python twin and of the C host (all four of them) were also produced at `98a7243` (before the rewrite, with the old drivers) and at the current tree, and compared as text: **identical, line for line**. So the rewrite changed no result of `wasi:filesystem` or `wasi:http` on these scripts, in either host, and there is **no new difference and no new finding** for 0.2. The wording of the verdicts F1 to F9 and of the http table above stands.

### The older flat experiment (cli, clocks, random, filesystem as flat core calls, and WASI 0.1)

`python docs/experiments/wasmtime_compare/run.py --all`: **301 of 365 steps agree**, the same as before the rewrite (the run at `98a7243` was repeated here and also gave 301). The results of every step of the five groups were written as JSON by both runs and compared, for Wasmtime and for fusedwasi: **no step changed** (0 of 365), so the list of changed steps is empty. That includes the 0.1 side, which was rewritten over 0.2: the 52 differences of WASI 0.1 (the E-rows above) are the same 52, with the same text. (The step labels were compared too; the number of steps is the same.)

### Not run, still not compared

Sockets, terminals, a real stdin, Windows, macOS, MSVC; the C host for cli, clocks, random and io (and for the flat experiment, which binds the Python functions only); streaming of big bodies; the CI of fusedwasi does not run any of this. Only the compilation with `-Wall -Wextra -O1 -c` of `ours_c.c` was seen to give three `-Wmaybe-uninitialized` warnings from `fusedwasi_wasi_filesystem_types_descriptor_read_directory` (GCC 13.3, inlined into the driver's `main`; the function sets the three results on every path that returns OK, so it looks like a false positive; a small file that calls the same function does not warn). Nothing was changed for it; the driver is built without `-Wall`.
