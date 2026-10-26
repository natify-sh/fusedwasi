# fusedwasi

Header-only C host for the WASI Component Model, for core modules fused from components (wasm2c and other C hosts), with a Python twin.

**Alpha: nothing is released yet.** Two hosts, each as C headers (and the one of 0.2 with a Python twin):

* **WASI 0.2** (`fusedwasi/p2/`, `fusedwasi.p2`): all 200 imports of the command world and of `wasi:http`, in C and in Python, with the tests that run the same scenario in both and compare the outputs (POSIX and Windows).
* **WASI 0.1** (`fusedwasi/p1/`, `fusedwasi.p1`): `wasi_snapshot_preview1` and `wasi_unstable`, in C and in Python (`fusedwasi.p1.wasi_p1`, `wasi_p1_unstable`; the sockets are the four calls that answer NOTSOCK, since a program of 0.1 cannot make one). The C is the core that `wasmext` builds into every module that imports them. It reads the memory of the module through a pointer to its base and one to its size (`fusedwasi_p1_bind`), so it knows nothing of `wasm2c`. **The state of 0.1 is over the state of 0.2** (`fusedwasi_p1_create(&config, &p2_state, ...)`): the arguments, the environment, the clocks, the random bytes, the sleep and the standard streams are those of the state of 0.2 that the host fills, the files are the system layer of 0.2 (`fusedwasi_p2_sys_*`, POSIX and Win32; in Python `fusedwasi.p2.wasi_p2_filesystem.sys_*`), and the end of the program is read from the state (`proc_exit` is `exited` and `exit_code` of the state of 0.2; `proc_raise` is `finished` and `end_value` of 0.1), not told by a hook. What stays 0.1's own: the descriptors with their rights and positions, `poll_oneoff` and the errno table. The Python twin is checked against the C: the same script of calls (`tests/data/p1/*.txt`) is run by a C driver and by Python, and the lines they print are compared.

## The idea

A tool such as `meld fuse` turns a composed component into **one core module**: a memory for each component, the adapters between them are plain wasm, and the imports are the flat core form of `wasi:*` functions. Such a module needs only a host that implements those imports. `fusedwasi` is that host and nothing else: no engine, no component parser, no canonical ABI between components (the fuse already built it in).

```mermaid
flowchart TB
    subgraph build["1. Build"]
        direction LR
        C1["component A<br/>(any language)"] --> F["meld fuse<br/>(or unbundle)"]
        C2["component B"] --> F
        F --> M["one core module<br/>memory per component<br/>imports wasi:*@0.2"]
    end

    subgraph run["2. Run"]
        direction LR
        H["C host (.h)<br/>wasmext, other C hosts"] --> FW
        Y["Python host (.py)<br/>wasmhost"] --> FW
        FW["fusedwasi<br/>state: handles, memories, hooks<br/>one function per import:<br/>cli · clocks · random · io · filesystem · sockets · http"]
    end

    build -- "AOT: wasm2c · runtime: any engine" --> run
```

**AOT and runtime.** The library asks the engine for two things only: the memory of each component (a base and a size) and a way to call a function for each import. So it does not matter how the module runs: compiled ahead of time to C with `wasm2c` (the C headers are included into the generated glue, nothing is needed at run time), or in an interpreter or a JIT that calls the same functions through its import table (the C headers from an embedder, the Python twin from a Python host). Only the AOT route has been run so far (wasmext, `wasm2c`); the runtime route follows from the interface and is not tested.

## The API (planned; the shape of the current code)

One state, one flat function per import, the same arguments in C and in Python. **Every C name has the prefix of the library** (`fusedwasi_` for functions and types, `FUSEDWASI_` for macros, then `p2_` for WASI 0.2): the headers are included into the C of other projects, so nothing may meet a name of the system, of another WASI host or of the user. The function of an import is `fusedwasi_` + the name that the Python twin has (`wasi_cli_environment_get_arguments`). The host gives the state what it decides (arguments, preopened directories, which sockets are allowed, the standard streams, a trap); the rest is the library.

```c
#include "wasi_p2_cli.h"             /* or wasi_p2_filesystem.h, wasi_p2_sockets.h, ... */

fusedwasi_p2_state s;
fusedwasi_p2_init(&s);                  /* defaults: system clocks, /dev/urandom, nothing allowed in sockets */
s.args = args, s.arg_count = 2;
s.realloc = my_cabi_realloc;         /* the cabi_realloc of component `comp` */
s.trap = my_trap;                    /* in a wasm2c module: wasm_rt_trap */
fusedwasi_p2_set_memory(&s, 0, base, size);       /* once for each component; set again when the memory grows */

/* the body of an import of the module: the first argument is the component whose memory it is */
fusedwasi_wasi_cli_environment_get_arguments(&s, /*comp*/ 0, /*retptr*/ ptr);
```

```python
from fusedwasi import Host
from fusedwasi.p2 import wasi_p2_cli as cli  # the module names follow the C headers

host = Host([memory], args=["prog"], realloc=my_cabi_realloc)  # a memory is any writable buffer
cli.wasi_cli_environment_get_arguments(host, 0, ptr)
```

| Part | What the host gives | What the library does |
|---|---|---|
| memory | a base and a size for each component | checked, little-endian loads and stores; a pointer outside is a trap |
| handles | nothing | the table of `own` and `borrow` (generation + index), `[resource-drop]` |
| lists and strings it returns | `realloc` (the `cabi_realloc` of the component) | placing them in the guest's memory |
| files | the preopened directories (`path`, `name`, `writable`) | paths confined below the preopen (POSIX and Windows) |
| sockets | `socket_addr_check`, `allow_ip_name_lookup`; refused by default | TCP, UDP, name lookup (POSIX and Winsock) |
| http | `http_check`, `http_send`; refused by default | an HTTP/1.1 client of its own (TLS in Python only), the types of `wasi:http` |
| time, random, exit, wait | a hook, or the system's own | the rest |

Every function is `static inline` and nothing is global or thread-local, so a module links only what it imports and several instances can live in a process.

## The headers come with the Python package

`pip install fusedwasi` (or a dependency on it) gives the headers too: `python -m fusedwasi` prints the directory to put on the include path, `fusedwasi.get_include()` gives it in Python, and the C is then `#include <fusedwasi/p2/wasi_p2_cli.h>`. With CMake: `find_package(fusedwasi)` and the target `fusedwasi::headers`. The headers in the wheel and in the CMake install are the same files (`src/fusedwasi/include/`).

## Who uses it

- **wasmext** (C, through `wasm2c`): the glue of a module includes the headers; no Python at run time.
- **wasmhost** (Python): the twin in `.py` under the engines it supports.
- **Any other host** that can give a memory and call a function for each import.

## Not here

Components made of components (the fuse does that), WASI 0.3 and async, an engine, a parser of WIT.

WASI 0.3 is not supported. If it is ever added through `meld fuse`, the support will be **partial**: meld 0.65 does not keep the async part of the canonical ABI (the `[async-lower]` imports are renamed or replaced by meld-private ones, two fused async components leave a dangling import, `--component` refuses async). Full p3 waits until meld keeps the standard async ABI.
